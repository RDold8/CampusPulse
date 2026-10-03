#include "desktop/ResourcePage.h"
#include "adapters/ResourceClassifier.h"
#include "adapters/ResourceDiscovery.h"
#include "desktop/BrandTheme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStyleHints>
#include <QTableView>
#include <QTextBrowser>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

namespace campus {
namespace {
QString text(const std::string &value) {
    return QString::fromStdString(value);
}
QString escaped(const std::string &value) {
    return text(value).toHtmlEscaped();
}
const std::pair<const char *, const char *> categories[] = {
    {"study_plan", "学习规划"}, {"course_material", "课程资料"},  {"library", "图书馆"},
    {"competition", "竞赛"},    {"academic_support", "学术支持"}, {"student_services", "办事服务"},
    {"career", "就业"},         {"campus_life", "校园服务"},      {"other", "其他"}};
QString categoryLabel(const std::string &value) {
    for (const auto &[key, label] : categories)
        if (value == key)
            return QString::fromUtf8(label);
    return "类别待核实";
}
QString purposeHint(const std::string &category) {
    if (category == "study_plan")
        return "查看培养方案与课程安排指引；适用年级以学校说明为准。";
    if (category == "course_material")
        return "查找课程资料与学习平台；课程内容和访问权限以提供方说明为准。";
    if (category == "library")
        return "查找馆藏、数据库和使用指引；权限以学校说明为准。";
    if (category == "competition")
        return "查找竞赛平台与参赛指引；报名安排以官方网站为准。";
    if (category == "academic_support")
        return "查找文献检索、学术工具与研究支持；服务范围以学校说明为准。";
    if (category == "student_services")
        return "查找学生办事入口与办理指引；具体要求以学校说明为准。";
    if (category == "career")
        return "查找就业服务与求职指引；招聘信息以发布方说明为准。";
    if (category == "campus_life")
        return "查找校园服务与生活指引；服务规则以学校说明为准。";
    return "查看学校官网提供的实用入口；具体用途与适用范围待核实。";
}
QString stageLabel(const SchoolResource &resource) {
    if (resource.audiences.empty())
        return "适用阶段待核实";
    QStringList labels;
    for (const auto &stage : resource.audiences)
        if (stage == "undergraduate")
            labels << "本科";
        else if (stage == "postgraduate")
            labels << "研究生";
        else if (stage == "general")
            labels << "通用";
    return labels.isEmpty() ? QString("适用阶段待核实") : labels.join(" / ");
}
QString accessLabel(const SchoolResource &resource) {
    if (resource.status == "login_required")
        return "入口需登录";
    if (resource.status == "unreachable")
        return "入口检查未通过";
    if (resource.linkKind == "official_recommended")
        return resource.status == "verified" ? "外部官网推荐 · 入口可访问"
                                             : "外部官网推荐 · 未验证";
    if (resource.status == "verified")
        return "入口可访问";
    return "已发现 · 待核实";
}
QString checkedAt(const std::string &value, const QString &timeZone) {
    if (value.empty())
        return "尚未检查";
    const auto date = QDateTime::fromString(text(value), Qt::ISODateWithMs);
    if (!date.isValid())
        return text(value);
    const QTimeZone zone(timeZone.toUtf8());
    return date.toTimeZone(zone.isValid() ? zone : QTimeZone(QTimeZone::UTC))
        .toString("yyyy-MM-dd HH:mm:ss");
}
} // namespace

ResourcePage::ResourcePage(const SchoolPackage &school, ResourceService &resources,
                           ResourceDiscovery &discovery, QWidget *parent)
    : QWidget(parent), school_(school), resources_(resources), discovery_(discovery) {
    setObjectName("resourcePage");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(14);
    auto *heading = new QLabel("学校资源");
    heading->setObjectName("heading");
    layout->addWidget(heading);
    auto *subtitle = new QLabel(school.name + " · 学习、办事与校园实用入口");
    subtitle->setObjectName("subtitle");
    subtitle->setTextFormat(Qt::PlainText);
    layout->addWidget(subtitle);
    auto *scope = new QLabel("默认展示全部学习阶段。入口可访问不代表已获得资源权限。"
                             "账号、机构授权和试用范围请查看学校或提供方说明。");
    scope->setObjectName("scope");
    scope->setWordWrap(true);
    layout->addWidget(scope);

    auto *filters = new QHBoxLayout;
    category_ = new QComboBox;
    category_->setObjectName("resourceCategoryFilter");
    category_->setAccessibleName("学校资源类别");
    category_->addItem("全部类别", "");
    for (const auto &[key, label] : categories)
        category_->addItem(QString::fromUtf8(label), key);
    stage_ = new QComboBox;
    stage_->setObjectName("resourceStageFilter");
    stage_->setAccessibleName("学校资源适用学习阶段");
    stage_->addItem("全部阶段", "");
    stage_->addItem("本科", "undergraduate");
    stage_->addItem("研究生", "postgraduate");
    stage_->addItem("通用", "general");
    stage_->addItem("适用阶段待核实", "unknown");
    search_ = new QLineEdit;
    search_->setObjectName("resourceSearch");
    search_->setAccessibleName("搜索学校资源名称、用途与提供方");
    search_->setPlaceholderText("搜索名称、用途或提供方");
    search_->setClearButtonEnabled(true);
    favorites_ = new QCheckBox("仅收藏");
    favorites_->setObjectName("resourceFavoritesOnly");
    filters->addWidget(category_);
    filters->addWidget(stage_);
    filters->addWidget(search_, 1);
    filters->addWidget(favorites_);
    layout->addLayout(filters);

    auto *controls = new QHBoxLayout;
    summary_ = new QLabel;
    summary_->setObjectName("resourceSummary");
    summary_->setWordWrap(true);
    controls->addWidget(summary_, 1);
    discover_ = new QPushButton("后台发现资源");
    discover_->setObjectName("discoverResourcesButton");
    cancel_ = new QPushButton("取消发现");
    cancel_->setObjectName("cancelResourceDiscoveryButton");
    controls->addWidget(discover_);
    controls->addWidget(cancel_);
    layout->addLayout(controls);

    auto *split = new QSplitter(Qt::Vertical);
    model_ = new QStandardItemModel(this);
    model_->setHorizontalHeaderLabels({"名称", "类别", "适用阶段", "访问状态", "提供方"});
    table_ = new QTableView;
    table_->setObjectName("resourceTable");
    table_->setAccessibleName("学校资源列表");
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setWordWrap(false);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->setColumnWidth(1, 112);
    table_->setColumnWidth(2, 154);
    table_->setColumnWidth(3, 204);
    table_->setColumnWidth(4, 158);
    split->addWidget(table_);
    detail_ = new QTextBrowser;
    detail_->setObjectName("resourceDetail");
    detail_->setAccessibleName("所选资源用途、访问要求与官方发现出处");
    detail_->setOpenLinks(false);
    detail_->setOpenExternalLinks(false);
    split->addWidget(detail_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);

    auto *actions = new QHBoxLayout;
    open_ = new QPushButton("打开资源");
    open_->setObjectName("openResourceButton");
    favorite_ = new QPushButton("收藏");
    favorite_->setObjectName("favoriteResourceButton");
    actions->addWidget(open_);
    actions->addWidget(favorite_);
    actions->addStretch();
    layout->addLayout(actions);
    status_ = new QLabel("已缓存的资源可直接查看；后台发现期间仍可筛选和收藏。");
    status_->setObjectName("resourceStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(100);
    connect(refreshTimer_, &QTimer::timeout, this, &ResourcePage::reload);

    connect(category_, &QComboBox::currentIndexChanged, this, &ResourcePage::reload);
    connect(stage_, &QComboBox::currentIndexChanged, this, &ResourcePage::reload);
    connect(search_, &QLineEdit::textChanged, this, &ResourcePage::reload);
    connect(favorites_, &QCheckBox::toggled, this, &ResourcePage::reload);
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this] { updateSelection(); });
    connect(open_, &QPushButton::clicked, this, &ResourcePage::openResource);
    connect(favorite_, &QPushButton::clicked, this, &ResourcePage::toggleFavorite);
    connect(discover_, &QPushButton::clicked, this, &ResourcePage::discover);
    connect(cancel_, &QPushButton::clicked, this, &ResourcePage::cancelDiscovery);
    connect(detail_, &QTextBrowser::anchorClicked, this, &ResourcePage::openOfficialOrigin);
    connect(&discovery_, &ResourceDiscovery::changed, this, [this] {
        if (!refreshTimer_->isActive())
            refreshTimer_->start();
    });
    connect(&discovery_, &ResourceDiscovery::started, this, [this] {
        cancelRequested_ = false;
        failureReason_.clear();
        status_->setText("正在后台发现学校官网中的实用资源；缓存仍可查看和收藏。");
        updateSelection();
    });
    connect(&discovery_, &ResourceDiscovery::progress, this,
            [this](const QString &message) { status_->setText(message); });
    connect(&discovery_, &ResourceDiscovery::failed, this, [this](const QString &reason) {
        failureReason_ = reason;
        reload();
        status_->setText("资源发现失败：" + reason + "；已缓存的资源仍可使用。");
        updateSelection();
    });
    connect(
        &discovery_, &ResourceDiscovery::finished, this,
        [this](int resources, int verified, int failed) {
            reload();
            if (!failureReason_.isEmpty())
                status_->setText("资源发现失败：" + failureReason_ + "；已缓存的资源仍可使用。");
            else if (cancelRequested_)
                status_->setText("资源发现已取消；已发现的结果和原有收藏均已保留。");
            else
                status_->setText(QString("本轮发现 %1 项资源，%2 项已检查可访问，%3 次检查未成功。"
                                         "未核实、登录受限和外部推荐情况请查看资源详情。")
                                     .arg(resources)
                                     .arg(verified)
                                     .arg(failed));
        });
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this] { reload(); });
    reload();
}

