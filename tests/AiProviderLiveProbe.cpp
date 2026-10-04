#include "adapters/AiProviderProbe.h"
#include "adapters/ArtifactWriter.h"
#include "adapters/DeepSeekSearch.h"
#include "adapters/SchoolOnboarding.h"
#include "adapters/SchoolPackage.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>
using namespace campus;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({"config", "School configuration", "path"});
    cli.addOption({"evidence", "Result JSON (no credentials)", "path"});
    cli.addOption({"model", "Official model", "id", "deepseek-flash"});
    cli.addOption({"search-only", "Explicit search diagnostic, skip models and connection"});
    cli.process(app);
    if (!cli.isSet("config") || !cli.isSet("evidence")) return 1;
    const auto key = QString::fromUtf8(qgetenv("DEEPSEEK_API_KEY"));
    if (key.isEmpty()) return 2;
    try {
        const auto school = SchoolPackage::load(cli.value("config"));
        auto profile = AiProviderConfig::deepSeekPreset();
        profile.model = cli.value("model");
        auto root = school.officialHomepage.host();
        if (root.startsWith("www.")) root.remove(0, 4);
        QJsonObject proof{{"started_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {"requested_model", profile.model}, {"school_id", school.id},
            {"real_api", true}, {"contains_key", false}};
        auto finish = [&](bool success) {
            proof["passed"] = success;
            proof["finished_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            writeArtifact(cli.value("evidence"), QJsonDocument(proof).toJson());
            app.exit(success ? 0 : 3);
        };
        AiProviderProbe probe;
        DeepSeekSearch search;
        QObject::connect(&search, &DeepSeekSearch::diagnostic, &app,
                         [&](const QJsonObject &metadata) { proof["search_response"] = metadata; });
        QObject::connect(&probe, &AiProviderProbe::finished, &app, [&](const AiProbeResult &result) {
            QJsonObject record{{"success", result.success}, {"http_status", result.httpStatus},
                {"elapsed_ms", result.elapsedMs}, {"message", result.message},
                {"response_model", result.responseModel}, {"usage", result.usage}};
            if (result.operation == AiProbeOperation::Models) {
                record["models"] = QJsonArray::fromStringList(result.modelIds);
                proof["models"] = record;
                if (!result.success) { finish(false); return; }
                QTimer::singleShot(0, &app, [&] { probe.probe(profile, key); });
            } else {
                proof["connection"] = record;
                if (!result.success) { finish(false); return; }
                QSet<QString> existing;
                for (const auto &source : school.catalog)
                    existing.insert(QString::fromStdString(source.entryUrl));
                QTimer::singleShot(0, &app, [&, existing] { search.search(profile, key, school.name, root, existing); });
            }
        });
        QObject::connect(&search, &DeepSeekSearch::failed, &app, [&](const QString &message) {
            proof["search"] = QJsonObject{{"success", false}, {"message", message}};
            finish(false);
        });
        QObject::connect(&search, &DeepSeekSearch::finished, &app,
            [&](const QJsonArray &hits, const QJsonObject &usage) {
            proof["search"] = QJsonObject{{"success", true}, {"candidate_count", hits.size()},
                {"candidates", hits}, {"usage", usage}, {"status", "candidate_only"}};
            if (hits.isEmpty()) { finish(true); return; }
            QFile source(school.configFile);
            if (!source.open(QIODevice::ReadOnly)) { finish(false); return; }
            auto seed = QJsonDocument::fromJson(source.readAll()).object();
            seed["auto_discovery"] = QJsonObject{{"department_urls", QJsonArray{}}, {"max_pages", 24}};
            const auto folder = QFileInfo(cli.value("evidence")).absolutePath() + "/ai-live-onboarding";
            QDir().mkpath(folder);
            const auto seedPath = folder + "/seed.json";
            writeArtifact(seedPath, QJsonDocument(seed).toJson());
            QStringList urls;
            for (const auto &hit : hits) urls << hit.toObject().value("url").toString();
            auto *scan = new SchoolOnboarding(seedPath, folder, &app, 3000, urls);
            QObject::connect(scan, &SchoolOnboarding::failed, &app, [&](const QString &message) {
                proof["candidate_validation"] = QJsonObject{{"success", false}, {"message", message}};
                // A real tool response can succeed while none of the candidate sections are valid.
                finish(true);
            });
            QObject::connect(scan, &SchoolOnboarding::finished, &app,
                [&](const QString &config, int count, int rows) {
                proof["candidate_validation"] = QJsonObject{{"success", true}, {"config", config},
                    {"verified_sources", count}, {"list_rows", rows}, {"model_calls", 0}};
                finish(true);
            });
            scan->start();
        });
        QTimer::singleShot(300000, &app, [&] { proof["timeout"] = true; finish(false); });
        QTimer::singleShot(0, &app, [&] {
            if (cli.isSet("search-only")) {
                proof["connection_test_skipped"] = true;
                QSet<QString> existing;
                for (const auto &source : school.catalog)
                    existing.insert(QString::fromStdString(source.entryUrl));
                search.search(profile, key, school.name, root, existing);
            } else probe.fetchModels(profile, key);
        });
        return app.exec();
    } catch (const std::exception &) { return 4; }
}
