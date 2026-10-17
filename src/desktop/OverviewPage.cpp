#include "desktop/OverviewPage.h"
#include "adapters/SchoolPackage.h"
#include "application/NoticeService.h"
#include "application/SourceService.h"
#include "application/TaskService.h"
#include "desktop/TaskDisplay.h"
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QScrollArea>
#include <QResizeEvent>
#include <QFontMetrics>
#include <QTimer>
#include <QTimeZone>
#include <QLocale>
#include <QVBoxLayout>
#include <algorithm>

namespace campus {
OverviewPage::OverviewPage(const SchoolPackage &school, NoticeService &notices,
                           SourceService &sources, TaskService &tasks, QWidget *parent)
    : QWidget(parent), notices_(notices), sources_(sources), tasks_(tasks), timeZone_(school.timeZone) {
    setObjectName("overviewPage");
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setObjectName("overviewScroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget;
    scroll->setWidget(content);
    outer->addWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(16);
    auto *title = new QLabel("校园概览");
    title->setProperty("brandRole", "heading");
    title->setWordWrap(true);
    layout->addWidget(title);
    auto *schoolLabel = new QLabel(school.name);
    schoolLabel->setObjectName("subtitle");
    schoolLabel->setTextFormat(Qt::PlainText);
    schoolLabel->setWordWrap(true);
    layout->addWidget(schoolLabel);
    date_ = new QLabel;
    layout->addWidget(date_);
    summary_ = new QLabel;
    summary_->setObjectName("overviewSummary");
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    actions_ = new QBoxLayout(QBoxLayout::LeftToRight);
    auto *actions = actions_;
    const auto addAction = [this, actions](const char *name, const QString &text, auto signal) {
        auto *button = new QPushButton(text);
        button->setObjectName(QString::fromLatin1(name));
        actions->addWidget(button);
        connect(button, &QPushButton::clicked, this, signal);
        return button;
    };
    addAction("overviewNoticesButton", "看通知", &OverviewPage::noticesRequested);
    auto *resourceButton = addAction("overviewResourcesButton", "找资源", &OverviewPage::resourcesRequested);
    resourceButton->setEnabled(false);
    resourceButton->setToolTip("学校资源服务尚未启用");
    addAction("overviewNewReminderButton", "新建提醒", &OverviewPage::newReminderRequested);
    actions->addStretch();
    layout->addLayout(actions);
    splitter_ = new QSplitter;
    splitter_->setObjectName("overviewSplitter");
    splitter_->setChildrenCollapsible(false);
    auto *splitter = splitter_;
    const auto section = [splitter](const QString &title, const QString &help, QListWidget *&list,
                                   const char *name) {
        auto *panel = new QWidget;
        auto *column = new QVBoxLayout(panel);
        panel->setObjectName("overviewPanel");
        column->setContentsMargins(16, 16, 16, 16);
        column->setSpacing(10);
        auto *heading = new QLabel(title);
        heading->setProperty("brandRole", "accessHeading");
        column->addWidget(heading);
        auto *description = new QLabel(help);
        description->setWordWrap(true);
        column->addWidget(description);
        list = new QListWidget;
        list->setObjectName(QString::fromLatin1(name));
        list->setAccessibleName(title);
        list->setWordWrap(true);
        list->setTextElideMode(Qt::ElideNone);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setUniformItemSizes(false);
        column->addWidget(list, 1);
        splitter->addWidget(panel);
    };
    section("接下来要办的事", "显示未完成事项；双击或按 Enter 查看。时间依据你确认的计划。",
            taskList_, "overviewTasks");
    section("最近保存的通知", "来自本地缓存，按发布日期排序；是否最新以来源更新状态为准。",
            noticeList_, "overviewNotices");
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    health_ = new QLabel;
    health_->setObjectName("overviewSourceHealth");
    health_->setWordWrap(true);
    layout->addWidget(health_);
    auto *secondaryActions = new QHBoxLayout;
    auto *tasksButton = new QPushButton("全部提醒");
    tasksButton->setObjectName("overviewTasksButton");
    connect(tasksButton, &QPushButton::clicked, this, &OverviewPage::tasksRequested);
    secondaryActions->addWidget(tasksButton);
    auto *sourcesButton = new QPushButton("来源与更新状态");
    sourcesButton->setObjectName("overviewSourcesButton");
    connect(sourcesButton, &QPushButton::clicked, this, &OverviewPage::sourcesRequested);
    secondaryActions->addWidget(sourcesButton);
    secondaryActions->addStretch();
    layout->addLayout(secondaryActions);
    auto *boundary = new QLabel("提醒需要 CampusPulse 保持运行；办理期限请核对官方原文。概览不会自动调用 AI。");
    boundary->setWordWrap(true);
    boundary->setProperty("brandRole", "muted");
    layout->addWidget(boundary);
    error_ = new QLabel;
    error_->setObjectName("overviewError");
    error_->setWordWrap(true);
    layout->addWidget(error_);
    connect(taskList_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (!item->data(Qt::UserRole).toString().isEmpty())
            emit taskRequested(item->data(Qt::UserRole).toString());
    });
    connect(noticeList_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (!item->data(Qt::UserRole).toString().isEmpty())
            emit noticeRequested(item->data(Qt::UserRole).toString());
    });
    auto *timer = new QTimer(this);
    timer->setInterval(60000);
    connect(timer, &QTimer::timeout, this, [this] { if (isVisible()) reload(); });
    timer->start();
    updateLayout();
    reload();
}

