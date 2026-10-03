#include "storage/SqliteSourceRepository.h"
#include <QDate>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace campus {
namespace {

QString text(const std::string &value) {
    return QString::fromStdString(value);
}

void prepare(QSqlQuery &query, const QString &sql) {
    if (!query.prepare(sql))
        throw std::runtime_error(query.lastError().text().toStdString());
}

void execute(QSqlQuery &query) {
    if (!query.exec())
        throw std::runtime_error(query.lastError().text().toStdString());
}

class Transaction {
  public:
    explicit Transaction(QSqlDatabase &database) : database_(database) {
        if (!database_.transaction())
            throw std::runtime_error("无法开始来源状态事务");
    }
    ~Transaction() {
        if (!committed_)
            database_.rollback();
    }
    void commit() {
        if (!database_.commit())
            throw std::runtime_error("来源状态事务提交失败");
        committed_ = true;
    }

  private:
    QSqlDatabase &database_;
    bool committed_ = false;
};

struct ActiveRun {
    QString schoolId;
    QString sourceId;
    int pages;
    int rows;
    QString latestDate;
};

ActiveRun activeRun(QSqlDatabase &database, std::int64_t runId) {
    QSqlQuery query(database);
    prepare(query, "SELECT school_id,source_id,successful_pages,row_count,latest_published_date,"
                   "status FROM fetch_run WHERE id=?");
    query.addBindValue(QVariant::fromValue(static_cast<qlonglong>(runId)));
    execute(query);
    if (!query.next() || query.value(5).toString() != "updating")
        throw std::runtime_error("采集任务不存在或已经结束");
    return {query.value(0).toString(), query.value(1).toString(), query.value(2).toInt(),
            query.value(3).toInt(), query.value(4).toString()};
}

void requireIdentity(const std::string &schoolId, const std::string &sourceId) {
    if (schoolId.empty() || sourceId.empty())
        throw std::invalid_argument("来源状态缺少学校或来源标识");
}

void requireTimestamp(const std::string &timestamp) {
    if (timestamp.empty())
        throw std::invalid_argument("采集任务缺少时间戳");
}

} // namespace

SqliteSourceRepository::SqliteSourceRepository(Database &database) : db_(database.connection()) {}

SourceState SqliteSourceRepository::state(const std::string &schoolId,
                                          const std::string &sourceId) const {
    requireIdentity(schoolId, sourceId);
    QSqlQuery query(db_);
    prepare(
        query,
        "SELECT COALESCE(s.status,'never_checked'),COALESCE(p.paused,0),"
        "COALESCE(s.last_attempt_at,''),COALESCE(s.last_success_at,''),"
        "MAX(COALESCE(s.latest_published_date,''),"
        "COALESCE((SELECT MAX(n.published_date) FROM source_occurrence o JOIN notices n "
        "ON n.id=o.notice_id AND n.school_id=o.school_id WHERE o.school_id=k.school_id "
        "AND o.source_id=k.source_id),'')),COALESCE(s.error,''),"
        "COALESCE(s.successful_pages,0),COALESCE(s.row_count,0) "
        "FROM (SELECT ? AS school_id,? AS source_id) k "
        "LEFT JOIN source_state s ON s.school_id=k.school_id AND s.source_id=k.source_id "
        "LEFT JOIN source_preferences p ON p.school_id=k.school_id AND p.source_id=k.source_id");
    query.addBindValue(text(schoolId));
    query.addBindValue(text(sourceId));
    execute(query);
    if (!query.next())
        throw std::runtime_error("无法读取来源状态");
    SourceState result;
    result.schoolId = schoolId;
    result.sourceId = sourceId;
    result.status = query.value(0).toString().toStdString();
    result.paused = query.value(1).toBool();
    result.lastAttemptAt = query.value(2).toString().toStdString();
    result.lastSuccessAt = query.value(3).toString().toStdString();
    result.latestPublishedDate = query.value(4).toString().toStdString();
    result.error = query.value(5).toString().toStdString();
    result.successfulPages = query.value(6).toInt();
    result.rowCount = query.value(7).toInt();
    return result;
}

void SqliteSourceRepository::setPaused(const std::string &schoolId, const std::string &sourceId,
                                       bool paused) {
    requireIdentity(schoolId, sourceId);
    Transaction transaction(db_);
    if (state(schoolId, sourceId).status == "updating")
        throw std::runtime_error("来源正在更新，不能修改暂停状态");
    QSqlQuery query(db_);
    prepare(query, "INSERT INTO source_preferences(school_id,source_id,paused) VALUES(?,?,?) "
                   "ON CONFLICT(school_id,source_id) DO UPDATE SET paused=excluded.paused");
    query.addBindValue(text(schoolId));
    query.addBindValue(text(sourceId));
    query.addBindValue(paused ? 1 : 0);
    execute(query);
    transaction.commit();
}

std::int64_t SqliteSourceRepository::beginRun(const std::string &schoolId,
                                              const std::string &sourceId,
                                              const std::string &timestamp) {
    requireIdentity(schoolId, sourceId);
    requireTimestamp(timestamp);
    Transaction transaction(db_);
    const auto previous = state(schoolId, sourceId);
    if (previous.paused || previous.status == "updating")
        throw std::runtime_error("来源已暂停或正在更新");
    QSqlQuery stateQuery(db_);
    prepare(stateQuery, "INSERT INTO source_state(school_id,source_id,status,last_attempt_at) "
                        "VALUES(?,?,'updating',?) ON CONFLICT(school_id,source_id) DO UPDATE SET "
                        "status='updating',last_attempt_at=excluded.last_attempt_at,error='',"
                        "successful_pages=0,row_count=0");
    stateQuery.addBindValue(text(schoolId));
    stateQuery.addBindValue(text(sourceId));
    stateQuery.addBindValue(text(timestamp));
    execute(stateQuery);
    QSqlQuery runQuery(db_);
    prepare(runQuery, "INSERT INTO fetch_run(school_id,source_id,started_at) VALUES(?,?,?)");
    runQuery.addBindValue(text(schoolId));
    runQuery.addBindValue(text(sourceId));
    runQuery.addBindValue(text(timestamp));
    execute(runQuery);
    const auto id = runQuery.lastInsertId().toLongLong();
    transaction.commit();
    return id;
}

