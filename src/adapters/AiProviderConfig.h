#pragma once
#include <QByteArray>
#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QUrl>

namespace campus {
enum class AiApiProtocol { DeepSeekNative, OpenAiCompatible };
enum class AiAuthMode { Automatic, BearerToken, ApiKeyHeader };

// Metadata only. Credentials never belong to this object or a university package.
struct AiProviderConfig {
    QString id;
    QString name;
    QString baseUrl;
    QString model;
    AiApiProtocol protocol = AiApiProtocol::DeepSeekNative;
    QString notes;
    QString website;
    bool fullUrl = false;
    AiAuthMode authMode = AiAuthMode::Automatic;

    // Requested Messages/tools mode; actual tool support still requires a typed response.
    bool nativeSearch() const;
    bool isOfficialDeepSeek() const;
    QJsonObject toJson() const;
    static AiProviderConfig fromJson(const QJsonObject &object);
    static AiProviderConfig deepSeekPreset();
    // Basic setup accepts an address and key. Resolve known endpoint suffixes
    // before selecting a wire protocol; never mix Messages and Chat paths.
    static AiProviderConfig automaticProfile(AiProviderConfig config);
    static QString validationError(const AiProviderConfig &config);
    static QString endpointError(const QString &baseUrl);
    static bool isPublicAddress(const QHostAddress &address);
    static QUrl requestEndpoint(const AiProviderConfig &provider);
    // Empty means this full request URL has no known model-list route; use a manual model ID.
    static QUrl modelsEndpoint(const AiProviderConfig &provider);
    static QMap<QByteArray, QByteArray> credentialHeaders(const AiProviderConfig &provider,
                                                         const QString &key);
};

// A local profile library. setKey(..., false) is session-only; true uses Windows DPAPI.
class AiProviderStore final {
  public:
    explicit AiProviderStore(QString directory);
    void load();
    QList<AiProviderConfig> providers() const;
    QString activeId() const;
    void upsert(const AiProviderConfig &provider);
    void remove(const QString &id);
    void setActive(const QString &id);
    QString key(const QString &id) const;
    void setKey(const QString &id, const QString &key, bool remember);
    // Commit profile, credential and activation together; failures preserve the previous state.
    void saveProvider(const AiProviderConfig &provider, const QString &key, bool remember,
                      bool activate);
    // Exercise a real transaction, then roll it back before any billable connection request.
    void checkWritable() const;
    bool keyIsRemembered(const QString &id) const;
    static bool persistentSecretsSupported();

  private:
    QString directory_;
    QString sessionScope_;
    QList<AiProviderConfig> providers_;
    QString activeId_;
    QMap<QString, QByteArray> protectedKeys_;
    void saveState(const QList<AiProviderConfig> &providers, const QString &activeId,
                   const QMap<QString, QByteArray> &secrets, bool probeOnly = false) const;
    bool contains(const QString &id) const;
};
} // namespace campus