void OverviewPage::setResourcesAvailable(bool available) {
    auto *button = findChild<QPushButton *>("overviewResourcesButton");
    button->setEnabled(available);
    button->setToolTip(available ? "查看学习与办事入口" : "学校资源服务尚未启用");
}

void OverviewPage::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateLayout();
}

void OverviewPage::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        updateLayout();
}

void OverviewPage::updateLayout() {
    const int breakpoint = std::max(800, fontMetrics().horizontalAdvance(QString(72, QLatin1Char('M'))));
    const bool narrow = width() < breakpoint;
    actions_->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    const auto orientation = narrow ? Qt::Vertical : Qt::Horizontal;
    if (splitter_->orientation() != orientation) {
        splitter_->setOrientation(orientation);
        splitter_->setSizes({1, 1});
    }
    const int listHeight = fontMetrics().lineSpacing() * 8;
    taskList_->setMinimumHeight(listHeight);
    noticeList_->setMinimumHeight(listHeight);
}

void OverviewPage::reload() {
    try {
        error_->clear();
        const auto now = QDateTime::currentDateTimeUtc();
        const auto local = now.toTimeZone(QTimeZone(timeZone_.toUtf8()));
        date_->setText(QLocale(QLocale::Chinese, QLocale::China).toString(local, "yyyy年M月d日 dddd"));
        auto tasks = tasks_.views();
        std::erase_if(tasks, [](const TaskView &view) { return !TaskService::active(view.task.status); });
        std::stable_sort(tasks.begin(), tasks.end(), [](const TaskView &a, const TaskView &b) {
            const auto key = [](const PersonalTask &task) {
                return task.time.precision == TimePrecision::DateTime ? task.time.utcDateTime
                     : task.time.precision == TimePrecision::DateOnly ? task.time.date : std::string("~");
            };
            return key(a.task) < key(b.task);
        });
        int overdue = 0;
        for (const auto &view : tasks)
            overdue += TaskService::overdue(view.task, local.date().toString(Qt::ISODate).toStdString(),
                                            now.toString(Qt::ISODate).toStdString());
        const auto notices = notices_.list();
        summary_->setText(QString("%1 项待办未完成 · %2 项已过计划时间 · 已保存 %3 条通知")
                             .arg(tasks.size()).arg(overdue).arg(notices.size()));
        taskList_->clear();
        for (const auto &view : tasks) {
            auto *item = new QListWidgetItem(QString::fromStdString(view.task.title) + "\n" +
                taskTimeText(view.task.time) + (view.needsReview() ? " · 原文有变化，请复核" : ""), taskList_);
            item->setData(Qt::UserRole, QString::fromStdString(view.task.id));
        }
        if (tasks.empty()) {
            auto *item = new QListWidgetItem("还没有未完成提醒。点击上方“新建提醒”，或从通知添加待办。", taskList_);
            item->setFlags(Qt::NoItemFlags);
        }
        noticeList_->clear();
        for (std::size_t i = 0; i < std::min<std::size_t>(notices.size(), 8); ++i) {
            const auto &notice = notices[i];
            auto *item = new QListWidgetItem(QString::fromStdString(notice.title) + "\n" +
                QString::fromStdString(notice.publishedDate.empty() ? "日期待核实" : notice.publishedDate) +
                " · " + QString::fromStdString(notice.sourceName), noticeList_);
            item->setData(Qt::UserRole, QString::fromStdString(notice.id));
        }
        if (notices.empty()) {
            auto *item = new QListWidgetItem("尚无本地通知。先选择学校，再在通知页更新官网。", noticeList_);
            item->setFlags(Qt::NoItemFlags);
        }
        int ready = 0, pending = 0, failed = 0, updating = 0, paused = 0;
        for (const auto &source : sources_.list()) {
            const auto status = source.effectiveStatus();
            ready += sources_.isRunnable(source.description.id);
            pending += status == "not_ready" || status == "login_required";
            failed += status == "failure" || status == "partial_success" || status == "interrupted";
            updating += status == "updating";
            paused += status == "paused";
        }
        health_->setText(QString("来源状态：%1 个可更新 · %2 个正在更新 · %3 个待接入或需登录 · %4 个已暂停 · %5 个存在更新问题。查看来源页了解具体原因。")
                             .arg(ready).arg(updating).arg(pending).arg(paused).arg(failed));
    } catch (const std::exception &error) {
        taskList_->clear();
        noticeList_->clear();
        summary_->clear();
        health_->clear();
        error_->setText("概览读取失败：" + QString::fromUtf8(error.what()));
    }
}
} // namespace campus
