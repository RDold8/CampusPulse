#include "adapters/ReminderScheduler.h"
#include "application/TaskService.h"
#include "desktop/BrandTheme.h"
#include "desktop/ReminderPopup.h"
#include "storage/Database.h"
#include "storage/SqliteReminderRepository.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteTaskRepository.h"
#include <QApplication>
#include <QDateTime>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QSettings>
#include <QtTest>
#include <stdexcept>

using namespace campus;

namespace {
template <class T> T *control(QWidget &widget, const char *name) {
    auto *result = widget.findChild<T *>(QString::fromLatin1(name));
    if (!result)
        throw std::runtime_error(std::string("Reminder control missing: ") + name);
    return result;
}

// Every database belongs to a temporary directory. These tests do not construct
// provider settings, university discovery, or any network/API client.
struct Store {
    QTemporaryDir folder;
    Database database;
    SqliteRepository noticeRepository;
    SqliteTaskRepository taskRepository;
    SqliteReminderRepository reminderRepository;
    NoticeService notices;
    TaskService tasks;

    Store()
        : database(folder.filePath("reminder-desktop.sqlite")), noticeRepository(database),
          taskRepository(database), reminderRepository(database.connection()),
          notices(noticeRepository, "cn-reminder-test"),
          tasks("cn-reminder-test", "Asia/Shanghai", taskRepository, notices) {
        Notice notice;
        notice.id = "reminder-desktop-test-notice";
        notice.schoolId = "cn-reminder-test";
        notice.sourceId = "test-source";
        notice.sourceName = "[测试] 本地提醒";
        notice.title = "[测试] 仅用于提醒界面验收";
        notice.url = "https://school.example.edu.cn/test.htm";
        notice.publishedDate = QDateTime::currentDateTimeUtc().date().toString(Qt::ISODate)
                                   .toStdString();
        notices.ingest({notice});
    }

    PersonalTask create(const std::string &title, const QDateTime &at) {
        auto task = tasks.draft("reminder-desktop-test-notice");
        task.title = title;
        task.time.precision = TimePrecision::DateTime;
        task.time.utcDateTime = at.toUTC().toString(Qt::ISODate).toStdString();
        task.time.confirmation = TimeConfirmation::Personal;
        task.reminder.enabled = true;
        task.reminder.minutesBefore = 0;
        return tasks.save(task, true);
    }

    int recordCount() {
        QSqlQuery query(database.connection());
        if (!query.exec("SELECT COUNT(*) FROM reminder_delivery") || !query.next())
            throw std::runtime_error(query.lastError().text().toStdString());
        return query.value(0).toInt();
    }

    QString status(const PersonalTask &task) {
        QSqlQuery query(database.connection());
        query.prepare("SELECT status FROM reminder_delivery WHERE task_id=?");
        query.addBindValue(QString::fromStdString(task.id));
        if (!query.exec() || !query.next())
            throw std::runtime_error("Expected reminder delivery record is missing");
        return query.value(0).toString();
    }
};
} // namespace

class ReminderDesktopTests final : public QObject {
    Q_OBJECT
  private slots:
    void escapeDismissesBatchAndStopsAudioWithoutEditingTasks() {
        Store store;
        const auto task = store.create("[测试] Esc保留事项", QDateTime::currentDateTimeUtc().addSecs(90));
        const auto before = store.tasks.list();
        ReminderPopup popup;
        popup.showTask(task);
        popup.showTest();
        QCOMPARE(popup.reminderCount(), 2);
        QTest::keyClick(&popup, Qt::Key_Escape);
        QVERIFY(!popup.isVisible());
        QCOMPARE(popup.reminderCount(), 0);
        QVERIFY(control<QLabel>(popup, "reminderAudioStatus")->text().contains("声音已停止"));
        QCOMPARE(store.tasks.list(), before);
        QCOMPARE(store.recordCount(), 0);
        popup.showTest();
        QCOMPARE(popup.reminderCount(), 1);
        popup.close();
    }

