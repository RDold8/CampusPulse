#include "adapters/RefreshCoordinator.h"
#include "application/TaskService.h"
#include "desktop/TaskEditorDialog.h"
#include "desktop/TaskPage.h"
#include "storage/Database.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteSourceRepository.h"
#include "storage/SqliteTaskRepository.h"
#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStackedWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <stdexcept>

using namespace campus;
namespace {
template <class T> T *control(QWidget &window, const char *name) {
    auto *result = window.findChild<T *>(name);
    if (!result)
        throw std::runtime_error(std::string("界面控件缺失：") + name);
    return result;
}
struct Session {
    SchoolPackage school;
    Database db;
    SqliteRepository noticeRepository;
    SqliteSourceRepository sourceRepository;
    SqliteTaskRepository taskRepository;
    NoticeService notices;
    SourceService sources;
    TaskService tasks;
    RefreshCoordinator coordinator;
    Session(const QString &path, SchoolPackage configuration)
        : school(std::move(configuration)), db(path), noticeRepository(db), sourceRepository(db),
          taskRepository(db), notices(noticeRepository, school.id.toStdString()),
          sources(school.id.toStdString(), school.catalog, sourceRepository),
          tasks(school.id.toStdString(), school.timeZone.toStdString(), taskRepository, notices),
          coordinator(school, notices, sources, nullptr, {0, 1000}) {
        Notice notice;
        notice.id = school.id.toStdString() + "-card-test-notice";
        notice.schoolId = school.id.toStdString();
        notice.sourceId = school.catalog.front().id;
        notice.sourceName = "卡片测试来源";
        notice.title = "【测试】学校公开通知";
        notice.url = school.officialHomepage.resolved(QUrl("card-test-notice.htm"))
                         .toString().toStdString();
        notice.publishedDate = "2026-10-06";
        notices.ingest({notice});
    }
    PersonalTask add(const std::string &title, const std::string &utc = "2027-01-01T08:00:15Z",
                     int minutesBefore = 0) {
        auto task = tasks.draft(school.id.toStdString() + "-card-test-notice");
        task.title = title;
        if (!utc.empty()) {
            task.time.precision = TimePrecision::DateTime;
            task.time.confirmation = TimeConfirmation::Personal;
            task.time.utcDateTime = utc;
            task.reminder.enabled = true;
            task.reminder.minutesBefore = minutesBefore;
        }
        return tasks.save(task, !utc.empty());
    }
};
QList<QFrame *> cards(TaskPage &page) {
    return control<QWidget>(page, "taskCardContent")
        ->findChildren<QFrame *>("taskCard", Qt::FindDirectChildrenOnly);
}
QFrame *card(TaskPage &page, const std::string &id) {
    for (auto *result : cards(page))
        if (result->property("taskId").toString().toStdString() == id)
            return result;
    throw std::runtime_error("当前卡片视图未显示指定待办");
}
void click(TaskPage &page, QWidget &widget, const QPoint &position = {}) {
    control<QScrollArea>(page, "taskCards")->ensureWidgetVisible(&widget);
    QApplication::processEvents();
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                      position.isNull() ? widget.rect().center() : position);
}
QString selectedListId(TaskPage &page) {
    auto *table = control<QTableView>(page, "taskTable");
    return table->model()->index(table->currentIndex().row(), 0).data(Qt::UserRole).toString();
}
QByteArray taskSnapshot(Database &database) {
    QJsonArray tables;
    for (const auto &sql : {"SELECT * FROM personal_task ORDER BY id",
                            "SELECT * FROM reminder_delivery ORDER BY task_id,trigger_utc"}) {
        QSqlQuery query(database.connection());
        if (!query.exec(QString::fromLatin1(sql)))
            throw std::runtime_error(query.lastError().text().toStdString());
        QJsonArray rows;
        while (query.next()) {
            QVariantList values;
            for (int column = 0; column < query.record().count(); ++column)
                values << query.value(column);
            rows.append(QJsonArray::fromVariantList(values));
        }
        tables.append(rows);
    }
    return QJsonDocument(tables).toJson(QJsonDocument::Compact);
}
void show(TaskPage &page) {
    page.resize(1100, 820);
    page.show();
}
} // namespace

