#include "desktop/TaskPage.h"
#include "desktop/TaskDisplay.h"
#include "desktop/TaskEditorDialog.h"
#include "adapters/ReminderScheduler.h"
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyle>
#include <QTableView>
#include <QTextBrowser>
#include <QTimeZone>
#include <QVBoxLayout>
#include <functional>
namespace campus {
namespace {
// A card has its own selection and keyboard behavior. Both visible views share the task ID.
class TaskCard final : public QFrame {
  public:
    std::function<void()> select, edit;
    explicit TaskCard(QWidget *parent = nullptr) : QFrame(parent) {
        setObjectName("taskCard");
        setFocusPolicy(Qt::StrongFocus);
        setFrameShape(QFrame::StyledPanel);
        setStyleSheet(
            "QFrame#taskCard { background: palette(base); border: 1px solid palette(mid); "
            "border-radius: 9px; }"
            "QFrame#taskCard[taskSelected=true] { border: 2px solid palette(link); }"
            "QFrame#taskCard:focus { border: 2px solid palette(link); }");
    }
  protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            setFocus(Qt::MouseFocusReason);
            select();
            event->accept();
            return;
        }
        QFrame::mousePressEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            select();
            edit();
            event->accept();
            return;
        }
        QFrame::mouseDoubleClickEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            select();
            edit();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Space) {
            select();
            event->accept();
            return;
        }
        QFrame::keyPressEvent(event);
    }
};
QString clockText(const QDateTime &time) {
    return time.toString(time.time().second() ? "yyyy-MM-dd HH:mm:ss" : "yyyy-MM-dd HH:mm");
}
QString actualReminderText(const PersonalTask &task) {
    if (task.status == TaskStatus::Completed)
        return "已完成 · 不再提醒";
    if (task.status == TaskStatus::Cancelled)
        return "已取消 · 不再提醒";
    if (!task.reminder.enabled)
        return "提醒关闭";
    if (task.time.precision == TimePrecision::Unknown ||
        task.time.confirmation == TimeConfirmation::None)
        return "尚未确认时间 · 不会提醒";
    const auto alarm = ReminderScheduler::trigger(task);
    const QTimeZone zone(QByteArray::fromStdString(task.time.timeZone));
    if (!alarm.isValid() || !zone.isValid())
        return "提醒时间无效 · 请修改时间";
    return "提醒：" + clockText(alarm.toTimeZone(zone)) +
           "（" + QString::fromStdString(task.time.timeZone) + "）";
}
QString cardTimeText(const TaskTime &time) {
    if (time.precision == TimePrecision::Unknown)
        return "时间未设置";
    if (time.precision == TimePrecision::DateOnly)
        return QString::fromStdString(time.date) + " · 仅日期";
    const auto utc = QDateTime::fromString(QString::fromStdString(time.utcDateTime), Qt::ISODate);
    const QTimeZone zone(QByteArray::fromStdString(time.timeZone));
    return utc.isValid() && zone.isValid()
               ? clockText(utc.toTimeZone(zone))
               : QString("时间无效 · 请重新设置");
}
} // namespace
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
        " · 从通知加入待办，设置日期和时间，到点弹出提醒。软件运行时提醒，可在托盘后台运行。");
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
    viewMode_ = new QComboBox;
    viewMode_->setObjectName("taskViewMode");
    viewMode_->setAccessibleName("待办视图");
    viewMode_->addItem("卡片", "cards");
    viewMode_->addItem("列表", "list");
    filters->addWidget(viewMode_);
    auto *go = new QPushButton("去通知页添加");
    go->setObjectName("goToNoticesButton");
    filters->addWidget(go);
    auto *testReminder = new QPushButton("测试提醒");
    testReminder->setObjectName("testTaskReminderButton");
    testReminder->setToolTip("立即显示一次测试提醒，不创建待办、不修改你的数据。");
    filters->addWidget(testReminder);
    layout->addLayout(filters);
    summary_ = new QLabel;
    summary_->setObjectName("taskSummary");
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    content_ = new QStackedWidget;
    content_->setObjectName("taskViews");
    cards_ = new QScrollArea;
    cards_->setObjectName("taskCards");
    cards_->setAccessibleName("闹钟待办卡片");
    cards_->setWidgetResizable(true);
    cards_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    cardContent_ = new QWidget;
    cardContent_->setObjectName("taskCardContent");
    cardLayout_ = new QVBoxLayout(cardContent_);
    cardLayout_->setContentsMargins(2, 2, 10, 2);
    cardLayout_->setSpacing(12);
    cards_->setWidget(cardContent_);
    content_->addWidget(cards_);
    auto *list = new QWidget;
    auto *listLayout = new QVBoxLayout(list);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listActions_ = new QWidget;
    auto *buttons = new QHBoxLayout(listActions_);
    buttons->setContentsMargins(0, 0, 0, 0);
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
    listLayout->addWidget(listActions_);
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
    listLayout->addWidget(splitter, 1);
    content_->addWidget(list);
    layout->addWidget(content_, 1);
    status_ = new QLabel;
    status_->setObjectName("taskPageStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    connect(go, &QPushButton::clicked, this, &TaskPage::showNoticesRequested);
    connect(testReminder, &QPushButton::clicked, this, &TaskPage::reminderTestRequested);
    connect(search_, &QLineEdit::textChanged, this, [this] { reload(); });
    connect(filter_, &QComboBox::currentIndexChanged, this, [this] { reload(); });
    connect(dateFilter_, &QComboBox::currentIndexChanged, this, [this] { reload(); });
    connect(viewMode_, &QComboBox::currentIndexChanged, this, [this](int index) {
        content_->setCurrentIndex(index);
        if (index == 0)
            for (auto *card : cardContent_->findChildren<QFrame *>("taskCard", Qt::FindDirectChildrenOnly))
                if (card->property("taskId").toString() == selectedTaskId_) {
                    cards_->ensureWidgetVisible(card);
                    break;
                }
    });
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &current) {
                selectTask(current.isValid()
                               ? model_->index(current.row(), 0).data(Qt::UserRole).toString()
                               : QString{});
            });
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
    return selectedTaskId_;
}
const TaskView *TaskPage::selected() const {
    const auto id = selectedId().toStdString();
    for (const auto &v : views_)
        if (v.task.id == id)
            return &v;
    return nullptr;
}
void TaskPage::selectTask(const QString &id) {
    selectedTaskId_ = id;
    const QSignalBlocker block(table_->selectionModel());
    int row = -1;
    for (int candidate = 0; candidate < static_cast<int>(visibleIds_.size()); ++candidate)
        if (visibleIds_[candidate] == id) {
            row = candidate;
            break;
        }
    table_->setCurrentIndex(model_->index(row, 0));
    selectionChanged();
    updateCardSelection();
}
void TaskPage::updateCardSelection() {
    for (auto *card : cardContent_->findChildren<QFrame *>("taskCard", Qt::FindDirectChildrenOnly)) {
        const bool selected = card->property("taskId").toString() == selectedTaskId_;
        if (card->property("taskSelected").toBool() == selected)
            continue;
        card->setProperty("taskSelected", selected);
        card->style()->unpolish(card);
        card->style()->polish(card);
        card->update();
    }
}
void TaskPage::rebuildCards() {
    while (auto *item = cardLayout_->takeAt(0)) {
        if (item->widget()) {
            // An action on a card may synchronously reload the page. Defer deletion of its sender.
            item->widget()->hide();
            item->widget()->setParent(nullptr);
            item->widget()->deleteLater();
        }
        delete item;
    }
    if (visibleIds_.empty()) {
        auto *empty = new QLabel("还没有符合条件的待办\n\n"
                                "在通知详情点击“加入待办”，选好日期和时间即可。\n"
                                "已有待办？试试清空搜索或调整筛选。");
        empty->setObjectName("taskCardsEmpty");
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        cardLayout_->addWidget(empty, 1);
        return;
    }
    for (const auto &id : visibleIds_) {
        const TaskView *view = nullptr;
        for (const auto &candidate : views_)
            if (QString::fromStdString(candidate.task.id) == id) {
                view = &candidate;
                break;
            }
        if (!view)
            continue;
        const auto &task = view->task;
        auto *card = new TaskCard;
        card->setProperty("taskId", id);
        card->setProperty("taskSelected", false);
        card->setAccessibleName(QString::fromStdString(task.title) + "，" + cardTimeText(task.time));
        card->setAccessibleDescription(actualReminderText(task));
        card->select = [this, id] { selectTask(id); };
        card->edit = [this] { edit(); };
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(18, 14, 18, 14);
        cardLayout->setSpacing(8);
        const auto label = [&](const QString &text, const char *name) {
            auto *result = new QLabel(text);
            result->setObjectName(name);
            result->setTextFormat(Qt::PlainText);
            result->setWordWrap(true);
            result->setAttribute(Qt::WA_TransparentForMouseEvents);
            return result;
        };
        auto *top = new QHBoxLayout;
        auto *title = label(QString::fromStdString(task.title), "taskCardTitle");
        auto titleFont = title->font();
        titleFont.setBold(true);
        titleFont.setPointSizeF(titleFont.pointSizeF() * 1.12);
        title->setFont(titleFont);
        top->addWidget(title, 1);
        auto state = taskStatusText(task.status);
        if (TaskService::overdue(task,
                                 QDateTime::currentDateTimeUtc()
                                     .toTimeZone(QTimeZone(school_.timeZone.toUtf8()))
                                     .date().toString(Qt::ISODate).toStdString(),
                                 QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()))
            state += " · 已逾期";
        top->addWidget(label(state, "taskCardStatus"));
        cardLayout->addLayout(top);
        auto *time = label(cardTimeText(task.time), "taskCardTime");
        auto timeFont = time->font();
        timeFont.setPointSizeF(timeFont.pointSizeF() * 1.5);
        timeFont.setBold(true);
        time->setFont(timeFont);
        cardLayout->addWidget(time);
        cardLayout->addWidget(label(actualReminderText(task), "taskCardReminder"));
        if (view->needsReview())
            cardLayout->addWidget(label("原文有更新，请核对事项和时间。", "taskCardReview"));
        auto *actions = new QHBoxLayout;
        auto *editButton = new QPushButton("修改时间");
        editButton->setObjectName("taskCardEditButton");
        editButton->setProperty("taskId", id);
        editButton->setToolTip("编辑事项、日期、提醒和备注。");
        connect(editButton, &QPushButton::clicked, this, [this, id] {
            selectTask(id);
            edit();
        });
        actions->addWidget(editButton);
        auto *doneButton = new QPushButton(task.status == TaskStatus::Completed ? "撤销完成"
                                               : task.status == TaskStatus::Cancelled ? "恢复待办"
                                                                                       : "完成");
        doneButton->setObjectName("taskCardCompleteButton");
        doneButton->setProperty("taskId", id);
        const auto nextStatus = TaskService::active(task.status) ? TaskStatus::Completed
                                                                : TaskStatus::NotStarted;
        connect(doneButton, &QPushButton::clicked, this, [this, id, nextStatus] {
            selectTask(id);
            changeStatus(nextStatus);
        });
        actions->addWidget(doneButton);
        actions->addStretch();
        auto *more = new QPushButton("更多");
        more->setObjectName("taskCardMoreButton");
        auto *menu = new QMenu(more);
        auto *notice = menu->addAction("查看关联通知");
        connect(notice, &QAction::triggered, this, [this, id] {
            selectTask(id);
            if (const auto *view = selected())
                emit noticeRequested(QString::fromStdString(view->task.noticeId));
        });
        if (view->needsReview()) {
            auto *review = menu->addAction("确认已核对当前原文");
            connect(review, &QAction::triggered, this, [this, id] {
                selectTask(id);
                acknowledge();
            });
        }
        if (task.status == TaskStatus::NotStarted) {
            auto *start = menu->addAction("开始办理");
            connect(start, &QAction::triggered, this, [this, id] {
                selectTask(id);
                changeStatus(TaskStatus::InProgress);
            });
        }
        if (TaskService::active(task.status)) {
            auto *cancel = menu->addAction("取消待办");
            connect(cancel, &QAction::triggered, this, [this, id] {
                selectTask(id);
                changeStatus(TaskStatus::Cancelled);
            });
        }
        more->setMenu(menu);
        actions->addWidget(more);
        cardLayout->addLayout(actions);
        cardLayout_->addWidget(card);
    }
    cardLayout_->addStretch();
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
        visibleIds_.clear();
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
            visibleIds_.push_back(QString::fromStdString(t.id));
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
        const auto selectedRow = target >= 0 ? target : !visibleIds_.empty() ? 0 : -1;
        rebuildCards();
        selectTask(selectedRow >= 0 ? visibleIds_[selectedRow] : QString{});
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
        status_->setText("从通知加入待办后，设置日期和时间即可提醒。可点击“测试提醒”检查本机效果。");
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
        "\n\n提醒偏好：" + reminder + "\n" + actualReminderText(t) +
        "\n软件运行时检查提醒，可在托盘后台运行；完全退出期间不检查。");
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
