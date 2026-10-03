#include "adapters/SchoolPackage.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QTimeZone>
#include <QRegularExpression>
#include <stdexcept>

namespace campus {
namespace {
QString required(const QJsonObject &obj, const char *key) {
    const auto value = obj.value(QLatin1String(key));
    if (!value.isString() || value.toString().trimmed().isEmpty())
        throw std::runtime_error(QString("配置字段缺失或无效：%1").arg(key).toStdString());
    return value.toString();
}

bool isOfficialLoginUrl(const QString &value, const QString &officialRoot,
                        const QStringList &allowedHosts) {
    // QUrl can normalize away empty user information or an empty port. Check
    // the original authority as well, before producing any browser link.
    static const QRegularExpression authority("^https://([^/?#]+)",
                                              QRegularExpression::CaseInsensitiveOption);
    const auto raw = authority.match(value);
    if (!raw.hasMatch() || raw.captured(1).contains('@') || raw.captured(1).contains(':'))
        return false;
    const QUrl url(value, QUrl::StrictMode);
    const auto host = url.host().toLower();
    return !officialRoot.isEmpty() && !host.isEmpty() && url.isValid() && url.scheme() == "https" &&
           url.userInfo().isEmpty() && !url.authority(QUrl::FullyEncoded).contains('@') &&
           url.port() == -1 && !url.authority(QUrl::FullyEncoded).contains(':') &&
           !value.contains(QRegularExpression("[\\s\\\\]")) &&
           (host == officialRoot || host.endsWith("." + officialRoot)) &&
           allowedHosts.contains(host);
}

void loadAccess(const QJsonObject &item, const QString &officialRoot,
                SourceDescription &description) {
    QStringList allowedHosts;
    for (const auto host : item.value("allowed_hosts").toArray())
        allowedHosts << host.toString().toLower();
    if (item.contains("access")) {
        if (!item.value("access").isObject())
            throw std::runtime_error("来源access必须为对象");
        const auto access = item.value("access").toObject();
        for (const auto &field : access.keys())
            if (field != "mode" && field != "login_url")
                throw std::runtime_error("来源access包含不支持的字段");
        const auto mode = required(access, "mode");
        if (mode != "public" && mode != "login_required")
            throw std::runtime_error("来源access.mode必须是public或login_required");
        QString loginUrl;
        if (access.contains("login_url") || mode == "login_required") {
            loginUrl = required(access, "login_url");
            if (!isOfficialLoginUrl(loginUrl, officialRoot, allowedHosts))
                throw std::runtime_error(
                    "登录入口必须为可信高校主机的HTTPS网址，且不能带认证信息或端口");
        }
        description.requiresLogin = mode == "login_required";
        if (description.requiresLogin)
            description.loginUrl =
                QUrl(loginUrl, QUrl::StrictMode).toString(QUrl::FullyEncoded).toStdString();
        return;
    }
    // Older generated packages only recorded this fact in their pending text.
    for (const auto reason : item.value("pending").toArray()) {
        const auto text = reason.toString();
        if (text.contains("需要登录") && !text.contains("不需要登录") &&
            !text.contains("可能需要登录") && !text.contains("疑似需要登录")) {
            description.requiresLogin = true;
            break;
        }
    }
    if (!description.requiresLogin)
        return;
    for (const auto *field : {"entry_url", "discovery_url"}) {
        const auto value = item.value(QLatin1String(field)).toString();
        if (isOfficialLoginUrl(value, officialRoot, allowedHosts)) {
            description.loginUrl =
                QUrl(value, QUrl::StrictMode).toString(QUrl::FullyEncoded).toStdString();
            break;
        }
    }
}
} // namespace
bool isAllowedUrl(const QUrl &url, const SourceConfig &source) {
    return url.isValid() && (url.scheme() == "https" || url.scheme() == "http") &&
           url.userName().isEmpty() && url.password().isEmpty() &&
           source.allowedHosts.contains(url.host().toLower());
}
SchoolPackage SchoolPackage::load(const QString &filename) {
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("无法读取学校配置");
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        throw std::runtime_error("学校配置JSON无效");
    const auto obj = doc.object();
    if (obj.value("schema_version").toString() != "0.1-draft")
        throw std::runtime_error("不支持的学校配置版本");
    const auto school = obj.value("school").toObject();
    SchoolPackage result{required(school, "key"), required(school, "name"), {}, {}};
    result.configFile = filename;
    result.timeZone = required(school, "timezone");
    if (!QTimeZone(result.timeZone.toUtf8()).isValid())
        throw std::runtime_error("学校时区无效");
    result.officialHomepage = QUrl(school.value("official_homepage").toString(), QUrl::StrictMode);
    auto officialRoot = QUrl(school.value("official_homepage").toString()).host().toLower();
    if (officialRoot.startsWith("www."))
        officialRoot.remove(0, 4);
    if (obj.contains("resource_discovery")) {
        if (!obj.value("resource_discovery").isObject())
            throw std::runtime_error("resource_discovery必须为对象");
        const auto resources = obj.value("resource_discovery").toObject();
        for (const auto &field : resources.keys())
            if (field != "department_urls" && field != "max_pages")
                throw std::runtime_error("资源发现包含不支持的字段");
        if (!resources.value("department_urls").isArray() ||
            resources.value("department_urls").toArray().size() > 12)
            throw std::runtime_error("资源部门入口必须为数组，最多12个");
        QSet<QString> entries;
        for (const auto entry : resources.value("department_urls").toArray()) {
            const auto value = entry.toString();
            const auto host = QUrl(value, QUrl::StrictMode).host().toLower();
            if (!entry.isString() || !isOfficialLoginUrl(value, officialRoot, {host}) ||
                entries.contains(value))
                throw std::runtime_error("资源入口必须为不重复的高校官方HTTPS地址");
            entries.insert(value);
            result.resourceDiscoveryEntries << value;
        }
        if (resources.contains("max_pages")) {
            const auto limit = resources.value("max_pages");
            if (!limit.isDouble() || limit.toDouble() != limit.toInt(-1))
                throw std::runtime_error("资源扫描页数必须为整数");
            result.resourceDiscoveryLimit = limit.toInt();
        }
        if (result.resourceDiscoveryLimit < 1 || result.resourceDiscoveryLimit > 32)
            throw std::runtime_error("资源扫描上限必须在1至32之间");
    }
    const auto discovery = obj.value("auto_discovery").toObject();
    if (!discovery.isEmpty()) {
        result.discoveryEntries << required(school, "official_homepage");
        for (const auto entry : discovery.value("department_urls").toArray())
            result.discoveryEntries << entry.toString();
        result.discoveryLimit = discovery.value("max_pages").toInt(16);
        if (result.discoveryLimit < 1 || result.discoveryLimit > 24)
            throw std::runtime_error("自动接入扫描上限必须在1至24之间");
    }
    QSet<QString> keys;
    for (const auto value : obj.value("sources").toArray()) {
        if (!value.isObject())
            throw std::runtime_error("来源配置必须是对象");
        const auto item = value.toObject();
        const auto key = required(item, "key");
        if (keys.contains(key))
            throw std::runtime_error("重复的来源标识");
        keys.insert(key);
        if (!item.value("enabled").isBool())
            throw std::runtime_error("enabled必须为布尔值");
        SourceDescription description;
        description.schoolId = result.id.toStdString();
        description.id = key.toStdString();
        description.name = required(item, "name").toStdString();
        description.configuredEnabled = item.value("enabled").toBool();
        description.entryUrl = item.value("entry_url").toString().toStdString();
        description.discoveryUrl = item.value("discovery_url").toString().toStdString();
        for (const auto category : item.value("category_hints").toArray())
            description.categories.push_back(category.toString().toStdString());
        QStringList pending;
        for (const auto reason : item.value("pending").toArray())
            pending << reason.toString();
        description.pendingReason = pending.join("；").toStdString();
        loadAccess(item, officialRoot, description);
        if (!description.configuredEnabled || description.requiresLogin) {
            result.catalog.push_back(std::move(description));
            continue;
        }
        if (required(item, "adapter") != "html_list_detail")
            throw std::runtime_error("此版本只支持html_list_detail适配器");
        SourceConfig source;
        source.schoolId = result.id;
        source.id = key;
        source.name = required(item, "name");
        source.entry = QUrl(required(item, "entry_url"));
        for (const auto host : item.value("allowed_hosts").toArray())
            source.allowedHosts << host.toString().toLower();
        if (!isAllowedUrl(source.entry, source))
            throw std::runtime_error("来源入口不在允许域名内");
        const auto extraction = item.value("extraction").toObject();
        source.autoDetect = extraction.value("query_language").toString() == "auto";
        source.allowUnknownDates = extraction.value("allow_unknown_dates").toBool(false);
        if (!source.autoDetect && required(extraction, "query_language") != "css")
            throw std::runtime_error("此适配器需要CSS选择器配置");
        if (!source.autoDetect) {
            source.itemSelector = required(extraction, "item_selector");
            source.titleSelector = required(extraction, "title_selector");
            source.dateSelector = required(extraction, "date_selector");
            source.bodySelector = required(extraction, "body_selector");
            source.attachmentSelector = required(extraction, "attachment_selector");
        }
        const auto pagination = extraction.value("pagination").toObject();
        if (!pagination.isEmpty()) {
            source.nextPageSelector = required(pagination, "next_selector");
            source.maxPages = pagination.value("max_pages").toInt(0);
            if (source.maxPages < 1 || source.maxPages > 10)
                throw std::runtime_error("分页上限必须在1至10之间");
        }
        result.sources.push_back(std::move(source));
        description.ready = true;
        description.maxPages = result.sources.back().maxPages;
        result.catalog.push_back(std::move(description));
    }
    if (result.catalog.empty() && result.discoveryEntries.empty())
        throw std::runtime_error("学校配置没有来源目录");
    return result;
}
} // namespace campus
