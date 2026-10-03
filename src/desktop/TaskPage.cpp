#include "desktop/TaskPage.h"
#include "desktop/TaskDisplay.h"
#include "desktop/TaskEditorDialog.h"
#include <QComboBox>
#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextBrowser>
#include <QTimeZone>
#include <QVBoxLayout>
namespace campus {
TaskPage::TaskPage(const SchoolPackage &school, TaskService &service,
                   RefreshCoordinator &coordinator, QWidget *parent)
    : QWidget(parent), school_(school), service_(service) {
    setObjectName("taskPage");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(14);
    auto *heading = new QLabel("我的待办");
    heading->setObjectName("heading");
    layout->addWidget(heading);
    auto *scope = new QLabel(
        school.name +
        " · 从通知详情加入办理事项，时间由你确认。完成与取消状态保存在本机，原文更新后可复核。");
    scope->setObjectName("scope");
    scope->setWordWrap(true);
    layout->addWidget(scope);
    auto *filters = new QHBoxLayout;
    search_ = new QLineEdit;
    search_->setObjectName("taskSearch");
    search_->setClearButtonEnabled(true);
    search_->setPlaceholderText("搜索办理事项、备注或关联通知");
    filters->addWidget(search_, 1);
    filter_ = new QComboBox;
    filter_->setObjectName("taskStatusFilter");
    filter_->addItem("全部状态", "all");
    filter_->addItem("未完成", "active");
    for (auto s : {TaskStatus::NotStarted, TaskStatus::InProgress, TaskStatus::Completed,
                   TaskStatus::Cancelled})
        filter_->addItem(taskStatusText(s), QString::fromStdString(taskStatusKey(s)));
    filters->addWidget(filter_);
    dateFilter_ = new QComboBox;
    dateFilter_->setObjectName("taskDateFilter");
    dateFilter_->addItem("全部日期", "all");
    dateFilter_->addItem("日期待定", "unknown");
    dateFilter_->addItem("已逾期", "overdue");
    filters->addWidget(dateFilter_);
    auto *go = new QPushButton("去通知页添加");
    go->setObjectName("goToNoticesButton");
    filters->addWidget(go);
    layout->addLayout(filters);
    summary_ = new QLabel;
    summary_->setObjectName("taskSummary");
    layout->addWidget(summary_);
    auto *buttons = new QHBoxLayout;
    const auto button = [&](const QString &text, const char *name) {
        auto *b = new QPushButton(text);
        b->setObjectName(name);
        buttons->addWidget(b);
        return b;
    };
    edit_ = button("编辑", "editTaskButton");
    start_ = button("开始办理", "startTaskButton");
    complete_ = button("标记完成", "completeTaskButton");
    undo_ = button("撤销完成", "undoCompleteTaskButton");
    cancel_ = button("取消待办", "cancelTaskButton");
    restore_ = button("恢复待办", "restoreTaskButton");
    buttons->addStretch();
    layout->addLayout(buttons);
    auto *splitter = new QSplitter;
    table_ = new QTableView;
    table_->setObjectName("taskTable");
    model_ = new QStandardItemModel(this);
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setWordWrap(false);
    table_->verticalHeader()->hide();
    splitter->addWidget(table_);
    auto *right = new QWidget;
    auto *details = new QVBoxLayout(right);
    detail_ = new QTextBrowser;
    detail_->setObjectName("taskDetail");
    details->addWidget(detail_, 1);
    open_ = new QPushButton("查看关联通知");
    open_->setObjectName("openTaskNoticeButton");
    details->addWidget(open_);
    review_ = new QPushButton("确认已核对当前原文");
    review_->setObjectName("reviewTaskNoticeButton");
    details->addWidget(review_);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({850, 480});
    layout->addWidget(splitter, 1);
    status_ = new QLabel;
    status_->setObjectName("taskPageStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    connect(go, &QPushButton::clicked, this, &TaskPage::showNoticesRequested);
    connect(search_, &QLineEdit::textChanged, this, [this] { reload(); });
    connect(filter_, &QComboBox::currentIndexChanged, this, [this] { reload(); });
    connect(dateFilter_, &QComboBox::currentIndexChanged, this, [this] { reload(); });
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this] { selectionChanged(); });
    connect(edit_, &QPushButton::clicked, this, &TaskPage::edit);
    connect(table_, &QTableView::doubleClicked, this, [this] { edit(); });
    connect(start_, &QPushButton::clicked, this, [this] { changeStatus(TaskStatus::InProgress); });
    connect(complete_, &QPushButton::clicked, this,
            [this] { changeStatus(TaskStatus::Completed); });
    connect(undo_, &QPushButton::clicked, this, [this] { changeStatus(TaskStatus::NotStarted); });
    connect(cancel_, &QPushButton::clicked, this, [this] { changeStatus(TaskStatus::Cancelled); });
    connect(restore_, &QPushButton::clicked, this,
            [this] { changeStatus(TaskStatus::NotStarted); });
    connect(review_, &QPushButton::clicked, this, &TaskPage::acknowledge);
    connect(open_, &QPushButton::clicked, this, [this] {
        if (const auto *v = selected())
            emit noticeRequested(QString::fromStdString(v->task.noticeId));
    });
    connect(&coordinator, &RefreshCoordinator::changed, this, [this] { reload(); });
    connect(&coordinator, &RefreshCoordinator::detailFinished, this,
            [this](const QString &) { reload(); });
    reload();
}
QString TaskPage::selectedId() const {
    return table_->currentIndex().isValid()
               ? model_->index(table_->currentIndex().row(), 0).data(Qt::UserRole).toString()
               : QString{};
}
const TaskView *TaskPage::selected() const {
    const auto id = selectedId().toStdString();
    for (const auto &v : views_)
        if (v.task.id == id)
            return &v;
    return nullptr;
}
void TaskPage::reload(const QString &requested) {
    try {
        const auto keep = requested.isEmpty() ? selectedId() : requested;
        views_ = service_.views();
        const auto now = QDateTime::currentDateTimeUtc();
        const auto today = now.toTimeZone(QTimeZone(school_.timeZone.toUtf8()))
                               .date()
                               .toString(Qt::ISODate)
                               .toStdString();
        const auto utc = now.toString(Qt::ISODate).toStdString();
        int active = 0, done = 0, cancelled = 0, review = 0, target = -1;
        const QSignalBlocker block(table_->selectionModel());
        model_->clear();
        model_->setHorizontalHeaderLabels(
            {"办理事项", "操作", "状态", "办理时间", "原文", "关联通知"});
        for (const auto &v : views_) {
            const auto &t = v.task;
            const bool late = TaskService::overdue(t, today, utc);
            if (TaskService::active(t.status))
                ++active;
            if (t.status == TaskStatus::Completed)
                ++done;
            if (t.status == TaskStatus::Cancelled)
                ++cancelled;
            if (v.needsReview())
                ++review;
            const auto state = filter_->currentData().toString();
            const auto dates = dateFilter_->currentData().toString();
            if (state == "active" && !TaskService::active(t.status))
                continue;
            if (state != "all" && state != "active" &&
                state != QString::fromStdString(taskStatusKey(t.status)))
                continue;
            if ((dates == "unknown" && t.time.precision != TimePrecision::Unknown) ||
                (dates == "overdue" && !late))
                continue;
            if (!(QString::fromStdString(t.title + " " + t.notes + " " + v.noticeTitle))
                     .contains(search_->text().trimmed(), Qt::CaseInsensitive))
                continue;
            QList<QStandardItem *> row;
            for (const auto &text :
                 QStringList{QString::fromStdString(t.title), taskActionText(t.action),
                             taskStatusText(t.status) + (late ? " · 逾期" : ""),
                             taskTimeText(t.time), v.needsReview() ? "待复核" : "暂无更新",
                             QString::fromStdString(v.noticeTitle)})
                row << new QStandardItem(text);
            row[0]->setData(QString::fromStdString(t.id), Qt::UserRole);
            model_->appendRow(row);
            if (QString::fromStdString(t.id) == keep)
                target = model_->rowCount() - 1;
        }
        summary_->setText(QString("本校共 %1 项 · 未完成 %2 项 · 已完成 %3 项 · 已取消 %4 项 · "
                                  "原文待复核 %5 项 · 当前显示 %6 项")
                              .arg(views_.size())
                              .arg(active)
                              .arg(done)
                              .arg(cancelled)
                              .arg(review)
                              .arg(model_->rowCount()));
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int column = 1; column <= 4; ++column)
            table_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
        table_->setColumnWidth(5, 160);
        table_->setCurrentIndex(model_->index(target >= 0              ? target
                                              : model_->rowCount() > 0 ? 0
                                                                       : -1,
                                              0));
        selectionChanged();
    } catch (const std::exception &e) {
        status_->setText("待办加载失败：" + QString::fromUtf8(e.what()));
    }
}
void TaskPage::selectionChanged() {
    const auto *v = selected();
    for (auto *b : {edit_, start_, complete_, undo_, cancel_, restore_, review_, open_})
        b->setEnabled(false);
    if (!v) {
        detail_->setPlainText("暂无符合条件的待办。可从通知详情加入办理事项，或调整筛选。");
        status_->setText("办理时间由你确认；可在日历查看并启用应用运行期间的提醒。");
        return;
    }
    const auto &t = v->task;
    edit_->setEnabled(true);
    open_->setEnabled(true);
    review_->setEnabled(v->needsReview());
    start_->setEnabled(t.status == TaskStatus::NotStarted);
    complete_->setEnabled(TaskService::active(t.status));
    undo_->setEnabled(t.status == TaskStatus::Completed);
    cancel_->setEnabled(TaskService::active(t.status));
    restore_->setEnabled(t.status == TaskStatus::Cancelled);
    QString reminder = "未启用";
    if (t.reminder.enabled)
        reminder = t.time.precision == TimePrecision::DateOnly
                       ? QString("提前 %1 天，在 %2（学校时区）")
                             .arg(t.reminder.daysBefore)
                             .arg(QString::fromStdString(t.reminder.dateOnlyAt))
                       : QString("提前 %1 分钟").arg(t.reminder.minutesBefore);
    const auto source =
        t.time.confirmation == TimeConfirmation::OriginalText ? QString("核对原文后的时间")
        : t.time.confirmation == TimeConfirmation::Personal   ? QString("个人计划时间")
                                                              : QString("日期待定");
    detail_->setPlainText(
        "办理事项：" + QString::fromStdString(t.title) + "\n操作：" + taskActionText(t.action) +
        "\n状态：" + taskStatusText(t.status) + "\n办理时间：" + taskTimeText(t.time) +
        "\n时间来源：" + source + "\n时间说明：" + QString::fromStdString(t.time.evidence) +
        "\n\n个人备注：\n" + QString::fromStdString(t.notes) + "\n\n关联通知：" +
        QString::fromStdString(v->noticeTitle) + "\n" +
        (v->needsReview()
             ? QString("原文有更新，请核对办理事项与已确认时间；核对后可确认当前原文。")
             : QString("关联原文暂无新版本。")) +
        "\n\n提醒偏好：" + reminder + "（应用运行时检查；退出期间不补发）");
    status_->setText(v->needsReview() ? "原文更新提醒：已确认时间与办理状态保留，请复核。"
                                      : "状态与时间保存在本机。完成状态由你记录。");
}
bool TaskPage::createForNotice(const QString &id) {
    try {
        auto task = service_.draft(id.toStdString());
        const auto original = QString::fromStdString(task.title);
        TaskEditorDialog dialog(school_, service_, std::move(task), original, this);
        if (dialog.exec() == QDialog::Accepted) {
            reload(QString::fromStdString(dialog.saved().id));
            emit tasksChanged();
            return true;
        }
    } catch (const std::exception &e) {
        status_->setText("创建待办失败：" + QString::fromUtf8(e.what()));
    }
    return false;
}
void TaskPage::edit() {
    try {
        if (const auto *v = selected()) {
            TaskEditorDialog dialog(school_, service_, service_.find(v->task.id),
                                    QString::fromStdString(v->noticeTitle), this);
            if (dialog.exec() == QDialog::Accepted) {
                reload(QString::fromStdString(dialog.saved().id));
                emit tasksChanged();
            }
        }
    } catch (const std::exception &e) {
        status_->setText("编辑失败：" + QString::fromUtf8(e.what()));
    }
}
void TaskPage::changeStatus(TaskStatus state) {
    try {
        if (const auto *v = selected()) {
            const auto saved = service_.setStatus(v->task.id, state);
            reload(QString::fromStdString(saved.id));
            emit tasksChanged();
        }
    } catch (const std::exception &e) {
        status_->setText("状态保存失败：" + QString::fromUtf8(e.what()));
    }
}
void TaskPage::acknowledge() {
    try {
        if (const auto *v = selected()) {
            const auto saved = service_.acknowledgeNotice(v->task.id);
            reload(QString::fromStdString(saved.id));
            emit tasksChanged();
            status_->setText("已确认核对当前原文，办理时间和状态保留。");
        }
    } catch (const std::exception &e) {
        status_->setText("复核保存失败：" + QString::fromUtf8(e.what()));
    }
}
} // namespace campus
