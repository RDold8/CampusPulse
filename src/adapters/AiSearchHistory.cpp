#include "adapters/AiSearchHistory.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <stdexcept>
namespace campus {
namespace {
class Connection {
  public:
    QString name = "ai-history-" + QUuid::createUuid().toString();
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
    ~Connection() { db.close(); db = {}; QSqlDatabase::removeDatabase(name); }
    void open(const QString &directory, bool readOnly) {
        db.setDatabaseName(QDir(directory).filePath("ai-search-history.sqlite"));
        db.setConnectOptions(readOnly ? "QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=3000" : "QSQLITE_BUSY_TIMEOUT=3000");
        if (!db.open()) error("打开");
    }
    [[noreturn]] void error(const QString &stage, const QString &detail = {}) {
        throw std::runtime_error(("AI 搜索记录" + stage + "失败：" +
            (detail.isEmpty() ? db.lastError().text() : detail)).toUtf8().constData());
    }
};
}
QJsonObject AiSearchHistory::load(const QString &directory, const QString &schoolId) {
    if (!QFileInfo::exists(QDir(directory).filePath("ai-search-history.sqlite"))) return {};
    Connection connection;
    connection.open(directory, true);
    QSqlQuery query(connection.db);
    query.prepare("SELECT payload FROM reports WHERE school_id=?");
    query.addBindValue(schoolId);
    if (!query.exec()) connection.error("读取", query.lastError().text());
    if (!query.next()) return {};
    const auto bytes = query.value(0).toByteArray();
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (bytes.size() > 512 * 1024 || error.error != QJsonParseError::NoError || !document.isObject())
        connection.error("解析", "搜索记录无效");
    return document.object();
}
void AiSearchHistory::save(const QString &directory, const QString &schoolId, const QJsonObject &report) {
    const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Compact);
    if (bytes.size() > 512 * 1024 || !QDir().mkpath(directory))
        throw std::runtime_error("搜索记录目录创建失败或记录超过大小上限");
    Connection connection;
    connection.open(directory, false);
    QSqlQuery query(connection.db);
    if (!query.exec("PRAGMA synchronous=FULL") || !connection.db.transaction()) connection.error("启动事务");
    if (!query.exec("CREATE TABLE IF NOT EXISTS reports(school_id TEXT PRIMARY KEY,payload BLOB NOT NULL)"))
        connection.error("创建", query.lastError().text());
    query.prepare("INSERT INTO reports(school_id,payload) VALUES(?,?) ON CONFLICT(school_id) DO UPDATE SET payload=excluded.payload");
    query.addBindValue(schoolId);
    query.addBindValue(bytes);
    if (!query.exec()) connection.error("写入", query.lastError().text());
    if (!connection.db.commit()) connection.error("提交");
}
}
