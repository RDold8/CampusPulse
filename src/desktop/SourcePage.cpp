#include "desktop/SourcePage.h"
#include "desktop/BrandTheme.h"
#include <QStyleHints>
#include <QGuiApplication>
#include "adapters/SchoolPackage.h"
#include "adapters/RefreshCoordinator.h"
#include <QTableView>
#include <QStandardItemModel>
#include <QTextBrowser>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDesktopServices>
#include <QDateTime>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QColor>

namespace campus {
namespace {
QString text(const std::string &value) {
    return QString::fromStdString(value);
}
QString statusLabel(const std::string &status) {
    if (status == "never_checked")
        return "尚未检查";
    if (status == "updating")
        return "正在更新";
    if (status == "success")
        return "完整成功";
    if (status == "partial_success")
        return "部分成功";
    if (status == "failure")
        return "更新失败";
    if (status == "paused")
        return "已暂停";
    if (status == "not_ready")
        return "待接入";
    if (status == "login_required")
        return "需要登录";
    if (status == "interrupted")
        return "上次更新中断";
    return "状态待核实";
}
QString timestamp(const std::string &value) {
    if (value.empty())
        return "未记录";
    const auto date = QDateTime::fromString(text(value), Qt::ISODateWithMs);
    if (!date.isValid())
        return text(value);
    return date.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
}
QString categoryLabel(const std::string &category) {
    if (category == "exam")
        return "考试";
    if (category == "competition")
        return "竞赛";
    if (category == "scholarship")
        return "奖助学金";
    if (category == "campus_activity")
        return "校园活动";
    if (category == "academic_affairs")
        return "教务通知";
    if (category == "career")
        return "就业招聘";
    return text(category);
}
QString escaped(const std::string &value) {
    return text(value).toHtmlEscaped();
}
QString officialLink(const std::string &url) {
    if (url.empty())
        return "未配置";
    const QString safe = escaped(url);
    return "<a href=\"" + safe + "\">" + safe + "</a>";
}
bool safeLoginAddress(const std::string &address) {
    const QUrl url(text(address));
    return !address.empty() && url.isValid() && url.scheme() == "https" &&
           !url.host().isEmpty() && url.userInfo().isEmpty() && url.port() == -1;
}
} // namespace

SourcePage::SourcePage(const SchoolPackage &school, SourceService &sources,
                       RefreshCoordinator &coordinator, QWidget *parent)
    : QWidget(parent), sources_(sources), coordinator_(coordinator) {
    setObjectName("sourcePage");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(14);
    auto *heading = new QLabel("来源管理");
    heading->setObjectName("heading");
    layout->addWidget(heading);
    auto *subtitle = new QLabel(school.name + " · 学校配置目录与本机更新状态");
    subtitle->setObjectName("subtitle");
    layout->addWidget(subtitle);
    auto *scope = new QLabel("选择来源可单独更新或暂停。暂停只保存在本机，已缓存通知仍可查看。"
                             "“完整成功”表示本轮按配置读取的列表页均成功，学校历史页面可能更多。");
    scope->setWordWrap(true);
    scope->setObjectName("scope");
    layout->addWidget(scope);

    auto *controls = new QHBoxLayout;
    summary_ = new QLabel;
    summary_->setObjectName("sourceSummary");
    controls->addWidget(summary_, 1);
    update_ = new QPushButton("更新此来源");
    update_->setObjectName("updateSourceButton");
    pause_ = new QPushButton("暂停更新");
    pause_->setObjectName("pauseSourceButton");
    controls->addWidget(update_);
    controls->addWidget(pause_);
    layout->addLayout(controls);

    auto *loginNotice = new QHBoxLayout;
    loginSummary_ = new QLabel;
    loginSummary_->setObjectName("sourceLoginSummary");
    loginSummary_->setWordWrap(true);
    showLogin_ = new QPushButton("查看需登录来源");
    showLogin_->setObjectName("showLoginSourceButton");
    loginNotice->addWidget(loginSummary_, 1);
    loginNotice->addWidget(showLogin_);
    layout->addLayout(loginNotice);

    model_ = new QStandardItemModel(this);
    model_->setHorizontalHeaderLabels(
        {"来源", "状态", "最近尝试", "最近完整成功", "最新发布日期", "成功页 / 本轮条数"});
    table_ = new QTableView;
    table_->setObjectName("sourceTable");
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->setAlternatingRowColors(true);
    table_->setWordWrap(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->setColumnWidth(1, 128);
    table_->setColumnWidth(2, 178);
    table_->setColumnWidth(3, 178);
    table_->setColumnWidth(4, 128);
    table_->setColumnWidth(5, 150);
    layout->addWidget(table_, 3);

    loginPanel_ = new QWidget;
    loginPanel_->setObjectName("sourceLoginPanel");
    auto *loginLayout = new QVBoxLayout(loginPanel_);
    loginLayout->setContentsMargins(12, 10, 12, 10);
    loginLayout->setSpacing(8);
    auto *loginHeading = new QLabel("此来源需要学校账号登录");
    loginHeading->setProperty("brandRole", "accessHeading");
    loginHeading->setObjectName("sourceLoginHeading");
    loginLayout->addWidget(loginHeading);
    loginExplanation_ = new QLabel;
    loginExplanation_->setObjectName("sourceLoginExplanation");
    loginExplanation_->setWordWrap(true);
    loginLayout->addWidget(loginExplanation_);
    loginAddress_ = new QLabel;
    loginAddress_->setObjectName("sourceLoginAddress");
    loginAddress_->setTextFormat(Qt::PlainText);
    loginAddress_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    loginAddress_->setWordWrap(true);
    loginLayout->addWidget(loginAddress_);
    openLogin_ = new QPushButton("打开官方登录入口");
    openLogin_->setObjectName("openOfficialLoginButton");
    openLogin_->setAccessibleName("在系统浏览器打开所选来源的官方登录入口");
    auto *loginActions = new QHBoxLayout;
    loginActions->addWidget(openLogin_);
    loginActions->addStretch();
    loginLayout->addLayout(loginActions);
    layout->addWidget(loginPanel_);

    detail_ = new QTextBrowser;
    detail_->setObjectName("sourceDetail");
    detail_->setOpenExternalLinks(false);
    layout->addWidget(detail_, 2);
    status_ = new QLabel("学校配置与本机偏好分别保存；暂停和恢复会立即保存。");
    status_->setObjectName("sourceStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);

    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this] { updateSelection(); });
    connect(update_, &QPushButton::clicked, this, &SourcePage::updateSelected);
    connect(pause_, &QPushButton::clicked, this, &SourcePage::togglePause);
    connect(showLogin_, &QPushButton::clicked, this, &SourcePage::selectLoginSource);
    connect(openLogin_, &QPushButton::clicked, this, &SourcePage::openOfficialLogin);
    connect(detail_, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        if (url.scheme() == "https" || url.scheme() == "http")
            QDesktopServices::openUrl(url);
    });
    connect(&coordinator_, &RefreshCoordinator::sourcesChanged, this, &SourcePage::reload);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this] { reload(); });
    connect(&coordinator_, &RefreshCoordinator::started, this, [this] {
        status_->setText("正在更新官网列表；本轮更新完成后可暂停或恢复来源。");
        reload();
    });
    connect(&coordinator_, &RefreshCoordinator::finished, this, [this](int good, int bad) {
        status_->setText(good == 0 && bad == 0
                             ? "当前没有可更新的来源。可恢复已暂停来源，或查看已有缓存。"
                             : QString("本轮完成：%1 个来源完整成功，%2 个来源未完整成功。"
                                       "具体进度和错误请查看下方详情。")
                                   .arg(good)
                                   .arg(bad));
        reload();
    });
    reload();
}