void ResourcePage::activation() {
    if (activated_)
        return;
    activated_ = true;
    try {
        if (resources_.list().empty())
            QTimer::singleShot(0, this, [this] { discover(); });
    } catch (const std::exception &error) {
        status_->setText("资源缓存读取失败：" + QString::fromUtf8(error.what()));
    }
}

void ResourcePage::reload() {
    // Filters and terminal discovery signals consume the queued update immediately.
    refreshTimer_->stop();
    try {
        const auto *previous = selected();
        const auto previousId = previous ? previous->id : std::string{};
        ResourceQuery query;
        query.category = category_->currentData().toString().toStdString();
        query.stage = stage_->currentData().toString().toStdString();
        query.keyword = search_->text().toStdString();
        query.onlyFavorites = favorites_->isChecked();
        auto current = resources_.list(query);
        const auto total = resources_.list().size();
        const QSignalBlocker blocker(table_->selectionModel());
        model_->removeRows(0, model_->rowCount());
        views_ = std::move(current);
        int target = -1;
        for (std::size_t row = 0; row < views_.size(); ++row) {
            const auto &resource = views_[row];
            QList<QStandardItem *> items;
            const QStringList values{
                (resource.favorite ? QString("★ ") : QString{}) + text(resource.title),
                categoryLabel(resource.category), stageLabel(resource), accessLabel(resource),
                resource.provider.empty() ? QString("提供方待核实") : text(resource.provider)};
            for (const auto &value : values) {
                auto *item = new QStandardItem(value);
                item->setToolTip(value);
                items.append(item);
            }
            items.front()->setData(text(resource.id), Qt::UserRole);
            if (resource.status == "unreachable")
                items[3]->setForeground(BrandTheme::statusColor(BrandTheme::StatusTone::Error));
            else if (resource.status == "login_required" ||
                     resource.linkKind == "official_recommended")
                items[3]->setForeground(BrandTheme::statusColor(BrandTheme::StatusTone::Warning));
            else if (resource.status == "verified")
                items[3]->setForeground(BrandTheme::statusColor(BrandTheme::StatusTone::Success));
            model_->appendRow(items);
            if (resource.id == previousId)
                target = static_cast<int>(row);
        }
        summary_->setText(QString("已缓存 %1 项 · 当前显示 %2 项").arg(total).arg(views_.size()));
        if (target < 0 && !views_.empty())
            target = 0;
        if (target >= 0)
            table_->setCurrentIndex(model_->index(target, 0));
        updateSelection();
    } catch (const std::exception &error) {
        status_->setText("学校资源读取失败：" + QString::fromUtf8(error.what()));
        open_->setEnabled(false);
        favorite_->setEnabled(false);
    }
}

