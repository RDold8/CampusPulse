#include "adapters/ReminderScheduler.h"
#include "application/TaskService.h"
#include "storage/Database.h"
#include "storage/SqliteReminderRepository.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteTaskRepository.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <stdexcept>

using namespace campus;

namespace {
QDateTime utc(const char *text) {
    const auto result = QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate);
    if (!result.isValid())
        throw std::runtime_error("Invalid test clock");
    return result.toUTC();
}

struct DeliveryState {
    QString status, error;
    int revision = 0;
};

struct Store {
    Database database;
    SqliteRepository notices;
    SqliteTaskRepository tasks;
    SqliteReminderRepository reminders;

    explicit Store(const QString &file)
        : database(file), notices(database), tasks(database), reminders(database.connection()) {}

    PersonalTask create(const std::string &suffix, const std::string &school = "cn-test",
                        const std::string &at = "2026-10-03T01:00:00Z") {
        Notice notice;
        notice.id = school + "-notice-" + suffix;
        notice.schoolId = school;
        notice.sourceId = "academic";
        notice.sourceName = "教务处";
        notice.title = "重修缴费通知 " + suffix;
        notice.url = "https://school.edu.cn/notices/" + suffix;
        notice.publishedDate = "2026-10-02";
        NoticeService scopedNotices(notices, school);
        scopedNotices.ingest({notice});
        TaskService scopedTasks(school, "Asia/Shanghai", tasks, scopedNotices,
                                [] { return "2026-10-02T00:00:00Z"; });
        auto task = scopedTasks.draft(notice.id);
        task.action = "payment";
        task.time.precision = TimePrecision::DateTime;
        task.time.utcDateTime = at;
        task.time.confirmation = TimeConfirmation::Personal;
        task.reminder.enabled = true;
        task.reminder.minutesBefore = 0;
        return scopedTasks.save(task, true);
    }

    PersonalTask save(PersonalTask task, bool confirm = false) {
        NoticeService scopedNotices(notices, task.schoolId);
        TaskService scopedTasks(task.schoolId, "Asia/Shanghai", tasks, scopedNotices,
                                [] { return "2026-10-02T00:00:00Z"; });
        return scopedTasks.save(std::move(task), confirm);
    }

    int count() {
        QSqlQuery query(database.connection());
        if (!query.exec("SELECT COUNT(*) FROM reminder_delivery") || !query.next())
            throw std::runtime_error(query.lastError().text().toStdString());
        return query.value(0).toInt();
    }

    DeliveryState state(const PersonalTask &task, const std::string &at = "") {
        QSqlQuery query(database.connection());
        query.prepare("SELECT status,error,task_revision FROM reminder_delivery "
                      "WHERE task_id=? AND trigger_utc=?");
        query.addBindValue(QString::fromStdString(task.id));
        query.addBindValue(at.empty() ? ReminderScheduler::trigger(task).toString(Qt::ISODate)
                                      : QString::fromStdString(at));
        if (!query.exec())
            throw std::runtime_error(query.lastError().text().toStdString());
        if (!query.next())
            throw std::runtime_error("Reminder record was not persisted");
        return {query.value(0).toString(), query.value(1).toString(), query.value(2).toInt()};
    }
};
} // namespace

class ReminderTests final : public QObject {
    Q_OBJECT
  private slots:
    void realDatabaseDeduplicatesConcurrentSchedulersAndTaskEdits() {
        QTemporaryDir folder;
        Store store(folder.filePath("reminders.sqlite"));
        auto task = store.create("dedup");
        int submissions = 0;
        auto delivery = [&](const PersonalTask &submitted) {
            QCOMPARE(submitted.id, task.id);
            ++submissions;
        };
        ReminderScheduler first(store.tasks, store.reminders, delivery);
        ReminderScheduler second(store.tasks, store.reminders, delivery);
        QSignalSpy delivered(&first, &ReminderScheduler::delivered);
        QSignalSpy errors(&first, &ReminderScheduler::failed);
        first.poll(utc("2026-10-03T00:59:59Z"));
        QCOMPARE(submissions, 0);
        first.poll(utc("2026-10-03T01:00:00Z"));
        second.poll(utc("2026-10-03T01:00:00Z"));
        first.poll(utc("2026-10-03T01:00:15Z"));
        QCOMPARE(submissions, 1);
        QCOMPARE(delivered.count(), 1);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(store.count(), 1);
        QCOMPARE(store.state(task).status, QString("submitted"));
        QCOMPARE(store.state(task).revision, 1);
        task.notes = "仅修改个人备注";
        task = store.save(task);
        QCOMPARE(task.revision, 2);
        ReminderScheduler afterEdit(store.tasks, store.reminders, delivery);
        afterEdit.poll(utc("2026-10-03T01:00:00Z"));
        QCOMPARE(submissions, 1);
        QCOMPARE(store.count(), 1);
        QCOMPARE(store.state(task).revision, 1);
    }

