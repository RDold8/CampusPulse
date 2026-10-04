#include "adapters/UnknownUniversityDiscovery.h"
#include "adapters/ArtifactWriter.h"
#include "adapters/HtmlAdapter.h"
#include "adapters/PublicUniversityNetwork.h"
#include "adapters/SchoolPackage.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
#include <stdexcept>

namespace campus {
namespace {
std::runtime_error invalidHomepage() {
    return std::runtime_error("陌生大学请输入 [www.]学校域名.edu.cn 的官网首页；"
                              "不接受普通网站、IP、账号、端口、路径或查询参数。"
                              "非edu.cn高校可通过经核验的社区学校包接入。");
}
} // namespace

UnknownUniversityDiscovery::UnknownUniversityDiscovery(QString homepageInput,
                                                       QString seedDirectory, QObject *parent)
    : QObject(parent), input_(std::move(homepageInput)), directory_(std::move(seedDirectory)) {}

QUrl UnknownUniversityDiscovery::normalizedHomepage(const QString &input) {
    auto value = input.trimmed();
    if (value.isEmpty() || value.size() > 2048 || value.contains(QRegularExpression("[\\s%@\\\\]")) ||
        std::any_of(value.begin(), value.end(), [](QChar c) {
            return c.unicode() < 33 || c.unicode() > 126;
        }))
        throw invalidHomepage();
    if (!value.contains("://"))
        value.prepend("https://");
    const QRegularExpression rawAuthority("^[a-zA-Z][a-zA-Z0-9+.-]*://([^/?#]*)");
    const auto raw = rawAuthority.match(value);
    if (!raw.hasMatch() || raw.captured(1).contains('@') || raw.captured(1).contains(':'))
        throw invalidHomepage();
    const QUrl parsed(value, QUrl::StrictMode);
    static const QRegularExpression hostPattern(
        "^(?:www\\.)?[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\\.edu\\.cn$");
    const auto host = parsed.host().toLower();
    const auto authority = parsed.authority(QUrl::FullyEncoded);
    if (!parsed.isValid() || (parsed.scheme() != "http" && parsed.scheme() != "https") ||
        !hostPattern.match(host).hasMatch() || !parsed.userInfo().isEmpty() ||
        parsed.port() != -1 || authority.contains('@') || authority.contains(':') ||
        parsed.hasQuery() || parsed.hasFragment() ||
        (!parsed.path().isEmpty() && parsed.path() != "/"))
        throw invalidHomepage();
    QUrl normalized;
    normalized.setScheme("https"); // Do not send a plaintext discovery request.
    normalized.setHost(host);
    normalized.setPath("/");
    return normalized;
}

QString UnknownUniversityDiscovery::officialRoot(const QUrl &homepage) {
    auto root = homepage.host().toLower();
    if (root.startsWith("www."))
        root.remove(0, 4);
    return root;
}

QString UnknownUniversityDiscovery::identifySchool(const QByteArray &html) {
    QStringList candidates{HtmlAdapter{}.pageTitle(html)};
    // Only explicit page-identity metadata is read; body prose, scripts, and
    // instructions in fetched HTML never determine execution or configuration.
    const auto source = QString::fromUtf8(html);
    const QRegularExpression meta("<meta\\b[^>]{0,2048}>", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression siteName(
        "(?:property|name)\\s*=\\s*['\"](?:og:site_name|application-name)['\"]",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression content("\\bcontent\\s*=\\s*(['\"])(.*?)\\1",
                                     QRegularExpression::CaseInsensitiveOption);
    auto iterator = meta.globalMatch(source);
    while (iterator.hasNext() && candidates.size() < 8) {
        const auto tag = iterator.next().captured();
        if (siteName.match(tag).hasMatch()) {
            const auto raw = content.match(tag).captured(2);
            candidates << HtmlAdapter{}.pageTitle(("<title>" + raw + "</title>").toUtf8());
        }
    }
    const QRegularExpression schoolName("^[\\p{Han}]{2,32}(?:大学|学院|高等专科学校)(?:[（(][\\p{Han}]{2,12}[）)])?$");
    const QRegularExpression split("[|｜_—–-]");
    const QRegularExpression suffix("(?:官方网站|官方主页|官方首页|官网|首页|门户网站)$");
    const QRegularExpression prefix("^(?:欢迎访问|欢迎来到|欢迎您访问)");
    const QRegularExpression englishSuffix("\\s+[A-Za-z][A-Za-z\\s&.,()'-]*$");
    for (const auto &candidate : candidates) {
        if (candidate.size() > 160)
            continue;
        for (auto part : candidate.split(split, Qt::SkipEmptyParts)) {
            part = part.simplified();
            part.remove(englishSuffix);
            part.remove(prefix);
            part.remove(suffix);
            part = part.trimmed();
            if (schoolName.match(part).hasMatch() && !part.contains("附属") &&
                !part.contains("中学") && !part.contains("小学"))
                return part;
        }
    }
    return {};
}

QJsonObject UnknownUniversityDiscovery::seedDocument(const QUrl &homepage, const QString &name) {
    const auto normalized = normalizedHomepage(homepage.toString());
    if (identifySchool(("<title>" + name + "</title>").toUtf8()) != name)
        throw std::runtime_error("无法从官网首页识别高校名称，保留当前学校。");
    const auto root = officialRoot(normalized);
    const auto slug = root.section('.', 0, 0);
    const auto digest = QString::fromLatin1(
        QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256).toHex().left(8));
    return QJsonObject{
        {"schema_version", "0.1-draft"},
        {"school", QJsonObject{{"key", "cn-auto-" + slug + "-" + digest},
                                {"name", name}, {"aliases", QJsonArray{}}, {"country", "CN"},
                                {"province", ""}, {"city", ""}, {"timezone", "Asia/Shanghai"},
                                {"official_homepage", normalized.toString()},
                                {"identity_provenance", "automatic_homepage"}, {"status", "draft"}}},
        {"status", "draft"}, {"reviewed_at", ""},
        {"categories", QJsonArray{"exam", "competition", "scholarship", "campus_activity",
                                    "academic_affairs", "career"}},
        {"fetch_defaults", QJsonObject{{"interval_minutes", 60}, {"jitter_seconds", 300},
                                        {"per_host_concurrency", 1},
                                        {"min_request_interval_seconds", 3},
                                        {"respect_source_policy", true},
                                        {"conditional_requests", true}}},
        {"sources", QJsonArray{}},
        {"auto_discovery", QJsonObject{{"department_urls", QJsonArray{}}, {"max_pages", 48}}},
        {"resource_discovery", QJsonObject{{"department_urls", QJsonArray{normalized.toString()}},
                                            {"max_pages", 32}}}};
}

void UnknownUniversityDiscovery::start() {
    if (started_)
        return;
    started_ = true;
    try {
        requested_ = normalizedHomepage(input_);
        root_ = officialRoot(requested_);
        if (directory_.isEmpty())
            throw std::runtime_error("自动识别学校的本地目录未配置。");
        emit progress("未安装该校配置，正在校验教育域官网与公网地址；无需AI。自动身份尚未人工核验。");
        fetch(requested_, 0);
    } catch (const std::exception &error) {
        emit failed(QString::fromUtf8(error.what()));
    }
}

void UnknownUniversityDiscovery::fetch(const QUrl &url, int redirects) {
    PublicUniversityNetwork::get(url, root_, this, [this, url, redirects](UniversityPageResponse result) {
        if (!result.error.isEmpty()) {
            emit failed("陌生学校官网校验失败：" + result.error);
            return;
        }
        if (!result.redirect.isEmpty()) {
            if (redirects >= 3 || !PublicUniversityNetwork::withinUniversity(result.redirect, root_)) {
                emit failed("官网重定向离开该校HTTPS域或超过3次，已停止接入。");
                return;
            }
            fetch(result.redirect, redirects + 1);
            return;
        }
        try {
            const auto name = identifySchool(result.bytes);
            if (name.isEmpty())
                throw std::runtime_error("该页面未识别到高校名称。请检查学校官网首页；教育域名也可能属于中小学或其他教育机构。");
            const auto seed = seedDocument(requested_, name);
            if (!QDir().mkpath(directory_))
                throw std::runtime_error("无法创建自动识别学校目录。");
            const auto filename = QDir(directory_).filePath(
                seed.value("school").toObject().value("key").toString() + ".json");
            writeArtifact(filename, QJsonDocument(seed).toJson(QJsonDocument::Indented));
            SchoolPackage::load(filename);
            emit progress("已自动识别：" + name + "。身份为草案，继续发现公开通知与资源；未进行人工核验。");
            emit finished(filename, name);
        } catch (const std::exception &error) {
            emit failed(QString::fromUtf8(error.what()));
        }
    }, 2 * 1024 * 1024, 12000, [this](const QString &message) { emit progress(message); });
}
} // namespace campus
