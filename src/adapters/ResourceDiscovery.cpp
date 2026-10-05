#include "adapters/ResourceDiscovery.h"
#include "adapters/ArtifactWriter.h"
#include "adapters/ResourceClassifier.h"
#include "adapters/PublicUniversityNetwork.h"
#include "adapters/SchoolOnboarding.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>
#include <lexbor/html/html.h>
#include <memory>
#include <stdexcept>

namespace campus {
namespace {
QString canonical(const QUrl &url) {
    return ResourceClassifier::canonicalUrl(url).toString(QUrl::FullyEncoded);
}
QString now() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
QString hash(const QString &value) {
    return QString::fromLatin1(
        QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}
// HtmlAdapter supplies mature DOM links. This supplementary raw-attribute
// check prevents QUrl normalization from erasing an empty @ or : boundary.
QSet<QString> unsafeTargets(const QByteArray &html, const QUrl &page) {
    const auto deleter = [](lxb_html_document_t *value) { lxb_html_document_destroy(value); };
    std::unique_ptr<lxb_html_document_t, decltype(deleter)> doc(lxb_html_document_create(),
                                                                deleter);
    QSet<QString> result;
    if (!doc ||
        lxb_html_document_parse(doc.get(), reinterpret_cast<const lxb_char_t *>(html.constData()),
                                size_t(html.size())) != LXB_STATUS_OK)
        throw std::runtime_error("资源HTML解析失败");
    std::vector<lxb_dom_node_t *> nodes{lxb_dom_interface_node(doc.get())};
    static const QRegularExpression authority("^(?:https?:)?//([^/?#]*)",
                                              QRegularExpression::CaseInsensitiveOption);
    while (!nodes.empty()) {
        const auto node = nodes.back();
        nodes.pop_back();
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT && node->local_name == LXB_TAG_A) {
            size_t size = 0;
            const auto attribute = lxb_dom_element_get_attribute(
                lxb_dom_interface_element(node), reinterpret_cast<const lxb_char_t *>("href"), 4,
                &size);
            if (attribute) {
                const auto href =
                    QString::fromUtf8(reinterpret_cast<const char *>(attribute), qsizetype(size))
                        .trimmed();
                const auto raw = authority.match(href);
                if ((raw.hasMatch() &&
                     (raw.captured(1).contains('@') || raw.captured(1).contains(':'))) ||
                    href.contains(QRegularExpression("[\\s\\\\]"))) {
                    auto url = page.resolved(QUrl(href));
                    if (url.scheme() == "http")
                        url.setScheme("https");
                    result.insert(canonical(url));
                }
            }
        }
        for (auto child = node->first_child; child; child = child->next)
            nodes.push_back(child);
    }
    return result;
}
bool navigation(const QString &label) {
    static const QRegularExpression pattern(
        "教务处|本科生教育|本科生院|研究生教育|在校生|院系机构|图书馆|研究生院|研究生部|学生工作|学工|财务|团委|就业|机构设置|部门导航|人才培养|"
        "教学服务|办事指南|课表考表|双创教育|创新创业|"
        "服务指南|电子资源");
    return label.size() <= 40 && !label.contains("通知") && !label.contains("关于") &&
           (pattern.match(label).hasMatch() || SchoolOnboarding::isDiscoveryLabel(label));
}
} // namespace

ResourceDiscovery::ResourceDiscovery(const SchoolPackage &school, ResourceService &service,
                                     QObject *parent, ResourceDiscoveryOptions options)
    : QObject(parent), school_(school), service_(service), options_(std::move(options)),
      officialRoot_(ResourceClassifier::officialRoot(school.officialHomepage)) {
    if (school_.id.isEmpty() || !ResourceClassifier::isSafeHttps(school_.officialHomepage) ||
        options_.maxPages < 1 || options_.maxPages > 32 || options_.requestIntervalMs < 0 ||
        options_.transferTimeoutMs < 1 || options_.maxResponseBytes < 1 ||
        options_.maxResponseBytes > 2 * 1024 * 1024 || options_.maxDepth < 1 ||
        options_.maxDepth > 3)
        throw std::invalid_argument("资源发现需要可信高校HTTPS首页及有效有界采集配置");
    const auto base = options_.evidenceDirectory.isEmpty()
                          ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                                "/resource-evidence"
                          : options_.evidenceDirectory;
    if (base.isEmpty())
        throw std::runtime_error("资源证据目录不可用");
    evidenceDirectory_ = QDir(base).filePath(school_.id);
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &ResourceDiscovery::next);
}
ResourceDiscovery::~ResourceDiscovery() {
    busy_ = false;
    timer_.stop();
    delete reply_.data();
}
bool ResourceDiscovery::busy() const {
    return busy_;
}

