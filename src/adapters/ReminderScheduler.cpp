#include "adapters/ReminderScheduler.h"
#include "application/ReminderService.h"
#include <QTimeZone>
#include <stdexcept>
namespace campus {
ReminderScheduler::ReminderScheduler(TaskRepository &tasks, ReminderRepository &records,
                                     Delivery delivery, QObject *parent)
    : QObject(parent), tasks_(tasks), records_(records), delivery_(std::move(delivery)) {
    timer_.setInterval(15000);
    connect(&timer_, &QTimer::timeout, this, [this] { poll(QDateTime::currentDateTimeUtc()); });
}
void ReminderScheduler::start() {
    records_.recover(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString());
    poll(QDateTime::currentDateTimeUtc());
    timer_.start();
}
QDateTime ReminderScheduler::trigger(const PersonalTask &task) {
    if (task.time.precision == TimePrecision::DateTime)
        return QDateTime::fromString(QString::fromStdString(task.time.utcDateTime), Qt::ISODate)
            .toUTC()
            .addSecs(-qint64(task.reminder.minutesBefore) * 60);
    if (task.time.precision == TimePrecision::DateOnly) {
        const auto date = QDate::fromString(QString::fromStdString(task.time.date), Qt::ISODate)
                              .addDays(-task.reminder.daysBefore);
        const auto time =
            QTime::fromString(QString::fromStdString(task.reminder.dateOnlyAt), "HH:mm");
        const QTimeZone zone(QByteArray::fromStdString(task.time.timeZone));
        if (date.isValid() && time.isValid() && zone.isValid())
            return QDateTime(date, time, zone, QDateTime::TransitionResolution::Reject).toUTC();
    }
    return {};
}
void ReminderScheduler::poll(const QDateTime &input) {
    const auto now = input.toUTC();
    if (!now.isValid())
        return;
    const auto prior = previous_.isValid() ? previous_ : now.addSecs(-1);
    previous_ = now;
    const auto stamp = [](const QDateTime &value) {
        return value.toString(Qt::ISODate).toStdString();
    };
    try {
        for (const auto &school : records_.schools())
            for (const auto &task : tasks_.list(school)) {
                const auto fire = trigger(task);
                const auto decision = ReminderService::decide(
                    task, stamp(fire), stamp(now), stamp(prior), stamp(now.addSecs(-300)));
                if (decision == ReminderDecision::Ignore)
                    continue;
                ReminderRecord record{
                    task.id,
                    stamp(fire),
                    decision == ReminderDecision::Expired ? "expired" : "attempting",
                    stamp(now),
                    "",
                    task.revision};
                if (!records_.claim(record) || decision == ReminderDecision::Expired)
                    continue;
                try {
                    delivery_(task);
                    record.status = "submitted";
                } catch (const std::exception &error) {
                    record.status = "failed";
                    record.error = error.what();
                }
                records_.finish(record);
                if (record.status == "submitted")
                    emit delivered(QString::fromStdString(task.title),
                                   QString::fromStdString(task.id));
                else
                    emit failed(QString::fromStdString(record.error));
            }
    } catch (const std::exception &error) {
        emit failed(QString::fromUtf8(error.what()));
    }
}
} // namespace campus