    void realTimerProducesVisibleNonModalPopupAndSubmittedRecord() {
        Store store;
        ReminderPopup popup;
        const auto task = store.create("[测试] 到点提醒", QDateTime::currentDateTimeUtc().addSecs(2));
        ReminderScheduler scheduler(store.taskRepository, store.reminderRepository,
                                    [&](const PersonalTask &due) {
                                        popup.showTask(due, "[测试] 学校");
                                    });
        QSignalSpy delivered(&scheduler, &ReminderScheduler::delivered);
        QSignalSpy failed(&scheduler, &ReminderScheduler::failed);
        scheduler.start();
        QVERIFY(!popup.isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(popup.isVisible(), 3500);
        QCOMPARE(delivered.count(), 1);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(popup.reminderCount(), 1);
        QVERIFY(!popup.isModal());
        QVERIFY(popup.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QCOMPARE(popup.objectName(), QString("reminderPopup"));
        QVERIFY(control<QListWidget>(popup, "reminderPopupItems")->item(0)->text()
                    .contains("[测试] 到点提醒"));
        QCOMPARE(store.status(task), QString("submitted"));
        scheduler.poll(QDateTime::currentDateTimeUtc());
        QCOMPARE(delivered.count(), 1);
        QCOMPARE(popup.reminderCount(), 1);
        popup.close();
    }

    void completedAndCancelledTasksDoNotProducePopupOrRecords() {
        Store store;
        const auto at = QDateTime::currentDateTimeUtc().addSecs(1);
        const auto cancelled = store.create("[测试] 已取消", at);
        const auto completed = store.create("[测试] 已完成", at);
        store.tasks.setStatus(cancelled.id, TaskStatus::Cancelled);
        store.tasks.setStatus(completed.id, TaskStatus::Completed);
        ReminderPopup popup;
        ReminderScheduler scheduler(store.taskRepository, store.reminderRepository,
                                    [&](const PersonalTask &due) { popup.showTask(due); });
        QSignalSpy delivered(&scheduler, &ReminderScheduler::delivered);
        scheduler.start();
        QTest::qWait(1500);
        QVERIFY(!popup.isVisible());
        QCOMPARE(popup.reminderCount(), 0);
        QCOMPARE(delivered.count(), 0);
        QCOMPARE(store.recordCount(), 0);
    }

    void multipleRemindersRemainUntilAcknowledgedWithoutChangingTasks() {
        Store store;
        const auto at = QDateTime::currentDateTimeUtc();
        const auto first = store.create("[测试] 缴费", at);
        const auto second = store.create("[测试] 竞赛报名", at);
        ReminderPopup popup;
        popup.showTask(first);
        popup.showTask(second);
        QCOMPARE(popup.reminderCount(), 2);
        QTest::qWait(1200);
        QVERIFY(popup.isVisible());
        QCOMPARE(popup.reminderCount(), 2);
        auto *items = control<QListWidget>(popup, "reminderPopupItems");
        items->setCurrentRow(0);
        control<QPushButton>(popup, "dismissReminderButton")->click();
        QCOMPARE(popup.reminderCount(), 1);
        QVERIFY(popup.isVisible());
        QVERIFY(items->item(0)->text().contains("竞赛报名"));
        QCOMPARE(store.tasks.find(first.id), first);
        QCOMPARE(store.tasks.find(second.id), second);
        QCOMPARE(store.recordCount(), 0);
        control<QPushButton>(popup, "dismissReminderButton")->click();
        QCOMPARE(popup.reminderCount(), 0);
        QVERIFY(!popup.isVisible());
    }

    void sameTaskAndTriggerAreDeduplicatedButRescheduledTaskCanAppearAgain() {
        Store store;
        auto task = store.create("[测试] 不重复", QDateTime::currentDateTimeUtc());
        ReminderPopup popup;
        popup.showTask(task);
        popup.showTask(task);
        QCOMPARE(popup.reminderCount(), 1);
        popup.close();
        QCOMPARE(popup.reminderCount(), 0);
        popup.showTask(task);
        QVERIFY(!popup.isVisible());
        QCOMPARE(popup.reminderCount(), 0);
        task.time.utcDateTime = QDateTime::currentDateTimeUtc().addSecs(60)
                                    .toString(Qt::ISODate).toStdString();
        task = store.tasks.save(task, true);
        popup.showTask(task);
        QCOMPARE(popup.reminderCount(), 1);
        QVERIFY(popup.isVisible());
        popup.close();
    }

    void testReminderDoesNotCreateOrEditTasksOrDeliveryRecords() {
        Store store;
        const auto task = store.create("[测试] 保留原待办", QDateTime::currentDateTimeUtc().addSecs(90));
        const auto before = store.tasks.list();
        ReminderPopup popup;
        QSignalSpy requested(&popup, &ReminderPopup::taskRequested);
        popup.showTest();
        QVERIFY(popup.isVisible());
        QCOMPARE(popup.reminderCount(), 1);
        QVERIFY(control<QListWidget>(popup, "reminderPopupItems")->item(0)->text()
                    .contains("测试不会创建待办"));
        control<QPushButton>(popup, "openReminderTaskButton")->click();
        QCOMPARE(requested.count(), 0);
        QCOMPARE(store.tasks.list(), before);
        QCOMPARE(store.tasks.find(task.id), task);
        QCOMPARE(store.recordCount(), 0);
        popup.close();
        QCOMPARE(popup.reminderCount(), 0);
    }

    void viewSelectedReminderEmitsItsSchoolAndTaskWithoutCompletingIt() {
        Store store;
        const auto first = store.create("[测试] 第一项", QDateTime::currentDateTimeUtc());
        const auto second = store.create("[测试] 第二项", QDateTime::currentDateTimeUtc());
        ReminderPopup popup;
        popup.showTask(first, "[测试] 学校");
        popup.showTask(second, "[测试] 学校");
        control<QListWidget>(popup, "reminderPopupItems")->setCurrentRow(0);
        QSignalSpy requested(&popup, &ReminderPopup::taskRequested);
        control<QPushButton>(popup, "openReminderTaskButton")->click();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.at(0).at(0).toString(), QString::fromStdString(first.schoolId));
        QCOMPARE(requested.at(0).at(1).toString(), QString::fromStdString(first.id));
        QCOMPARE(store.tasks.find(first.id), first);
        QCOMPARE(popup.reminderCount(), 2);
        QVERIFY(popup.isVisible());
        popup.close();
    }
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    application.setOrganizationName("CampusPulseAcceptance");
    application.setApplicationName("ReminderDesktopTests");
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings().setValue("reminders/soundTone", "mute");
    application.setQuitOnLastWindowClosed(false);
    BrandTheme::installApplication(application);
    ReminderDesktopTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "ReminderDesktopTests.moc"
