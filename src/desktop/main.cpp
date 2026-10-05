#include "desktop/MainWindow.h"
#include "adapters/SchoolPackage.h"
#include "adapters/RefreshCoordinator.h"
#include "adapters/UniversityRegistry.h"
#include "adapters/SchoolOnboarding.h"
#include "adapters/UnknownUniversityDiscovery.h"
#include "desktop/UniversityPage.h"
#include "application/NoticeService.h"
#include "application/SourceService.h"
#include "storage/SqliteRepository.h"
#include "storage/Database.h"
#include "storage/SqliteSourceRepository.h"
#include "storage/SqliteSubscriptionRepository.h"
#include "application/SubscriptionService.h"
#include "application/TaskService.h"
#include "storage/SqliteTaskRepository.h"
#include "storage/SqliteReminderRepository.h"
#include "storage/SqliteResourceRepository.h"
#include "application/ResourceService.h"
#include "adapters/ResourceDiscovery.h"
#include "adapters/ReminderScheduler.h"
#include "desktop/BrandTheme.h"
#include "desktop/DesktopWorkspace.h"
#include "application/NoticeMatcher.h"
#include "domain/Theme.h"
#include "adapters/ArtifactWriter.h"
#include <QCryptographicHash>
#include <QSystemTrayIcon>
#include <QDateTime>
#include <QFileInfo>
#include <QLockFile>
#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QTimer>
#include <QNetworkProxyFactory>
#include <QSettings>
#include <memory>
#include <optional>
#include <QDebug>

using namespace campus;

