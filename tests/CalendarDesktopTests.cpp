#include "desktop/CalendarPage.h"
#include "desktop/TaskEditorDialog.h"
#include "storage/Database.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteTaskRepository.h"
#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace campus;
namespace {
template <class T> T *control(QWidget &widget, const char *name) {
    auto *result = widget.findChild<T *>(name);
    if (!result)
        throw std::runtime_error(std::string("日历控件缺失：") + name);
    return result;
}
struct Session {
    QTemporaryDir folder;
    SchoolPackage school = SchoolPackage::load(CONFIG_FILE);
    Database db;
    SqliteRepository noticeRepository;
    SqliteTaskRepository taskRepository;
    NoticeService notices;
    TaskService tasks;
    Session()
        : db(folder.filePath("calendar-ui.sqlite")), noticeRepository(db), taskRepository(db),
          notices(noticeRepository, school.id.toStdString()),
          tasks(school.id.toStdString(), school.timeZone.toStdString(), taskRepository, notices,
                [] { return "2026-10-03T02:00:00Z"; }) {
        Notice n;
        n.id = "calendar-ui-notice";
        n.schoolId = school.id.toStdString();
        n.title = "【演示】奖学金申请与缴费通知";
        n.url = "https://jwc.neepu.edu.cn/info/123/456.htm";
        n.sourceId = "academic-affairs-notices";
        n.sourceName = "教务处通知公告";
        n.publishedDate = "2026-10-01";
        notices.ingest({n});
    }
    PersonalTask date(const std::string &title, const std::string &value = "2026-10-06") {
        auto task = tasks.draft("calendar-ui-notice");
        task.title = title;
        task.time.precision = TimePrecision::DateOnly;
        task.time.date = value;
        task.time.confirmation = TimeConfirmation::Personal;
        return tasks.save(task, true);
    }
};
void selectDate(CalendarPage &page, const QDate &date) {
    auto *calendar = control<QCalendarWidget>(page, "calendarMonth");
    calendar->setSelectedDate(date);
    calendar->showSelectedDate();
    page.reload();
}
} // namespace
class CalendarDesktopTests final : public QObject {
    Q_OBJECT
  private slots:
    void monthDayFiltersUnknownDatesAndNavigation() {
        Session s;
        s.date("【演示】缴费");
        auto completed = s.date("【演示】已完成活动");
        s.tasks.setStatus(completed.id, TaskStatus::Completed);
        s.date("【演示】十一月申请", "2026-11-02");
        auto unknown = s.tasks.draft("calendar-ui-notice");
        unknown.title = "【演示】日期待定";
        s.tasks.save(unknown);
        CalendarPage page(s.school, s.tasks);
        selectDate(page, QDate(2026, 10, 6));
        auto *table = control<QTableView>(page, "calendarDayTable");
        QCOMPARE(table->model()->rowCount(), 1);
        QVERIFY(control<QLabel>(page, "calendarSummary")->text().contains("未确认 1 项"));
        QVERIFY(control<QLabel>(page, "calendarSummary")->text().contains("已确认时间 1 项"));
        control<QComboBox>(page, "calendarVisibility")->setCurrentIndex(1);
        QCOMPARE(table->model()->rowCount(), 2);
        QSignalSpy notice(&page, &CalendarPage::noticeRequested);
        control<QPushButton>(page, "calendarOpenNoticeButton")->click();
        QCOMPARE(notice.count(), 1);
        QCOMPARE(notice.at(0).at(0).toString(), QString("calendar-ui-notice"));
        QSignalSpy tasks(&page, &CalendarPage::showTasksRequested);
        control<QPushButton>(page, "calendarShowTasksButton")->click();
        QCOMPARE(tasks.count(), 1);
        selectDate(page, QDate(2026, 10, 7));
        QCOMPARE(table->model()->rowCount(), 0);
        QVERIFY(!control<QPushButton>(page, "calendarExportSelectedButton")->isEnabled());
        QVERIFY(control<QPushButton>(page, "calendarExportMonthButton")->isEnabled());
        selectDate(page, QDate(2026, 12, 1));
        QVERIFY(!control<QPushButton>(page, "calendarExportMonthButton")->isEnabled());
    }
    void sharedEditorMovesTaskWithStableIdentity() {
        Session s;
        const auto task = s.date("【演示】申请材料");
        CalendarPage page(s.school, s.tasks);
        page.show();
        selectDate(page, QDate(2026, 10, 6));
        QSignalSpy changed(&page, &CalendarPage::tasksChanged);
        bool editorVisited = false;
        QTimer::singleShot(0, &page, [&] {
            auto *editor = qobject_cast<TaskEditorDialog *>(QApplication::activeModalWidget());
            QVERIFY(editor);
            QTimer::singleShot(5000, editor, &QDialog::reject);
            editorVisited = true;
            control<QDateEdit>(*editor, "taskDate")->setDate(QDate(2026, 11, 4));
            control<QLineEdit>(*editor, "taskTitle")->setText("【演示】修改后的申请材料");
            control<QCheckBox>(*editor, "taskTimeConfirmed")->setChecked(true);
            control<QPushButton>(*editor, "saveTaskButton")->click();
            QCOMPARE(editor->result(), int(QDialog::Accepted));
        });
        control<QPushButton>(page, "calendarEditTaskButton")->click();
        QVERIFY(editorVisited);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(s.tasks.find(task.id).calendarUid, task.calendarUid);
        QCOMPARE(s.tasks.find(task.id).time.date, std::string("2026-11-04"));
        QCOMPARE(s.tasks.find(task.id).revision, task.revision + 1);
        QCOMPARE(control<QCalendarWidget>(page, "calendarMonth")->selectedDate(),
                 QDate(2026, 11, 4));
        QCOMPARE(control<QTableView>(page, "calendarDayTable")->model()->rowCount(), 1);
        selectDate(page, QDate(2026, 10, 6));
        QCOMPARE(control<QTableView>(page, "calendarDayTable")->model()->rowCount(), 0);
    }
    void selectedAndMonthExportsReadLatestTasksAndReportFailures() {
        Session s;
        const auto first = s.date("【演示】缴费");
        s.date("【演示】竞赛报名");
        s.date("【演示】另一天活动", "2026-10-07");
        s.date("【演示】下月申请", "2026-11-01");
        CalendarPage page(s.school, s.tasks);
        selectDate(page, QDate(2026, 10, 6));
        auto *table = control<QTableView>(page, "calendarDayTable");
        const auto monthPath = s.folder.filePath("month.ics");
        QVERIFY(page.exportToFile(monthPath, true, false));
        QFile month(monthPath);
        QVERIFY(month.open(QIODevice::ReadOnly));
        QCOMPARE(month.readAll().count("BEGIN:VEVENT"), 3);
        table->selectAll();
        const auto selectionPath = s.folder.filePath("selection.ics");
        QVERIFY(page.exportToFile(selectionPath, false, false));
        QFile selection(selectionPath);
        QVERIFY(selection.open(QIODevice::ReadOnly));
        QCOMPARE(selection.readAll().count("BEGIN:VEVENT"), 2);
        auto moved = s.tasks.find(first.id);
        moved.time.date = "2026-11-01";
        s.tasks.save(moved, true);
        const auto movedSelectionPath = s.folder.filePath("moved-selection.ics");
        QVERIFY(page.exportToFile(movedSelectionPath, false, false));
        QFile movedSelection(movedSelectionPath);
        QVERIFY(movedSelection.open(QIODevice::ReadOnly));
        const auto movedSelectionData = movedSelection.readAll();
        QCOMPARE(movedSelectionData.count("BEGIN:VEVENT"), 2);
        QVERIFY(movedSelectionData.contains("DTSTART;VALUE=DATE:20261101"));
        const auto freshPath = s.folder.filePath("fresh.ics");
        QVERIFY(page.exportToFile(freshPath, true, false));
        QFile fresh(freshPath);
        QVERIFY(fresh.open(QIODevice::ReadOnly));
        QCOMPARE(fresh.readAll().count("BEGIN:VEVENT"), 2);
        QVERIFY(!page.exportToFile(s.folder.path(), true, false));
        QVERIFY(control<QLabel>(page, "calendarStatus")->text().startsWith("日历导出失败："));
        table->clearSelection();
        QVERIFY(!page.exportToFile(s.folder.filePath("empty.ics"), false, false));
        QVERIFY(!QFile::exists(s.folder.filePath("empty.ics")));
        QVERIFY(control<QLabel>(page, "calendarImportHint")->text().contains("不会自动同步"));
    }
};
QTEST_MAIN(CalendarDesktopTests)
#include "CalendarDesktopTests.moc"