const SchoolResource *ResourcePage::selected() const {
    const int row = table_->currentIndex().row();
    return row >= 0 && static_cast<std::size_t>(row) < views_.size()
               ? &views_[static_cast<std::size_t>(row)]
               : nullptr;
}

bool ResourcePage::officialOrigin(const QUrl &url) const {
    return ResourceClassifier::isSafeHttps(url) &&
           ResourceClassifier::isOfficial(
               url, ResourceClassifier::officialRoot(school_.officialHomepage));
}

bool ResourcePage::canOpen(const SchoolResource &resource) const {
    const QUrl url(text(resource.url), QUrl::StrictMode);
    if (!ResourceClassifier::isSafeHttps(text(resource.url)))
        return false;
    if (resource.linkKind == "official")
        return officialOrigin(url);
    return resource.linkKind == "official_recommended" &&
           ResourceClassifier::isSafeHttps(text(resource.discoveredFrom)) &&
           officialOrigin(QUrl(text(resource.discoveredFrom), QUrl::StrictMode));
}

void ResourcePage::updateSelection() {
    const auto *resource = selected();
    open_->setEnabled(resource && canOpen(*resource));
    favorite_->setEnabled(resource != nullptr);
    favorite_->setText(resource && resource->favorite ? "取消收藏" : "收藏");
    discover_->setEnabled(!discovery_.busy());
    cancel_->setEnabled(discovery_.busy());
    if (!resource) {
        detail_->setPlainText(
            "当前筛选下没有资源。可清除筛选，或点击“后台发现资源”从学校官网查找实用入口。");
        return;
    }
    QString details = "<h3>" + escaped(resource->title) + "</h3><p><b>资源地址：</b>" +
                      escaped(resource->url) + "<br><b>官网发现出处：</b>";
    const QUrl origin(text(resource->discoveredFrom), QUrl::StrictMode);
    if (ResourceClassifier::isSafeHttps(text(resource->discoveredFrom)) && officialOrigin(origin))
        details += "<a href=\"" + origin.toString(QUrl::FullyEncoded).toHtmlEscaped() + "\">" +
                   escaped(resource->discoveredFrom) + "</a>";
    else
        details += resource->discoveredFrom.empty()
                       ? QString("尚未记录")
                       : escaped(resource->discoveredFrom) + "（官方出处待核实）";
    details += "</p>";
    if (!resource->accessNote.empty()) {
        details += "<p><b>用途与使用条件：</b>" +
                   escaped(resource->accessNote).replace("\n", "<br>") +
                   "<br><b>说明出处：</b>";
        const QUrl evidence(text(resource->accessEvidence), QUrl::StrictMode);
        if (ResourceClassifier::isSafeHttps(text(resource->accessEvidence)) &&
            officialOrigin(evidence))
            details += "<a href=\"" + evidence.toString(QUrl::FullyEncoded).toHtmlEscaped() +
                       "\">" + escaped(resource->accessEvidence) + "</a>";
        else
            details += escaped(resource->accessEvidence) + "（学校官方出处待核实）";
        details += "</p>";
    } else {
        details += "<p><b>用途提示：</b>" + purposeHint(resource->category) + "</p>";
    }
    details += "<p><b>类别：</b>" +
        categoryLabel(resource->category) + " · <b>适用阶段：</b>" + stageLabel(*resource) +
        " · <b>提供方：</b>" +
        (resource->provider.empty() ? QString("提供方待核实") : escaped(resource->provider)) +
        "<br><b>访问状态：</b>" + accessLabel(*resource) + " · <b>最近检查：</b>" +
        checkedAt(resource->lastCheckedAt, school_.timeZone).toHtmlEscaped() + "</p>";
    if (resource->linkKind == "official_recommended")
        details += resource->status == "verified"
                       ? "<p><b>学校官网推荐的外部资源，公开入口已检查。</b>"
                         "入口可访问不代表学校购买全部内容，也不代表个人已获得全文权限。</p>"
                       : "<p><b>学校官网推荐的外部资源，尚未验证。</b>"
                         "本次自动检查未通过时，仍可能需要浏览器、校园网或机构登录。</p>";
    if (resource->status == "login_required")
        details += "<p>"
                   "请按学校或提供方说明在官方页面登录。CampusPulse不接收或保存账号密码；浏览器"
                   "登录不会自动连通采集。</p>";
    if (!canOpen(*resource))
        details += "<p><b>暂不能打开：</b>资源地址或学校官方出处尚未通过校验。</p>";
    if (!resource->error.empty())
        details +=
            "<p><b>最近检查说明：</b>" + escaped(resource->error).replace("\n", "<br>") + "</p>";
    if (!resource->description.empty()) {
        auto excerpt = text(resource->description).simplified();
        if (excerpt.size() > 140)
            excerpt = excerpt.left(139) + QChar(0x2026);
        if (!excerpt.isEmpty())
            details += "<p><b>网页摘录：</b>" + excerpt.toHtmlEscaped() + "</p>";
    }
    detail_->setHtml(details);
}