class TaskCardTests final : public QObject {
    Q_OBJECT
  private slots:
    void cardsAreDefaultAndShareFiltersWithList() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Session session(directory.filePath("filters.sqlite"), SchoolPackage::load(CONFIG_FILE));
        const auto payment = session.add("【测试】重修缴费", "2027-01-01T08:00:15Z", 10);
        const auto scholarship = session.add("【测试】奖学金材料待定", "");
        const auto activity = session.add("【测试】校园活动");
        session.tasks.setStatus(activity.id, TaskStatus::Completed);
        TaskPage page(session.school, session.tasks, session.coordinator);
        show(page);
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        QCOMPARE(control<QComboBox>(page, "taskViewMode")->currentData().toString(), QString("cards"));
        QCOMPARE(control<QStackedWidget>(page, "taskViews")->currentIndex(), 0);
        QVERIFY(control<QScrollArea>(page, "taskCards")->isVisible());
        QVERIFY(!control<QTableView>(page, "taskTable")->isVisible());
        QCOMPARE(cards(page).size(), 3);
        const auto screenshot = qEnvironmentVariable("CAMPUSPULSE_CARD_SCREENSHOT");
        if (!screenshot.isEmpty())
            QVERIFY2(page.grab().save(screenshot), qPrintable("卡片截图保存失败：" + screenshot));
        auto *status = control<QComboBox>(page, "taskStatusFilter");
        status->setCurrentIndex(status->findData("active"));
        QCOMPARE(cards(page).size(), 2);
        QCOMPARE(control<QTableView>(page, "taskTable")->model()->rowCount(), 2);
        auto *date = control<QComboBox>(page, "taskDateFilter");
        date->setCurrentIndex(date->findData("unknown"));
        QCOMPARE(cards(page).size(), 1);
        QCOMPARE(card(page, scholarship.id)->property("taskId").toString(),
                 QString::fromStdString(scholarship.id));
        date->setCurrentIndex(date->findData("all"));
        control<QLineEdit>(page, "taskSearch")->setText("缴费");
        QCOMPARE(cards(page).size(), 1);
        QVERIFY(card(page, payment.id));
        control<QLineEdit>(page, "taskSearch")->setText("不存在的事项");
        QVERIFY(cards(page).isEmpty());
        QTRY_VERIFY(control<QLabel>(page, "taskCardsEmpty")->isVisible());
        QCOMPARE(control<QTableView>(page, "taskTable")->model()->rowCount(), 0);
        control<QLineEdit>(page, "taskSearch")->clear();
        QCOMPARE(cards(page).size(), 2);
    }
    void selectionMovesBothWaysBetweenCardsAndList() {
        QTemporaryDir directory;
        Session session(directory.filePath("selection.sqlite"), SchoolPackage::load(CONFIG_FILE));
        const auto first = session.add("【测试】第一张卡片");
        const auto second = session.add("【测试】第二张卡片");
        TaskPage page(session.school, session.tasks, session.coordinator);
        show(page);
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        click(page, *card(page, second.id), QPoint(10, 10));
        QCOMPARE(selectedListId(page), QString::fromStdString(second.id));
        QVERIFY(card(page, second.id)->property("taskSelected").toBool());
        auto *mode = control<QComboBox>(page, "taskViewMode");
        mode->setFocus();
        QTest::keyClick(mode, Qt::Key_Down);
        QCOMPARE(control<QStackedWidget>(page, "taskViews")->currentIndex(), 1);
        QCOMPARE(selectedListId(page), QString::fromStdString(second.id));
        auto *table = control<QTableView>(page, "taskTable");
        QVERIFY(table->isVisible());
        QModelIndex row;
        for (int index = 0; index < table->model()->rowCount(); ++index)
            if (table->model()->index(index, 0).data(Qt::UserRole).toString() ==
                QString::fromStdString(first.id))
                row = table->model()->index(index, 0);
        QVERIFY(row.isValid());
        QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                          table->visualRect(row).center());
        QCOMPARE(selectedListId(page), QString::fromStdString(first.id));
        mode->setFocus();
        QTest::keyClick(mode, Qt::Key_Up);
        QCOMPARE(control<QStackedWidget>(page, "taskViews")->currentIndex(), 0);
        QVERIFY(card(page, first.id)->property("taskSelected").toBool());
        QVERIFY(!card(page, second.id)->property("taskSelected").toBool());
        page.reload();
        QCOMPARE(selectedListId(page), QString::fromStdString(first.id));
        QVERIFY(card(page, first.id)->property("taskSelected").toBool());
    }
    void actualAlarmDatesPrecisionsAndInactiveStates() {
        QTemporaryDir directory;
        Session session(directory.filePath("times.sqlite"), SchoolPackage::load(CONFIG_FILE));
        const auto exact = session.add("【测试】提前十分钟", "2027-01-01T08:00:15Z", 10);
        auto date = session.tasks.draft(session.school.id.toStdString() + "-card-test-notice");
        date.title = "【测试】只有日期";
        date.time.precision = TimePrecision::DateOnly;
        date.time.confirmation = TimeConfirmation::Personal;
        date.time.date = "2027-01-02";
        date.reminder.enabled = true;
        date.reminder.daysBefore = 2;
        date.reminder.dateOnlyAt = "18:20";
        date = session.tasks.save(date, true);
        auto disabled = session.add("【测试】提醒关闭");
        disabled.reminder.enabled = false;
        disabled = session.tasks.save(disabled);
        const auto completed = session.add("【测试】已经完成");
        session.tasks.setStatus(completed.id, TaskStatus::Completed);
        const auto cancelled = session.add("【测试】已经取消");
        session.tasks.setStatus(cancelled.id, TaskStatus::Cancelled);
        TaskPage page(session.school, session.tasks, session.coordinator);
        show(page);
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        QCOMPARE(control<QLabel>(*card(page, exact.id), "taskCardTime")->text(),
                 QString("2027-01-01 16:00:15"));
        const auto alarm = control<QLabel>(*card(page, exact.id), "taskCardReminder")->text();
        QVERIFY(alarm.contains("2027-01-01 15:50:15"));
        QVERIFY(alarm.contains("Asia/Shanghai"));
        QCOMPARE(control<QLabel>(*card(page, date.id), "taskCardTime")->text(),
                 QString("2027-01-02 · 仅日期"));
        QVERIFY(control<QLabel>(*card(page, date.id), "taskCardReminder")
                    ->text().contains("2026-12-31 18:20"));
        QCOMPARE(control<QLabel>(*card(page, disabled.id), "taskCardReminder")->text(),
                 QString("提醒关闭"));
        QCOMPARE(control<QLabel>(*card(page, completed.id), "taskCardReminder")->text(),
                 QString("已完成 · 不再提醒"));
        QCOMPARE(control<QLabel>(*card(page, cancelled.id), "taskCardReminder")->text(),
                 QString("已取消 · 不再提醒"));
        QCOMPARE(session.tasks.find(exact.id), exact);
        QCOMPARE(session.tasks.find(date.id), date);
    }
    void completeUndoCancelRestoreFromCardControls() {
        QTemporaryDir directory;
        Session session(directory.filePath("actions.sqlite"), SchoolPackage::load(CONFIG_FILE));
        const auto original = session.add("【测试】操作不改提醒时间", "2027-01-01T08:00:15Z", 17);
        TaskPage page(session.school, session.tasks, session.coordinator);
        show(page);
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        QSignalSpy changes(&page, &TaskPage::tasksChanged);
        click(page, *control<QPushButton>(*card(page, original.id), "taskCardCompleteButton"));
        QCOMPARE(session.tasks.find(original.id).status, TaskStatus::Completed);
        QCOMPARE(control<QPushButton>(*card(page, original.id), "taskCardCompleteButton")->text(),
                 QString("撤销完成"));
        QCOMPARE(control<QLabel>(*card(page, original.id), "taskCardReminder")->text(),
                 QString("已完成 · 不再提醒"));
        click(page, *control<QPushButton>(*card(page, original.id), "taskCardCompleteButton"));
        QCOMPARE(session.tasks.find(original.id).status, TaskStatus::NotStarted);
        auto *more = control<QPushButton>(*card(page, original.id), "taskCardMoreButton");
        auto *menu = more->menu();
        QVERIFY(menu);
        QAction *cancel = nullptr;
        for (auto *action : menu->actions())
            if (action->text() == "取消待办")
                cancel = action;
        QVERIFY(cancel);
        // Schedule the menu click after Qt has opened and laid out the native popup.
        control<QScrollArea>(page, "taskCards")->ensureWidgetVisible(more);
        QApplication::processEvents();
        QTimer::singleShot(0, &page, [menu, cancel] {
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(cancel).center());
        });
        QTest::mouseClick(more, Qt::LeftButton);
        QTRY_COMPARE(session.tasks.find(original.id).status, TaskStatus::Cancelled);
        QCOMPARE(control<QPushButton>(*card(page, original.id), "taskCardCompleteButton")->text(),
                 QString("恢复待办"));
        click(page, *control<QPushButton>(*card(page, original.id), "taskCardCompleteButton"));
        const auto restored = session.tasks.find(original.id);
        QCOMPARE(restored.status, TaskStatus::NotStarted);
        QCOMPARE(restored.time, original.time);
        QCOMPARE(restored.reminder, original.reminder);
        QCOMPARE(restored.calendarUid, original.calendarUid);
        QCOMPARE(changes.count(), 4);
        QCOMPARE(selectedListId(page), QString::fromStdString(original.id));
    }
    void cardEditAndDoubleClickUseTheSameEditorWithoutMutatingOnCancel() {
        QTemporaryDir directory;
        Session session(directory.filePath("edit.sqlite"), SchoolPackage::load(CONFIG_FILE));
        session.add("【测试】另一事项");
        const auto original = session.add("【测试】编辑这一事项", "2027-01-01T08:00:15Z", 17);
        TaskPage page(session.school, session.tasks, session.coordinator);
        show(page);
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        const auto before = taskSnapshot(session.db);
        int editors = 0;
        const auto rejectEditor = [&] {
            auto *editor = qobject_cast<TaskEditorDialog *>(QApplication::activeModalWidget());
            if (!editor)
                return;
            ++editors;
            const auto title = control<QLineEdit>(*editor, "taskTitle")->text();
            editor->reject();
            QCOMPARE(title, QString::fromStdString(original.title));
        };
        auto *editButton = control<QPushButton>(*card(page, original.id), "taskCardEditButton");
        control<QScrollArea>(page, "taskCards")->ensureWidgetVisible(editButton);
        QApplication::processEvents();
        QTimer::singleShot(0, &page, rejectEditor);
        QTest::mouseClick(editButton, Qt::LeftButton);
        QCOMPARE(editors, 1);
        auto *target = card(page, original.id);
        control<QScrollArea>(page, "taskCards")->ensureWidgetVisible(target);
        QApplication::processEvents();
        QTimer::singleShot(0, &page, rejectEditor);
        QTest::mouseDClick(target, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        QCOMPARE(editors, 2);
        QCOMPARE(taskSnapshot(session.db), before);
        QCOMPARE(selectedListId(page), QString::fromStdString(original.id));
    }
    void reminderTestOnlySignalsAndSchoolDataStaysIsolated() {
        QTemporaryDir directory;
        const auto firstSchool = SchoolPackage::load(CONFIG_FILE);
        auto secondSchool = firstSchool;
        secondSchool.id = "cn-card-test-second";
        secondSchool.name = "【测试】第二学校";
        for (auto &source : secondSchool.catalog)
            source.schoolId = secondSchool.id.toStdString();
        for (auto &source : secondSchool.sources)
            source.schoolId = secondSchool.id;
        // Two school services on the same temporary database exercise actual school partitioning.
        const auto path = directory.filePath("schools.sqlite");
        Session first(path, firstSchool);
        Session second(path, secondSchool);
        const auto firstTask = first.add("【测试】第一学校事项");
        const auto secondTask = second.add("【测试】第二学校事项");
        TaskPage firstPage(first.school, first.tasks, first.coordinator);
        TaskPage secondPage(second.school, second.tasks, second.coordinator);
        show(firstPage);
        QVERIFY(QTest::qWaitForWindowExposed(&firstPage));
        QCOMPARE(cards(firstPage).size(), 1);
        QCOMPARE(cards(secondPage).size(), 1);
        QVERIFY(card(firstPage, firstTask.id));
        QVERIFY(card(secondPage, secondTask.id));
        const auto before = taskSnapshot(first.db);
        QSignalSpy test(&firstPage, &TaskPage::reminderTestRequested);
        QSignalSpy changes(&firstPage, &TaskPage::tasksChanged);
        QTest::mouseClick(control<QPushButton>(firstPage, "testTaskReminderButton"), Qt::LeftButton);
        QCOMPARE(test.count(), 1);
        QCOMPARE(changes.count(), 0);
        QCOMPARE(taskSnapshot(first.db), before);
        click(firstPage, *control<QPushButton>(*card(firstPage, firstTask.id), "taskCardCompleteButton"));
        QCOMPARE(first.tasks.find(firstTask.id).status, TaskStatus::Completed);
        QCOMPARE(second.tasks.find(secondTask.id), secondTask);
        QVERIFY(!second.taskRepository.find(second.school.id.toStdString(), firstTask.id));
        // An independent local database must never reuse the first school's tasks either.
        Session fresh(directory.filePath("fresh.sqlite"), firstSchool);
        TaskPage empty(fresh.school, fresh.tasks, fresh.coordinator);
        QVERIFY(cards(empty).isEmpty());
        QVERIFY(control<QLabel>(empty, "taskCardsEmpty"));
    }
};
QTEST_MAIN(TaskCardTests)
#include "TaskCardTests.moc"
