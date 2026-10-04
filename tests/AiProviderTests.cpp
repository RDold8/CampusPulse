#include "adapters/AiProviderConfig.h"
#include "adapters/AiProviderProbe.h"
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <stdexcept>

using namespace campus;
namespace {
AiProviderConfig compatible(const QString &id = "custom") {
    return {id, "测试兼容服务", "https://api.example.com/v1", "example-model",
            AiApiProtocol::OpenAiCompatible};
}
QByteArray readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
}
class AiProviderTests final : public QObject {
    Q_OBJECT
  private slots:
    void presetsAndProfileJsonHaveExplicitCapabilities() {
        const auto preset = AiProviderConfig::deepSeekPreset();
        QVERIFY(preset.nativeSearch());
        QVERIFY(preset.isOfficialDeepSeek());
        QVERIFY(AiProviderConfig::validationError(preset).isEmpty());
        QVERIFY(!compatible().nativeSearch());
        QCOMPARE(AiProviderConfig::fromJson(preset.toJson()).id, preset.id);
        auto unknown = compatible().toJson();
        unknown.insert("api_key", "synthetic-secret");
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, AiProviderConfig::fromJson(unknown));
        auto changed = preset;
        changed.baseUrl = "https://api.example.com/anthropic/v1";
        QVERIFY(AiProviderConfig::validationError(changed).isEmpty());
        QVERIFY(changed.nativeSearch());
        QVERIFY(!changed.isOfficialDeepSeek());
        const auto reopened = AiProviderConfig::fromJson(changed.toJson());
        QVERIFY(reopened.nativeSearch());
        QCOMPARE(reopened.baseUrl, changed.baseUrl);
        changed.protocol = AiApiProtocol::OpenAiCompatible;
        QVERIFY(AiProviderConfig::validationError(changed).isEmpty());
        QVERIFY(!changed.nativeSearch());
    }
    void officialIdentityIsAnExactAddressCheck_data() {
        QTest::addColumn<QString>("base");
        QTest::addColumn<bool>("official");
        const QList<QPair<QString, bool>> samples{
            {"https://api.deepseek.com", true}, {"https://api.deepseek.com/", true},
            {"https://api.deepseek.com/v1", true}, {"https://api.deepseek.com/v1/", true},
            {"https://api.deepseek.com/anthropic", true},
            {"https://api.deepseek.com/anthropic/v1", true},
            {"https://api.deepseek.com/anthropic/v1/", true},
            {"https://api.deepseek.com/anthropic/v1/messages", true},
            {"https://api.deepseek.com/v1/chat/completions", true},
            {"https://api.deepseek.com:443/anthropic/v1", true},
            {"http://api.deepseek.com/anthropic/v1", false},
            {"https://api.deepseek.com.evil.example/anthropic/v1", false},
            {"https://other.deepseek.com/anthropic/v1", false},
            {"https://api.example.com/anthropic/v1", false},
            {"https://api.deepseek.com/other/v1", false},
            {"https://api.deepseek.com/anthropic/v1?key=x", false},
            {"https://api.deepseek.com/anthropic/v1#fragment", false}};
        for (int i = 0; i < samples.size(); ++i)
            QTest::newRow(qPrintable(QString::number(i))) << samples[i].first << samples[i].second;
    }
    void officialIdentityIsAnExactAddressCheck() {
        QFETCH(QString, base);
        QFETCH(bool, official);
        auto profile = AiProviderConfig::deepSeekPreset();
        profile.baseUrl = base;
        QCOMPARE(profile.isOfficialDeepSeek(), official);
        profile.protocol = AiApiProtocol::OpenAiCompatible;
        QCOMPARE(profile.isOfficialDeepSeek(), official);
    }
    void extendedMetadataRoundTripsAndLegacyJsonUsesSafeDefaults() {
        auto provider = compatible();
        provider.notes = "仅用于公开栏目候选\n手动确认搜索支持";
        provider.website = "https://provider.example.com/docs";
        provider.fullUrl = true;
        provider.authMode = AiAuthMode::ApiKeyHeader;
        const auto reopened = AiProviderConfig::fromJson(provider.toJson());
        QCOMPARE(reopened.notes, provider.notes);
        QCOMPARE(reopened.website, provider.website);
        QVERIFY(reopened.fullUrl);
        QCOMPARE(reopened.authMode, AiAuthMode::ApiKeyHeader);
        auto legacy = provider.toJson();
        for (const auto *field : {"notes", "website", "full_url", "auth_mode"})
            legacy.remove(QLatin1String(field));
        const auto original = AiProviderConfig::fromJson(legacy);
        QVERIFY(original.notes.isEmpty());
        QVERIFY(original.website.isEmpty());
        QVERIFY(!original.fullUrl);
        QCOMPARE(original.authMode, AiAuthMode::Automatic);
        auto invalid = provider.toJson();
        invalid["full_url"] = "true";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, AiProviderConfig::fromJson(invalid));
        invalid = provider.toJson();
        invalid["auth_mode"] = "unknown-mode";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, AiProviderConfig::fromJson(invalid));
        invalid = provider.toJson();
        invalid["notes"] = 1;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, AiProviderConfig::fromJson(invalid));
        QVERIFY(!provider.toJson().contains("api_key"));
    }
    void sharedRoutingHelpersFollowOnlyTheChosenAddress_data() {
        QTest::addColumn<QString>("base");
        QTest::addColumn<bool>("native");
        QTest::addColumn<bool>("full");
        QTest::addColumn<QString>("request");
        QTest::addColumn<QString>("models");
        QTest::newRow("official-anthropic-base")
            << QString("https://api.deepseek.com/anthropic") << true << false
            << QString("https://api.deepseek.com/anthropic/v1/messages")
            << QString("https://api.deepseek.com/models");
        QTest::newRow("official-native-v1")
            << QString("https://api.deepseek.com/anthropic/v1/") << true << false
            << QString("https://api.deepseek.com/anthropic/v1/messages")
            << QString("https://api.deepseek.com/models");
        QTest::newRow("native-custom-root")
            << QString("https://gateway.example.com") << true << false
            << QString("https://gateway.example.com/v1/messages")
            << QString("https://gateway.example.com/models");
        QTest::newRow("native-custom-v1")
            << QString("https://gateway.example.com/relay/v1") << true << false
            << QString("https://gateway.example.com/relay/v1/messages")
            << QString("https://gateway.example.com/relay/v1/models");
        QTest::newRow("native-custom-relay-base")
            << QString("https://gateway.example.com/relay") << true << false
            << QString("https://gateway.example.com/relay/v1/messages")
            << QString("https://gateway.example.com/relay/models");
        QTest::newRow("openai-custom-v1")
            << QString("https://gateway.example.com/relay/v1/") << false << false
            << QString("https://gateway.example.com/relay/v1/chat/completions")
            << QString("https://gateway.example.com/relay/v1/models");
        QTest::newRow("openai-custom-nonstandard-path")
            << QString("https://gateway.example.com/relay/v2") << false << false
            << QString("https://gateway.example.com/relay/v2/chat/completions")
            << QString("https://gateway.example.com/relay/v2/models");
        QTest::newRow("openai-official-root")
            << QString("https://api.openai.com") << false << false
            << QString("https://api.openai.com/v1/chat/completions")
            << QString("https://api.openai.com/v1/models");
        QTest::newRow("openai-official-root-trailing-slash")
            << QString("https://api.openai.com/") << false << false
            << QString("https://api.openai.com/v1/chat/completions")
            << QString("https://api.openai.com/v1/models");
        QTest::newRow("deepseek-openai-official-root")
            << QString("https://api.deepseek.com") << false << false
            << QString("https://api.deepseek.com/v1/chat/completions")
            << QString("https://api.deepseek.com/models");
        QTest::newRow("full-native-official")
            << QString("https://api.deepseek.com/anthropic/v1/messages") << true << true
            << QString("https://api.deepseek.com/anthropic/v1/messages")
            << QString("https://api.deepseek.com/models");
        QTest::newRow("full-native-custom")
            << QString("https://gateway.example.com/relay/v1/messages") << true << true
            << QString("https://gateway.example.com/relay/v1/messages")
            << QString("https://gateway.example.com/relay/v1/models");
        QTest::newRow("full-openai-custom")
            << QString("https://gateway.example.com/relay/v1/chat/completions") << false << true
            << QString("https://gateway.example.com/relay/v1/chat/completions")
            << QString("https://gateway.example.com/relay/v1/models");
        QTest::newRow("full-custom-unknown-route")
            << QString("https://gateway.example.com/submit-request") << true << true
            << QString("https://gateway.example.com/submit-request") << QString{};
        QTest::newRow("full-custom-unrelated-native-route")
            << QString("https://gateway.example.com/chat/submit") << false << true
            << QString("https://gateway.example.com/chat/submit") << QString{};
        QTest::newRow("reject-loopback-route")
            << QString("https://127.0.0.1/messages") << true << true << QString{} << QString{};
        QTest::newRow("reject-query-route")
            << QString("https://gateway.example.com/messages?token=x") << true << true
            << QString{} << QString{};
    }
    void sharedRoutingHelpersFollowOnlyTheChosenAddress() {
        QFETCH(QString, base);
        QFETCH(bool, native);
        QFETCH(bool, full);
        QFETCH(QString, request);
        QFETCH(QString, models);
        auto provider = compatible();
        provider.baseUrl = base;
        provider.protocol = native ? AiApiProtocol::DeepSeekNative : AiApiProtocol::OpenAiCompatible;
        provider.fullUrl = full;
        QCOMPARE(AiProviderConfig::requestEndpoint(provider).toString(), request);
        QCOMPARE(AiProviderConfig::modelsEndpoint(provider).toString(), models);
        QCOMPARE(AiProviderProbe::endpoint(provider, AiProbeOperation::Connection).toString(), request);
        QCOMPARE(AiProviderProbe::endpoint(provider, AiProbeOperation::Models).toString(), models);
    }
    void explicitAuthenticationSelectsOnlyTheRequestedCredentialHeader() {
        auto provider = compatible();
        const QString secret = "synthetic-test-key";
        auto headers = AiProviderConfig::credentialHeaders(provider, secret);
        QCOMPARE(headers.value("Authorization"), QByteArray("Bearer synthetic-test-key"));
        QVERIFY(!headers.contains("x-api-key"));
        QVERIFY(!headers.contains("anthropic-version"));
        provider.protocol = AiApiProtocol::DeepSeekNative;
        headers = AiProviderConfig::credentialHeaders(provider, secret);
        QVERIFY(!headers.contains("Authorization"));
        QCOMPARE(headers.value("x-api-key"), QByteArray("synthetic-test-key"));
        QCOMPARE(headers.value("anthropic-version"), QByteArray("2023-06-01"));
        provider.authMode = AiAuthMode::BearerToken;
        headers = AiProviderConfig::credentialHeaders(provider, secret);
        QVERIFY(!headers.contains("x-api-key"));
        QVERIFY(headers.contains("Authorization"));
        QVERIFY(headers.contains("anthropic-version"));
        provider = AiProviderConfig::deepSeekPreset();
        headers = AiProviderConfig::credentialHeaders(provider, secret);
        QVERIFY(headers.contains("x-api-key"));
        QVERIFY(headers.contains("Authorization"));
        provider.authMode = AiAuthMode::ApiKeyHeader;
        headers = AiProviderConfig::credentialHeaders(provider, secret);
        QVERIFY(headers.contains("x-api-key"));
        QVERIFY(!headers.contains("Authorization"));
        provider.authMode = AiAuthMode::BearerToken;
        headers = AiProviderConfig::credentialHeaders(provider, secret);
        QVERIFY(!headers.contains("x-api-key"));
        QVERIFY(headers.contains("Authorization"));
        provider = compatible();
        provider.authMode = AiAuthMode::ApiKeyHeader;
        headers = AiProviderConfig::credentialHeaders(provider, secret);
        QVERIFY(headers.contains("x-api-key"));
        QVERIFY(!headers.contains("Authorization"));
        QVERIFY(!headers.contains("anthropic-version"));
    }
    void publicHttpsMetadataBoundaries_data() {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("allowed");
        const QList<QPair<QString, bool>> samples{
            {"https://api.example.com/v1", true}, {"https://api.example.com:443/v1", true},
            {"https://api.example.com/v1/", true}, {"http://api.example.com/v1", false},
            {"https://127.0.0.1/v1", false}, {"https://[::1]/v1", false},
            {"https://2130706433/v1", false}, {"https://localhost/v1", false},
            {"https://api.local/v1", false}, {"https://api.internal/v1", false},
            {"https://api.example.com:8080/v1", false},
            {"https://user:password@api.example.com/v1", false},
            {"https://@api.example.com/v1", false},
            {"https://api.example.com/v1?key=abc", false},
            {"https://api.example.com/v1#secret", false},
            {"https://api.example.com/v1?", false},
            {"https://api.example.com./v1", false},
            {"https://api.example.com\\@evil.example.com/v1", false},
            {"https://api.example.com/v1\r\nInjected: x", false}};
        for (int i = 0; i < samples.size(); ++i)
            QTest::newRow(qPrintable(QString::number(i))) << samples[i].first << samples[i].second;
    }
    void publicHttpsMetadataBoundaries() {
        QFETCH(QString, url);
        QFETCH(bool, allowed);
        QCOMPARE(AiProviderConfig::endpointError(url).isEmpty(), allowed);
    }
    void publicDnsAddresses_data() {
        QTest::addColumn<QString>("address");
        QTest::addColumn<bool>("allowed");
        const QList<QPair<QString, bool>> samples{
            {"8.8.8.8", true}, {"1.1.1.1", true}, {"2001:4860:4860::8888", true},
            {"2606:4700:4700::1111", true}, {"0.0.0.0", false}, {"10.0.0.1", false},
            {"127.1.2.3", false}, {"169.254.169.254", false}, {"172.16.0.1", false},
            {"172.31.255.1", false}, {"192.168.1.1", false}, {"100.64.0.1", false},
            {"198.18.0.1", false}, {"192.0.2.1", false}, {"198.51.100.3", false},
            {"203.0.113.3", false}, {"224.0.0.1", false}, {"255.255.255.255", false},
            {"::1", false}, {"::", false}, {"fc00::1", false}, {"fe80::1", false},
            {"ff02::1", false}, {"::ffff:127.0.0.1", false}, {"2001:db8::1", false},
            {"2002:7f00:1::", false}, {"3fff::1", false}};
        for (int i = 0; i < samples.size(); ++i)
            QTest::newRow(qPrintable(QString::number(i))) << samples[i].first << samples[i].second;
    }
    void publicDnsAddresses() {
        QFETCH(QString, address);
        QFETCH(bool, allowed);
        QCOMPARE(AiProviderConfig::isPublicAddress(QHostAddress(address)), allowed);
    }
    void profilesPersistAndDeleteActiveDisablesIt() {
        QTemporaryDir directory;
        AiProviderStore store(directory.path());
        store.load();
        QCOMPARE(store.providers().size(), 1);
        store.upsert(compatible());
        store.setActive("custom");
        AiProviderStore reopened(directory.path());
        reopened.load();
        QCOMPARE(reopened.providers().size(), 2);
        QCOMPARE(reopened.activeId(), QString("custom"));
        reopened.remove("custom");
        QVERIFY(reopened.activeId().isEmpty());
        reopened.setActive("deepseek-official");
        reopened.setActive({});
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, reopened.setActive("missing"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, reopened.remove("missing"));
    }
    void sessionKeysSurvivePageRecreationButNeverReachDiskOrOtherDirectories() {
        QTemporaryDir first;
        QTemporaryDir second;
        AiProviderStore store(first.path());
        store.load();
        store.upsert(compatible());
        const QString secret = "synthetic-session-secret-987";
        store.setKey("custom", secret, false);
        QVERIFY(!store.keyIsRemembered("custom"));
        QVERIFY(!readFile(first.filePath("providers.json")).contains(secret.toUtf8()));
        QVERIFY(!QFile::exists(first.filePath("credentials.dpapi.json")));
        AiProviderStore recreated(first.path());
        recreated.load();
        QCOMPARE(recreated.key("custom"), secret);
        AiProviderStore isolated(second.path());
        isolated.load();
        isolated.upsert(compatible());
        QVERIFY(isolated.key("custom").isEmpty());
        recreated.setKey("custom", {}, false);
        QVERIFY(store.key("custom").isEmpty());
    }
    void credentialIsRevokedWhenEndpointOrProtocolChanges() {
        QTemporaryDir directory;
        AiProviderStore store(directory.path());
        store.load();
        auto profile = compatible();
        store.upsert(profile);
        store.setKey(profile.id, "synthetic-original-secret", false);
        profile.model = "updated-model";
        store.upsert(profile);
        QCOMPARE(store.key(profile.id), QString("synthetic-original-secret"));
        profile.baseUrl = "https://other.example.com/v1";
        store.upsert(profile);
        QVERIFY(store.key(profile.id).isEmpty());
        auto native = AiProviderConfig::deepSeekPreset();
        store.setKey(native.id, "synthetic-native-secret", false);
        native.protocol = AiApiProtocol::OpenAiCompatible;
        store.upsert(native);
        QVERIFY(store.key(native.id).isEmpty());
    }
    void customMessagesRoutingKeepsTheProtocolButRevokesTheOldRouteCredential() {
        QTemporaryDir directory;
        AiProviderStore store(directory.path());
        store.load();
        auto route = AiProviderConfig::deepSeekPreset();
        store.setKey(route.id, "synthetic-official-route-key", false);
        route.baseUrl = "https://gateway.example.com/anthropic/v1";
        store.upsert(route);
        QVERIFY(route.nativeSearch());
        QVERIFY(!route.isOfficialDeepSeek());
        QVERIFY(store.key(route.id).isEmpty());
        store.setKey(route.id, "synthetic-custom-route-key", false);
        route.baseUrl = "https://other.example.com/messages-api/v1";
        store.upsert(route);
        QVERIFY(store.key(route.id).isEmpty());
        QCOMPARE(AiProviderProbe::endpoint(route, AiProbeOperation::Connection).toString(),
                 QString("https://other.example.com/messages-api/v1/messages"));
        QCOMPARE(AiProviderProbe::endpoint(route, AiProbeOperation::Models).toString(),
                 QString("https://other.example.com/messages-api/v1/models"));
        QVERIFY(!AiProviderProbe::endpoint(route, AiProbeOperation::Models).host()
                     .contains("deepseek"));
    }
    void urlAndAuthenticationModesAreCredentialBoundButNotesAreNot() {
        QTemporaryDir directory;
        AiProviderStore store(directory.path());
        store.load();
        auto provider = compatible();
        store.upsert(provider);
        store.setKey(provider.id, "synthetic-original-key", false);
        provider.notes = "备注可独立修改";
        provider.website = "https://provider.example.com";
        store.upsert(provider);
        QCOMPARE(store.key(provider.id), QString("synthetic-original-key"));
        provider.fullUrl = true;
        store.upsert(provider);
        QVERIFY(store.key(provider.id).isEmpty());
        store.setKey(provider.id, "synthetic-full-url-key", false);
        provider.authMode = AiAuthMode::BearerToken;
        store.upsert(provider);
        QVERIFY(store.key(provider.id).isEmpty());
        store.setKey(provider.id, "synthetic-bearer-key", false);
        provider.authMode = AiAuthMode::ApiKeyHeader;
        store.upsert(provider);
        QVERIFY(store.key(provider.id).isEmpty());
        store.setKey(provider.id, "synthetic-api-header-key", false);
        provider.authMode = AiAuthMode::Automatic;
        store.upsert(provider);
        QVERIFY(store.key(provider.id).isEmpty());
    }
    void rememberedCredentialUsesDpapiAndCanBeRevoked() {
        if (!AiProviderStore::persistentSecretsSupported())
            QSKIP("Windows DPAPI is unavailable on this OS");
        QTemporaryDir first;
        QTemporaryDir second;
        AiProviderStore store(first.path());
        store.load();
        store.upsert(compatible());
        const QString secret = "synthetic-encrypted-secret-987";
        store.setKey("custom", secret, true);
        QVERIFY(store.keyIsRemembered("custom"));
        const auto encrypted = readFile(first.filePath("credentials.dpapi.json"));
        QVERIFY(!encrypted.contains(secret.toUtf8()));
        QVERIFY(!readFile(first.filePath("providers.json")).contains(secret.toUtf8()));
        QVERIFY(QFile::copy(first.filePath("providers.json"), second.filePath("providers.json")));
        QVERIFY(QFile::copy(first.filePath("credentials.dpapi.json"),
                           second.filePath("credentials.dpapi.json")));
        // Another directory has no shared session key; this exercises real current-user decrypt.
        AiProviderStore decrypted(second.path());
        decrypted.load();
        QCOMPARE(decrypted.key("custom"), secret);
        decrypted.setKey("custom", secret, false);
        QVERIFY(!decrypted.keyIsRemembered("custom"));
        QVERIFY(!readFile(second.filePath("credentials.dpapi.json")).contains(secret.toUtf8()));
        auto profile = compatible();
        store.upsert(profile);
        profile.baseUrl = "https://other.example.com/v1";
        store.upsert(profile);
        QVERIFY(!store.keyIsRemembered("custom"));
        QVERIFY(store.key("custom").isEmpty());
    }
    void invalidMetadataCannotPublishOrReplaceStoreState() {
        QTemporaryDir directory;
        AiProviderStore store(directory.path());
        store.load();
        auto profile = compatible();
        profile.baseUrl = "http://127.0.0.1/v1";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, store.upsert(profile));
        QCOMPARE(store.providers().size(), 1);
        QVERIFY(!QFile::exists(directory.filePath("providers.json")));
        store.upsert(compatible());
        QFile file(directory.filePath("providers.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{invalid");
        file.close();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, store.load());
        QCOMPARE(store.providers().size(), 2);
    }
    void metadataTamperingCannotReuseAnEncryptedCredentialForAnotherEndpoint() {
        if (!AiProviderStore::persistentSecretsSupported())
            QSKIP("Windows DPAPI is unavailable on this OS");
        QTemporaryDir source;
        QTemporaryDir tampered;
        AiProviderStore store(source.path());
        store.load();
        store.upsert(compatible());
        store.setKey("custom", "synthetic-bound-secret", true);
        QVERIFY(QFile::copy(source.filePath("credentials.dpapi.json"),
                           tampered.filePath("credentials.dpapi.json")));
        auto object = QJsonDocument::fromJson(readFile(source.filePath("providers.json"))).object();
        auto profiles = object.value("providers").toArray();
        auto changed = profiles[1].toObject();
        changed.insert("base_url", "https://different.example.com/v1");
        profiles[1] = changed;
        object.insert("providers", profiles);
        QFile metadata(tampered.filePath("providers.json"));
        QVERIFY(metadata.open(QIODevice::WriteOnly));
        metadata.write(QJsonDocument(object).toJson());
        metadata.close();
        AiProviderStore reopened(tampered.path());
        reopened.load();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, reopened.key("custom"));
    }
    void refusedFilesystemWritesDoNotPretendToSaveNewProfiles() {
        QTemporaryDir directory;
        const auto blocker = directory.filePath("occupied");
        QFile file(blocker);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("file, not directory");
        file.close();
        AiProviderStore store(blocker);
        store.load();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, store.upsert(compatible()));
        QCOMPARE(store.providers().size(), 1);
        QCOMPARE(store.activeId(), QString("deepseek-official"));
        store.setKey("deepseek-official", "synthetic-memory-key", false);
        if (AiProviderStore::persistentSecretsSupported()) {
            QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                store.setKey("deepseek-official", "synthetic-new-key", true));
            QCOMPARE(store.key("deepseek-official"), QString("synthetic-memory-key"));
            QVERIFY(!store.keyIsRemembered("deepseek-official"));
        }
    }
    void connectionAndModelsAreDifferentOperations() {
        const auto native = AiProviderConfig::deepSeekPreset();
        QCOMPARE(AiProviderProbe::endpoint(native, AiProbeOperation::Models).toString(),
                 QString("https://api.deepseek.com/models"));
        QCOMPARE(AiProviderProbe::endpoint(native, AiProbeOperation::Connection).toString(),
                 QString("https://api.deepseek.com/anthropic/v1/messages"));
        auto customNative = native;
        customNative.baseUrl = "https://api.example.com/messages/v1/";
        QCOMPARE(AiProviderProbe::endpoint(customNative, AiProbeOperation::Models).toString(),
                 QString("https://api.example.com/messages/v1/models"));
        QCOMPARE(AiProviderProbe::endpoint(customNative, AiProbeOperation::Connection).toString(),
                 QString("https://api.example.com/messages/v1/messages"));
        QVERIFY(AiProviderConfig::validationError(customNative).isEmpty());
        QCOMPARE(AiProviderProbe::endpoint(compatible(), AiProbeOperation::Connection).toString(),
                 QString("https://api.example.com/v1/chat/completions"));
        const auto body = AiProviderProbe::connectionBody(native);
        QVERIFY(!body.contains("tools"));
        QVERIFY(!body.contains("api_key"));
        QCOMPARE(body.value("max_tokens").toInt(), 128);
        const auto models = AiProviderProbe::evaluate(AiApiProtocol::OpenAiCompatible,
            AiProbeOperation::Models, 200, R"({"data":[{"id":"z"},{"id":"a"},{"id":"a"}]})", 18);
        QVERIFY(models.success);
        QCOMPARE(models.modelIds, QStringList({"a", "z"}));
        QVERIFY(models.message.contains("不证明"));
        const auto probe = AiProviderProbe::evaluate(AiApiProtocol::OpenAiCompatible,
            AiProbeOperation::Connection, 200,
            R"({"model":"test","choices":[{"message":{"content":"OK"}}],"usage":{"total_tokens":9,"untrusted":"text"}})", 25);
        QVERIFY(probe.success);
        QCOMPARE(probe.responseModel, QString("test"));
        QCOMPARE(probe.usage.value("total_tokens").toInt(), 9);
        QVERIFY(!probe.usage.contains("untrusted"));
        QVERIFY(probe.message.contains("没有执行网页检索"));
        const auto nativeProbe = AiProviderProbe::evaluate(AiApiProtocol::DeepSeekNative,
            AiProbeOperation::Connection, 200,
            R"({"model":"test","content":[{"type":"text","text":"OK"}]})", 40);
        QVERIFY(nativeProbe.success);
    }
    void responsesRequireActualDataAndExposeFailures_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("reason");
        QTest::newRow("auth") << 401 << QByteArray("{}") << QString("鉴权");
        QTest::newRow("rate") << 429 << QByteArray("{}") << QString("限流");
        QTest::newRow("redirect") << 302 << QByteArray("{}") << QString("重定向");
        QTest::newRow("bad-json") << 200 << QByteArray("<html>OK</html>") << QString("JSON");
        QTest::newRow("empty") << 200 << QByteArray("{}") << QString("结构");
        QTest::newRow("unsupported") << 404 << QByteArray("{}") << QString("手动");
        QTest::newRow("invalid-model") << 200 << QByteArray(R"({"data":[{"id":""}]})") << QString("无效");
        QTest::newRow("empty-models") << 200 << QByteArray(R"({"data":[]})") << QString("空");
        QTest::newRow("error-object") << 200 << QByteArray(R"({"error":"bad"})") << QString("错误");
    }
    void successfulTextCanConfirmConnectionWithoutAReportedModelName() {
        const auto chat = AiProviderProbe::evaluate(AiApiProtocol::OpenAiCompatible,
            AiProbeOperation::Connection, 200,
            R"({"choices":[{"message":{"content":"OK"}}]})", 17);
        QVERIFY(chat.success);
        QVERIFY(chat.responseModel.isEmpty());
        QVERIFY(chat.message.contains("未回显模型"));
        const auto messages = AiProviderProbe::evaluate(AiApiProtocol::DeepSeekNative,
            AiProbeOperation::Connection, 200,
            R"({"content":[{"type":"text","text":"OK"}]})", 24);
        QVERIFY(messages.success);
        QVERIFY(messages.responseModel.isEmpty());
        const auto bad = AiProviderProbe::evaluate(AiApiProtocol::OpenAiCompatible,
            AiProbeOperation::Connection, 200,
            R"({"model":123,"choices":[{"message":{"content":"OK"}}]})", 25);
        QVERIFY(!bad.success);
        const auto injected = AiProviderProbe::evaluate(AiApiProtocol::OpenAiCompatible,
            AiProbeOperation::Connection, 200,
            R"({"model":"bad\nmodel","choices":[{"message":{"content":"OK"}}]})", 25);
        QVERIFY(!injected.success);
    }
    void responsesRequireActualDataAndExposeFailures() {
        QFETCH(int, status);
        QFETCH(QByteArray, bytes);
        QFETCH(QString, reason);
        const auto result = AiProviderProbe::evaluate(AiApiProtocol::OpenAiCompatible,
            AiProbeOperation::Models, status, bytes, 42);
        QVERIFY(!result.success);
        QVERIFY(result.message.contains(reason));
        QCOMPARE(result.httpStatus, status);
        QCOMPARE(result.elapsedMs, 42);
    }
    void rejectUnsafeInputsBeforeDnsAndNeverConnectOnConstruction() {
        AiProviderProbe probe;
        QSignalSpy outcomes(&probe, &AiProviderProbe::finished);
        QVERIFY(!probe.busy());
        QCOMPARE(outcomes.count(), 0);
        auto blocked = compatible();
        blocked.baseUrl = "https://localhost/v1";
        probe.probe(blocked, "synthetic-key");
        QCOMPARE(outcomes.count(), 1);
        QVERIFY(!probe.busy());
        const auto result = outcomes.takeFirst().at(0).value<AiProbeResult>();
        QVERIFY(!result.success);
        QCOMPARE(result.httpStatus, 0);
        probe.fetchModels(compatible(), "key\r\nX-Injected: true");
        QCOMPARE(outcomes.count(), 1);
        QVERIFY(!probe.busy());
        probe.cancel();
        QCOMPARE(outcomes.count(), 1);
        auto unknownModels = compatible();
        unknownModels.baseUrl = "https://gateway.example.com/arbitrary-call";
        unknownModels.fullUrl = true;
        outcomes.clear();
        probe.fetchModels(unknownModels, "synthetic-key");
        QCOMPARE(outcomes.count(), 1);
        QVERIFY(!probe.busy());
        const auto manual = outcomes.takeFirst().at(0).value<AiProbeResult>();
        QVERIFY(!manual.success);
        QVERIFY(manual.message.contains("手动"));
        QCOMPARE(manual.httpStatus, 0);
    }
};
QTEST_GUILESS_MAIN(AiProviderTests)
#include "AiProviderTests.moc"
