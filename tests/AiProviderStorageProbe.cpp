#include "adapters/AiProviderConfig.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <stdexcept>
using namespace campus;
// Production storage in a disposable child of the specified directory.
// No real credential, API call, provider import, or existing profile mutation.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) return 2;
    QJsonObject proof{{"api_calls", 0}, {"real_credentials_used", false}};
    try {
        AiProviderStore realDirectory(app.arguments().at(1));
        realDirectory.load();
        realDirectory.checkWritable();
        proof["original_directory_preflight"] = true;
        QTemporaryDir directory(QDir(app.arguments().at(1)).filePath("campuspulse-storage-check-XXXXXX"));
        if (!directory.isValid()) throw std::runtime_error("Cannot create isolated storage check");
        auto provider = AiProviderConfig::deepSeekPreset();
        provider.model = "storage-check-model";
        const QString syntheticKey = "synthetic-campus-storage-check-key";
        AiProviderStore store(directory.path());
        store.load();
        store.checkWritable();
        store.saveProvider(provider, syntheticKey, AiProviderStore::persistentSecretsSupported(), true);
        AiProviderStore reopened(directory.path());
        reopened.load();
        const auto profile = reopened.providers().first();
        proof["profile_persisted"] = profile.toJson() == provider.toJson();
        proof["activation_persisted"] = reopened.activeId() == provider.id;
        proof["credential_recovered"] = reopened.key(provider.id) == syntheticKey;
        proof["dpapi_remembered"] = reopened.keyIsRemembered(provider.id);
        QFile file(directory.filePath("ai-providers.sqlite"));
        if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot verify stored bytes");
        proof["plaintext_absent"] = !file.readAll().contains(syntheticKey.toUtf8());
        file.close();
        proof["passed"] = proof.value("profile_persisted").toBool() &&
            proof.value("original_directory_preflight").toBool() &&
            proof.value("activation_persisted").toBool() &&
            proof.value("credential_recovered").toBool() && proof.value("plaintext_absent").toBool();
        proof["cleanup"] = directory.remove();
        if (!proof.value("cleanup").toBool()) proof["passed"] = false;
    } catch (const std::exception &error) {
        proof["passed"] = false;
        proof["error"] = QString::fromUtf8(error.what());
    }
    QTextStream(stdout) << QJsonDocument(proof).toJson();
    return proof.value("passed").toBool() ? 0 : 1;
}
