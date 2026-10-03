#include "storage/SqliteSubscriptionRepository.h"
#include "storage/SqliteJson.h"
#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <stdexcept>

namespace campus {
namespace {
constexpr auto fields =
    "id,school_id,name,paused,source_ids,theme_keys,stage_keys,"
    "keyword_all,keyword_any,keyword_exclude,year_policy,fixed_year,created_at,updated_at";
void execute(QSqlQuery &query) {
    if (!query.exec())
        throw std::runtime_error(query.lastError().text().toStdString());
}
Subscription read(const QSqlQuery &query) {
    Subscription result;
    result.id = query.value(0).toString().toStdString();
    result.schoolId = query.value(1).toString().toStdString();
    result.name = query.value(2).toString().toStdString();
    result.paused = query.value(3).toBool();
    result.query.schoolId = result.schoolId;
    result.query.sourceIds = stringsFromJson(query.value(4).toString());
    result.query.themeKeys = stringsFromJson(query.value(5).toString());
    result.query.stageKeys = stringsFromJson(query.value(6).toString());
    result.query.keywordAll = stringsFromJson(query.value(7).toString());
    result.query.keywordAny = stringsFromJson(query.value(8).toString());
    result.query.keywordExclude = stringsFromJson(query.value(9).toString());
    result.query.yearPolicy = yearPolicyFromKey(query.value(10).toString().toStdString());
    result.query.fixedYear = query.value(11).toInt();
    result.createdAt = query.value(12).toString().toStdString();
    result.updatedAt = query.value(13).toString().toStdString();
    return result;
}
} // namespace
SqliteSubscriptionRepository::SqliteSubscriptionRepository(Database &database)
    : db_(database.connection()) {}
std::vector<Subscription> SqliteSubscriptionRepository::list(const std::string &schoolId) const {
    QSqlQuery query(db_);
    query.prepare(
        QString("SELECT %1 FROM subscription WHERE school_id=? ORDER BY name,id").arg(fields));
    query.addBindValue(QString::fromStdString(schoolId));
    execute(query);
    std::vector<Subscription> result;
    while (query.next())
        result.push_back(read(query));
    return result;
}
std::optional<Subscription> SqliteSubscriptionRepository::find(const std::string &schoolId,
                                                               const std::string &id) const {
    QSqlQuery query(db_);
    query.prepare(QString("SELECT %1 FROM subscription WHERE school_id=? AND id=?").arg(fields));
    query.addBindValue(QString::fromStdString(schoolId));
    query.addBindValue(QString::fromStdString(id));
    execute(query);
    return query.next() ? std::optional<Subscription>(read(query)) : std::nullopt;
}
Subscription SqliteSubscriptionRepository::save(const Subscription &subscription) {
    auto saved = subscription;
    const bool create = saved.id.empty();
    if (saved.schoolId.empty() || saved.query.schoolId != saved.schoolId)
        throw std::invalid_argument("订阅的学校范围无效");
    if (!db_.transaction())
        throw std::runtime_error("无法开始订阅保存事务");
    try {
        const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
        if (create) {
            saved.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            saved.createdAt = now;
        } else {
            const auto previous = find(saved.schoolId, saved.id);
            if (!previous)
                throw std::invalid_argument("订阅不存在或不属于当前学校");
            saved.createdAt = previous->createdAt;
        }
        saved.updatedAt = now;
        QSqlQuery query(db_);
        if (create)
            query.prepare(
                QString("INSERT INTO subscription(%1) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)")
                    .arg(fields));
        else
            query.prepare(
                "UPDATE subscription SET name=?,paused=?,source_ids=?,theme_keys=?,stage_keys=?,"
                "keyword_all=?,keyword_any=?,keyword_exclude=?,year_policy=?,fixed_year=?,"
                "updated_at=? WHERE school_id=? AND id=?");
        const auto bind = [&](const std::string &value) {
            query.addBindValue(QString::fromStdString(value));
        };
        if (create) {
            bind(saved.id);
            bind(saved.schoolId);
        }
        bind(saved.name);
        query.addBindValue(saved.paused ? 1 : 0);
        query.addBindValue(stringsJson(saved.query.sourceIds));
        query.addBindValue(stringsJson(saved.query.themeKeys));
        query.addBindValue(stringsJson(saved.query.stageKeys));
        query.addBindValue(stringsJson(saved.query.keywordAll));
        query.addBindValue(stringsJson(saved.query.keywordAny));
        query.addBindValue(stringsJson(saved.query.keywordExclude));
        bind(yearPolicyKey(saved.query.yearPolicy));
        query.addBindValue(saved.query.fixedYear);
        if (create)
            bind(saved.createdAt);
        bind(saved.updatedAt);
        if (!create) {
            bind(saved.schoolId);
            bind(saved.id);
        }
        execute(query);
        if (!db_.commit())
            throw std::runtime_error("订阅保存提交失败");
        return saved;
    } catch (...) {
        db_.rollback();
        throw;
    }
}
void SqliteSubscriptionRepository::remove(const std::string &schoolId, const std::string &id) {
    QSqlQuery query(db_);
    query.prepare("DELETE FROM subscription WHERE school_id=? AND id=?");
    query.addBindValue(QString::fromStdString(schoolId));
    query.addBindValue(QString::fromStdString(id));
    execute(query);
    if (query.numRowsAffected() != 1)
        throw std::invalid_argument("订阅不存在或不属于当前学校");
}
} // namespace campus