void SourcePage::reload() {
    try {
        const auto *previous = selected();
        const std::string selectedId = previous ? previous->description.id : std::string{};
        auto current = sources_.list();
        const QSignalBlocker block(table_->selectionModel());
        model_->removeRows(0, model_->rowCount());
        views_ = std::move(current);
        int configured = 0;
        int paused = 0;
        int loginRequired = 0;
        int target = -1;
        for (std::size_t row = 0; row < views_.size(); ++row) {
            const auto &view = views_[row];
            const auto &state = view.state;
            const auto effective = view.effectiveStatus();
            if (view.description.requiresLogin)
                ++loginRequired;
            else if (view.description.configuredEnabled && view.description.ready) {
                ++configured;
                if (state.paused)
                    ++paused;
            }
            QList<QStandardItem *> items;
            const QStringList values{
                text(view.description.name),
                statusLabel(effective),
                timestamp(state.lastAttemptAt),
                timestamp(state.lastSuccessAt),
                state.latestPublishedDate.empty() ? QString("未记录")
                                                  : text(state.latestPublishedDate),
                QString("%1 页 / %2 条").arg(state.successfulPages).arg(state.rowCount)};
            for (const auto &value : values)
                items.append(new QStandardItem(value));
            items.front()->setToolTip(text(view.description.id));
            if (effective == "success")
                items[1]->setForeground(BrandTheme::statusColor(BrandTheme::StatusTone::Success));
            else if (effective == "failure")
                items[1]->setForeground(BrandTheme::statusColor(BrandTheme::StatusTone::Error));
            else if (effective == "partial_success" || effective == "interrupted" ||
                     effective == "login_required")
                items[1]->setForeground(BrandTheme::statusColor(BrandTheme::StatusTone::Warning));
            model_->appendRow(items);
            if (view.description.id == selectedId)
                target = static_cast<int>(row);
        }
        const auto baseSummary = QString("共 %1 个来源 · 已接入 %2 个 · 本机暂停 %3 个")
                                     .arg(views_.size()).arg(configured).arg(paused);
        summary_->setText(loginRequired > 0
                             ? baseSummary + QString(" · 需登录 %1 个 · 其他待接入 %2 个")
                                                 .arg(loginRequired)
                                                 .arg(static_cast<int>(views_.size()) - configured - loginRequired)
                             : baseSummary + QString(" · 待接入 %1 个")
                                                 .arg(static_cast<int>(views_.size()) - configured));
        loginSummary_->setText(QString("%1 个来源需要学校账号登录，可打开官方入口查看。").arg(loginRequired));
        loginSummary_->setVisible(loginRequired > 0);
        showLogin_->setVisible(loginRequired > 0);
        if (target < 0 && !views_.empty())
            target = 0;
        if (target >= 0)
            table_->setCurrentIndex(model_->index(target, 0));
        updateSelection();
    } catch (const std::exception &error) {
        status_->setText("来源读取失败：" + QString::fromUtf8(error.what()));
        update_->setEnabled(false);
        pause_->setEnabled(false);
        loginPanel_->hide();
        loginSummary_->hide();
        showLogin_->hide();
        openLogin_->setEnabled(false);
    }
}

