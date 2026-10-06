#include "adapters/RefreshCoordinator.h"
#include "adapters/UniversityRegistry.h"
#include "application/SubscriptionService.h"
#include "application/TaskService.h"
#include "desktop/MainWindow.h"
#include "desktop/BrandTheme.h"
#include "desktop/TaskEditorDialog.h"
#include "storage/Database.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteSourceRepository.h"
#include "storage/SqliteSubscriptionRepository.h"
#include "storage/SqliteTaskRepository.h"
#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkProxy>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScopeGuard>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>
#include <QTimeEdit>
#include <QToolButton>
#include <QWheelEvent>
#include <algorithm>

using namespace campus;
namespace {
template <class T> T *control(QWidget &window, const char *name) {
    auto *result = window.findChild<T *>(name);
    if (!result)
        throw std::runtime_error(std::string("界面控件缺失：") + name);
    return result;
}
struct Session {
    Database db;
    SqliteRepository noticeRepository;
    SqliteSourceRepository sourceRepository;
    SqliteSubscriptionRepository subscriptionRepository;
    SqliteTaskRepository taskRepository;
    NoticeService notices;
    SourceService sources;
    SubscriptionService subscriptions;
    TaskService tasks;
    RefreshCoordinator coordinator;
    Session(const QString &path, const SchoolPackage &school)
        : db(path), noticeRepository(db), sourceRepository(db), subscriptionRepository(db),
          taskRepository(db), notices(noticeRepository, school.id.toStdString()),
          sources(school.id.toStdString(), school.catalog, sourceRepository),
          subscriptions(school.id.toStdString(), subscriptionRepository, notices),
          tasks(school.id.toStdString(), school.timeZone.toStdString(), taskRepository, notices),
          coordinator(school, notices, sources, nullptr, {0, 1000}) {}
    void seed(const std::string &url = "https://jwc.neepu.edu.cn/demo") {
        Notice n;
        n.id = "task-ui-demo";
        n.schoolId = "cn-neepu";
        n.sourceId = "academic-affairs-notices";
        n.sourceName = "教务处通知公告";
        n.title = "【演示数据】重修报名与缴费通知";
        n.url = url;
        n.publishedDate = QDate::currentDate().toString(Qt::ISODate).toStdString();
        notices.ingest({n});
        n.body = "界面测试缓存正文。此处日期与事项均为演示，不代表官网实时公告。";
        notices.saveDetail(n);
    }
};
TaskEditorDialog *activeEditor() {
    auto *dialog = qobject_cast<TaskEditorDialog *>(QApplication::activeModalWidget());
    if (dialog)
        QTimer::singleShot(5000, dialog, &QDialog::reject);
    return dialog;
}
void selectNotice(MainWindow &window) {
    control<QTabWidget>(window, "mainTabs")->setCurrentIndex(0);
    auto *table = control<QTableView>(window, "noticeTable");
    table->setCurrentIndex(table->model()->index(0, 1));
}
void selectTask(MainWindow &window, const std::string &id) {
    auto *table = control<QTableView>(window, "taskTable");
    for (int row = 0; row < table->model()->rowCount(); ++row)
        if (table->model()->index(row, 0).data(Qt::UserRole).toString().toStdString() == id) {
            table->setCurrentIndex(table->model()->index(row, 0));
            return;
        }
    throw std::runtime_error("待办未显示");
}
// Exercise the real network/parser/storage/UI boundary without contacting a school.
class DetailServer : public QObject {
  public:
    QTcpServer server;
    int status = 200;
    int requests = 0;
    QByteArray body =
        QString("<html><div class='body'><p>演示原文已调整，请重新核对办理事项。</p></div></html>")
            .toUtf8();
    DetailServer() {
        if (!server.listen(QHostAddress::LocalHost, 0))
            throw std::runtime_error("Local server failed");
        connect(&server, &QTcpServer::newConnection, this, [this] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                if (socket->property("handled").toBool())
                    return;
                auto bytes = socket->property("bytes").toByteArray() + socket->readAll();
                socket->setProperty("bytes", bytes);
                if (!bytes.contains("\r\n\r\n"))
                    return;
                socket->setProperty("handled", true);
                ++requests;
                socket->write(
                    "HTTP/1.1 " + QByteArray::number(status) +
                    " Fixture\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                    QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }
    QUrl url() const {
        return QUrl(QString("http://127.0.0.1:%1/demo").arg(server.serverPort()));
    }
};
} // namespace

class TaskDesktopTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    }
    void createMultipleTasksStatesFiltersAndRestart() {
        const auto school = SchoolPackage::load(CONFIG_FILE);
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        QTemporaryDir folder;
        const auto path = folder.filePath("tasks.sqlite");
        std::string payment, registration, uid;
        {
            Session s(path, school);
            s.seed();
            MainWindow window(school, s.notices, s.sources, s.coordinator, registry,
                              s.subscriptions, s.tasks);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto *tabs = control<QTabWidget>(window, "mainTabs");
            QCOMPARE(tabs->count(), 7);
            QVERIFY(!control<QPushButton>(window, "addTaskButton")->isEnabled());
            selectNotice(window);
            QTimer::singleShot(0, &window, [&] {
                auto *editor = activeEditor();
                QVERIFY(editor);
                QCOMPARE(control<QComboBox>(*editor, "taskTimePrecision")->currentData().toString(),
                         QString("datetime"));
                QVERIFY(control<QCheckBox>(*editor, "taskReminderEnabled")->isChecked());
                QVERIFY(control<QWidget>(*editor, "taskAdvancedOptions")->isHidden());
                editor->reject();
            });
            control<QPushButton>(window, "addTaskButton")->click();
            QVERIFY(s.tasks.list().empty());
            QCOMPARE(tabs->currentIndex(), 0);
            QTimer::singleShot(0, &window, [&] {
                auto *editor = activeEditor();
                QVERIFY(editor);
                control<QLineEdit>(*editor, "taskTitle")->setText("【演示】完成重修缴费");
                auto *action = control<QComboBox>(*editor, "taskAction");
                action->setCurrentIndex(action->findData("payment"));
                auto *precision = control<QComboBox>(*editor, "taskTimePrecision");
                precision->setCurrentIndex(precision->findData("date"));
                control<QDateEdit>(*editor, "taskDate")->setDate(QDate::currentDate().addDays(2));
                auto *source = control<QComboBox>(*editor, "taskTimeSource");
                source->setCurrentIndex(source->findData("original_text"));
                control<QTextEdit>(*editor, "taskTimeEvidence")
                    ->setPlainText("【演示摘录】两天后办理；此日期并非真实官网时间。<b>原文</b>");
                control<QTextEdit>(*editor, "taskNotes")
                    ->setPlainText("【演示备注】<b>保持字面文字</b>");
                control<QPushButton>(*editor, "saveTaskButton")->click();
                QVERIFY(!control<QLabel>(*editor, "taskError")->text().isEmpty());
                QVERIFY(s.tasks.list().empty());
                control<QCheckBox>(*editor, "taskTimeConfirmed")->setChecked(true);
                control<QCheckBox>(*editor, "taskReminderEnabled")->setChecked(true);
                if (QDir(EVIDENCE_DIR).exists())
                    QVERIFY(editor->grab().save(QString(EVIDENCE_DIR) + "/stage3-editor-demo.png"));
                control<QPushButton>(*editor, "saveTaskButton")->click();
                QCOMPARE(editor->result(), int(QDialog::Accepted));
            });
            control<QPushButton>(window, "addTaskButton")->click();
            QCOMPARE(tabs->tabText(tabs->currentIndex()), QString("我的待办"));
            QCOMPARE(s.tasks.list().size(), size_t(1));
            auto saved = s.tasks.list().front();
            payment = saved.id;
            uid = saved.calendarUid;
            QCOMPARE(saved.time.precision, TimePrecision::DateOnly);
            QVERIFY(saved.time.utcDateTime.empty());
            QVERIFY(saved.reminder.enabled);
            QTimer::singleShot(0, &window, [&] {
                auto *editor = activeEditor();
                QVERIFY(editor);
                QCOMPARE(control<QTextEdit>(*editor, "taskNotes")->toPlainText(),
                         QString::fromStdString(saved.notes));
                QCOMPARE(control<QTextEdit>(*editor, "taskTimeEvidence")->toPlainText(),
                         QString::fromStdString(saved.time.evidence));
                control<QPushButton>(*editor, "saveTaskButton")->click();
            });
            control<QPushButton>(window, "editTaskButton")->click();
            QCOMPARE(s.tasks.find(payment), saved); // Reopening then saving is a no-op.
            selectNotice(window);
            QTimer::singleShot(0, &window, [&] {
                auto *editor = activeEditor();
                QVERIFY(editor);
                control<QLineEdit>(*editor, "taskTitle")->setText("【演示】重修报名，时间待核对");
                auto *action = control<QComboBox>(*editor, "taskAction");
                action->setCurrentIndex(action->findData("registration"));
                control<QCheckBox>(*editor, "taskTimeScheduled")->setChecked(false);
                control<QPushButton>(*editor, "saveTaskButton")->click();
            });
            control<QPushButton>(window, "addTaskButton")->click();
            QCOMPARE(s.tasks.list().size(), size_t(2));
            for (const auto &task : s.tasks.list())
                if (task.id != payment)
                    registration = task.id;
            selectTask(window, payment);
            control<QPushButton>(window, "startTaskButton")->click();
            QCOMPARE(s.tasks.find(payment).status, TaskStatus::InProgress);
            control<QPushButton>(window, "completeTaskButton")->click();
            QCOMPARE(s.tasks.find(payment).status, TaskStatus::Completed);
            QCOMPARE(s.tasks.find(registration).status, TaskStatus::NotStarted);
            control<QPushButton>(window, "undoCompleteTaskButton")->click();
            QCOMPARE(s.tasks.find(payment).status, TaskStatus::NotStarted);
            control<QPushButton>(window, "cancelTaskButton")->click();
            QCOMPARE(s.tasks.find(payment).status, TaskStatus::Cancelled);
            control<QPushButton>(window, "restoreTaskButton")->click();
            QCOMPARE(s.tasks.find(payment).status, TaskStatus::NotStarted);
            auto *dateFilter = control<QComboBox>(window, "taskDateFilter");
            dateFilter->setCurrentIndex(dateFilter->findData("unknown"));
            QCOMPARE(control<QTableView>(window, "taskTable")->model()->rowCount(), 1);
            dateFilter->setCurrentIndex(0);
            control<QLineEdit>(window, "taskSearch")->setText("完成重修缴费");
            QCOMPARE(control<QTableView>(window, "taskTable")->model()->rowCount(), 1);
            control<QLineEdit>(window, "taskSearch")->clear();
            selectTask(window, payment);
            if (QDir(EVIDENCE_DIR).exists())
                QVERIFY(window.grab().save(QString(EVIDENCE_DIR) + "/stage3-tasks-demo.png"));
            control<QPushButton>(window, "completeTaskButton")->click();
        }
        {
            Session s(path, school);
            MainWindow window(school, s.notices, s.sources, s.coordinator, registry,
                              s.subscriptions, s.tasks);
            window.show();
            control<QTabWidget>(window, "mainTabs")->setCurrentIndex(3);
            QCOMPARE(control<QTableView>(window, "taskTable")->model()->rowCount(), 2);
            selectTask(window, payment);
            QCOMPARE(s.tasks.find(payment).calendarUid, uid);
            QCOMPARE(s.tasks.find(payment).status, TaskStatus::Completed);
            QVERIFY(control<QPushButton>(window, "undoCompleteTaskButton")->isEnabled());
            control<QPushButton>(window, "openTaskNoticeButton")->click();
            QCOMPARE(control<QTabWidget>(window, "mainTabs")->currentIndex(), 0);
            QVERIFY(control<QLabel>(window, "detailTitle")->text().contains("重修报名与缴费"));
        }
    }
    void exactPersonalTimeSavesWithoutAnExtraConfirmation() {
        const auto school = SchoolPackage::load(CONFIG_FILE);
        QTemporaryDir dir;
        Session s(dir.filePath("datetime.sqlite"), school);
        s.seed();
        TaskEditorDialog editor(school, s.tasks, s.tasks.draft("task-ui-demo"), "演示通知");
        auto *precision = control<QComboBox>(editor, "taskTimePrecision");
        precision->setCurrentIndex(precision->findData("datetime"));
        auto *time = control<QDateTimeEdit>(editor, "taskDateTime");
        time->setDate(QDate(2026, 10, 4));
        time->setTime(QTime(16, 30, 15));
        auto *confirm = control<QCheckBox>(editor, "taskTimeConfirmed");
        confirm->setChecked(true);
        time->setTime(QTime(16, 31, 15));
        QVERIFY(!confirm->isChecked());
        control<QPushButton>(editor, "saveTaskButton")->click();
        QCOMPARE(editor.result(), int(QDialog::Accepted));
        QCOMPARE(editor.saved().time.utcDateTime, std::string("2026-10-04T08:31:15Z"));
        QVERIFY(editor.saved().time.date.empty());
        QCOMPARE(editor.saved().time.confirmation, TimeConfirmation::Personal);
    }
    void compactAlarmPresetsAndExplicitUnscheduledChoice() {
        const auto previousFont = qApp->font();
        const auto previousStyle = qApp->styleSheet();
        const auto restoreTheme = qScopeGuard([&] {
            qApp->setFont(previousFont);
            qApp->setStyleSheet(previousStyle);
        });
        auto themedFont = previousFont;
        themedFont.setPointSizeF(std::max(themedFont.pointSizeF(), 10.5));
        qApp->setFont(themedFont);
        qApp->setStyleSheet(BrandTheme::styleSheet());
        const auto school = SchoolPackage::load(CONFIG_FILE);
        QTemporaryDir dir;
        Session s(dir.filePath("compact.sqlite"), school);
        s.seed();
        TaskEditorDialog editor(school, s.tasks, s.tasks.draft("task-ui-demo"), "演示通知");
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        QVERIFY(control<QWidget>(editor, "taskAdvancedOptions")->isHidden());
        QVERIFY(!control<QComboBox>(editor, "taskTimePrecision")->isVisible());
        QVERIFY(control<QDateTimeEdit>(editor, "taskDateTime")->isVisible());
        QVERIFY(!control<QDateEdit>(editor, "taskDate")->isVisible());
        QVERIFY(!control<QSpinBox>(editor, "taskReminderMinutes")->isVisible());
        QVERIFY(editor.height() <= 350);
        auto *scroll = editor.findChild<QScrollArea *>();
        QVERIFY(scroll);
        QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
        for (const auto *name : {"taskTitle", "taskDateTime", "taskReminderPreset",
                                 "taskReminderPreview", "taskMoreOptions"}) {
            auto *widget = control<QWidget>(editor, name);
            const auto position = widget->mapTo(scroll->viewport(), QPoint());
            QVERIFY(scroll->viewport()->rect().contains(QRect(position, widget->size())));
        }
        if (QDir(EVIDENCE_DIR).exists())
            QVERIFY(editor.grab().save(QString(EVIDENCE_DIR) + "/simple-reminder-editor-demo.png"));
        editor.resize(810, 350);
        control<QToolButton>(editor, "taskMoreOptions")->click();
        QTRY_VERIFY(editor.height() >= 600);
        QCOMPARE(editor.width(), 810);
        control<QToolButton>(editor, "taskMoreOptions")->click();
        QTRY_VERIFY(editor.height() <= 350);
        QCOMPARE(editor.width(), 810);
        QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), 0);
        auto *time = control<QDateTimeEdit>(editor, "taskDateTime");
        QVERIFY(time->dateTime() > QDateTime::currentDateTimeUtc());
        auto tomorrow = QDateTime::currentDateTimeUtc().addDays(1).toTimeZone(QTimeZone("Asia/Shanghai"));
        tomorrow.setTime(QTime(tomorrow.time().hour(), tomorrow.time().minute(), 0));
        time->setDateTime(tomorrow);
        auto *preset = control<QComboBox>(editor, "taskReminderPreset");
        preset->setCurrentIndex(preset->findData("10"));
        QCOMPARE(control<QSpinBox>(editor, "taskReminderMinutes")->value(), 10);
        const auto expected = tomorrow.addSecs(-600).toString("yyyy年MM月dd日 HH:mm");
        QVERIFY(control<QLabel>(editor, "taskReminderPreview")->text().contains(expected));
        QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        time->setCurrentSection(QDateTimeEdit::YearSection);
        QApplication::sendEvent(time, &wheel);
        QCOMPARE(time->dateTime(), tomorrow);
        control<QPushButton>(editor, "saveTaskButton")->click();
        QCOMPARE(editor.result(), int(QDialog::Accepted));
        QVERIFY(editor.saved().reminder.enabled);
        QCOMPARE(editor.saved().reminder.minutesBefore, 10);
        QCOMPARE(editor.saved().time.confirmation, TimeConfirmation::Personal);
        TaskEditorDialog unscheduled(school, s.tasks, s.tasks.draft("task-ui-demo"), "演示通知");
        control<QCheckBox>(unscheduled, "taskTimeScheduled")->setChecked(false);
        control<QPushButton>(unscheduled, "saveTaskButton")->click();
        QCOMPARE(unscheduled.result(), int(QDialog::Accepted));
        QCOMPARE(unscheduled.saved().time.precision, TimePrecision::Unknown);
        QVERIFY(!unscheduled.saved().reminder.enabled);
    }
    void dateOnlyPresetsAndCustomPreferencesSurviveReopening() {
        const auto school = SchoolPackage::load(CONFIG_FILE);
        QTemporaryDir dir;
        Session s(dir.filePath("date-custom.sqlite"), school);
        s.seed();
        TaskEditorDialog editor(school, s.tasks, s.tasks.draft("task-ui-demo"), "演示通知");
        auto *precision = control<QComboBox>(editor, "taskTimePrecision");
        precision->setCurrentIndex(precision->findData("date"));
        const auto date = QDate::currentDate().addDays(4);
        control<QDateEdit>(editor, "taskDate")->setDate(date);
        auto *preset = control<QComboBox>(editor, "taskReminderPreset");
        QVERIFY(preset->findData("10") < 0);
        preset->setCurrentIndex(preset->findData("1"));
        QCOMPARE(control<QSpinBox>(editor, "taskReminderDays")->value(), 1);
        QCOMPARE(control<QTimeEdit>(editor, "taskReminderTime")->time(), QTime(9, 0));
        QVERIFY(control<QLabel>(editor, "taskReminderPreview")
                    ->text().contains(date.addDays(-1).toString("yyyy年MM月dd日") + " 09:00"));
        preset->setCurrentIndex(preset->findData("custom"));
        control<QSpinBox>(editor, "taskReminderDays")->setValue(2);
        control<QTimeEdit>(editor, "taskReminderTime")->setTime(QTime(18, 20));
        control<QPushButton>(editor, "saveTaskButton")->click();
        QCOMPARE(editor.result(), int(QDialog::Accepted));
        const auto original = editor.saved();
        QCOMPARE(original.reminder.daysBefore, 2);
        QCOMPARE(original.reminder.dateOnlyAt, std::string("18:20"));
        QVERIFY(original.time.utcDateTime.empty());
        TaskEditorDialog reopened(school, s.tasks, original, "演示通知");
        QCOMPARE(control<QComboBox>(reopened, "taskReminderPreset")->currentData().toString(),
                 QString("custom"));
        control<QPushButton>(reopened, "saveTaskButton")->click();
        QCOMPARE(reopened.result(), int(QDialog::Accepted));
        QCOMPARE(reopened.saved(), original);
        auto disabled = original;
        disabled.reminder.enabled = false;
        disabled = s.tasks.save(disabled);
        TaskEditorDialog noReminder(school, s.tasks, disabled, "演示通知");
        QCOMPARE(control<QComboBox>(noReminder, "taskReminderPreset")->currentData().toString(),
                 QString("off"));
        control<QPushButton>(noReminder, "saveTaskButton")->click();
        QCOMPARE(noReminder.saved(), disabled);
        auto exact = s.tasks.draft("task-ui-demo");
        exact.time.precision = TimePrecision::DateTime;
        exact.time.utcDateTime = "2027-01-01T08:00:15Z";
        exact.time.confirmation = TimeConfirmation::Personal;
        exact.reminder.enabled = true;
        exact.reminder.minutesBefore = 17;
        exact.reminder.daysBefore = 3; // Preserve unused precision preferences as well.
        exact = s.tasks.save(exact, true);
        TaskEditorDialog customExact(school, s.tasks, exact, "演示通知");
        QCOMPARE(control<QComboBox>(customExact, "taskReminderPreset")->currentData().toString(),
                 QString("custom"));
        control<QPushButton>(customExact, "saveTaskButton")->click();
        QCOMPARE(customExact.saved(), exact);
    }
    void originalTextTimeStillNeedsEvidenceAndExplicitConfirmation() {
        const auto school = SchoolPackage::load(CONFIG_FILE);
        QTemporaryDir dir;
        Session s(dir.filePath("original-time.sqlite"), school);
        s.seed();
        TaskEditorDialog editor(school, s.tasks, s.tasks.draft("task-ui-demo"), "演示通知");
        auto *source = control<QComboBox>(editor, "taskTimeSource");
        source->setCurrentIndex(source->findData("original_text"));
        control<QPushButton>(editor, "saveTaskButton")->click();
        QVERIFY(s.tasks.list().empty());
        QVERIFY(control<QToolButton>(editor, "taskMoreOptions")->isChecked());
        control<QTextEdit>(editor, "taskTimeEvidence")->setPlainText("演示原文明确时刻，仅为测试样本");
        control<QPushButton>(editor, "saveTaskButton")->click();
        QVERIFY(s.tasks.list().empty());
        control<QCheckBox>(editor, "taskTimeConfirmed")->setChecked(true);
        control<QPushButton>(editor, "saveTaskButton")->click();
        QCOMPARE(editor.result(), int(QDialog::Accepted));
        QCOMPARE(editor.saved().time.confirmation, TimeConfirmation::OriginalText);
        TaskEditorDialog editOriginal(school, s.tasks, editor.saved(), "演示通知");
        auto *time = control<QDateTimeEdit>(editOriginal, "taskDateTime");
        time->setDateTime(time->dateTime().addSecs(60));
        QVERIFY(!control<QCheckBox>(editOriginal, "taskTimeConfirmed")->isChecked());
        control<QPushButton>(editOriginal, "saveTaskButton")->click();
        QCOMPARE(s.tasks.find(editor.saved().id), editor.saved());
        QVERIFY(control<QToolButton>(editOriginal, "taskMoreOptions")->isChecked());
    }
    void refreshOriginalReviewFailureAndPausedSource() {
        DetailServer server;
        auto school = SchoolPackage::load(CONFIG_FILE);
        auto source = school.sources.front();
        source.id = "academic-affairs-notices";
        source.entry = server.url();
        source.allowedHosts = {"127.0.0.1"};
        source.bodySelector = "div.body";
        source.attachmentSelector = "a.attachment";
        school.sources = {source};
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        QTemporaryDir dir;
        Session s(dir.filePath("refresh.sqlite"), school);
        s.seed(server.url().toString().toStdString());
        auto task = s.tasks.draft("task-ui-demo");
        task.time.precision = TimePrecision::DateOnly;
        task.time.date = "2026-10-05";
        task.time.confirmation = TimeConfirmation::Personal;
        task = s.tasks.save(task, true);
        task = s.tasks.setStatus(task.id, TaskStatus::Completed);
        MainWindow window(school, s.notices, s.sources, s.coordinator, registry, s.subscriptions,
                          s.tasks);
        window.show();
        selectNotice(window);
        QSignalSpy finished(&s.coordinator, &RefreshCoordinator::detailFinished);
        QSignalSpy failed(&s.coordinator, &RefreshCoordinator::detailFailed);
        auto *refresh = control<QPushButton>(window, "refreshOriginalButton");
        refresh->click();
        QVERIFY(!refresh->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QCOMPARE(server.requests, 1);
        QVERIFY(refresh->isEnabled());
        QCOMPARE(s.tasks.find(task.id), task);
        QVERIFY(s.tasks.views().front().needsReview());
        control<QTabWidget>(window, "mainTabs")->setCurrentIndex(3);
        QVERIFY(control<QPushButton>(window, "reviewTaskNoticeButton")->isEnabled());
        QVERIFY(control<QTextBrowser>(window, "taskDetail")->toPlainText().contains("原文有更新"));
        if (QDir(EVIDENCE_DIR).exists())
            QVERIFY(window.grab().save(QString(EVIDENCE_DIR) + "/stage3-review-demo.png"));
        control<QPushButton>(window, "reviewTaskNoticeButton")->click();
        QVERIFY(!s.tasks.views().front().needsReview());
        const auto reviewed = s.tasks.find(task.id);
        QCOMPARE(reviewed.time, task.time);
        QCOMPARE(reviewed.status, task.status);
        QCOMPARE(reviewed.calendarUid, task.calendarUid);
        QCOMPARE(reviewed.revision, task.revision + 1);
        control<QPushButton>(window, "openTaskNoticeButton")->click();
        server.status = 503;
        const auto original = s.notices.list().front();
        refresh->click();
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        QCOMPARE(server.requests, 2);
        QCOMPARE(s.notices.list().front().body, original.body);
        QCOMPARE(s.notices.list().front().revisionId, original.revisionId);
        QCOMPARE(s.tasks.find(task.id), reviewed);
        QVERIFY(refresh->isEnabled());
        s.sources.setPaused("academic-affairs-notices", true);
        refresh->click();
        QCOMPARE(failed.count(), 2);
        QCOMPARE(server.requests, 2);
        QVERIFY(failed.back().at(1).toString().contains("暂停"));
    }
};
QTEST_MAIN(TaskDesktopTests)
#include "TaskDesktopTests.moc"
