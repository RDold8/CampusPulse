#include "adapters/UniversityRegistry.h"
#include "adapters/SchoolPackage.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <stdexcept>

namespace campus {
namespace {

std::runtime_error invalidHomepage() {
    return std::runtime_error("请输入高校官网首页网址或域名；仅支持 HTTP/HTTPS，不支持路径、账号、"
                              "端口、查询参数、片段、IP 或本地地址。");
}

QUrl homepage(const QString &input, bool allowPlainDomain) {
    auto value = input.trimmed();
    if (value.isEmpty() || value.contains(QRegularExpression("[\\s%@\\\\]")) ||
        std::any_of(value.begin(), value.end(),
                    [](QChar ch) { return ch.unicode() < 33 || ch.unicode() > 126; }))
        throw invalidHomepage();
    if (!value.contains("://")) {
        if (!allowPlainDomain)
            throw invalidHomepage();
        value.prepend("https://");
    }
    const QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http") ||
        !url.userInfo().isEmpty() || url.port() != -1 || url.hasQuery() || url.hasFragment() ||
        (!url.path().isEmpty() && url.path() != "/"))
        throw invalidHomepage();
    const auto host = QString::fromLatin1(QUrl::toAce(url.host())).toLower();
    static const QRegularExpression hostname(
        "^(?=.{1,253}$)(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\\.)+"
        "[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$");
    QHostAddress address;
    if (!hostname.match(host).hasMatch() || address.setAddress(host) || host == "localhost" ||
        host.endsWith(".localhost") || host.endsWith(".local") || host.endsWith(".internal"))
        throw invalidHomepage();
    QUrl normalized;
    normalized.setScheme(url.scheme());
    normalized.setHost(host);
    normalized.setPath("/");
    return normalized;
}

} // namespace

UniversityRegistry::UniversityRegistry(const QString &directory) {
    const QFileInfo directoryInfo(directory);
    const QDir root(directoryInfo.canonicalFilePath());
    if (!directoryInfo.exists() || !directoryInfo.isDir() || root.path().isEmpty())
        throw std::runtime_error("高校学校包目录不存在");
    QSet<QString> ids;
    QSet<QString> hosts;
    for (const auto &file : root.entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
        try {
            const auto configFile = file.canonicalFilePath();
            const auto relativePath = root.relativeFilePath(configFile);
            if (configFile.isEmpty() || relativePath.startsWith("../") ||
                relativePath.contains('/') || relativePath.contains('\\'))
                throw std::runtime_error("学校包必须位于已安装的高校目录内");
            const auto school = SchoolPackage::load(configFile);
            QFile content(configFile);
            if (!content.open(QIODevice::ReadOnly))
                throw std::runtime_error("无法读取学校包");
            const auto schoolObject =
                QJsonDocument::fromJson(content.readAll()).object().value("school").toObject();
            const auto configuredHomepage = schoolObject.value("official_homepage");
            if (!configuredHomepage.isString())
                throw std::runtime_error("学校包缺少 official_homepage 官网首页字段");
            const auto official = homepage(configuredHomepage.toString(), false);
            if (ids.contains(school.id) || hosts.contains(official.host()))
                throw std::runtime_error("高校标识或官网域名重复，请修正社区学校包");
            ids.insert(school.id);
            hosts.insert(official.host());
            universities_.push_back({school.id, school.name, official, configFile});
        } catch (const std::exception &error) {
            throw std::runtime_error(QString("学校包 %1 无法收录：%2")
                                         .arg(file.fileName(), QString::fromUtf8(error.what()))
                                         .toStdString());
        }
    }
}

const std::vector<RegisteredUniversity> &UniversityRegistry::list() const {
    return universities_;
}

RegisteredUniversity UniversityRegistry::resolve(const QString &input) const {
    const auto requested = homepage(input, true);
    const auto found =
        std::find_if(universities_.begin(), universities_.end(), [&](const auto &university) {
            return university.homepage.host() == requested.host();
        });
    if (found == universities_.end())
        throw std::runtime_error("尚未收录核验的高校官网，请由社区添加学校包。");
    return *found;
}

} // namespace campus