const SourceView *SourcePage::selected() const {
    const int row = table_->currentIndex().row();
    if (row < 0 || static_cast<std::size_t>(row) >= views_.size())
        return nullptr;
    return &views_[static_cast<std::size_t>(row)];
}

void SourcePage::updateSelection() {
    const auto *view = selected();
    const bool requiresLogin = view && view->description.requiresLogin;
    const bool configured = view && !requiresLogin && view->description.configuredEnabled && view->description.ready;
    update_->setEnabled(view && sources_.isRunnable(view->description.id) && !coordinator_.busy());
    pause_->setEnabled(configured && view->state.status != "updating" && !coordinator_.busy());
    pause_->setText(view && view->state.paused ? "恢复更新" : "暂停更新");
    loginPanel_->setVisible(requiresLogin);
    openLogin_->setEnabled(requiresLogin && safeLoginAddress(view->description.loginUrl));
    if (!view) {
        detail_->setPlainText("选择一个来源查看配置、采集进度与错误。");
        return;
    }
    const auto &description = view->description;
    const auto &state = view->state;
    if (requiresLogin) {
        loginExplanation_->setText(
            "请使用学校账号（如学号或教务账号）在官方网页登录后查看通知。"
            "账号和密码在学校页面输入，CampusPulse 当前不接收或保存密码。\n"
            "当前版本尚未连接浏览器登录会话，登录后本来源仍不自动采集；受限通知请在官网查看。");
        loginAddress_->setText(!safeLoginAddress(description.loginUrl)
                                  ? QString("尚未核验官方登录入口，请联系学校包维护者补充。")
                                  : "官方入口：" + text(description.loginUrl));
    }
    QStringList categories;
    for (const auto &category : description.categories)
        categories.append(categoryLabel(category));
    QString details = "<h3>" + escaped(description.name) + "</h3>";
    // Legacy login failures retain their raw URLs as diagnostic text. Only the
    // validated login URL may become an actionable link for a protected source.
    const auto sourceLink = [&](const std::string &url) {
        if (requiresLogin && (url != description.loginUrl || !safeLoginAddress(url)))
            return url.empty() ? QString("未配置") : escaped(url);
        return officialLink(url);
    };
    details += "<p><b>来源标识：</b>" + escaped(description.schoolId) + " / " +
               escaped(description.id) + "<br><b>采集入口：</b>" +
               sourceLink(description.entryUrl) + "<br><b>官方导航：</b>" +
               sourceLink(description.discoveryUrl) + "</p>";
    details += "<p><b>主题覆盖：</b>" + (categories.isEmpty() ? QString("以通知标题分类，可在通知页筛选") : categories.join("、")).toHtmlEscaped() +
               "<br><b>配置页数上限：</b>" + QString::number(description.maxPages) +
               " 页<br><b>社区配置：</b>" +
               (requiresLogin ? QString("需要账号登录，尚未连接登录会话")
                              : configured ? QString("已启用，可采集") : QString("未就绪，不参与采集")) +
               "<br><b>本机偏好：</b>" +
               (state.paused ? QString("已暂停，保留通知缓存") : QString("未暂停")) + "</p>";
    details += "<p><b>当前状态：</b>" + statusLabel(view->effectiveStatus()) +
               "<br><b>最近尝试：</b>" + timestamp(state.lastAttemptAt) +
               "<br><b>最近完整成功：</b>" + timestamp(state.lastSuccessAt) +
               "<br><b>已成功读取：</b>" + QString::number(state.successfulPages) + " 页，共 " +
               QString::number(state.rowCount) + " 条（各页列表条数累计）</p>";
    if (!description.pendingReason.empty())
        details += "<p><b>配置说明：</b>" + escaped(description.pendingReason) + "</p>";
    if (!state.error.empty())
        details += "<p><b>最近错误：</b>" + escaped(state.error).replace("\n", "<br>") + "</p>";
    if (state.status == "updating" && !coordinator_.busy())
        details += "<p>此来源仍记录为更新中，更新和暂停暂不可用。若上次采集未正常结束，"
                   "重新启动后会标记为中断，已缓存通知仍可查看。</p>";
    if (state.lastAttemptAt.empty())
        details += "<p>旧版缓存不包含采集时间；首次更新后开始记录尝试时间与完整成功时间。</p>";
    detail_->setHtml(details);
}

