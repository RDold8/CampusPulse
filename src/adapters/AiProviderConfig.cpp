#include "adapters/AiProviderConfig.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <stdexcept>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dpapi.h>
#endif

namespace campus {
namespace {
struct SessionCredential {
    QString value;
    QString binding;
};
// Shared only inside this process, scoped by the real local profile directory.
QMap<QString, QMap<QString, SessionCredential>> sessionCredentials;
QString authName(AiAuthMode mode) {
    if (mode == AiAuthMode::BearerToken)
        return "bearer-token";
    if (mode == AiAuthMode::ApiKeyHeader)
        return "api-key-header";
    return "automatic";
}
QString credentialBinding(const AiProviderConfig &provider) {
    auto base = QUrl(provider.baseUrl);
    if (base.path().endsWith('/'))
        base.setPath(base.path().left(base.path().size() - 1));
    auto result = provider.id + '\n' + base.toString(QUrl::FullyEncoded) + '\n' +
                  (provider.protocol == AiApiProtocol::DeepSeekNative ? "native" : "compatible");
    // Preserve the original default scope so encrypted credentials from older builds still open.
    if (provider.fullUrl || provider.authMode != AiAuthMode::Automatic)
        result += '\n' + QString(provider.fullUrl ? "full-url" : "base-url") + '\n' +
                  authName(provider.authMode);
    return result;
}
[[noreturn]] void fail(const QString &message) {
    throw std::runtime_error(message.toUtf8().constData());
}
bool validId(const QString &id) {
    static const QRegularExpression expression("^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,79}$");
    return expression.match(id).hasMatch();
}
QString protocolName(AiApiProtocol protocol) {
    return protocol == AiApiProtocol::DeepSeekNative ? "deepseek-native" : "openai-compatible";
}
QJsonObject readObject(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 512 * 1024)
        fail("AI 配置无法读取或超过大小上限：" + QFileInfo(path).fileName());
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        fail("AI 配置不是有效 JSON：" + QFileInfo(path).fileName());
    return document.object();
}
void requireFields(const QJsonObject &object, const QSet<QString> &allowed,
                   const QString &reason) {
    for (auto entry = object.begin(); entry != object.end(); ++entry)
        if (!allowed.contains(entry.key()))
            fail(reason);
}
void writeObject(const QString &path, const QJsonObject &object) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        fail("无法创建本机 AI 配置目录");
    QSaveFile file(path);
    const auto bytes = QJsonDocument(object).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        fail("本机 AI 配置保存失败：" + QFileInfo(path).fileName());
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}
QByteArray protect(const QString &key, const QString &binding) {
#ifdef Q_OS_WIN
    auto bytes = key.toUtf8();
    auto entropy = binding.toUtf8();
    DATA_BLOB input{static_cast<DWORD>(bytes.size()),
                    reinterpret_cast<BYTE *>(bytes.data())};
    DATA_BLOB output{};
    DATA_BLOB extra{static_cast<DWORD>(entropy.size()),
                   reinterpret_cast<BYTE *>(entropy.data())};
    const bool ok = CryptProtectData(&input, L"CampusPulse API credential", &extra, nullptr,
                                     nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    SecureZeroMemory(bytes.data(), static_cast<SIZE_T>(bytes.size()));
    if (!ok)
        fail("Windows 当前用户密钥加密失败；没有保存明文 Key");
    QByteArray result(reinterpret_cast<const char *>(output.pbData),
                      static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return result;
#else
    Q_UNUSED(key)
    Q_UNUSED(binding)
    fail("当前系统尚未实现安全凭据存储，请仅在本次进程使用 Key");
#endif
}
QString unprotect(const QByteArray &bytes, const QString &binding) {
#ifdef Q_OS_WIN
    auto entropy = binding.toUtf8();
    DATA_BLOB extra{static_cast<DWORD>(entropy.size()),
                   reinterpret_cast<BYTE *>(entropy.data())};
    DATA_BLOB input{static_cast<DWORD>(bytes.size()),
                   reinterpret_cast<BYTE *>(const_cast<char *>(bytes.constData()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, &extra, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output))
        fail("保存的 API Key 无法解密，请在当前 Windows 用户下重新填写");
    const auto result = QString::fromUtf8(reinterpret_cast<const char *>(output.pbData),
                                         static_cast<qsizetype>(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
#else
    Q_UNUSED(bytes)
    Q_UNUSED(binding)
    fail("当前系统不支持解密 Windows 保存的 API Key");
#endif
}
}

bool AiProviderConfig::nativeSearch() const {
    return protocol == AiApiProtocol::DeepSeekNative;
}
bool AiProviderConfig::isOfficialDeepSeek() const {
    if (!endpointError(baseUrl).isEmpty())
        return false;
    const QUrl url(baseUrl, QUrl::StrictMode);
    auto path = url.path();
    if (path.endsWith('/'))
        path.chop(1);
    return url.host().toLower() == "api.deepseek.com" &&
           (path.isEmpty() || path == "/v1" || path == "/anthropic" ||
            path == "/anthropic/v1" || path == "/anthropic/v1/messages" ||
            path == "/chat/completions" || path == "/v1/chat/completions");
}
QString AiProviderConfig::endpointError(const QString &value) {
    static const QRegularExpression authority(
        "^https://([a-zA-Z0-9.-]+)(?::443)?(?:/[^?#]*)?$",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hostname(
        "^(?=.{1,253}$)(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?\\.)+"
        "[a-zA-Z](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?$");
    if (value.size() > 2048 || value != value.trimmed() ||
        value.contains(QRegularExpression("[\\s\\\\]")))
        return "API 地址不能含空白或反斜线";
    const auto match = authority.match(value);
    if (!match.hasMatch() || !hostname.match(match.captured(1)).hasMatch())
        return "API 地址须为 HTTPS 公共域名，只允许默认 443 端口，不接受账号、IP 或查询参数";
    const QUrl url(value, QUrl::StrictMode);
    const auto host = url.host().toLower();
    if (!url.isValid() || url.scheme() != "https" || !url.userName().isEmpty() ||
        !url.password().isEmpty() || url.hasQuery() || url.hasFragment() ||
        (url.port(-1) != -1 && url.port() != 443) || host == "localhost" ||
        host.endsWith(".localhost") || host.endsWith(".local") ||
        host.endsWith(".internal") || host.endsWith(".invalid") || host.endsWith(".onion"))
        return "API 地址不是允许的 HTTPS 公共地址";
    return {};
}
QString AiProviderConfig::validationError(const AiProviderConfig &config) {
    if (!validId(config.id))
        return "供应商 ID 无效";
    if (config.name.trimmed().isEmpty() || config.name.size() > 80 ||
        config.name.contains(QRegularExpression("[\\x00-\\x1f\\x7f]")))
        return "请填写有效供应商名称，最多 80 个字符";
    if (config.model.size() > 160 ||
        config.model.contains(QRegularExpression("[\\x00-\\x20\\x7f]")))
        return "模型名称不能含空白或控制字符，最多 160 个字符";
    if (config.protocol != AiApiProtocol::DeepSeekNative &&
        config.protocol != AiApiProtocol::OpenAiCompatible)
        return "供应商协议无效";
    if (config.authMode != AiAuthMode::Automatic && config.authMode != AiAuthMode::BearerToken &&
        config.authMode != AiAuthMode::ApiKeyHeader)
        return "供应商认证模式无效";
    if (config.notes.size() > 2000 ||
        config.notes.contains(QRegularExpression("[\\x00-\\x08\\x0b\\x0c\\x0e-\\x1f\\x7f]")))
        return "备注最多 2000 个字符，不能含无效控制字符";
    if (!config.website.isEmpty() && !endpointError(config.website).isEmpty())
        return "官网地址须为不含账号的 HTTPS 公共网址";
    const auto error = endpointError(config.baseUrl);
    if (!error.isEmpty())
        return error;
    return {};
}
bool AiProviderConfig::isPublicAddress(const QHostAddress &address) {
    bool ipv4 = false;
    const auto value = address.toIPv4Address(&ipv4);
    if (ipv4) {
        const QList<QPair<quint32, int>> forbidden{
            {0x00000000, 8}, {0x0a000000, 8}, {0x64400000, 10}, {0x7f000000, 8},
            {0xa9fe0000, 16}, {0xac100000, 12}, {0xc0000000, 24}, {0xc0000200, 24},
            {0xc0586300, 24}, {0xc0a80000, 16}, {0xc6120000, 15}, {0xc6336400, 24},
            {0xcb007100, 24}, {0xe0000000, 4}, {0xf0000000, 4}};
        for (const auto &[prefix, bits] : forbidden) {
            const quint32 mask = 0xffffffffU << (32 - bits);
            if ((value & mask) == prefix)
                return false;
        }
        return true;
    }
    if (address.protocol() != QAbstractSocket::IPv6Protocol ||
        !address.isInSubnet(QHostAddress("2000::"), 3) ||
        address.isInSubnet(QHostAddress("2001::"), 23) ||
        address.isInSubnet(QHostAddress("2001:db8::"), 32) ||
        address.isInSubnet(QHostAddress("2002::"), 16) ||
        address.isInSubnet(QHostAddress("3fff::"), 20))
        return false;
    return true;
}
QJsonObject AiProviderConfig::toJson() const {
    return {{"id", id}, {"name", name}, {"base_url", baseUrl}, {"model", model},
            {"protocol", protocolName(protocol)}, {"notes", notes}, {"website", website},
            {"full_url", fullUrl}, {"auth_mode", authName(authMode)}};
}
AiProviderConfig AiProviderConfig::fromJson(const QJsonObject &object) {
    static const QSet<QString> fields{"id", "name", "base_url", "model", "protocol", "notes",
                                     "website", "full_url", "auth_mode"};
    for (auto entry = object.begin(); entry != object.end(); ++entry)
        if (!fields.contains(entry.key()) ||
            (entry.key() == "full_url" ? !entry.value().isBool() : !entry.value().isString()))
            fail("AI 配置含不支持的字段；API Key 必须单独保存");
    AiProviderConfig result;
    result.id = object.value("id").toString();
    result.name = object.value("name").toString();
    result.baseUrl = object.value("base_url").toString();
    result.model = object.value("model").toString();
    result.notes = object.value("notes").toString();
    result.website = object.value("website").toString();
    result.fullUrl = object.value("full_url").toBool(false);
    const auto auth = object.value("auth_mode").toString("automatic");
    if (auth == "automatic")
        result.authMode = AiAuthMode::Automatic;
    else if (auth == "bearer-token")
        result.authMode = AiAuthMode::BearerToken;
    else if (auth == "api-key-header")
        result.authMode = AiAuthMode::ApiKeyHeader;
    else
        fail("AI 配置认证模式无效");
    const auto protocol = object.value("protocol").toString();
    if (protocol == "deepseek-native")
        result.protocol = AiApiProtocol::DeepSeekNative;
    else if (protocol == "openai-compatible")
        result.protocol = AiApiProtocol::OpenAiCompatible;
    else
        fail("AI 配置协议无效");
    const auto error = validationError(result);
    if (!error.isEmpty())
        fail(error);
    return result;
}
AiProviderConfig AiProviderConfig::deepSeekPreset() {
    return {"deepseek-official", "DeepSeek 官方", "https://api.deepseek.com/anthropic/v1",
            "deepseek-flash", AiApiProtocol::DeepSeekNative};
}
QUrl AiProviderConfig::requestEndpoint(const AiProviderConfig &provider) {
    if (!endpointError(provider.baseUrl).isEmpty())
        return {};
    if (provider.fullUrl)
        return QUrl(provider.baseUrl, QUrl::StrictMode);
    auto base = provider.baseUrl;
    while (base.endsWith('/'))
        base.chop(1);
    if (provider.nativeSearch())
        return QUrl(base + (base.endsWith("/v1") ? "/messages" : "/v1/messages"));
    const auto parsed = QUrl(base);
    const bool knownOfficialRoot = parsed.path().isEmpty() &&
        (parsed.host() == "api.deepseek.com" || parsed.host() == "api.openai.com");
    return QUrl(base + (knownOfficialRoot ? "/v1/chat/completions" : "/chat/completions"));
}
QUrl AiProviderConfig::modelsEndpoint(const AiProviderConfig &provider) {
    if (!endpointError(provider.baseUrl).isEmpty())
        return {};
    if (provider.isOfficialDeepSeek())
        return QUrl("https://api.deepseek.com/models");
    auto base = provider.baseUrl;
    while (base.endsWith('/'))
        base.chop(1);
    const auto parsed = QUrl(base);
    if (!provider.fullUrl && parsed.host() == "api.openai.com" && parsed.path().isEmpty())
        return QUrl(base + "/v1/models");
    if (provider.fullUrl) {
        if (base.endsWith("/chat/completions"))
            base.chop(QString("/chat/completions").size());
        else if (base.endsWith("/messages"))
            base.chop(QString("/messages").size());
        else
            return {};
    }
    return QUrl(base + "/models");
}
QMap<QByteArray, QByteArray> AiProviderConfig::credentialHeaders(const AiProviderConfig &provider,
                                                               const QString &key) {
    QMap<QByteArray, QByteArray> headers;
    if (provider.nativeSearch())
        headers.insert("anthropic-version", "2023-06-01");
    if (provider.authMode == AiAuthMode::BearerToken)
        headers.insert("Authorization", "Bearer " + key.toUtf8());
    else if (provider.authMode == AiAuthMode::ApiKeyHeader)
        headers.insert("x-api-key", key.toUtf8());
    else if (provider.nativeSearch()) {
        headers.insert("x-api-key", key.toUtf8());
        if (provider.isOfficialDeepSeek())
            headers.insert("Authorization", "Bearer " + key.toUtf8());
    } else
        headers.insert("Authorization", "Bearer " + key.toUtf8());
    return headers;
}

AiProviderStore::AiProviderStore(QString directory) {
    const QFileInfo info(directory);
    directory_ = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
    directory_ = QDir::cleanPath(directory_);
    sessionScope_ = directory_;
#ifdef Q_OS_WIN
    sessionScope_ = sessionScope_.toLower();
#endif
}
bool AiProviderStore::contains(const QString &id) const {
    for (const auto &provider : providers_)
        if (provider.id == id)
            return true;
    return false;
}
void AiProviderStore::load() {
    QList<AiProviderConfig> profiles;
    QString active;
    QMap<QString, QByteArray> secrets;
    const auto profilePath = QDir(directory_).filePath("providers.json");
    if (!QFileInfo::exists(profilePath)) {
        profiles.append(AiProviderConfig::deepSeekPreset());
        active = profiles.first().id;
    } else {
        const auto document = readObject(profilePath);
        requireFields(document, {"version", "active_id", "providers"},
                      "AI 供应商文件含不支持的字段；明文 Key 不能保存在此文件");
        if (document.value("version") != QJsonValue(1) ||
            !document.value("providers").isArray() ||
            document.value("providers").toArray().size() > 100 ||
            !document.value("active_id").isString())
            fail("AI 供应商配置版本或结构无效");
        QSet<QString> seen;
        for (const auto &entry : document.value("providers").toArray()) {
            if (!entry.isObject())
                fail("AI 供应商配置无效");
            const auto provider = AiProviderConfig::fromJson(entry.toObject());
            if (seen.contains(provider.id))
                fail("AI 供应商 ID 重复");
            seen.insert(provider.id);
            profiles.append(provider);
        }
        active = document.value("active_id").toString();
        if (!active.isEmpty() && !seen.contains(active))
            fail("当前启用的 AI 供应商不存在");
    }
    const auto secretPath = QDir(directory_).filePath("credentials.dpapi.json");
    if (QFileInfo::exists(secretPath)) {
        const auto document = readObject(secretPath);
        requireFields(document, {"version", "protection", "credentials"},
                      "本机 AI 凭据文件含不支持的字段");
        if (document.value("version") != QJsonValue(1) ||
            document.value("protection").toString() != "windows-dpapi-current-user" ||
            !document.value("credentials").isObject())
            fail("本机 AI 凭据文件结构无效");
        const auto entries = document.value("credentials").toObject();
        if (entries.size() > 100)
            fail("本机 AI 凭据数量超过上限");
        for (auto entry = entries.begin(); entry != entries.end(); ++entry) {
            if (!validId(entry.key()) || !entry.value().isString())
                fail("本机 AI 凭据字段无效");
            bool hasProfile = false;
            for (const auto &profile : profiles)
                if (profile.id == entry.key())
                    hasProfile = true;
            if (!hasProfile)
                fail("本机 AI 凭据不对应现有供应商，请修正配置后重试");
            const auto decoded = QByteArray::fromBase64Encoding(
                entry.value().toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded || decoded.decoded.isEmpty() || decoded.decoded.size() > 16384)
                fail("本机 AI 凭据加密数据无效");
            secrets.insert(entry.key(), decoded.decoded);
        }
    }
    providers_ = profiles;
    activeId_ = active;
    protectedKeys_ = secrets;
    auto &sessions = sessionCredentials[sessionScope_];
    for (auto entry = sessions.begin(); entry != sessions.end();)
        if (!contains(entry.key()))
            entry = sessions.erase(entry);
        else
            ++entry;
}
QList<AiProviderConfig> AiProviderStore::providers() const {
    return providers_;
}
QString AiProviderStore::activeId() const {
    return activeId_;
}
void AiProviderStore::saveProfiles(const QList<AiProviderConfig> &providers,
                                  const QString &activeId) const {
    QJsonArray entries;
    for (const auto &provider : providers)
        entries.append(provider.toJson());
    writeObject(QDir(directory_).filePath("providers.json"),
                {{"version", 1}, {"active_id", activeId}, {"providers", entries}});
}
void AiProviderStore::saveSecrets(const QMap<QString, QByteArray> &secrets) const {
    QJsonObject entries;
    for (auto entry = secrets.begin(); entry != secrets.end(); ++entry)
        entries.insert(entry.key(), QString::fromLatin1(entry.value().toBase64()));
    writeObject(QDir(directory_).filePath("credentials.dpapi.json"),
                {{"version", 1}, {"protection", "windows-dpapi-current-user"},
                 {"credentials", entries}});
}
void AiProviderStore::upsert(const AiProviderConfig &provider) {
    const auto error = AiProviderConfig::validationError(provider);
    if (!error.isEmpty())
        fail(error);
    auto profiles = providers_;
    bool replaced = false;
    bool credentialScopeChanged = false;
    for (auto &entry : profiles)
        if (entry.id == provider.id) {
            credentialScopeChanged = credentialBinding(entry) != credentialBinding(provider);
            entry = provider;
            replaced = true;
            break;
        }
    if (!replaced) {
        if (profiles.size() >= 100)
            fail("最多保存 100 套 AI 供应商配置");
        profiles.append(provider);
    }
    if (credentialScopeChanged) {
        auto secrets = protectedKeys_;
        if (secrets.remove(provider.id)) {
            saveSecrets(secrets);
            protectedKeys_ = secrets;
        }
        sessionCredentials[sessionScope_].remove(provider.id);
    }
    saveProfiles(profiles, activeId_);
    providers_ = profiles;
}
void AiProviderStore::remove(const QString &id) {
    if (!contains(id))
        fail("供应商不存在");
    auto profiles = providers_;
    for (auto entry = profiles.begin(); entry != profiles.end();)
        if (entry->id == id)
            entry = profiles.erase(entry);
        else
            ++entry;
    const auto active = activeId_ == id ? QString{} : activeId_;
    // Delete the credential first. If a later metadata write fails it remains safely absent.
    auto secrets = protectedKeys_;
    if (secrets.remove(id)) {
        saveSecrets(secrets);
        protectedKeys_ = secrets;
    }
    sessionCredentials[sessionScope_].remove(id);
    saveProfiles(profiles, active);
    providers_ = profiles;
    activeId_ = active;
}
void AiProviderStore::setActive(const QString &id) {
    if (!id.isEmpty() && !contains(id))
        fail("供应商不存在");
    saveProfiles(providers_, id);
    activeId_ = id;
}
QString AiProviderStore::key(const QString &id) const {
    if (!contains(id))
        return {};
    QString binding;
    for (const auto &profile : providers_)
        if (profile.id == id)
            binding = credentialBinding(profile);
    const auto sessions = sessionCredentials.value(sessionScope_);
    if (sessions.contains(id) && sessions.value(id).binding == binding)
        return sessions.value(id).value;
    if (protectedKeys_.contains(id))
        return unprotect(protectedKeys_.value(id), binding);
    return {};
}
void AiProviderStore::setKey(const QString &id, const QString &value, bool remember) {
    if (!contains(id))
        fail("供应商不存在");
    if (value.size() > 4096 || value.contains(QRegularExpression("[^\\x21-\\x7e]")))
        fail("API Key 不能含空白或控制字符，最多 4096 个字符");
    auto secrets = protectedKeys_;
    QString binding;
    for (const auto &profile : providers_)
        if (profile.id == id)
            binding = credentialBinding(profile);
    if (remember && !value.isEmpty())
        secrets.insert(id, protect(value, binding));
    else
        secrets.remove(id);
    if (secrets != protectedKeys_) {
        saveSecrets(secrets);
        protectedKeys_ = secrets;
    }
    if (value.isEmpty())
        sessionCredentials[sessionScope_].remove(id);
    else
        sessionCredentials[sessionScope_].insert(id, {value, binding});
}
bool AiProviderStore::keyIsRemembered(const QString &id) const {
    return contains(id) && protectedKeys_.contains(id);
}
bool AiProviderStore::persistentSecretsSupported() {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}
} // namespace campus