    void changingConfirmedTriggerCreatesOneNewDelivery() {
        QTemporaryDir folder;
        Store store(folder.filePath("reminders.sqlite"));
        auto task = store.create("rescheduled");
        int submissions = 0;
        ReminderScheduler scheduler(store.tasks, store.reminders,
                                    [&](const PersonalTask &) { ++submissions; });
        scheduler.poll(utc("2026-10-03T01:00:00Z"));
        task.time.utcDateTime = "2026-10-04T01:00:00Z";
        task = store.save(task, true);
        scheduler.poll(utc("2026-10-04T01:00:00Z"));
        scheduler.poll(utc("2026-10-04T01:00:15Z"));
        QCOMPARE(submissions, 2);
        QCOMPARE(store.count(), 2);
        QCOMPARE(store.state(task, "2026-10-03T01:00:00Z").status, QString("submitted"));
        QCOMPARE(store.state(task).status, QString("submitted"));
        QCOMPARE(store.state(task).revision, 2);
    }

    void reopeningDatabaseDoesNotResubmitPreviouslyDeliveredTask() {
        QTemporaryDir folder;
        const auto file = folder.filePath("reminders.sqlite");
        PersonalTask task;
        int submissions = 0;
        {
            Store store(file);
            task = store.create("restart");
            ReminderScheduler scheduler(store.tasks, store.reminders,
                                        [&](const PersonalTask &) { ++submissions; });
            scheduler.poll(utc("2026-10-03T01:00:00Z"));
            QCOMPARE(submissions, 1);
        }
        {
            Store reopened(file);
            QCOMPARE(reopened.count(), 1);
            QCOMPARE(reopened.state(task).status, QString("submitted"));
            ReminderScheduler restarted(reopened.tasks, reopened.reminders,
                                        [&](const PersonalTask &) { ++submissions; });
            restarted.poll(utc("2026-10-03T01:00:00Z"));
            restarted.poll(utc("2026-10-03T01:00:15Z"));
            QCOMPARE(submissions, 1);
            QCOMPARE(reopened.count(), 1);
        }
    }

    void completedCancelledAndDisabledTasksAreNotDelivered() {
        QTemporaryDir folder;
        Store store(folder.filePath("reminders.sqlite"));
        auto completed = store.create("completed");
        completed.status = TaskStatus::Completed;
        store.save(completed);
        auto cancelled = store.create("cancelled");
        cancelled.status = TaskStatus::Cancelled;
        store.save(cancelled);
        auto disabled = store.create("disabled");
        disabled.reminder.enabled = false;
        store.save(disabled);
        auto active = store.create("active");
        active.status = TaskStatus::InProgress;
        active = store.save(active);
        std::vector<std::string> submitted;
        ReminderScheduler scheduler(store.tasks, store.reminders, [&](const PersonalTask &task) {
            submitted.push_back(task.id);
        });
        scheduler.poll(utc("2026-10-03T01:00:00Z"));
        QCOMPARE(submitted.size(), size_t(1));
        QCOMPARE(submitted.front(), active.id);
        QCOMPARE(store.count(), 1);
    }

    void pastRemindersExpireOnStartupAndAfterLongSleepWithoutCatchup() {
        QTemporaryDir folder;
        Store store(folder.filePath("reminders.sqlite"));
        const auto old = store.create("already-past");
        int submissions = 0;
        ReminderScheduler onStartup(store.tasks, store.reminders,
                                    [&](const PersonalTask &) { ++submissions; });
        // Even a recent reminder is not replayed when the app first starts after its time.
        onStartup.poll(utc("2026-10-03T01:00:02Z"));
        QCOMPARE(submissions, 0);
        QCOMPARE(store.state(old).status, QString("expired"));
        const auto slept = store.create("slept", "cn-test", "2026-10-03T02:00:00Z");
        ReminderScheduler afterSleep(store.tasks, store.reminders,
                                     [&](const PersonalTask &) { ++submissions; });
        afterSleep.poll(utc("2026-10-03T01:59:45Z"));
        afterSleep.poll(utc("2026-10-03T02:10:00Z"));
        QCOMPARE(submissions, 0);
        QCOMPARE(store.state(slept).status, QString("expired"));
        QCOMPARE(store.count(), 2);
    }

    void dateOnlyReminderUsesSchoolTimeZoneWithoutInventingDeadlineTime() {
        QTemporaryDir folder;
        Store store(folder.filePath("reminders.sqlite"));
        auto task = store.create("date-only");
        task.time.precision = TimePrecision::DateOnly;
        task.time.date = "2026-10-04";
        task.reminder.daysBefore = 1;
        task.reminder.dateOnlyAt = "09:30";
        task = store.save(task, true);
        QVERIFY(task.time.utcDateTime.empty());
        QCOMPARE(task.time.timeZone, std::string("Asia/Shanghai"));
        QCOMPARE(ReminderScheduler::trigger(task), utc("2026-10-03T01:30:00Z"));
        int submissions = 0;
        ReminderScheduler scheduler(store.tasks, store.reminders,
                                    [&](const PersonalTask &) { ++submissions; });
        scheduler.poll(utc("2026-10-03T01:29:59Z"));
        scheduler.poll(utc("2026-10-03T01:30:00Z"));
        QCOMPARE(submissions, 1);
        const auto saved = store.tasks.find("cn-test", task.id);
        QVERIFY(saved.has_value());
        QCOMPARE(saved->time.precision, TimePrecision::DateOnly);
        QCOMPARE(saved->time.date, std::string("2026-10-04"));
        QVERIFY(saved->time.utcDateTime.empty());
        QCOMPARE(store.state(task).status, QString("submitted"));
    }

