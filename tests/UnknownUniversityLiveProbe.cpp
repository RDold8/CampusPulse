#include "adapters/ArtifactWriter.h"
#include "adapters/SchoolOnboarding.h"
#include "adapters/SchoolPackage.h"
#include "adapters/UnknownUniversityDiscovery.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>
#include <stdexcept>

using namespace campus;

// Explicit opt-in probe, never a network-running CTest. Uses isolated output
// files, no user database, no desktop window, and no model/API request.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("CampusPulse unknown university live probe");
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({"url", "Previously unconfigured school homepage", "url"});
    cli.addOption({"output", "Isolated seed and onboarding output directory", "directory"});
    cli.addOption({"evidence", "JSON evidence output file", "file"});
    cli.process(app);
    if (!cli.isSet("url") || !cli.isSet("output") || !cli.isSet("evidence"))
        cli.showHelp(2);
    const auto output = QFileInfo(cli.value("output")).absoluteFilePath();
    const auto evidenceFile = QFileInfo(cli.value("evidence")).absoluteFilePath();
    QJsonObject evidence{{"requested_url", cli.value("url")},
                          {"model_calls", 0}, {"passed", false},
                          {"started_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                          {"scope", "Unconfigured homepage identity plus bounded public-column discovery; no resource scan or user database"}};
    bool ended = false;
    QString seedFile, generatedFile, reportFile;
    QTimer timeout;
    timeout.setSingleShot(true);
    const auto finish = [&](int code, const QString &error) {
        if (ended)
            return;
        ended = true;
        timeout.stop();
        evidence["finished_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        evidence["error"] = error;
        evidence["seed_file"] = seedFile;
        evidence["generated_config"] = generatedFile;
        evidence["onboarding_report"] = reportFile;
        QFile report(reportFile);
        if (!reportFile.isEmpty() && report.open(QIODevice::ReadOnly))
            evidence["scan"] = QJsonDocument::fromJson(report.readAll()).object();
        try {
            if (!QDir().mkpath(QFileInfo(evidenceFile).absolutePath()))
                throw std::runtime_error("Cannot create evidence output directory");
            writeArtifact(evidenceFile, QJsonDocument(evidence).toJson(QJsonDocument::Indented));
        } catch (const std::exception &failure) {
            qCritical("Cannot write live-probe evidence: %s", failure.what());
            code = 3;
        }
        app.exit(code);
    };
    QObject::connect(&timeout, &QTimer::timeout, &app,
                     [&] { finish(2, "Live probe exceeded its 300 second bound"); });
    auto *discovery = new UnknownUniversityDiscovery(cli.value("url"), output + "/identities", &app);
    QObject::connect(discovery, &UnknownUniversityDiscovery::failed, &app,
                     [&](const QString &error) { finish(1, error); });
    QObject::connect(discovery, &UnknownUniversityDiscovery::finished, &app,
                     [&](const QString &file, const QString &name) {
        seedFile = file;
        evidence["school_name"] = name;
        evidence["identity_provenance"] = "automatic_homepage";
        try {
            const auto school = SchoolPackage::load(file);
            evidence["school_id"] = school.id;
            reportFile = output + "/" + school.id + "/report.json";
            auto *scan = new SchoolOnboarding(file, output, &app, 3000);
            QObject::connect(scan, &SchoolOnboarding::failed, &app,
                             [&](const QString &error) { finish(1, error); });
            QObject::connect(scan, &SchoolOnboarding::finished, &app,
                             [&](const QString &config, int sources, int rows) {
                generatedFile = config;
                evidence["ready_sources"] = sources;
                evidence["cached_rows"] = rows;
                evidence["passed"] = sources > 0;
                finish(sources > 0 ? 0 : 1, {});
            });
            scan->start();
        } catch (const std::exception &failure) {
            finish(1, QString::fromUtf8(failure.what()));
        }
    });
    timeout.start(300000);
    QTimer::singleShot(0, discovery, &UnknownUniversityDiscovery::start);
    return app.exec();
}
