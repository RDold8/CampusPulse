#include "storage/SqliteReminderRepository.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <stdexcept>
namespace campus {
namespace {
void check(QSqlQuery &query) {
    if (!query.exec())
        throw std::runtime_error(query.lastError().text().toStdString());
}
} // namespace
std::vector<std::string> SqliteReminderRepository::schools() const {
    QSqlQuery query(database_);
    query.prepare("SELECT DISTINCT school_id FROM personal_task WHERE reminder_enabled=1");
    check(query);
    std::vector<std::string> result;
    while (query.next())
        result.push_back(query.value(0).toString().toStdString());
    return result;
}
bool SqliteReminderRepository::claim(const ReminderRecord &record) {
    QSqlQuery query(database_);
    query.prepare(
        "INSERT INTO reminder_delivery(task_id,trigger_utc,task_revision,status,recorded_at,error) "
        "VALUES(?,?,?,?,?,?) ON CONFLICT(task_id,trigger_utc) DO NOTHING");
    for (auto value : {record.taskId, record.triggerUtc})
        query.addBindValue(QString::fromStdString(value));
    query.addBindValue(record.taskRevision);
    for (auto value : {record.status, record.recordedAt, record.error})
        query.addBindValue(QString::fromStdString(value));
    check(query);
    return query.numRowsAffected() == 1;
}
void SqliteReminderRepository::finish(const ReminderRecord &record) {
    QSqlQuery query(database_);
    query.prepare("UPDATE reminder_delivery SET status=?,recorded_at=?,error=? WHERE task_id=? AND "
                  "trigger_utc=? AND status='attempting'");
    for (auto value :
         {record.status, record.recordedAt, record.error, record.taskId, record.triggerUtc})
        query.addBindValue(QString::fromStdString(value));
    check(query);
    if (query.numRowsAffected() != 1)
        throw std::runtime_error("提醒记录已变化，无法提交状态");
}
void SqliteReminderRepository::recover(const std::string &now) {
    QSqlQuery query(database_);
    query.prepare("UPDATE reminder_delivery SET "
                  "status='interrupted',recorded_at=?,error='上次提交中断，未自动重发' WHERE "
                  "status='attempting'");
    query.addBindValue(QString::fromStdString(now));
    check(query);
}
} // namespace campus