void SourcePage::updateSelected() {
    const auto *view = selected();
    if (!view || coordinator_.busy() || !sources_.isRunnable(view->description.id))
        return;
    try {
        coordinator_.refreshSource(text(view->description.id));
    } catch (const std::exception &error) {
        status_->setText("来源更新失败：" + QString::fromUtf8(error.what()));
        reload();
    }
}

void SourcePage::togglePause() {
    const auto *view = selected();
    if (!view || coordinator_.busy() || !view->description.configuredEnabled ||
        !view->description.ready)
        return;
    const auto id = view->description.id;
    const auto name = text(view->description.name);
    const bool paused = !view->state.paused;
    try {
        sources_.setPaused(id, paused);
        reload();
        status_->setText(name + (paused ? "：已暂停更新，保留通知缓存。" : "：已恢复更新。"));
    } catch (const std::exception &error) {
        status_->setText("本机偏好保存失败：" + QString::fromUtf8(error.what()));
        QMessageBox::warning(this, "来源偏好保存失败", QString::fromUtf8(error.what()));
    }
}

void SourcePage::selectLoginSource() {
    const int currentRow = table_->currentIndex().row();
    for (int offset = 1; offset <= static_cast<int>(views_.size()); ++offset) {
        const int row = (currentRow + offset) % static_cast<int>(views_.size());
        if (views_[static_cast<std::size_t>(row)].description.requiresLogin) {
            table_->setCurrentIndex(model_->index(row, 0));
            table_->scrollTo(table_->currentIndex());
            return;
        }
    }
}

void SourcePage::openOfficialLogin() {
    const auto *view = selected();
    if (!view || !view->description.requiresLogin || !safeLoginAddress(view->description.loginUrl))
        return;
    if (QDesktopServices::openUrl(QUrl(text(view->description.loginUrl))))
        status_->setText("已请求系统浏览器打开官方入口，请在学校网页登录。浏览器登录不会自动启用此来源的采集。");
    else
        status_->setText("无法打开系统浏览器，请复制上方官方入口自行访问。");
}
} // namespace campus
