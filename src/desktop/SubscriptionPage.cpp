#include "desktop/SubscriptionPage.h"
#include "desktop/SubscriptionEditorDialog.h"
#include "domain/Theme.h"
#include <QDate>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableView>
#include <QVBoxLayout>

namespace campus {
namespace {
QString joined(const std::vector<std::string> &values) {
    QStringList parts;
    for (const auto &value : values)
        parts << QString::fromStdString(value);
    return parts.join("、");
}
QString yearLabel(const NoticeQuery &query) {
    switch (query.yearPolicy) {
    case YearPolicy::CurrentYear:
        return "今年（随年份变化）";
    case YearPolicy::AllYears:
        return "全部年份";
    case YearPolicy::FixedYear:
        return QString("固定 %1 年").arg(query.fixedYear);
    case YearPolicy::UnknownDate:
        return "日期待核实";
    }
    return "年份策略无效";
}
QString keysLabel(const std::vector<std::string> &keys, bool stages) {
    if (keys.empty())
        return "全部";
    QStringList result;
    for (const auto &key : keys) {
        const auto name = stages ? stageLabel(key) : themeLabel(key);
        result << QString::fromUtf8(name.data(), static_cast<int>(name.size()));
    }
    return result.join("、");
}
} // namespace
SubscriptionPage::SubscriptionPage(const SchoolPackage &school, SubscriptionService &service,
                                   RefreshCoordinator &coordinator, QWidget *parent)
    : QWidget(parent), school_(school), service_(service), matches_(this) {
    setObjectName("subscriptionPage");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(14);
    auto *heading = new QLabel("我的订阅");
    heading->setObjectName("heading");
    layout->addWidget(heading);
    auto *scope = new QLabel(school.name + " · 保存关注条件，点击更新官网后重新匹配本地通知。"
                                           "订阅暂停后停止显示命中；保存的规则保留。申请、结果、公"
                                           "示仅为标题提示，办理时间请核对原文。");
    scope->setObjectName("scope");
    scope->setWordWrap(true);
    layout->addWidget(scope);
    auto *controls = new QHBoxLayout;
    summary_ = new QLabel;
    summary_->setObjectName("subscriptionSummary");
    controls->addWidget(summary_, 1);
    auto *create = new QPushButton("新建订阅");
    create->setObjectName("newSubscriptionButton");
    edit_ = new QPushButton("编辑");
    edit_->setObjectName("editSubscriptionButton");
    pause_ = new QPushButton("暂停订阅");
    pause_->setObjectName("pauseSubscriptionButton");
    remove_ = new QPushButton("删除");
    remove_->setObjectName("deleteSubscriptionButton");
    for (auto *button : {create, edit_, pause_, remove_})
        controls->addWidget(button);
    layout->addLayout(controls);
    auto *splitter = new QSplitter;
    list_ = new QListWidget;
    list_->setObjectName("subscriptionList");
    list_->setMinimumWidth(240);
    splitter->addWidget(list_);
    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rules_ = new QLabel("选择一个订阅查看规则与匹配通知。");
    rules_->setObjectName("subscriptionRules");
    rules_->setTextFormat(Qt::PlainText);
    rules_->setWordWrap(true);
    rightLayout->addWidget(rules_);
    table_ = new QTableView;
    table_->setObjectName("subscriptionNoticeTable");
    table_->setModel(&matches_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setWordWrap(false);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->setColumnWidth(0, 110);
    table_->setColumnWidth(3, 125);
    table_->setColumnWidth(4, 120);
    rightLayout->addWidget(table_, 1);
    open_ = new QPushButton("在通知页查看详情");
    open_->setObjectName("openSubscriptionNoticeButton");
    rightLayout->addWidget(open_);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);
    layout->addWidget(splitter, 1);
    status_ =
        new QLabel("暂无订阅时，可点击“新建订阅”，或在通知页将当前筛选保存。订阅仅保存在本机。");
    status_->setObjectName("subscriptionStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    connect(create, &QPushButton::clicked, this, [this] {
        NoticeQuery query;
        query.schoolId = school_.id.toStdString();
        createFromQuery(query);
    });
    connect(edit_, &QPushButton::clicked, this, &SubscriptionPage::edit);
    connect(pause_, &QPushButton::clicked, this, &SubscriptionPage::togglePaused);
    connect(remove_, &QPushButton::clicked, this, &SubscriptionPage::remove);
    connect(list_, &QListWidget::currentRowChanged, this, [this] { selectionChanged(); });
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this] { open_->setEnabled(table_->currentIndex().isValid()); });
    connect(open_, &QPushButton::clicked, this, &SubscriptionPage::openNotice);
    connect(table_, &QTableView::doubleClicked, this, [this] { openNotice(); });
    connect(&coordinator, &RefreshCoordinator::changed, this, [this] { reload(); });
    reload();
}
QString SubscriptionPage::selectedId() const {
    return list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : QString{};
}
void SubscriptionPage::reload(const QString &selectId) {
    try {
        const auto selected = selectId.isEmpty() ? selectedId() : selectId;
        const QSignalBlocker block(list_);
        list_->clear();
        const auto subscriptions = service_.list();
        int active = 0, target = -1;
        for (const auto &subscription : subscriptions) {
            if (!subscription.paused)
                ++active;
            auto *item = new QListWidgetItem(
                QString::fromStdString(subscription.name) +
                    (subscription.paused ? "\n已暂停"
                                         : "\n启用 · " + yearLabel(subscription.query)),
                list_);
            item->setData(Qt::UserRole, QString::fromStdString(subscription.id));
            if (QString::fromStdString(subscription.id) == selected)
                target = list_->count() - 1;
        }
        summary_->setText(QString("共 %1 个订阅 · 启用 %2 个 · 暂停 %3 个")
                              .arg(subscriptions.size())
                              .arg(active)
                              .arg(static_cast<int>(subscriptions.size()) - active));
        list_->setCurrentRow(target >= 0 ? target : list_->count() > 0 ? 0 : -1);
        selectionChanged();
    } catch (const std::exception &error) {
        status_->setText("订阅读取失败：" + QString::fromUtf8(error.what()));
    }
}
void SubscriptionPage::selectionChanged() {
    const auto id = selectedId();
    for (auto *button : {edit_, pause_, remove_})
        button->setEnabled(!id.isEmpty());
    open_->setEnabled(false);
    matches_.setNotices({});
    if (id.isEmpty()) {
        rules_->setText("暂无订阅，可新建或从通知筛选保存。");
        pause_->setText("暂停订阅");
        status_->setText(
            "暂无订阅时，可点击“新建订阅”，或在通知页将当前筛选保存。订阅仅保存在本机。");
        return;
    }
    try {
        const auto subscription = service_.find(id.toStdString());
        const auto &query = subscription.query;
        QStringList sources;
        for (const auto &key : query.sourceIds) {
            auto name = QString::fromStdString(key) + "（目录已移除）";
            for (const auto &source : school_.catalog)
                if (source.id == key)
                    name = QString::fromStdString(source.name);
            sources << name;
        }
        rules_->setText("年份：" + yearLabel(query) + "\n来源：" +
                        (sources.isEmpty() ? QString("全部") : sources.join("、")) + "\n主题：" +
                        keysLabel(query.themeKeys, false) + " · 阶段：" +
                        keysLabel(query.stageKeys, true) + "\n全部包含：" +
                        joined(query.keywordAll) + " · 任一包含：" + joined(query.keywordAny) +
                        " · 排除：" + joined(query.keywordExclude));
        pause_->setText(subscription.paused ? "恢复订阅" : "暂停订阅");
        auto notices = service_.matches(subscription.id, QDate::currentDate().year());
        status_->setText(
            subscription.paused
                ? "此订阅已暂停；规则保留，恢复后重新匹配缓存。"
                : QString("当前缓存命中 %1 条通知 · 选择通知查看官方详情。").arg(notices.size()));
        matches_.setNotices(std::move(notices));
    } catch (const std::exception &error) {
        status_->setText("订阅匹配失败：" + QString::fromUtf8(error.what()));
    }
}
bool SubscriptionPage::createFromQuery(NoticeQuery query) {
    Subscription initial;
    initial.schoolId = school_.id.toStdString();
    initial.query = std::move(query);
    initial.name = initial.query.themeKeys.empty()
                       ? "我的关注"
                       : std::string(themeLabel(initial.query.themeKeys.front()));
    SubscriptionEditorDialog dialog(school_, service_, std::move(initial), this);
    if (dialog.exec() == QDialog::Accepted) {
        reload(QString::fromStdString(dialog.saved().id));
        return true;
    }
    return false;
}
void SubscriptionPage::edit() {
    try {
        SubscriptionEditorDialog dialog(school_, service_,
                                        service_.find(selectedId().toStdString()), this);
        if (dialog.exec() == QDialog::Accepted)
            reload(QString::fromStdString(dialog.saved().id));
    } catch (const std::exception &error) {
        status_->setText("编辑失败：" + QString::fromUtf8(error.what()));
    }
}
void SubscriptionPage::togglePaused() {
    try {
        const auto subscription = service_.find(selectedId().toStdString());
        service_.setPaused(subscription.id, !subscription.paused);
        reload(QString::fromStdString(subscription.id));
    } catch (const std::exception &error) {
        status_->setText("暂停状态保存失败：" + QString::fromUtf8(error.what()));
    }
}
void SubscriptionPage::remove() {
    try {
        const auto subscription = service_.find(selectedId().toStdString());
        if (QMessageBox::question(
                this, "删除订阅",
                "删除“" + QString::fromStdString(subscription.name) + "”？已采集通知和正文会保留。",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        service_.remove(subscription.id);
        reload();
    } catch (const std::exception &error) {
        status_->setText("删除失败：" + QString::fromUtf8(error.what()));
    }
}
void SubscriptionPage::openNotice() {
    const auto index = table_->currentIndex();
    if (index.isValid())
        emit noticeRequested(QString::fromStdString(matches_.notice(index.row()).id));
}
} // namespace campus
