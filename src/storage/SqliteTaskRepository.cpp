#include "storage/SqliteTaskRepository.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QVariant>
#include <stdexcept>

namespace campus {
namespace {
constexpr auto fields =
    "id,school_id,notice_id,notice_revision_id,title,notes,action,status,"
    "time_precision,time_date,time_utc,time_zone,time_evidence,time_source,time_confirmed_at,"
    "reminder_enabled,reminder_minutes,reminder_days,reminder_time,calendar_uid,created_at,updated_"
    "at,revision";
void execute(QSqlQuery &q) {
    if (!q.exec())
        throw std::runtime_error(q.lastError().text().toStdString());
}
PersonalTask read(const QSqlQuery &q) {
    PersonalTask t;
    t.id = q.value(0).toString().toStdString();
    t.schoolId = q.value(1).toString().toStdString();
    t.noticeId = q.value(2).toString().toStdString();
    t.noticeRevisionId = q.value(3).toLongLong();
    t.title = q.value(4).toString().toStdString();
    t.notes = q.value(5).toString().toStdString();
    t.action = q.value(6).toString().toStdString();
    t.status = taskStatusFromKey(q.value(7).toString().toStdString());
    t.time.precision = timePrecisionFromKey(q.value(8).toString().toStdString());
    t.time.date = q.value(9).toString().toStdString();
    t.time.utcDateTime = q.value(10).toString().toStdString();
    t.time.timeZone = q.value(11).toString().toStdString();
    t.time.evidence = q.value(12).toString().toStdString();
    t.time.confirmation = confirmationFromKey(q.value(13).toString().toStdString());
    t.time.confirmedAt = q.value(14).toString().toStdString();
    t.reminder.enabled = q.value(15).toBool();
    t.reminder.minutesBefore = q.value(16).toInt();
    t.reminder.daysBefore = q.value(17).toInt();
    t.reminder.dateOnlyAt = q.value(18).toString().toStdString();
    t.calendarUid = q.value(19).toString().toStdString();
    t.createdAt = q.value(20).toString().toStdString();
    t.updatedAt = q.value(21).toString().toStdString();
    t.revision = q.value(22).toInt();
    return t;
}
QVariantList values(const PersonalTask &t) {
    const auto s = [](const std::string &v) { return QVariant(QString::fromStdString(v)); };
    return {s(t.id),
            s(t.schoolId),
            s(t.noticeId),
            t.noticeRevisionId > 0 ? QVariant::fromValue<qlonglong>(t.noticeRevisionId)
                                   : QVariant(QMetaType::fromType<qlonglong>()),
            s(t.title),
            s(t.notes),
            s(t.action),
            s(taskStatusKey(t.status)),
            s(timePrecisionKey(t.time.precision)),
            s(t.time.date),
            s(t.time.utcDateTime),
            s(t.time.timeZone),
            s(t.time.evidence),
            s(confirmationKey(t.time.confirmation)),
            s(t.time.confirmedAt),
            QVariant(t.reminder.enabled ? 1 : 0),
            QVariant(t.reminder.minutesBefore),
            QVariant(t.reminder.daysBefore),
            s(t.reminder.dateOnlyAt),
            s(t.calendarUid),
            s(t.createdAt),
            s(t.updatedAt),
            QVariant(t.revision)};
}
} // namespace
SqliteTaskRepository::SqliteTaskRepository(Database &database) : db_(database.connection()) {}
std::vector<PersonalTask> SqliteTaskRepository::list(const std::string &schoolId) const {
    QSqlQuery q(db_);
    q.prepare(QString("SELECT %1 FROM personal_task WHERE school_id=? ORDER BY updated_at DESC,id")
                  .arg(fields));
    q.addBindValue(QString::fromStdString(schoolId));
    execute(q);
    std::vector<PersonalTask> result;
    while (q.next())
        result.push_back(read(q));
    return result;
}
std::optional<PersonalTask> SqliteTaskRepository::find(const std::string &schoolId,
                                                       const std::string &id) const {
    QSqlQuery q(db_);
    q.prepare(QString("SELECT %1 FROM personal_task WHERE school_id=? AND id=?").arg(fields));
    q.addBindValue(QString::fromStdString(schoolId));
    q.addBindValue(QString::fromStdString(id));
    execute(q);
    return q.next() ? std::optional<PersonalTask>(read(q)) : std::nullopt;
}
PersonalTask SqliteTaskRepository::save(const PersonalTask &input, int expected) {
    auto t = input;
    const bool create = t.id.empty();
    if (t.schoolId.empty() || t.noticeId.empty() || expected < 0 || expected >= 1000000000 ||
        (create != (expected == 0)))
        throw std::invalid_argument("待办身份或版本无效");
    if (!db_.transaction())
        throw std::runtime_error("无法开始待办保存事务");
    try {
        if (create) {
            t.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            t.calendarUid = t.id + "@campuspulse";
        } else {
            const auto old = find(t.schoolId, t.id);
            if (!old || old->revision != expected || old->noticeId != t.noticeId)
                throw std::runtime_error("待办已被修改或不属于当前学校，请重新打开");
            t.calendarUid = old->calendarUid;
            t.createdAt = old->createdAt;
        }
        t.revision = expected + 1;
        const auto data = values(t);
        QSqlQuery q(db_);
        if (create) {
            q.prepare(QString("INSERT INTO personal_task(%1) "
                              "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)")
                          .arg(fields));
            for (const auto &value : data)
                q.addBindValue(value);
        } else {
            q.prepare(
                "UPDATE personal_task SET notice_revision_id=?,title=?,notes=?,action=?,status=?,"
                "time_precision=?,time_date=?,time_utc=?,time_zone=?,time_evidence=?,time_source=?,"
                "time_confirmed_at=?,reminder_enabled=?,reminder_minutes=?,reminder_days=?,"
                "reminder_time=?,"
                "calendar_uid=?,updated_at=?,revision=? WHERE school_id=? AND id=? AND notice_id=? "
                "AND revision=?");
            for (int i = 3; i <= 19; ++i)
                q.addBindValue(data[i]);
            q.addBindValue(data[21]);
            q.addBindValue(data[22]);
            q.addBindValue(data[1]);
            q.addBindValue(data[0]);
            q.addBindValue(data[2]);
            q.addBindValue(expected);
        }
        execute(q);
        if (q.numRowsAffected() != 1)
            throw std::runtime_error("待办保存发生版本冲突");
        if (!db_.commit())
            throw std::runtime_error("待办保存提交失败");
        return t;
    } catch (...) {
        db_.rollback();
        throw;
    }
}
} // namespace campus
