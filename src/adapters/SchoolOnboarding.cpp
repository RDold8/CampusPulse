#include "adapters/SchoolOnboarding.h"
#include "adapters/ArtifactWriter.h"
#include "adapters/PublicUniversityNetwork.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <QDebug>
#include <stdexcept>

namespace campus {
namespace {
QString key(const QUrl &url) {
    return "auto-" + QCryptographicHash::hash(url.toString(QUrl::FullyEncoded).toUtf8(),
                                              QCryptographicHash::Sha256)
                         .toHex()
                         .left(12);
}
bool isLoginPage(const QByteArray &bytes, const QString &title, const QUrl &url) {
    static const QRegularExpression passwordField(
        "<input\\b[^>]*\\btype\\s*=\\s*(?:\"password\"|'password'|password(?:\\s|>))",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression loginPath("(?:^|/)login(?:[/.]|$)",
                                              QRegularExpression::CaseInsensitiveOption);
    const bool identityTitle = (title.contains("统一身份") || title.contains("身份认证")) &&
                               !title.contains("通知") && !title.contains("指南");
    const bool loginForm = passwordField.match(QString::fromUtf8(bytes)).hasMatch();
    return identityTitle ||
           (loginForm && (loginPath.match(url.path()).hasMatch() || title.contains("用户登录") ||
                          title.contains("账号登录")));
}
} // namespace
SchoolOnboarding::SchoolOnboarding(const QString &seedFile, const QString &outputDirectory,
                                   QObject *parent, int intervalMs, QStringList supplementalEntries)
    : QObject(parent), school_(SchoolPackage::load(seedFile)),
      directory_(QDir(outputDirectory).filePath(school_.id)),
      supplementalEntries_(std::move(supplementalEntries)), interval_(intervalMs) {
    QFile file(seedFile);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("无法读取高校身份包");
    seed_ = QJsonDocument::fromJson(file.readAll()).object();
    if (!supplementalEntries_.empty())
        sources_ = seed_.value("sources").toArray();
    root_ = QUrl(seed_.value("school").toObject().value("official_homepage").toString())
                .host()
                .toLower();
    if (root_.startsWith("www."))
        root_.remove(0, 4);
    if (school_.discoveryEntries.empty() || root_.isEmpty() || interval_ < 0)
        throw std::runtime_error("高校身份包未启用自动接入");
}
bool SchoolOnboarding::withinUniversity(const QUrl &url, const QString &root) {
    return PublicUniversityNetwork::withinUniversity(url, root);
}
bool SchoolOnboarding::isDiscoveryLabel(const QString &label) {
    static const QRegularExpression interest(
        "通知公告|教务通知|教学通知|考试通知|考试安排|补考安排|重修通知|选课通知|学生工作|共青团|"
        "本科教育|本科生教育|本科生院|本科教学|教学管理|教务处|教务在线|人才培养|学生处|团委|研究生教育|"
        "招生就业|就业服务|学生就业|招聘快讯|招聘简章|校园招聘|奖贷学金|"
        "奖助|学生资助|财务|缴费|收费|竞赛|校园活动|学生活动|研究生培养|研究生工作|机构设置|"
        "组织机构|部门导航|管理、服务与业务机构|管理与服务机构");
    return label.size() <= 35 && !label.contains("关于") && interest.match(label).hasMatch();
}
void SchoolOnboarding::enqueue(QUrl url, QString label, int depth, bool candidate) {
    // Official homepages often link their own departmental pages using HTTP.
    if (url.scheme() == "http")
        url.setScheme("https");
    url.setFragment({});
    // Article links are only fetched as explicit body checks, never as navigation pages.
    if (url.path().contains("/info/") || url.path().endsWith("/details") ||
        url.path().endsWith("/content.jsp") ||
        QRegularExpression("/[0-9a-fA-F]{24,64}\\.htm[l]?$").match(url.path()).hasMatch() ||
        QRegularExpression("/\\d+\\.jhtml$").match(url.path()).hasMatch())
        return;
    if (!withinUniversity(url, root_) || depth > 3 || queue_.size() >= 48)
        return;
    const auto canonical = url.toString(QUrl::FullyEncoded);
    if (queued_.contains(canonical))
        return;
    queued_.insert(canonical);
    Page page{url, label, depth, candidate};
    page.origin = url;
    if (label.contains("教务") || label.contains("本科生院") || label.contains("本科教学") ||
        label.contains("本科生教育") || label.contains("教学管理") ||
        (candidate && (label.contains("考试") || label.contains("补考") || label.contains("重修"))))
        queue_.push_front(page);
    else
        queue_.push_back(page);
}
void SchoolOnboarding::start() {
    if (started_)
        return;
    started_ = true;
    if (!QDir().mkpath(directory_ + "/samples")) {
        emit failed("无法创建自动接入目录");
        return;
    }
    if (supplementalEntries_.empty()) {
        for (const auto &entry : school_.discoveryEntries)
            enqueue(QUrl(entry), "官方入口", 0, true);
    } else {
        for (const auto &source : sources_) {
            QUrl existing(source.toObject().value("entry_url").toString());
            if (existing.scheme() == "http")
                existing.setScheme("https");
            queued_.insert(existing.toString(QUrl::FullyEncoded));
        }
        for (const auto &entry : supplementalEntries_)
            enqueue(QUrl(entry), "AI候选栏目", 0, true);
    }
    emit progress("学校官网入口已加载，后台发现公开栏目；无需模型调用。");
    next();
}
void SchoolOnboarding::next() {
    if (queue_.empty() || fetched_ >= school_.discoveryLimit) {
        finish();
        return;
    }
    const auto page = queue_.front();
    queue_.pop_front();
    ++fetched_;
    emit progress(QString("后台接入：第%1/%2页 · %3 · 已验证%4个来源")
                      .arg(fetched_)
                      .arg(school_.discoveryLimit)
                      .arg(page.url.host())
                      .arg(ready_));
    PublicUniversityNetwork::get(page.url, root_, this, [this, page](UniversityPageResponse result) {
        const auto redirected = result.redirect;
        if (!redirected.isEmpty()) {
            if (withinUniversity(redirected, root_) && page.redirects < 3) {
                auto nextPage = page;
                nextPage.url = redirected;
                ++nextPage.redirects;
                queue_.push_front(nextPage);
            } else
                consume(page, {}, "重定向超出高校官方域或次数上限");
        } else if (!result.error.isEmpty())
            consume(page, {}, result.error);
        else
            consume(page, result.bytes, {});
        QTimer::singleShot(interval_, this, &SchoolOnboarding::next);
    }, 5 * 1024 * 1024, 12000);
}
void SchoolOnboarding::consume(const Page &page, const QByteArray &bytes, const QString &error) {
    QJsonObject failure{{"url", page.url.toString()},
                        {"error", error},
                        {"discovered_from", page.origin.toString()}};
    const auto addLoginAccess = [&](QJsonObject &value, const QUrl &original) {
        QUrl loginUrl(value.value("entry_url").toString());
        if (!withinUniversity(loginUrl, root_))
            loginUrl = original;
        if (!withinUniversity(loginUrl, root_))
            throw std::runtime_error("登录入口不在高校官方HTTPS域内");
        value["access"] = QJsonObject{{"mode", "login_required"},
                                      {"login_url", loginUrl.toString(QUrl::FullyEncoded)}};
        value["enabled"] = false;
    };
    const auto markFailure = [&](const QString &reason, bool requiresLogin = false) {
        failure["error"] = reason;
        failures_.append(failure);
        if (page.candidate && page.sourceKey.isEmpty()) {
            const auto original = page.origin.isEmpty() ? page.url : page.origin;
            const auto id = key(original);
            bool exists = false;
            for (int i = 0; i < sources_.size(); ++i) {
                auto value = sources_[i].toObject();
                if (value.value("key").toString() != id)
                    continue;
                exists = true;
                if (requiresLogin) {
                    addLoginAccess(value, original);
                    value["pending"] = QJsonArray{reason};
                    sources_[i] = value;
                }
            }
            if (!exists) {
                QJsonObject value{{"key", id},
                                  {"name", page.label + " · " + original.host()},
                                  {"discovery_url", original.toString()},
                                  {"entry_url", original.toString()},
                                  {"allowed_hosts", QJsonArray{original.host()}},
                                  {"enabled", false},
                                  {"adapter", "pending"},
                                  {"extraction", QJsonValue::Null},
                                  {"category_hints", QJsonArray{}},
                                  {"validation_state", "discovered"},
                                  {"pending", QJsonArray{reason}}};
                if (requiresLogin)
                    addLoginAccess(value, original);
                sources_.append(value);
            }
        }
        if (!page.sourceKey.isEmpty())
            for (int i = 0; i < sources_.size(); ++i) {
                auto value = sources_[i].toObject();
                if (value.value("key").toString() == page.sourceKey) {
                    value["pending"] = QJsonArray{"正文校验失败：" + reason};
                    if (requiresLogin)
                        addLoginAccess(value, page.origin.isEmpty() ? page.url : page.origin);
                    sources_[i] = value;
                }
            }
    };
    if (!error.isEmpty()) {
        markFailure(error);
        return;
    }
    try {
        if (bytes.size() > 5 * 1024 * 1024)
            throw std::runtime_error("页面超过5MB");
        const auto id = key(page.url);
        const auto samplePath = "samples/" + id + ".html";
        writeArtifact(directory_ + "/" + samplePath, bytes);
        samples_.append(QJsonObject{
            {"url", page.url.toString()},
            {"file", samplePath},
            {"sha256", QString::fromLatin1(
                           QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
            {"captured_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {"bytes", bytes.size()}});
        const auto pageTitle = parser_.pageTitle(bytes);
        if ((page.candidate || page.detail) && isLoginPage(bytes, pageTitle, page.url)) {
            markFailure("此入口需要登录，未采集受限内容；可补充公开通知栏目。", true);
            return;
        }
        if (page.detail) {
            SourceConfig source;
            source.schoolId = school_.id;
            source.id = page.sourceKey;
            source.entry = page.url;
            source.autoDetect = true;
            source.allowedHosts = {page.url.host()};
            const auto detail = parser_.parseDetail(bytes, source, *page.detail);
            if (detail.body.size() < 30)
                throw std::runtime_error("正文过短，未通过自动校验");
            for (int i = 0; i < sources_.size(); ++i) {
                auto value = sources_[i].toObject();
                if (value.value("key").toString() != page.sourceKey)
                    continue;
                value["enabled"] = true;
                value["validation_state"] = "adapter_verified";
                value["pending"] =
                    QJsonArray{"已自动验证列表和一条正文；范围为当前列表页，尚不含历史全量"};
                sources_[i] = value;
                ++ready_;
                rows_ += counts_.value(page.sourceKey);
                emit progress("列表与正文校验通过：" + value.value("name").toString());
            }
            return;
        }
        if (page.candidate) {
            SourceConfig source;
            source.schoolId = school_.id;
            source.id = id;
            source.entry = page.url;
            source.name = parser_.pageTitle(bytes);
            source.allowedHosts = {page.url.host()};
            source.autoDetect = true;
            source.allowUnknownDates = true;
            QJsonObject value{
                {"key", id},
                {"name", source.name.isEmpty() ? page.label : source.name},
                {"discovery_url", page.url.toString()},
                {"entry_url", page.url.toString()},
                {"allowed_hosts", QJsonArray{page.url.host()}},
                {"adapter", "html_list_detail"},
                {"category_hints", QJsonArray{}},
                {"pending",
                 QJsonArray{"自动识别只验证当前列表页，正文首次查看时校验；尚不含历史全量"}}};
            try {
                const auto notices = parser_.parseList(bytes, source);
                counts_[id] = static_cast<int>(notices.size());
                value["enabled"] = false;
                value["validation_state"] = "detail_discovered";
                value["extraction"] =
                    QJsonObject{{"query_language", "auto"}, {"allow_unknown_dates", true}};
                value["pending"] = QJsonArray{"等待首条正文校验"};
                Page detailPage{QUrl(QString::fromStdString(notices.front().url)), "正文校验",
                                page.depth, false};
                detailPage.detail = notices.front();
                detailPage.sourceKey = id;
                queue_.push_front(detailPage);
                emit progress(QString("已识别 %1：%2条通知；日期不明保留待核实")
                                  .arg(source.name)
                                  .arg(notices.size()));
            } catch (const std::exception &e) {
                value["enabled"] = false;
                value["validation_state"] = "list_readable";
                value["adapter"] = "pending";
                value["extraction"] = QJsonValue::Null;
                value["pending"] = QJsonArray{QString::fromUtf8(e.what())};
                failure["error"] = QString::fromUtf8(e.what());
                failures_.append(failure);
            }
            sources_.append(value);
        }
        for (const auto &link : parser_.links(bytes, page.url)) {
            if (school_.discoveryEntries.size() > 1 &&
                (link.title.contains("机构设置") || link.title.contains("部门导航") ||
                 link.title.contains("管理、服务与业务机构")))
                continue;
            static const QRegularExpression listPath(
                "(?:/tzgg(?:[/.]|$)|/notices?(?:[/.]|$)|/announcements?(?:[/.]|$)|/recruit/list$)");
            const bool knownListPath = listPath.match(link.url.path()).hasMatch();
            if (!isDiscoveryLabel(link.title) && !knownListPath)
                continue;
            const bool list = link.title.contains("通知") || link.title.contains("考试安排") ||
                              link.title.contains("补考安排") || link.title.contains("招聘") ||
                              link.title.contains("奖助") || link.title.contains("资助") ||
                              knownListPath;
            enqueue(link.url, link.title, page.depth + 1, list);
        }
    } catch (const std::exception &e) {
        markFailure(QString::fromUtf8(e.what()));
    }
}
void SchoolOnboarding::finish() {
    try {
        const QJsonObject report{{"school_id", school_.id}, {"ready_sources", ready_},
                                 {"list_rows", rows_},      {"fetched_pages", fetched_},
                                 {"model_calls", 0},        {"samples", samples_},
                                 {"failures", failures_},   {"passed", ready_ > 0}};
        writeArtifact(directory_ + "/report.json", QJsonDocument(report).toJson());
        if (ready_ == 0) {
            emit failed("已保存扫描报告，但没有通过校验的列表；保留当前学校，需其他适配器。" +
                        directory_ + "/report.json");
            return;
        }
        seed_.remove("auto_discovery");
        seed_["onboarding_version"] = AlgorithmVersion;
        seed_["sources"] = sources_;
        if (seed_.value("school").toObject().value("identity_provenance").toString() !=
            "automatic_homepage")
            seed_["reviewed_at"] = QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd");
        const auto config = directory_ + "/school.json";
        writeArtifact(config, QJsonDocument(seed_).toJson());
        SchoolPackage::load(config);
        emit finished(config, ready_, rows_);
    } catch (const std::exception &e) {
        emit failed(QString::fromUtf8(e.what()));
    }
}
} // namespace campus