namespace {
// One active school session; storage is shared while notices and source preferences are scoped.
struct DesktopSession {
    SchoolPackage school;
    NoticeService service;
    SourceService sources;
    SubscriptionService subscriptions;
    TaskService tasks;
    ResourceService resources;
    RefreshCoordinator network;
    ResourceDiscovery resourceDiscovery;
    MainWindow window;
    DesktopSession(const QString &configFile, SqliteRepository &repository,
                   SqliteSourceRepository &sourceRepository,
                   SqliteSubscriptionRepository &subscriptionRepository,
                   SqliteTaskRepository &taskRepository,
                   SqliteResourceRepository &resourceRepository, const UniversityRegistry &registry,
                   bool allowUnregistered = false, ResourceDiscoveryOptions resourceOptions = {})
        : school(registry.loadSessionPackage(configFile, allowUnregistered)), service(repository, school.id.toStdString()),
          sources(school.id.toStdString(), school.catalog, sourceRepository),
          subscriptions(school.id.toStdString(), subscriptionRepository, service),
          tasks(school.id.toStdString(), school.timeZone.toStdString(), taskRepository, service),
          resources(resourceRepository, school.id.toStdString()),
          network(school, service, sources),
          resourceDiscovery(school, resources, nullptr, std::move(resourceOptions)),
          window(school, service, sources, network, registry, subscriptions, tasks,
                 &resources, &resourceDiscovery) {
        sources.recover(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString());
        emit network.sourcesChanged();
    }
};
} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("CampusPulse");
    app.setApplicationName("CampusPulse");
    app.setApplicationVersion("0.1.3");
    const auto defaultConfig =
        QCoreApplication::applicationDirPath() + "/configs/schools/neepu.example.json";
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    QCommandLineParser cli;
    cli.setApplicationDescription("CampusPulse C++桌面原型");
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addOption({"config", "学校配置文件", "path", defaultConfig});
    cli.addOption({"settings-dir", "独立INI设置目录，不与默认用户设置共享", "path"});
    cli.addOption({"database", "本地数据库文件", "path"});
    cli.addOption({"desktop-id", "将应用窗口放到指定Windows虚拟桌面，不切换当前桌面", "guid"});
    cli.addOption({"no-system-notifications", "仅显示应用内提醒，用于隔离桌面测试"});
    cli.addOption({"verify-live", "读取官网、验证缴费详情并保存运行证据后退出"});
    cli.addOption({"verify-school", "通用学校列表和首条正文验证，保存证据后退出"});
    cli.addOption({"verify-detail",
                   "在指定验证数据库中读取标题含关键词的唯一通知正文，保存验证结果", "keyword"});
    cli.addOption({"onboard", "从社区配置或陌生大学官网识别学校并发现公开栏目", "url"});
    cli.addOption({"onboard-output", "自动接入输出目录", "path"});
    cli.addOption({"evidence", "验证结果JSON文件", "path", "live-proof.json"});
    cli.process(app);
    if (cli.isSet("settings-dir")) {
        const auto settingsDirectory = QFileInfo(cli.value("settings-dir")).absoluteFilePath();
        if (cli.value("settings-dir").isEmpty() || !QDir().mkpath(settingsDirectory)) {
            qCritical("Cannot create independent settings directory");
            return 2;
        }
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory);
    }
    BrandTheme::installApplication(app);
    QSettings preferences;
    auto selectedConfig = preferences.value("selectedSchoolConfig", defaultConfig).toString();
    if (!QFileInfo::exists(selectedConfig)) selectedConfig = defaultConfig;
    try {
        const auto dataFolder =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dataFolder);
        const auto onboardingFolder = cli.isSet("onboard-output") ? cli.value("onboard-output")
                                                                  : dataFolder + "/auto-schools";
        const auto identitiesFolder = onboardingFolder + "/identities";
        UniversityRegistry registry(QCoreApplication::applicationDirPath() +
                                    "/configs/schools", identitiesFolder);
        auto startupConfig = cli.isSet("config") ? cli.value("config") : selectedConfig;
        if (!cli.isSet("config")) {
            try {
                registry.loadSessionPackage(startupConfig);
            } catch (const std::exception &error) {
                qWarning() << "已保存的学校配置无法匹配身份，回到默认学校:" << error.what();
                startupConfig = defaultConfig;
            }
        }
        if (cli.isSet("onboard")) {
            auto fail = [&](const QString &reason) {
                QFile out(cli.value("evidence"));
                if (out.open(QIODevice::WriteOnly))
                    out.write(QJsonDocument(QJsonObject{{"passed", false}, {"error", reason},
                                                       {"model_calls", 0}}).toJson());
                qWarning().noquote() << reason;
                app.exit(2);
            };
            auto scanSeed = [&](const QString &seed) {
                auto *scan = new SchoolOnboarding(seed, onboardingFolder, &app);
                QObject::connect(scan, &SchoolOnboarding::progress, &app,
                                 [](const QString &message) { qInfo().noquote() << message; });
                QObject::connect(scan, &SchoolOnboarding::finished, &app,
                                 [&](const QString &file, int sources, int pages) {
                    QFile out(cli.value("evidence"));
                    if (out.open(QIODevice::WriteOnly))
                        out.write(QJsonDocument(QJsonObject{{"passed", true}, {"config_file", file},
                            {"sources", sources}, {"pages", pages}, {"model_calls", 0}}).toJson());
                    app.exit(0);
                });
                QObject::connect(scan, &SchoolOnboarding::failed, &app, fail);
                scan->start();
            };
            QTimer::singleShot(0, &app, [&] {
                try {
                    scanSeed(registry.resolve(cli.value("onboard")).configFile);
                } catch (const std::exception &) {
                    auto *identity = new UnknownUniversityDiscovery(cli.value("onboard"), identitiesFolder, &app);
                    QObject::connect(identity, &UnknownUniversityDiscovery::progress, &app,
                                     [](const QString &message) { qInfo().noquote() << message; });
                    QObject::connect(identity, &UnknownUniversityDiscovery::failed, &app, fail);
                    QObject::connect(identity, &UnknownUniversityDiscovery::finished, &app,
                                     [&](const QString &seed, const QString &) { scanSeed(seed); });
                    identity->start();
                }
            });
            return app.exec();
        }
        const auto dbfile =
            cli.isSet("database") ? cli.value("database") : dataFolder + "/campuspulse.sqlite";
        const QFileInfo databaseFile(dbfile);
        const auto databasePath = databaseFile.exists() ? databaseFile.canonicalFilePath()
                                                        : databaseFile.absoluteFilePath();
        QLockFile sessionLock(databasePath + ".lock");
        sessionLock.setStaleLockTime(0);
        if (!sessionLock.tryLock(0))
            throw std::runtime_error("本地数据库已被其他 CampusPulse 窗口使用，或无法创建运行锁");
        Database database(dbfile);
        SqliteRepository repository(database);
        SqliteSourceRepository sourceRepository(database);
        SqliteSubscriptionRepository subscriptionRepository(database);
        SqliteTaskRepository taskRepository(database);
        SqliteResourceRepository resourceRepository(database);
        ResourceDiscoveryOptions resourceOptions;
        if (cli.isSet("database"))
            resourceOptions.evidenceDirectory = QFileInfo(dbfile).absolutePath() + "/resource-evidence";
        auto session =
            std::make_unique<DesktopSession>(startupConfig, repository, sourceRepository,
                                             subscriptionRepository, taskRepository, resourceRepository,
                                             registry, cli.isSet("config"), resourceOptions);
        auto &school = session->school;
        auto &service = session->service;
        auto &sources = session->sources;
        auto &network = session->network;
        auto &window = session->window;
        placeWindowOnDesktop(window, cli.value("desktop-id"));
        window.show();
        if (cli.isSet("verify-detail")) {
            if (!cli.isSet("database") || cli.value("verify-detail").trimmed().isEmpty())
                throw std::runtime_error("正文验证须指定独立数据库和非空标题关键词");
            std::vector<Notice> candidates;
            for (const auto &notice : service.list())
                if (QString::fromStdString(notice.title).contains(cli.value("verify-detail")))
                    candidates.push_back(notice);
            if (candidates.size() != 1)
                throw std::runtime_error("正文验证须匹配唯一通知；请使用更具体的标题关键词");
            const auto id = candidates.front().id;
            QObject::connect(
                &network, &RefreshCoordinator::detailFinished, &app, [&](const QString &) {
                    for (const auto &notice : service.list())
                        if (notice.id == id) {
                            NoticeQuery payment;
                            payment.yearPolicy = YearPolicy::AllYears;
                            payment.themeKeys = {"payment"};
                            const QJsonObject proof{
                                {"passed", !notice.body.empty()},
                                {"title", QString::fromStdString(notice.title)},
                                {"published_date", QString::fromStdString(notice.publishedDate)},
                                {"url", QString::fromStdString(notice.url)},
                                {"body", QString::fromStdString(notice.body)},
                                {"body_sha256",
                                 QString::fromLatin1(QCryptographicHash::hash(
                                                         QByteArray::fromStdString(notice.body),
                                                         QCryptographicHash::Sha256)
                                                         .toHex())},
                                {"payment_theme_matches",
                                 NoticeMatcher::matches(notice, payment,
                                                        QDate::currentDate().year())},
                                {"model_calls", 0},
                                {"database", dbfile},
                                {"tasks_created", 0}};
                            try {
                                writeArtifact(cli.value("evidence"), QJsonDocument(proof).toJson());
                                app.exit(notice.body.empty() ? 2 : 0);
                            } catch (const std::exception &error) {
                                qCritical() << error.what();
                                app.exit(3);
                            }
                            return;
                        }
                    app.exit(2);
                });
            QObject::connect(&network, &RefreshCoordinator::detailFailed, &app,
                             [&](const QString &, const QString &reason) {
                                 qCritical() << reason;
                                 app.exit(2);
                             });
            QTimer::singleShot(15000, &app, [&] { app.exit(4); });
            QTimer::singleShot(0, &network, [&] { network.loadDetail(candidates.front()); });
            return app.exec();
        }
        if (cli.isSet("verify-live") || cli.isSet("verify-school")) {
            const bool genericVerification = cli.isSet("verify-school");
            QStringList logs;
            QObject::connect(&network, &RefreshCoordinator::message, &app,
                             [&](const QString &m) { logs << m; });
            auto writeProof = [&](bool passed, int successes, int failures) {
                QJsonArray records;
                for (const auto &n : service.list())
                    records.append(
                        QJsonObject{{"id", QString::fromStdString(n.id)},
                                    {"title", QString::fromStdString(n.title)},
                                    {"published_date", QString::fromStdString(n.publishedDate)},
                                    {"url", QString::fromStdString(n.url)},
                                    {"source", QString::fromStdString(n.sourceName)},
                                    {"category", QString::fromStdString(n.category)},
                                    {"body_cached", !n.body.empty()},
                                    {"attachments", static_cast<int>(n.attachments.size())}});
                QJsonArray sourceStates;
                for (const auto &view : sources.list())
                    sourceStates.append(QJsonObject{
                        {"source_id", QString::fromStdString(view.description.id)},
                        {"status", QString::fromStdString(view.effectiveStatus())},
                        {"last_attempt_at", QString::fromStdString(view.state.lastAttemptAt)},
                        {"last_full_success_at", QString::fromStdString(view.state.lastSuccessAt)},
                        {"successful_pages", view.state.successfulPages},
                        {"row_count", view.state.rowCount},
                        {"error", QString::fromStdString(view.state.error)}});
                QJsonObject proof{
                    {"passed", passed},           {"successful_sources", successes},
                    {"failed_sources", failures}, {"notice_count", records.size()},
                    {"database", dbfile},         {"logs", QJsonArray::fromStringList(logs)},
                    {"notices", records},         {"sources", sourceStates}};
                QJsonObject themes;
                for (const auto &theme : Themes) {
                    NoticeQuery query;
                    query.themeKeys = {std::string(theme.key)};
                    int all = 0, current = 0;
                    for (const auto &notice : service.list()) {
                        query.yearPolicy = YearPolicy::AllYears;
                        if (NoticeMatcher::matches(notice, query, QDate::currentDate().year()))
                            ++all;
                        query.yearPolicy = YearPolicy::CurrentYear;
                        if (NoticeMatcher::matches(notice, query, QDate::currentDate().year()))
                            ++current;
                    }
                    themes[QString::fromUtf8(theme.key.data(), int(theme.key.size()))] =
                        QJsonObject{{"all_years", all}, {"current_year", current}};
                }
                proof["themes"] = themes;
                proof["model_calls"] = 0;
                proof["year"] = QDate::currentDate().year();
                QFile output(cli.value("evidence"));
                if (!output.open(QIODevice::WriteOnly)) {
                    app.exit(3);
                    return;
                }
                output.write(QJsonDocument(proof).toJson(QJsonDocument::Indented));
                output.close();
                window.grab().save(cli.value("evidence") + ".png");
                app.exit(passed ? 0 : 2);
            };
            QObject::connect(
                &network, &RefreshCoordinator::finished, &app, [&, writeProof](int good, int bad) {
                    if (bad || good != static_cast<int>(school.sources.size())) {
                        writeProof(false, good, bad);
                        return;
                    }
                    bool found = false;
                    for (const auto &n : service.list())
                        if (genericVerification ||
                            QString::fromStdString(n.title).contains("重修缴费")) {
                            found = true;
                            break;
                        }
                    if (!found) {
                        writeProof(false, good, bad);
                        return;
                    }
                    Notice payment;
                    for (const auto &n : service.list())
                        if (genericVerification ||
                            QString::fromStdString(n.title).contains("重修缴费")) {
                            payment = n;
                            break;
                        }
                    window.selectContaining(QString::fromStdString(payment.title));
                    auto connection = std::make_shared<QMetaObject::Connection>();
                    *connection = QObject::connect(
                        &network, &RefreshCoordinator::detailFinished, &app,
                        [&, writeProof, good, bad, connection](const QString &id) {
                            QObject::disconnect(*connection);
                            bool valid = false;
                            for (const auto &n : service.list())
                                if (QString::fromStdString(n.id) == id)
                                    valid = !n.body.empty() &&
                                            (genericVerification || !n.attachments.empty());
                            QTimer::singleShot(250, &app, [writeProof, valid, good, bad] {
                                writeProof(valid, good, bad);
                            });
                        });
                    QObject::connect(
                        &network, &RefreshCoordinator::detailFailed, &app,
                        [&, writeProof, good, bad](const QString &, const QString &reason) {
                            logs << "正文验证失败：" + reason;
                            writeProof(false, good, bad);
                        },
                        Qt::SingleShotConnection);
                    // Verify the selected record even if the current UI filter hides it.
                    network.loadDetail(payment);
                });
            QTimer::singleShot(90000, &app, [&] {
                QFile output(cli.value("evidence"));
                if (output.open(QIODevice::WriteOnly))
                    output.write(
                        QJsonDocument(QJsonObject{{"passed", false},
                                                  {"error", "运行验证超时"},
                                                  {"logs", QJsonArray::fromStringList(logs)}})
                            .toJson());
                app.exit(4);
            });
            QTimer::singleShot(0, &network, &RefreshCoordinator::refresh);
            // All proof callbacks reference these locals; keep their scope alive through exec().
            return app.exec();
        }
        std::function<void(DesktopSession &)> attachSelection;
        std::function<void(const QString &)> switchSchool;
        bool onboardingBusy = false;
        switchSchool = [&](const QString &configFile) {
            auto next =
                std::make_unique<DesktopSession>(configFile, repository, sourceRepository,
                                                 subscriptionRepository, taskRepository, resourceRepository, registry,
                                                 false, resourceOptions);
            next->window.setGeometry(session->window.geometry());
            placeWindowOnDesktop(next->window, cli.value("desktop-id"));
            next->window.show();
            session.swap(next);
            attachSelection(*session);
            preferences.setValue("selectedSchoolConfig", configFile);
            // A homepage-only onboarding includes practical resources. Run the
            // existing bounded collector after notices, without requiring the
            // user to open a second tab or start a separate crawl manually.
            if (session->resources.list().empty())
                QObject::connect(&session->network, &RefreshCoordinator::finished,
                    &session->resourceDiscovery,
                    [scanner = &session->resourceDiscovery](int, int) { scanner->start(); },
                    Qt::SingleShotConnection);
            QTimer::singleShot(100, &session->network, &RefreshCoordinator::refresh);
        };
        attachSelection = [&](DesktopSession &current) {
            QObject::connect(&current.window, &MainWindow::universityHomepageRequested, &app,
                             [&](const QString &homepage) {
                if (session->network.busy() || session->resourceDiscovery.busy() || onboardingBusy)
                    return;
                auto *page = session->window.findChild<UniversityPage *>("universityPage");
                auto *identity = new UnknownUniversityDiscovery(homepage, identitiesFolder, &app);
                onboardingBusy = true;
                page->setBusy(true);
                QObject::connect(identity, &UnknownUniversityDiscovery::progress, page,
                                 &UniversityPage::setFeedback);
                QObject::connect(identity, &UnknownUniversityDiscovery::failed, page,
                                 [&, page, identity](const QString &reason) {
                    onboardingBusy = false;
                    page->setBusy(false);
                    page->setFeedback(reason);
                    identity->deleteLater();
                });
                QObject::connect(identity, &UnknownUniversityDiscovery::finished, &app,
                                 [&, identity](const QString &seed, const QString &name) {
                    onboardingBusy = false;
                    auto *page = session->window.findChild<UniversityPage *>("universityPage");
                    // Keep the animation running through identity -> column discovery.
                    try {
                        registry.addLocalDiscoveredPackage(seed);
                        page->setFeedback("已自动识别：" + name + "（待核验）。正在发现公开栏目……");
                        emit session->window.universitySelected(seed);
                    } catch (const std::exception &error) {
                        page->setBusy(false);
                        page->setFeedback(QString::fromUtf8(error.what()));
                    }
                    identity->deleteLater();
                });
                identity->start();
            });
            QObject::connect(
                &current.window, &MainWindow::universitySelected, &app,
                [&](const QString &configFile) {
                    if (onboardingBusy)
                        return;
                    if (session->network.busy() || session->resourceDiscovery.busy()) {
                        auto *page = session->window.findChild<UniversityPage *>("universityPage");
                        page->setBusy(false);
                        page->setFeedback("当前采集仍在运行，请在完成后重新接入学校。");
                        return;
                    }
                    onboardingBusy = true;
                    session->window.findChild<UniversityPage *>("universityPage")->setBusy(true);
                    // Lock the originating page before queuing its replacement.
                    QTimer::singleShot(0, &app, [&, configFile] {
                        try {
                            const auto school = SchoolPackage::load(configFile);
                            if (school.discoveryEntries.empty()) {
                                switchSchool(configFile);
                                onboardingBusy = false;
                                return;
                            }
                            const auto cachedConfig =
                                QDir(onboardingFolder).filePath(school.id + "/school.json");
                            QFile cachedFile(cachedConfig);
                            const bool currentCache =
                                cachedFile.open(QIODevice::ReadOnly) &&
                                QJsonDocument::fromJson(cachedFile.readAll())
                                        .object()
                                        .value("onboarding_version")
                                        .toInt() == SchoolOnboarding::AlgorithmVersion;
                            cachedFile.close();
                            if (currentCache) {
                                std::optional<SchoolPackage> cached;
                                try {
                                    cached = SchoolPackage::load(cachedConfig);
                                } catch (const std::exception &e) {
                                    qWarning() << "自动接入缓存损坏，重新扫描:" << e.what();
                                }
                                if (cached) {
                                    if (cached->id != school.id)
                                        throw std::runtime_error("自动接入缓存的学校身份不一致");
                                    auto expectedHost = school.officialHomepage.host();
                                    auto cachedHost = cached->officialHomepage.host();
                                    if (expectedHost.startsWith("www.")) expectedHost.remove(0, 4);
                                    if (cachedHost.startsWith("www.")) cachedHost.remove(0, 4);
                                    if (expectedHost != cachedHost ||
                                        cached->automaticallyIdentified != school.automaticallyIdentified)
                                        throw std::runtime_error("自动接入缓存的官网域或自动身份标记不一致，请重新扫描");
                                    switchSchool(cachedConfig);
                                    onboardingBusy = false;
                                    return;
                                }
                            }
                            auto *page =
                                session->window.findChild<UniversityPage *>("universityPage");
                            auto *scan = new SchoolOnboarding(configFile, onboardingFolder, &app);
                            onboardingBusy = true;
                            page->setBusy(true);
                            QObject::connect(scan, &SchoolOnboarding::progress, page,
                                             &UniversityPage::setFeedback);
                            QObject::connect(scan, &SchoolOnboarding::failed, page,
                                             [&, page, scan](const QString &error) {
                                                 onboardingBusy = false;
                                                 page->setBusy(false);
                                                 page->setFeedback(error);
                                                 scan->deleteLater();
                                             });
                            QObject::connect(
                                scan, &SchoolOnboarding::finished, &app,
                                [&, scan](const QString &generated, int, int) {
                                    const auto verified = scan->verifiedNotices();
                                    QTimer::singleShot(0, &app, [&, generated, scan, verified] {
                                        try {
                                            switchSchool(generated);
                                            session->service.ingest(verified);
                                            for (const auto &notice : verified)
                                                session->service.saveDetail(notice);
                                            session->window.reload();
                                        } catch (const std::exception &e) {
                                            auto *currentPage =
                                                session->window.findChild<UniversityPage *>(
                                                    "universityPage");
                                            currentPage->setBusy(false);
                                            QMessageBox::warning(&session->window, "自动接入失败",
                                                                 QString::fromUtf8(e.what()));
                                        }
                                        onboardingBusy = false;
                                        scan->deleteLater();
                                    });
                                });
                            scan->start();
                        } catch (const std::exception &e) {
                            onboardingBusy = false;
                            auto *page = session->window.findChild<UniversityPage *>("universityPage");
                            page->setBusy(false);
                            page->setFeedback(QString::fromUtf8(e.what()));
                            QMessageBox::warning(&session->window, "学校加载失败",
                                                 QString::fromUtf8(e.what()));
                        }
                    });
                });
        };
        attachSelection(*session);
        SqliteReminderRepository reminderRepository(database.connection());
        QSystemTrayIcon tray(BrandTheme::applicationIcon());
        tray.setToolTip("CampusPulse · 应用运行时检查待办提醒");
        const bool nativeMessages = !cli.isSet("no-system-notifications") &&
                                    QSystemTrayIcon::isSystemTrayAvailable() &&
                                    QSystemTrayIcon::supportsMessages();
        if (nativeMessages)
            tray.show();
        ReminderScheduler reminders(
            taskRepository, reminderRepository, [&](const PersonalTask &task) {
                session->window.showReminder(QString::fromStdString(task.title));
                if (nativeMessages)
                    tray.showMessage("CampusPulse 待办提醒", QString::fromStdString(task.title),
                                     BrandTheme::applicationIcon(), 15000);
            });
        QObject::connect(&reminders, &ReminderScheduler::failed, &app, [&](const QString &reason) {
            session->window.showReminder("提醒失败：" + reason);
        });
        reminders.start();
        if (service.list().empty())
            QTimer::singleShot(100, &network, &RefreshCoordinator::refresh);
        return app.exec();
    } catch (const std::exception &e) {
        if (cli.isSet("desktop-id") || cli.isSet("onboard") || cli.isSet("verify-live") ||
            cli.isSet("verify-school") || cli.isSet("verify-detail"))
            qCritical() << "CampusPulse 启动失败:" << e.what();
        else
            QMessageBox::critical(nullptr, "CampusPulse 启动失败", QString::fromUtf8(e.what()));
        return 1;
    }
}