void ResourcePage::discover() {
    if (discovery_.busy())
        return;
    cancelRequested_ = false;
    failureReason_.clear();
    try {
        discovery_.start();
        updateSelection();
    } catch (const std::exception &error) {
        status_->setText("资源发现启动失败：" + QString::fromUtf8(error.what()));
        updateSelection();
    }
}

void ResourcePage::cancelDiscovery() {
    if (!discovery_.busy())
        return;
    cancelRequested_ = true;
    discovery_.cancel();
    updateSelection();
}

void ResourcePage::toggleFavorite() {
    const auto *resource = selected();
    if (!resource)
        return;
    const auto id = resource->id;
    const bool favorite = !resource->favorite;
    try {
        resources_.setFavorite(id, favorite);
        reload();
        status_->setText(favorite ? "资源已收藏，更新和重新启动后仍会保留。" : "已取消资源收藏。");
    } catch (const std::exception &error) {
        status_->setText("收藏保存失败：" + QString::fromUtf8(error.what()));
    }
}

void ResourcePage::openResource() {
    const auto *resource = selected();
    if (!resource || !canOpen(*resource)) {
        status_->setText("资源地址或学校官方出处尚未通过校验，暂不能打开。");
        return;
    }
    if (QDesktopServices::openUrl(QUrl(text(resource->url), QUrl::StrictMode)))
        status_->setText(resource->linkKind == "official_recommended"
                             ? "已请求浏览器打开学校官网推荐的外部入口；使用权限请按官方说明核对。"
                             : "已请求浏览器打开学校资源；需要登录时请按官方页面说明操作。");
    else
        status_->setText("无法打开系统浏览器，请复制详情中的资源地址自行访问。");
}

void ResourcePage::openOfficialOrigin(const QUrl &url) {
    const auto *resource = selected();
    const bool discoveryOrigin = resource &&
        ResourceClassifier::isSafeHttps(text(resource->discoveredFrom)) &&
        url == QUrl(text(resource->discoveredFrom), QUrl::StrictMode);
    const bool accessEvidence = resource && !resource->accessNote.empty() &&
        ResourceClassifier::isSafeHttps(text(resource->accessEvidence)) &&
        url == QUrl(text(resource->accessEvidence), QUrl::StrictMode);
    if ((!discoveryOrigin && !accessEvidence) || !officialOrigin(url)) {
        status_->setText("该链接尚未通过学校官方出处校验，暂不能打开。");
        return;
    }
    if (!QDesktopServices::openUrl(url))
        status_->setText("无法打开系统浏览器，请复制官网发现出处自行访问。");
    else
        status_->setText("已请求浏览器打开学校官网中的资源说明出处。");
}
} // namespace campus
