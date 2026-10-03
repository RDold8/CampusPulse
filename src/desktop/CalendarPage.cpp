#include "desktop/CalendarPage.h"
#include "adapters/IcsExporter.h"
#include "desktop/TaskDisplay.h"
#include "desktop/TaskEditorDialog.h"
#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTimeZone>
#include <QVBoxLayout>
#include <set>
#include <stdexcept>

namespace campus {
namespace {
std::string localDate(const std::string &utc, const std::string &timeZone) {
    const QTimeZone zone(QByteArray::fromStdString(timeZone));
    const auto time = QDateTime::fromString(QString::fromStdString(utc), Qt::ISODate);
    if (!zone.isValid() || !time.isValid())
        throw std::invalid_argument("日历学校时区或已确认时刻无效");
    return time.toTimeZone(zone).date().toString(Qt::ISODate).toStdString();
}
QString calendarTimeText(const TaskTime &time, const QString &displayZone) {
    if (time.precision == TimePrecision::DateOnly)
        return "全天（仅日期）";
    return QDateTime::fromString(QString::fromStdString(time.utcDateTime), Qt::ISODate)
        .toTimeZone(QTimeZone(displayZone.toUtf8()))
        .toString("HH:mm:ss");
}
} // namespace
CalendarPage::CalendarPage(const SchoolPackage &school, TaskService &tasks, QWidget *parent)
    : QWidget(parent), school_(school), tasks_(tasks),
      calendarService_(tasks, school.timeZone.toStdString(), localDate) {
    setObjectName("calendarPage");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(14);
    auto *title = new QLabel("我的日历");
    title->setObjectName("heading");
    layout->addWidget(title);
    auto *tip = new QLabel("这里只显示你已确认时间的待办。日期待定的事项保留在“我的待办”。");
    tip->setObjectName("scope");
    tip->setWordWrap(true);
    layout->addWidget(tip);
    auto *controls = new QHBoxLayout;
    auto *today = new QPushButton("今天");
    today->setObjectName("calendarTodayButton");
    visibility_ = new QComboBox;
    visibility_->setObjectName("calendarVisibility");
    visibility_->addItem("未完成待办", false);
    visibility_->addItem("全部（含完成与取消）", true);
    auto *tasksButton = new QPushButton("查看日期待定 / 我的待办");
    tasksButton->setObjectName("calendarShowTasksButton");
    controls->addWidget(today);
    controls->addWidget(visibility_);
    controls->addWidget(new QLabel("显示时区：" + school_.timeZone));
    controls->addStretch();
    controls->addWidget(tasksButton);
    layout->addLayout(controls);
    summary_ = new QLabel;
    summary_->setObjectName("calendarSummary");
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    auto *splitter = new QSplitter;
    calendar_ = new QCalendarWidget;
    calendar_->setObjectName("calendarMonth");
    calendar_->setGridVisible(true);
    calendar_->setFirstDayOfWeek(Qt::Monday);
    calendar_->setMinimumDate(QDate(1, 1, 1));
    calendar_->setMaximumDate(QDate(9999, 12, 30));
    const QTimeZone zone(school_.timeZone.toUtf8());
    if (!zone.isValid())
        throw std::invalid_argument("日历学校时区无效");
    calendar_->setSelectedDate(QDateTime::currentDateTimeUtc().toTimeZone(zone).date());
    splitter->addWidget(calendar_);
    auto *dayPanel = new QWidget;
    auto *dayLayout = new QVBoxLayout(dayPanel);
    dayLayout->setContentsMargins(10, 0, 0, 0);
    day_ = new QLabel;
    day_->setObjectName("calendarDayHeading");
    dayLayout->addWidget(day_);
    model_ = new QStandardItemModel(this);
    table_ = new QTableView;
    table_->setObjectName("calendarDayTable");
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    dayLayout->addWidget(table_, 2);
    detail_ = new QTextBrowser;
    detail_->setObjectName("calendarTaskDetail");
    detail_->setOpenLinks(false);
    dayLayout->addWidget(detail_, 1);
    splitter->addWidget(dayPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);
    auto *buttons = new QHBoxLayout;
    edit_ = new QPushButton("编辑待办");
    edit_->setObjectName("calendarEditTaskButton");
    open_ = new QPushButton("查看关联通知");
    open_->setObjectName("calendarOpenNoticeButton");
    exportSelected_ = new QPushButton("导出选中任务 .ics");
    exportSelected_->setObjectName("calendarExportSelectedButton");
    exportMonth_ = new QPushButton("导出当前月份 .ics");
    exportMonth_->setObjectName("calendarExportMonthButton");
    alarms_ = new QCheckBox("包含已启用的提醒");
    alarms_->setObjectName("calendarIncludeAlarms");
    buttons->addWidget(edit_);
    buttons->addWidget(open_);
    buttons->addStretch();
    buttons->addWidget(alarms_);
    buttons->addWidget(exportSelected_);
    buttons->addWidget(exportMonth_);
    layout->addLayout(buttons);
    status_ = new QLabel;
    status_->setObjectName("calendarStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *boundary =
        new QLabel("将 .ics 文件传到手机后，用系统日历导入。文件导入后，本机的修改与完成状态"
                   "不会自动同步到手机；重复导入能否更新取决于手机日历。");
    boundary->setObjectName("calendarImportHint");
    boundary->setWordWrap(true);
    layout->addWidget(boundary);
    connect(calendar_, &QCalendarWidget::selectionChanged, this, [this] { reload(); });
    connect(calendar_, &QCalendarWidget::currentPageChanged, this, [this] { reload(); });
    connect(visibility_, &QComboBox::currentIndexChanged, this, [this] { reload(); });
    connect(today, &QPushButton::clicked, this, [this] {
        calendar_->setSelectedDate(QDateTime::currentDateTimeUtc()
                                       .toTimeZone(QTimeZone(school_.timeZone.toUtf8()))
                                       .date());
        calendar_->showSelectedDate();
    });
    connect(tasksButton, &QPushButton::clicked, this, &CalendarPage::showTasksRequested);
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { selectionChanged(); });
    connect(table_, &QTableView::doubleClicked, this, [this] { edit(); });
    connect(edit_, &QPushButton::clicked, this, &CalendarPage::edit);
    connect(open_, &QPushButton::clicked, this, [this] {
        if (const auto *item = selected())
            emit noticeRequested(QString::fromStdString(item->view.task.noticeId));
    });
    connect(exportSelected_, &QPushButton::clicked, this, [this] { chooseExport(false); });
    connect(exportMonth_, &QPushButton::clicked, this, [this] { chooseExport(true); });
    reload();
}
QString CalendarPage::selectedId() const {
    const auto row = table_->currentIndex().row();
    return row < 0 ? QString{} : model_->index(row, 0).data(Qt::UserRole).toString();
}
const CalendarItem *CalendarPage::selected() const {
    const auto id = selectedId().toStdString();
    for (const auto &item : monthItems_)
        if (item.view.task.id == id)
            return &item;
    return nullptr;
}
void CalendarPage::reload(const QString &selectId) {
    try {
        const auto keep = selectId.isEmpty() ? selectedId() : selectId;
        monthItems_ = calendarService_.month(calendar_->yearShown(), calendar_->monthShown(),
                                             visibility_->currentData().toBool());
        calendar_->setDateTextFormat(QDate(), QTextCharFormat());
        const QSignalBlocker block(table_->selectionModel());
        model_->clear();
        model_->setHorizontalHeaderLabels({"办理事项", "时间", "状态", "原文"});
        int selectedRow = -1;
        std::set<std::string> activeDays;
        std::set<std::string> historyDays;
        for (const auto &item : monthItems_) {
            const auto &task = item.view.task;
            (TaskService::active(task.status) ? activeDays : historyDays).insert(item.localDate);
            if (QString::fromStdString(item.localDate) !=
                calendar_->selectedDate().toString(Qt::ISODate))
                continue;
            QList<QStandardItem *> row;
            for (const auto &value : QStringList{QString::fromStdString(task.title),
                                                 calendarTimeText(task.time, school_.timeZone),
                                                 taskStatusText(task.status),
                                                 item.view.needsReview() ? "待复核" : "暂无更新"})
                row << new QStandardItem(value);
            row[0]->setData(QString::fromStdString(task.id), Qt::UserRole);
            model_->appendRow(row);
            if (QString::fromStdString(task.id) == keep)
                selectedRow = model_->rowCount() - 1;
        }
        for (const auto &date : historyDays) {
            QTextCharFormat format;
            format.setForeground(QColor("#64748b"));
            calendar_->setDateTextFormat(
                QDate::fromString(QString::fromStdString(date), Qt::ISODate), format);
        }
        for (const auto &date : activeDays) {
            QTextCharFormat format;
            format.setFontWeight(QFont::Bold);
            format.setBackground(QColor("#dbeafe"));
            format.setForeground(QColor("#163c73"));
            calendar_->setDateTextFormat(
                QDate::fromString(QString::fromStdString(date), Qt::ISODate), format);
        }
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int column = 1; column < 4; ++column)
            table_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
        const auto selectedDate = calendar_->selectedDate();
        day_->setText(selectedDate.toString("yyyy年M月d日") +
                      QString(" · %1 项").arg(model_->rowCount()));
        summary_->setText(
            QString("%1年%2月 · 已确认时间 %3 项 · 日期待定 / 未确认 %4 项（保留在待办）")
                .arg(calendar_->yearShown())
                .arg(calendar_->monthShown())
                .arg(monthItems_.size())
                .arg(calendarService_.undatedCount()));
        table_->setCurrentIndex(model_->index(selectedRow >= 0     ? selectedRow
                                              : model_->rowCount() ? 0
                                                                   : -1,
                                              0));
        exportMonth_->setEnabled(!monthItems_.empty());
        selectionChanged();
        status_->setText("加粗蓝色日期有未完成事项；完成与取消可切换到全部查看。");
    } catch (const std::exception &e) {
        model_->clear();
        monthItems_.clear();
        exportMonth_->setEnabled(false);
        selectionChanged();
        status_->setText("日历加载失败：" + QString::fromUtf8(e.what()));
    }
}
void CalendarPage::selectionChanged() {
    const auto *item = selected();
    edit_->setEnabled(item != nullptr);
    open_->setEnabled(item != nullptr);
    exportSelected_->setEnabled(!table_->selectionModel()->selectedRows().isEmpty());
    if (!item) {
        detail_->setPlainText(
            "这一天暂无符合条件的已确认事项。选择有标记的日期，或在我的待办确认时间。");
        return;
    }
    const auto &task = item->view.task;
    detail_->setPlainText(
        "办理事项：" + QString::fromStdString(task.title) + "\n状态：" +
        taskStatusText(task.status) + "\n办理时间：" + taskTimeText(task.time) + "\n时间来源：" +
        (task.time.confirmation == TimeConfirmation::OriginalText ? QString("核对原文后的时间")
                                                                  : QString("个人计划时间")) +
        "\n时间说明：" + QString::fromStdString(task.time.evidence) + "\n\n个人备注：\n" +
        QString::fromStdString(task.notes) + "\n\n关联通知：" +
        QString::fromStdString(item->view.noticeTitle) +
        (item->view.needsReview() ? "\n原文已更新，请复核确认时间。" : ""));
}
void CalendarPage::edit() {
    try {
        if (const auto *item = selected()) {
            TaskEditorDialog editor(school_, tasks_, tasks_.find(item->view.task.id),
                                    QString::fromStdString(item->view.noticeTitle), this);
            if (editor.exec() == QDialog::Accepted) {
                const auto &saved = editor.saved();
                if (CalendarService::hasConfirmedTime(saved)) {
                    const auto date =
                        saved.time.precision == TimePrecision::DateOnly
                            ? saved.time.date
                            : localDate(saved.time.utcDateTime, school_.timeZone.toStdString());
                    const QSignalBlocker block(calendar_);
                    calendar_->setSelectedDate(
                        QDate::fromString(QString::fromStdString(date), Qt::ISODate));
                    calendar_->showSelectedDate();
                }
                reload(QString::fromStdString(saved.id));
                emit tasksChanged();
            }
        }
    } catch (const std::exception &e) {
        status_->setText("待办编辑失败：" + QString::fromUtf8(e.what()));
    }
}
bool CalendarPage::exportToFile(const QString &path, bool wholeMonth, bool includeAlarm) {
    try {
        // Reload the task facts for export even if the page has not yet been revisited.
        const auto items =
            wholeMonth ? calendarService_.month(calendar_->yearShown(), calendar_->monthShown(),
                                                visibility_->currentData().toBool())
                       : calendarService_.items(visibility_->currentData().toBool());
        std::vector<CalendarItem> selectedItems;
        std::set<std::string> selectedIds;
        if (!wholeMonth)
            for (const auto &index : table_->selectionModel()->selectedRows())
                selectedIds.insert(
                    model_->index(index.row(), 0).data(Qt::UserRole).toString().toStdString());
        for (const auto &item : items)
            if (wholeMonth || selectedIds.contains(item.view.task.id))
                selectedItems.push_back(item);
        IcsExporter::write(path, selectedItems, {includeAlarm});
        status_->setText(QString("已导出 %1 项到 %2；手机导入后请核对日期与提醒。")
                             .arg(selectedItems.size())
                             .arg(QFileInfo(path).fileName()));
        return true;
    } catch (const std::exception &e) {
        status_->setText("日历导出失败：" + QString::fromUtf8(e.what()));
        return false;
    }
}
void CalendarPage::chooseExport(bool wholeMonth) {
    const auto name =
        QString("CampusPulse-%1-%2.ics")
            .arg(school_.id)
            .arg(QDate(calendar_->yearShown(), calendar_->monthShown(), 1).toString("yyyy-MM"));
    auto path = QFileDialog::getSaveFileName(this, "导出日历文件", name, "日历文件 (*.ics)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".ics", Qt::CaseInsensitive))
        path += ".ics";
    exportToFile(path, wholeMonth, alarms_->isChecked());
}
} // namespace campus
