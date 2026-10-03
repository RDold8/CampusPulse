#include "adapters/RefreshCoordinator.h"
#include "adapters/UniversityRegistry.h"
#include "application/SubscriptionService.h"
#include "application/TaskService.h"
#include "desktop/MainWindow.h"
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
#include <QTabWidget>
#include <QTableView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>

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
                         QString("unknown"));
                QVERIFY(!control<QCheckBox>(*editor, "taskReminderEnabled")->isEnabled());
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
    void exactTimeEditorConvertsSchoolZoneAndRequiresReconfirmation() {
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
        QVERIFY(s.tasks.list().empty());
        confirm->setChecked(true);
        control<QPushButton>(editor, "saveTaskButton")->click();
        QCOMPARE(editor.result(), int(QDialog::Accepted));
        QCOMPARE(editor.saved().time.utcDateTime, std::string("2026-10-04T08:31:15Z"));
        QVERIFY(editor.saved().time.date.empty());
        QCOMPARE(editor.saved().time.confirmation, TimeConfirmation::Personal);
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