void SqliteSourceRepository::recordPage(std::int64_t runId, int rows,
                                        const std::string &latestDate) {
    if (rows < 0)
        throw std::invalid_argument("采集条目数不能为负数");
    const auto date = text(latestDate);
    if (!date.isEmpty() && (!QDate::fromString(date, Qt::ISODate).isValid() ||
                            QDate::fromString(date, Qt::ISODate).toString(Qt::ISODate) != date))
        throw std::invalid_argument("来源发布日期必须是 YYYY-MM-DD 或空值");
    Transaction transaction(db_);
    const auto run = activeRun(db_, runId);
    if (run.pages == std::numeric_limits<int>::max() ||
        rows > std::numeric_limits<int>::max() - run.rows)
        throw std::overflow_error("采集统计超过允许范围");
    const auto latest = std::max(run.latestDate, date);
    QSqlQuery runQuery(db_);
    prepare(runQuery, "UPDATE fetch_run SET successful_pages=?,row_count=?,"
                      "latest_published_date=? WHERE id=?");
    runQuery.addBindValue(run.pages + 1);
    runQuery.addBindValue(run.rows + rows);
    runQuery.addBindValue(latest);
    runQuery.addBindValue(QVariant::fromValue(static_cast<qlonglong>(runId)));
    execute(runQuery);
    QSqlQuery stateQuery(db_);
    prepare(stateQuery,
            "UPDATE source_state SET successful_pages=?,row_count=?,"
            "latest_published_date=MAX(latest_published_date,?) WHERE school_id=? AND source_id=?");
    stateQuery.addBindValue(run.pages + 1);
    stateQuery.addBindValue(run.rows + rows);
    stateQuery.addBindValue(latest);
    stateQuery.addBindValue(run.schoolId);
    stateQuery.addBindValue(run.sourceId);
    execute(stateQuery);
    transaction.commit();
}

void SqliteSourceRepository::finishRun(std::int64_t runId, const std::string &error,
                                       const std::string &timestamp) {
    requireTimestamp(timestamp);
    Transaction transaction(db_);
    const auto run = activeRun(db_, runId);
    const QString status = error.empty()   ? "success"
                           : run.pages > 0 ? "partial_success"
                                           : "failure";
    QSqlQuery runQuery(db_);
    prepare(runQuery, "UPDATE fetch_run SET status=?,finished_at=?,error=? WHERE id=?");
    runQuery.addBindValue(status);
    runQuery.addBindValue(text(timestamp));
    runQuery.addBindValue(text(error));
    runQuery.addBindValue(QVariant::fromValue(static_cast<qlonglong>(runId)));
    execute(runQuery);
    QSqlQuery stateQuery(db_);
    prepare(stateQuery, "UPDATE source_state SET status=?,error=?,"
                        "last_success_at=CASE WHEN ?='success' THEN ? ELSE last_success_at END "
                        "WHERE school_id=? AND source_id=?");
    stateQuery.addBindValue(status);
    stateQuery.addBindValue(text(error));
    stateQuery.addBindValue(status);
    stateQuery.addBindValue(text(timestamp));
    stateQuery.addBindValue(run.schoolId);
    stateQuery.addBindValue(run.sourceId);
    execute(stateQuery);
    QSqlQuery cleanup(db_);
    prepare(
        cleanup,
        "DELETE FROM fetch_run WHERE school_id=? AND source_id=? AND id NOT IN "
        "(SELECT id FROM fetch_run WHERE school_id=? AND source_id=? ORDER BY id DESC LIMIT 30)");
    cleanup.addBindValue(run.schoolId);
    cleanup.addBindValue(run.sourceId);
    cleanup.addBindValue(run.schoolId);
    cleanup.addBindValue(run.sourceId);
    execute(cleanup);
    transaction.commit();
}

void SqliteSourceRepository::recoverInterrupted(const std::string &schoolId,
                                                const std::string &timestamp) {
    if (schoolId.empty())
        throw std::invalid_argument("启动恢复缺少学校标识");
    requireTimestamp(timestamp);
    Transaction transaction(db_);
    const QString error = "上次采集在完成前中断，已保留成功页面及原有缓存；可以重新更新。";
    QSqlQuery runs(db_);
    prepare(runs, "UPDATE fetch_run SET status='interrupted',finished_at=?,error=? "
                  "WHERE school_id=? AND status='updating'");
    runs.addBindValue(text(timestamp));
    runs.addBindValue(error);
    runs.addBindValue(text(schoolId));
    execute(runs);
    QSqlQuery states(db_);
    prepare(states, "UPDATE source_state SET status='interrupted',error=? "
                    "WHERE school_id=? AND status='updating'");
    states.addBindValue(error);
    states.addBindValue(text(schoolId));
    execute(states);
    QSqlQuery cleanup(db_);
    prepare(cleanup,
            "DELETE FROM fetch_run WHERE school_id=? AND "
            "(SELECT COUNT(*) FROM fetch_run newer WHERE newer.school_id=fetch_run.school_id "
            "AND newer.source_id=fetch_run.source_id AND newer.id>fetch_run.id)>=30");
    cleanup.addBindValue(text(schoolId));
    execute(cleanup);
    transaction.commit();
}

} // namespace campus
