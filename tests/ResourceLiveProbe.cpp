#include "adapters/ArtifactWriter.h"
#include "adapters/ResourceClassifier.h"
#include "adapters/ResourceDiscovery.h"
#include "adapters/SchoolPackage.h"
#include "application/ResourceService.h"
#include "storage/Database.h"
#include "storage/SqliteResourceRepository.h"
#include "ResourceProbeJson.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QNetworkProxyFactory>
#include <QTextStream>
#include <QTimer>
#include <stdexcept>

using namespace campus;

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    application.setOrganizationName("CampusPulseAcceptance");
    application.setApplicationName("ResourceLiveProbe");
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"config", "Reviewed university config", "file"});
    parser.addOption({"database", "Explicit acceptance SQLite file", "file"});
    parser.addOption({"evidence", "JSON result", "file"});
    parser.addOption({"samples", "Raw website evidence directory", "directory"});
    parser.addOption({"import-json", "Import a reviewed resource acceptance report without requests", "file"});
    parser.process(application);
    try {
        for (const auto *option : {"config", "database", "evidence"})
            if (!parser.isSet(option))
                throw std::invalid_argument(std::string("Missing --") + option);
        const auto school = SchoolPackage::load(parser.value("config"));
        const auto databasePath = QFileInfo(parser.value("database")).absoluteFilePath();
        if (!QDir().mkpath(QFileInfo(databasePath).absolutePath()))
            throw std::runtime_error("Unable to create acceptance database directory");
        QLockFile lock(databasePath + ".lock");
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0))
            throw std::runtime_error("Database is in use; no data was written");
        Database database(databasePath);
        SqliteResourceRepository repository(database);
        ResourceService resources(repository, school.id.toStdString());
        bool timedOut = false;
        const auto writeProof = [&](bool success, bool imported, const QString &error = {}) {
            QJsonArray records;
            QJsonObject statuses, categories;
            for (const auto &resource : resources.list()) {
                records.append(resource_probe::json(resource));
                const auto status = QString::fromStdString(resource.status);
                const auto category = QString::fromStdString(resource.category);
                statuses[status] = statuses.value(status).toInt() + 1;
                categories[category] = categories.value(category).toInt() + 1;
            }
            const QJsonObject proof{{"passed", success}, {"school_id", school.id},
                                    {"school_name", school.name}, {"database", databasePath},
                                    {"resource_count", records.size()}, {"statuses", statuses},
                                    {"categories", categories}, {"resources", records},
                                    {"import_only", imported}, {"model_calls", 0},
                                    {"error", error}, {"timeout", timedOut}};
            writeArtifact(parser.value("evidence"), QJsonDocument(proof).toJson());
            QTextStream(stdout) << "Resources: " << records.size()
                                << "; verified: " << statuses.value("verified").toInt()
                                << "; passed: " << success << '\n';
        };
        if (parser.isSet("import-json")) {
            QFile input(parser.value("import-json"));
            if (!input.open(QIODevice::ReadOnly))
                throw std::runtime_error("Cannot read reviewed resource report");
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(input.readAll(), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject() ||
                !document.object().value("passed").toBool() ||
                document.object().value("school_id").toString() != school.id)
                throw std::runtime_error("Invalid acceptance report or mismatching university");
            auto root = school.officialHomepage.host().toLower();
            if (root.startsWith("www."))
                root.remove(0, 4);
            std::vector<SchoolResource> imported;
            for (const auto value : document.object().value("resources").toArray()) {
                auto resource = resource_probe::resource(value.toObject());
                const QUrl target(QString::fromStdString(resource.url), QUrl::StrictMode);
                const QUrl origin(QString::fromStdString(resource.discoveredFrom), QUrl::StrictMode);
                if (!ResourceClassifier::isSafeHttps(QString::fromStdString(resource.url)) ||
                    !ResourceClassifier::isSafeHttps(QString::fromStdString(resource.discoveredFrom)) ||
                    !ResourceClassifier::isOfficial(origin, root) ||
                    ((resource.linkKind == "official") != ResourceClassifier::isOfficial(target, root)))
                    throw std::runtime_error("Imported resource lacks valid university provenance");
                if (!resource.accessEvidence.empty() &&
                    (!ResourceClassifier::isSafeHttps(QString::fromStdString(resource.accessEvidence)) ||
                     !ResourceClassifier::isOfficial(
                         QUrl(QString::fromStdString(resource.accessEvidence), QUrl::StrictMode), root)))
                    throw std::runtime_error("Reviewed usage conditions lack university evidence");
                imported.push_back(std::move(resource));
            }
            if (imported.empty())
                throw std::runtime_error("Resource report is empty");
            resources.ingest(std::move(imported));
            writeProof(true, true);
            return 0;
        }
        ResourceDiscoveryOptions options;
        options.evidenceDirectory = parser.isSet("samples")
                                        ? parser.value("samples")
                                        : QFileInfo(parser.value("evidence")).absolutePath() + "/resource-samples";
        ResourceDiscovery discovery(school, resources, nullptr, options);
        QObject::connect(&discovery, &ResourceDiscovery::progress, &application,
                         [](const QString &message) {
                             QTextStream(stdout) << message << '\n' << Qt::flush;
                         });
        QObject::connect(&discovery, &ResourceDiscovery::finished, &application,
                         [&](int, int verified, int) {
                             const bool passed = !timedOut && verified > 0 && !resources.list().empty();
                             writeProof(passed, false);
                             application.exit(passed ? 0 : 1);
                         });
        QObject::connect(&discovery, &ResourceDiscovery::failed, &application,
                         [&](const QString &reason) {
                             writeProof(false, false, reason);
                             application.exit(1);
                         });
        QTimer::singleShot(600000, &application, [&] {
            timedOut = true;
            discovery.cancel();
        });
        QTimer::singleShot(0, &discovery, &ResourceDiscovery::start);
        return application.exec();
    } catch (const std::exception &error) {
        QTextStream(stderr) << QString::fromUtf8(error.what()) << '\n';
        return 1;
    }
}
