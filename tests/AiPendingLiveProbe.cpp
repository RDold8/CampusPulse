#include "adapters/AiProviderConfig.h"
#include "adapters/AiSearchHistory.h"
#include "adapters/ArtifactWriter.h"
#include "adapters/DeepSeekSearch.h"
#include "adapters/RefreshCoordinator.h"
#include "adapters/UniversityRegistry.h"
#include "desktop/AiSourcesPage.h"
#include "desktop/BrandTheme.h"
#include "storage/Database.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteSourceRepository.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QJsonDocument>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
using namespace campus;
// Explicit live acceptance using the production page; credentials remain in memory.
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("CampusPulseAcceptance");
    app.setApplicationName("AiPendingLiveProbe");
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({"config", "Existing school configuration", "path"});
    cli.addOption({"providers", "Explicit production provider directory", "directory"});
    cli.addOption({"output", "New evidence directory", "directory"});
    cli.addOption({"replay", "Render saved real results without any API request"});
    cli.process(app);
    if (!cli.isSet("config") || !cli.isSet("providers") || !cli.isSet("output") ||
        QDir(cli.value("output")).exists()) return 1;
    try {
        QDir().mkpath(cli.value("output"));
        AiProviderStore store(cli.value("providers"));
        store.load();
        QString key;
        for (const auto &provider : store.providers())
            if (provider.id == store.activeId()) key = store.key(provider.id);
        if (key.isEmpty() && !cli.isSet("replay")) return 2;
        const auto school = SchoolPackage::load(cli.value("config"));
        QTemporaryDir data;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data.path());
        UniversityRegistry registry(data.path());
        Database db(data.filePath("test.sqlite"));
        SqliteRepository noticeRepository(db);
        SqliteSourceRepository sourceRepository(db);
        NoticeService notices(noticeRepository, school.id.toStdString());
        SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
        RefreshCoordinator refresh(school, notices, sources);
        BrandTheme::installApplication(app);
        AiSourcesPage page(school, registry, refresh, nullptr, cli.value("providers"));
        page.resize(1360, 920);
        page.show();
        auto *button = page.findChild<QPushButton *>("aiSearchButton");
        auto *search = page.findChild<DeepSeekSearch *>();
        if (!button || !search) return 3;
        QJsonObject proof{{"real_api_requested", !cli.isSet("replay")}, {"production_page", true},
            {"normal_database_used", false}, {"credentials_logged", false}};
        QObject::connect(search, &DeepSeekSearch::diagnostic, &app,
            [&](const QJsonObject &stats) { proof["search_diagnostic"] = stats; });
        QObject::connect(search, &DeepSeekSearch::finished, &app, [&](QJsonArray, QJsonObject) {
            page.grab().save(cli.value("output") + "/candidates.png");
        });
        bool done = false;
        auto finish = [&](bool timeout) {
            if (done) return;
            done = true;
            proof["timeout"] = timeout;
            proof["report"] = AiSearchHistory::load(cli.value("providers"), school.id);
            page.grab().save(cli.value("output") + "/results.png");
            const auto bytes = QJsonDocument(proof).toJson();
            if (!key.isEmpty() && bytes.contains(key.toUtf8())) { app.exit(5); return; }
            writeArtifact(cli.value("output") + "/result.json", bytes);
            app.exit(timeout ? 4 : 0);
        };
        QTimer monitor;
        QObject::connect(&monitor, &QTimer::timeout, &app, [&] {
            if (!button->isEnabled()) return;
            const auto phase = AiSearchHistory::load(cli.value("providers"), school.id).value("phase").toString();
            if (phase == "completed" || phase == "failed") finish(false);
        });
        QTimer::singleShot(0, &app, [&] {
            if (cli.isSet("replay")) { finish(false); return; }
            button->click(); monitor.start(500);
        });
        QTimer::singleShot(300000, &app, [&] { finish(true); });
        return app.exec();
    } catch (const std::exception &) { return 6; }
}