void ResourceDiscovery::saveResource(SchoolResource resource) {
    const auto url = QString::fromStdString(resource.url);
    if (resources_.contains(url)) {
        const auto &previous = resources_[url];
        if (!previous.discoveredFrom.empty())
            resource.discoveredFrom = previous.discoveredFrom;
        if (resource.description.empty())
            resource.description = previous.description;
        if (resource.accessNote.empty() && resource.accessEvidence.empty()) {
            resource.accessNote = previous.accessNote;
            resource.accessEvidence = previous.accessEvidence;
        }
        if (resource.status == "discovered" && resource.lastCheckedAt.empty() &&
            previous.status != "discovered") {
            resource.status = previous.status;
            resource.lastCheckedAt = previous.lastCheckedAt;
            resource.error = previous.error;
        }
    }
    service_.ingest({resource});
    resources_[url] = std::move(resource);
    observed_.insert(url);
    emit changed();
}
void ResourceDiscovery::enqueue(const Page &candidate) {
    auto page = candidate;
    page.url = ResourceClassifier::canonicalUrl(page.url);
    if (!ResourceClassifier::isOfficial(page.url, officialRoot_) ||
        ResourceClassifier::isDownload(page.url) || page.depth > options_.maxDepth ||
        queue_.size() >= 256)
        return;
    const auto url = canonical(page.url);
    if (queued_.contains(url))
        return;
    queued_.insert(url);
    if (page.origin.isEmpty())
        page.origin = page.url;
    queue_.push_back(std::move(page));
}
void ResourceDiscovery::start() {
    if (busy_)
        return;
    busy_ = true;
    cancelled_ = false;
    queue_.clear();
    queued_.clear();
    observed_.clear();
    resources_.clear();
    verifiedUrls_.clear();
    hostRequests_.clear();
    fetched_ = failures_ = 0;
    try {
        for (const auto &resource : service_.list())
            if (ResourceClassifier::isSafeHttps(QString::fromStdString(resource.url)))
                resources_.insert(QString::fromStdString(resource.url), resource);
        if (!QDir().mkpath(evidenceDirectory_))
            throw std::runtime_error("无法创建资源证据目录");
        enqueue({school_.officialHomepage, school_.officialHomepage, school_.officialHomepage,
                 school_.name, 0, 0});
        for (const auto &entry : school_.resourceDiscoveryEntries) {
            const QUrl url(entry, QUrl::StrictMode);
            if (!ResourceClassifier::isOfficial(url, officialRoot_))
                continue;
            auto resource = ResourceClassifier::describe(
                school_.id, "学校资源入口 · " + url.host() + url.path().left(160), url,
                school_.officialHomepage);
            saveResource(resource);
            enqueue({url, url, school_.officialHomepage, {}, 0, 0});
        }
        for (const auto &source : school_.catalog) {
            const auto entry = source.entryUrl.empty() ? source.discoveryUrl : source.entryUrl;
            if (!ResourceClassifier::isSafeHttps(QString::fromStdString(entry)))
                continue;
            const QUrl url(QString::fromStdString(entry), QUrl::StrictMode);
            if (!ResourceClassifier::isOfficial(url, officialRoot_))
                continue;
            if (source.requiresLogin) {
                if (!source.loginUrl.empty() &&
                    ResourceClassifier::isSafeHttps(QString::fromStdString(source.loginUrl))) {
                    auto resource = ResourceClassifier::describe(
                        school_.id, QString::fromStdString(source.name),
                        QUrl(QString::fromStdString(source.loginUrl)), school_.officialHomepage);
                    resource.status = "login_required";
                    resource.error = source.pendingReason;
                    saveResource(resource);
                }
                continue; // Known authentication is never retried as public content.
            }
            enqueue(
                {url, url, school_.officialHomepage, QString::fromStdString(source.name), 0, 0});
        }
        emit started();
        emit progress("开始后台发现官网学习与办事资源；未访问的链接保留为待验证。");
        timer_.start(0); // Lets an immediate cancel stop before the first request.
    } catch (const std::exception &error) {
        busy_ = false;
        emit failed(QString::fromUtf8(error.what()));
    }
}
void ResourceDiscovery::cancel() {
    if (!busy_)
        return;
    cancelled_ = true;
    timer_.stop();
    queue_.clear();
    if (reply_) {
        delete reply_.data();
        reply_ = nullptr;
    }
    emit progress("资源发现已取消；已保存的资源与收藏保留。");
    complete();
}
void ResourceDiscovery::complete() {
    if (!busy_)
        return;
    busy_ = false;
    QJsonArray frontier;
    for (const auto &page : queue_)
        frontier.append(QJsonObject{{"url", canonical(page.url)}, {"label", page.label},
                                    {"depth", page.depth}, {"redirects", page.redirects}});
    try {
        writeArtifact(evidenceDirectory_ + "/report.json", QJsonDocument(QJsonObject{
            {"school_id", school_.id}, {"fetched_pages", fetched_}, {"model_calls", 0},
            {"resources", static_cast<int>(observed_.size())},
            {"verified", static_cast<int>(verifiedUrls_.size())}, {"failures", failures_},
            {"cancelled", cancelled_}, {"finished_at", now()},
            {"budget_exhausted", !queue_.empty() && fetched_ >= std::min(options_.maxPages, school_.resourceDiscoveryLimit)},
            {"pending_frontier", frontier}}).toJson());
    } catch (const std::exception &error) {
        emit failed("无法保存资源发现报告：" + QString::fromUtf8(error.what()));
    }
    emit finished(observed_.size(), verifiedUrls_.size(), failures_);
}
void ResourceDiscovery::next() {
    if (!busy_)
        return;
    if (queue_.empty() || fetched_ >= std::min(options_.maxPages, school_.resourceDiscoveryLimit)) {
        if (!queue_.empty())
            emit progress("已达到本轮有界请求上限，其余官网链接仍为待验证。");
        complete();
        return;
    }
    const auto page = takeNext();
    if (!ResourceClassifier::isOfficial(page.url, officialRoot_)) {
        next();
        return;
    }
    ++fetched_;
    ++hostRequests_[page.url.host()];
    emit progress(QString("资源发现 %1/%2：%3")
                      .arg(fetched_)
                      .arg(std::min(options_.maxPages, school_.resourceDiscoveryLimit))
                      .arg(page.url.host()));
    reply_ = PublicUniversityNetwork::get(
        page.url, officialRoot_, this, [this, page](UniversityPageResponse result) {
            if (!busy_)
                return;
            reply_ = nullptr;
            auto redirect = result.redirect;
            QString error = result.error;
            const QByteArray bytes = result.bytes;
            if (error.isEmpty() && !redirect.isEmpty()) {
                if (ResourceClassifier::isOfficial(redirect, officialRoot_) &&
                    !ResourceClassifier::isDownload(redirect) && page.redirects < 3) {
                    try {
                        const QJsonObject evidence{{"requested_url", canonical(page.url)},
                                                   {"resource_url", canonical(page.origin)},
                                                   {"redirect_url", canonical(redirect)},
                                                   {"discovered_from", canonical(page.discoveredFrom)},
                                                   {"checked_at", now()},
                                                   {"status", "redirect"}};
                        writeArtifact(evidenceDirectory_ + "/" + hash(canonical(page.url)) +
                                          "-redirect-" + QString::number(page.redirects) + ".json",
                                      QJsonDocument(evidence).toJson());
                        auto target = page;
                        target.url = redirect;
                        ++target.redirects;
                        queue_.push_front(target);
                    } catch (const std::exception &failure) {
                        error = QString::fromUtf8(failure.what());
                    }
                } else
                    error = "重定向超出高校官方HTTPS域、指向文件或超过次数上限";
            }
            if (!redirect.isEmpty() && error.isEmpty()) {
                emit progress("官网重定向已记录，等待目标静态内容验证。");
            } else {
                try {
                    consume(page, bytes, error);
                } catch (const std::exception &failure) {
                    busy_ = false;
                    queue_.clear();
                    emit failed(QString::fromUtf8(failure.what()));
                    return;
                }
            }
            if (busy_)
                timer_.start(options_.requestIntervalMs);
        }, options_.maxResponseBytes, options_.transferTimeoutMs);
}
ResourceDiscovery::Page ResourceDiscovery::takeNext() {
    auto selected = queue_.begin();
    if (fetched_ > 0) {
        const auto score = [&](const Page &entry) {
            // Finish a discovered deadline-related guide before expanding more
            // administrative menus. Host accounting still yields to other hosts
            // after a bounded sequence; the global request cap is unchanged.
            const bool urgentBody = HtmlAdapter::isArticleUrl(entry.url) &&
                QRegularExpression("重修|补考|缴费").match(entry.label).hasMatch();
            const bool actionSection = QRegularExpression(
                "选课|退课|考试(?:和|与|及)成绩|成绩.*证明").match(entry.label).hasMatch();
            const bool guide = ResourceClassifier::isStudentServiceNavigation(entry.label) ||
                QRegularExpression("重修|补考|选课|退课|成绩.*证明").match(entry.label).hasMatch();
            const int purpose = urgentBody ? 8 : actionSection ? 6 : guide ? 4 :
                entry.label.contains("图书馆") || entry.label.contains("机构") ? 3 :
                ResourceClassifier::isPractical(entry.label, entry.url) ? 2 : 1;
            return purpose * 10 - hostRequests_.value(entry.url.host()) * 10 - entry.depth;
        };
        for (auto entry = queue_.begin(); entry != queue_.end(); ++entry) {
            if (entry->redirects > 0) {
                selected = entry;
                break;
            }
            if (score(*entry) > score(*selected))
                selected = entry;
        }
    }
    const auto page = *selected;
    queue_.erase(selected);
    return page;
}
void ResourceDiscovery::consume(const Page &page, const QByteArray &bytes, const QString &failure) {
    const auto checked = now();
    auto error = failure;
    if (bytes.size() > options_.maxResponseBytes)
        error = "HTML响应超过大小上限";
    const auto targetUrl = canonical(page.origin.isEmpty() ? page.url : page.origin);
    QJsonObject evidence{{"requested_url", canonical(page.url)},
                         {"resource_url", targetUrl},
                         {"discovered_from", canonical(page.discoveredFrom)},
                         {"checked_at", checked},
                         {"error", error},
                         {"bytes", bytes.size()}};
    const auto name = hash(canonical(page.url));
    if (!QDir().mkpath(evidenceDirectory_))
        throw std::runtime_error("无法保存资源采集证据");
    if (error.isEmpty()) {
        writeArtifact(evidenceDirectory_ + "/" + name + ".html", bytes);
        evidence["sha256"] = QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        evidence["file"] = name + ".html";
    }
    writeArtifact(evidenceDirectory_ + "/" + name + ".json", QJsonDocument(evidence).toJson());
    auto candidate = resources_.value(targetUrl);
    if (candidate.id.empty() && ResourceClassifier::isPractical(page.label, QUrl(targetUrl)))
        candidate = ResourceClassifier::describe(school_.id, page.label, QUrl(targetUrl),
                                                 page.discoveredFrom);
    if (!error.isEmpty()) {
        ++failures_;
        if (!candidate.id.empty()) {
            candidate.status = "unreachable";
            candidate.error = error.toStdString();
            candidate.lastCheckedAt = checked.toStdString();
            saveResource(candidate);
        }
        emit progress("官网资源访问失败：" + page.url.host() + " · " + error);
        return;
    }
    const auto title = parser_.pageTitle(bytes);
    if (!candidate.id.empty() && page.label.isEmpty() && !title.isEmpty())
        candidate =
            ResourceClassifier::describe(school_.id, title, QUrl(targetUrl),
                                         QUrl(QString::fromStdString(candidate.discoveredFrom)));
    if (candidate.id.empty() && ResourceClassifier::isPractical(title, page.url))
        candidate =
            ResourceClassifier::describe(school_.id, title, QUrl(targetUrl), page.discoveredFrom);
    if (ResourceClassifier::isLoginPage(bytes, title, page.url)) {
        if (candidate.id.empty())
            candidate =
                ResourceClassifier::describe(school_.id, page.label.isEmpty() ? title : page.label,
                                             QUrl(targetUrl), page.discoveredFrom);
        candidate.status = "login_required";
        candidate.lastCheckedAt = checked.toStdString();
        candidate.error = "此入口需要登录；仅提供官方浏览器入口，不读取登录后内容";
        saveResource(candidate);
        return;
    }
    const auto blocked = unsafeTargets(bytes, page.url);
    auto links = parser_.links(bytes, page.url);
    bool useful = false;
    int accepted = 0;
    for (auto &link : links) {
        link.title = ResourceClassifier::labelInContext(
            link.title, QString::fromStdString(candidate.category));
        if (link.url.scheme() == "http" &&
            (link.url.host().toLower() == officialRoot_ ||
             link.url.host().toLower().endsWith("." + officialRoot_)))
            link.url.setScheme("https");
        link.url = ResourceClassifier::canonicalUrl(link.url);
        if (blocked.contains(canonical(link.url)) || !ResourceClassifier::isSafeHttps(link.url))
            continue;
        const bool official = ResourceClassifier::isOfficial(link.url, officialRoot_);
        const bool practical = ResourceClassifier::isPractical(link.title, link.url);
        if (practical) {
            useful = true;
            auto resource =
                ResourceClassifier::describe(school_.id, link.title, link.url, page.url);
            if (!official)
                resource.linkKind = "official_recommended";
            if (ResourceClassifier::isDownload(link.url))
                resource.error = "官网文件链接仅供浏览器打开，本轮未下载或验证文件内容";
            saveResource(resource);
            if (++accepted >= 512) {
                emit progress("本页资源链接达到512条上限，保留后续待发现。");
                break;
            }
        }
        if (official && (practical || navigation(link.title)))
            enqueue({link.url, link.url, page.url, link.title, page.depth + 1, 0});
    }
    if (!candidate.id.empty()) {
        const auto text = ResourceClassifier::staticText(bytes);
        auto reason = ResourceClassifier::unverifiedReason(bytes, text, useful);
        QString description = text;
        if (reason.isEmpty() && HtmlAdapter::isArticleUrl(page.url)) {
            // A guide article is useful only after its actual body is read;
            // menu/footer text cannot establish the guide's content.
            try {
                SourceConfig source;
                source.entry = page.url;
                source.allowedHosts = {page.url.host()};
                source.autoDetect = true;
                Notice article;
                article.url = page.url.toString(QUrl::FullyEncoded).toStdString();
                const auto detail = parser_.parseDetail(bytes, source, std::move(article));
                description = QString::fromStdString(detail.body);
                if (description.size() < 30)
                    reason = "公开指南正文过短，尚未核实";
            } catch (const std::exception &failure) {
                reason = "公开指南正文尚未核实：" + QString::fromUtf8(failure.what());
            }
        }
        if (title.isEmpty() && page.label.isEmpty())
            reason = "资源入口缺少可识别的真实页面标题，默认入口名称不作为可用证据";
        candidate.status = reason.isEmpty() ? "verified" : "discovered";
        candidate.lastCheckedAt = checked.toStdString();
        candidate.error = reason.toStdString();
        if (reason.isEmpty()) {
            candidate.description = description.left(500).toStdString();
            verifiedUrls_.insert(targetUrl);
        }
        saveResource(candidate);
    }
}
} // namespace campus