    void schedulerIncludesEnabledTasksAcrossSchools() {
        QTemporaryDir folder;
        Store store(folder.filePath("reminders.sqlite"));
        const auto first = store.create("cross-a", "cn-neepu");
        const auto second = store.create("cross-b", "cn-jlu");
        std::vector<std::string> deliveredSchools;
        ReminderScheduler scheduler(store.tasks, store.reminders, [&](const PersonalTask &task) {
            deliveredSchools.push_back(task.schoolId);
        });
        scheduler.poll(utc("2026-10-03T01:00:00Z"));
        std::sort(deliveredSchools.begin(), deliveredSchools.end());
        QCOMPARE(deliveredSchools, (std::vector<std::string>{"cn-jlu", "cn-neepu"}));
        QCOMPARE(store.count(), 2);
        QCOMPARE(store.state(first).status, QString("submitted"));
        QCOMPARE(store.state(second).status, QString("submitted"));
    }

    void deliveryFailureIsVisibleAndPersistedWithoutAutomaticRetry() {
        QTemporaryDir folder;
        const auto file = folder.filePath("reminders.sqlite");
        PersonalTask task;
        int attempts = 0;
        {
            Store store(file);
            task = store.create("channel-failed");
            ReminderScheduler scheduler(store.tasks, store.reminders, [&](const PersonalTask &) {
                ++attempts;
                throw std::runtime_error("Notification channel unavailable");
            });
            QSignalSpy failed(&scheduler, &ReminderScheduler::failed);
            QSignalSpy delivered(&scheduler, &ReminderScheduler::delivered);
            scheduler.poll(utc("2026-10-03T01:00:00Z"));
            scheduler.poll(utc("2026-10-03T01:00:15Z"));
            QCOMPARE(attempts, 1);
            QCOMPARE(failed.count(), 1);
            QVERIFY(failed.at(0).at(0).toString().contains("channel unavailable"));
            QCOMPARE(delivered.count(), 0);
            QCOMPARE(store.state(task).status, QString("failed"));
            QVERIFY(store.state(task).error.contains("channel unavailable"));
        }
        {
            Store reopened(file);
            ReminderScheduler scheduler(reopened.tasks, reopened.reminders,
                                        [&](const PersonalTask &) { ++attempts; });
            scheduler.poll(utc("2026-10-03T01:00:00Z"));
            QCOMPARE(attempts, 1);
            QCOMPARE(reopened.state(task).status, QString("failed"));
        }
    }

    void interruptedSubmissionSurvivesRecoveryAndIsNotRetried() {
        QTemporaryDir folder;
        const auto file = folder.filePath("reminders.sqlite");
        PersonalTask task;
        {
            Store store(file);
            task = store.create("interrupted");
            ReminderRecord record{task.id,      "2026-10-03T01:00:00Z",
                                  "attempting", "2026-10-03T01:00:00Z",
                                  "",           task.revision};
            QVERIFY(store.reminders.claim(record));
            QVERIFY(!store.reminders.claim(record));
        }
        {
            Store reopened(file);
            reopened.reminders.recover("2026-10-03T01:00:00Z");
            QCOMPARE(reopened.state(task).status, QString("interrupted"));
            QVERIFY(reopened.state(task).error.contains("未自动重发"));
            int submissions = 0;
            ReminderScheduler scheduler(reopened.tasks, reopened.reminders,
                                        [&](const PersonalTask &) { ++submissions; });
            scheduler.poll(utc("2026-10-03T01:00:00Z"));
            QCOMPARE(submissions, 0);
            QCOMPARE(reopened.count(), 1);
        }
    }

    void unknownDatesAndCorruptedExactTimeDoNotCauseDeliveryOrCrash() {
        QTemporaryDir folder;
        Store store(folder.filePath("reminders.sqlite"));
        auto unknown = store.create("unknown");
        unknown.time.precision = TimePrecision::Unknown;
        unknown = store.save(unknown);
        QVERIFY(!unknown.reminder.enabled);
        const auto invalid = store.create("invalid");
        QSqlQuery query(store.database.connection());
        query.prepare("UPDATE personal_task SET time_utc='invalid-date' WHERE id=?");
        query.addBindValue(QString::fromStdString(invalid.id));
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
        int submissions = 0;
        ReminderScheduler scheduler(store.tasks, store.reminders,
                                    [&](const PersonalTask &) { ++submissions; });
        QSignalSpy errors(&scheduler, &ReminderScheduler::failed);
        scheduler.poll(utc("2026-10-03T01:00:00Z"));
        QCOMPARE(submissions, 0);
        QCOMPARE(store.count(), 0);
        QCOMPARE(errors.count(), 0);
    }
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication application(argc, argv);
    ReminderTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "ReminderTests.moc"
