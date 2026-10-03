#include "adapters/RefreshCoordinator.h"
#include "adapters/ResourceDiscovery.h"
#include "adapters/UniversityRegistry.h"
#include "application/SubscriptionService.h"
#include "application/TaskService.h"
#include "desktop/AiSourcesPage.h"
#include "desktop/MainWindow.h"
#include "desktop/ResourcePage.h"
#include "storage/Database.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteResourceRepository.h"
#include "storage/SqliteSourceRepository.h"
#include "storage/SqliteSubscriptionRepository.h"
#include "storage/SqliteTaskRepository.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QtTest>

using namespace campus;
namespace {
template <class T> T *control(QWidget &widget, const char *name) {
    auto *result = widget.findChild<T *>(name);
    if (!result)
        throw std::runtime_error(std::string("学校资源控件缺失：") + name);
    return result;
}
SchoolPackage schoolPackage() {
    SchoolPackage school;
    school.id = "cn-neepu";
    school.name = "东北电力大学";
    school.officialHomepage = QUrl("https://www.neepu.edu.cn/");
    school.discoveryEntries = {school.officialHomepage.toString()};
    return school;
}
ResourceDiscoveryOptions discoveryOptions(const QString &folder) {
    ResourceDiscoveryOptions options;
    options.evidenceDirectory = folder;
    options.maxPages = 1;
    options.requestIntervalMs = 300000;
    options.transferTimeoutMs = 1000;
    return options;
}
struct Session {
    SchoolPackage school = schoolPackage();
    Database db;
    SqliteResourceRepository repository;
    ResourceService resources;
    ResourceDiscovery discovery;
    Session(const QString &filename, const QString &evidence)
        : db(filename), repository(db), resources(repository, school.id.toStdString()),
          discovery(school, resources, nullptr, discoveryOptions(evidence)) {}
};
SchoolResource resource(const std::string &id, const std::string &title,
                        const std::string &category, const std::vector<std::string> &audiences) {
    SchoolResource result;
    result.id = id;
    result.schoolId = "cn-neepu";
    result.title = title;
    result.url = "https://library.neepu.edu.cn/" + id;
    result.description = "【测试数据】学习与办事实用入口";
    result.category = category;
    result.audiences = audiences;
    result.provider = "学校图书馆";
    result.discoveredFrom = "https://www.neepu.edu.cn/resources.htm";
    result.lastCheckedAt = "2026-10-03T03:15:00Z";
    result.status = "verified";
    return result;
}
void choose(QComboBox *combo, const QString &key) {
    const int index = combo->findData(key);
    if (index < 0)
        throw std::runtime_error("学校资源筛选键不存在");
    combo->setCurrentIndex(index);
}
void selectResource(ResourcePage &page, const QString &id) {
    auto *table = control<QTableView>(page, "resourceTable");
    for (int row = 0; row < table->model()->rowCount(); ++row)
        if (table->model()->index(row, 0).data(Qt::UserRole).toString() == id) {
            table->setCurrentIndex(table->model()->index(row, 0));
            return;
        }
    throw std::runtime_error("学校资源列表中不存在指定资源");
}
QStringList visibleIds(ResourcePage &page) {
    QStringList ids;
    auto *table = control<QTableView>(page, "resourceTable");
    for (int row = 0; row < table->model()->rowCount(); ++row)
        ids << table->model()->index(row, 0).data(Qt::UserRole).toString();
    return ids;
}
} // namespace
class ResourceUrlCapture final : public QObject {
    Q_OBJECT
  public:
    QList<QUrl> requests;
  public slots:
    void capture(const QUrl &url) {
        requests << url;
    }
};
namespace {
struct BrowserGuard {
    ResourceUrlCapture capture;
    BrowserGuard() {
        QDesktopServices::setUrlHandler("https", &capture, "capture");
        QDesktopServices::setUrlHandler("http", &capture, "capture");
    }
    ~BrowserGuard() {
        QDesktopServices::unsetUrlHandler("https");
        QDesktopServices::unsetUrlHandler("http");
    }
};
} // namespace
class ResourceDesktopTests final : public QObject {
    Q_OBJECT
  private slots:
    void cachedResourcesAppearImmediatelyWithAllStagesByDefault() {
        QTemporaryDir folder;
        Session s(folder.filePath("cache.sqlite"), folder.filePath("evidence"));
        s.resources.ingest(
            {resource("undergrad", "【测试】本科课程资料", "course_material", {"undergraduate"}),
             resource("graduate", "【测试】研究生学术支持", "academic_support", {"postgraduate"}),
             resource("unknown", "【测试】适用阶段待核实", "library", {})});
        QSignalSpy started(&s.discovery, &ResourceDiscovery::started);
        ResourcePage page(s.school, s.resources, s.discovery);
        QCOMPARE(visibleIds(page).size(), 3);
        QCOMPARE(control<QComboBox>(page, "resourceStageFilter")->currentData().toString(),
                 QString{});
        QVERIFY(control<QLabel>(page, "resourceSummary")->text().contains("已缓存 3 项"));
        QVERIFY(control<QPushButton>(page, "openResourceButton")->isEnabled());
        page.activation();
        page.activation();
        QApplication::processEvents();
        QCOMPARE(started.count(), 0);
        QVERIFY(!s.discovery.busy());
    }
    void combinedFiltersKeepUnknownSeparateFromGeneral() {
        QTemporaryDir folder;
        Session s(folder.filePath("filters.sqlite"), folder.filePath("evidence"));
        auto common = resource("general", "【测试】通用文献检索", "library", {"general"});
        common.description = "可检索文献和电子书";
        auto graduate = resource("graduate", "【测试】研究生文献检索", "library", {"postgraduate"});
        auto unknown = resource("unknown", "【测试】文献入口阶段待核实", "library", {});
        auto competition =
            resource("competition", "【测试】竞赛文献入口", "competition", {"undergraduate"});
        s.resources.ingest({common, graduate, unknown, competition});
        s.resources.setFavorite("general", true);
        s.resources.setFavorite("competition", true);
        ResourcePage page(s.school, s.resources, s.discovery);
        choose(control<QComboBox>(page, "resourceCategoryFilter"), "library");
        choose(control<QComboBox>(page, "resourceStageFilter"), "undergraduate");
        control<QLineEdit>(page, "resourceSearch")->setText("文献");
        control<QCheckBox>(page, "resourceFavoritesOnly")->setChecked(true);
        QCOMPARE(visibleIds(page), QStringList{"general"});
        control<QCheckBox>(page, "resourceFavoritesOnly")->setChecked(false);
        choose(control<QComboBox>(page, "resourceStageFilter"), "postgraduate");
        QCOMPARE(visibleIds(page).size(), 2);
        QVERIFY(visibleIds(page).contains("general"));
        QVERIFY(visibleIds(page).contains("graduate"));
        choose(control<QComboBox>(page, "resourceStageFilter"), "unknown");
        QCOMPARE(visibleIds(page), QStringList{"unknown"});
        choose(control<QComboBox>(page, "resourceStageFilter"), "general");
        QCOMPARE(visibleIds(page), QStringList{"general"});
        control<QLineEdit>(page, "resourceSearch")->setText("不存在的关键词");
        QVERIFY(visibleIds(page).isEmpty());
        QVERIFY(!control<QPushButton>(page, "openResourceButton")->isEnabled());
        QVERIFY(!control<QPushButton>(page, "favoriteResourceButton")->isEnabled());
    }
    void favoriteSurvivesMetadataRefreshAndDatabaseRestart() {
        QTemporaryDir folder;
        const auto filename = folder.filePath("favorites.sqlite");
        {
            Session s(filename, folder.filePath("evidence"));
            s.resources.ingest({resource("stable", "【测试】旧资源名称", "library", {"general"}),
                                resource("other", "【测试】其他资源", "competition", {})});
            ResourcePage page(s.school, s.resources, s.discovery);
            selectResource(page, "stable");
            control<QPushButton>(page, "favoriteResourceButton")->click();
            auto refreshed = resource("stable", "【测试】更新后的资源名称", "library", {"general"});
            refreshed.lastCheckedAt = "2026-10-04T03:15:00Z";
            s.resources.ingest({refreshed});
            page.reload();
            page.reload();
            auto *table = control<QTableView>(page, "resourceTable");
            QCOMPARE(table->currentIndex().data(Qt::UserRole).toString(), QString("stable"));
            QCOMPARE(control<QPushButton>(page, "favoriteResourceButton")->text(),
                     QString("取消收藏"));
            control<QCheckBox>(page, "resourceFavoritesOnly")->setChecked(true);
            QCOMPARE(visibleIds(page), QStringList{"stable"});
            QVERIFY(table->model()->index(0, 0).data().toString().contains("更新后的资源名称"));
        }
        {
            Session restarted(filename, folder.filePath("evidence"));
            ResourcePage page(restarted.school, restarted.resources, restarted.discovery);
            control<QCheckBox>(page, "resourceFavoritesOnly")->setChecked(true);
            QCOMPARE(visibleIds(page), QStringList{"stable"});
            control<QPushButton>(page, "favoriteResourceButton")->click();
            QVERIFY(visibleIds(page).isEmpty());
            QVERIFY(restarted.resources.list(ResourceQuery{.onlyFavorites = true}).empty());
        }
    }
    void officialAndRecommendedLinksUseHandlerWhileUnsafeCacheCannotOpen() {
        QTemporaryDir folder;
        Session s(folder.filePath("links.sqlite"), folder.filePath("evidence"));
        BrowserGuard browser;
        auto official = resource("official", "【测试】学校图书馆", "library", {"general"});
        official.status = "login_required";
        auto recommended = resource("external", "【测试】官网推荐外部课程", "course_material", {});
        recommended.url = "https://courses.example.org/catalog";
        recommended.linkKind = "official_recommended";
        recommended.status = "discovered";
        s.resources.ingest({official, recommended});
        // A historical/corrupt cache must not bypass the page's URL and provenance gate.
        auto spoofed = recommended;
        spoofed.id = "spoofed";
        spoofed.discoveredFrom = "https://www.neepu.edu.cn.evil.example/resources";
        auto unsafe = official;
        unsafe.id = "unsafe";
        unsafe.url = "javascript:alert(1)";
        auto emptyUser = official;
        emptyUser.id = "empty-user";
        emptyUser.url = "https://@library.neepu.edu.cn/";
        auto normalizedOrigin = recommended;
        normalizedOrigin.id = "empty-origin-user";
        normalizedOrigin.discoveredFrom = "https://@www.neepu.edu.cn/resources";
        s.repository.upsert(s.school.id.toStdString(),
                            {spoofed, unsafe, emptyUser, normalizedOrigin});
        ResourcePage page(s.school, s.resources, s.discovery);
        selectResource(page, "official");
        auto *detail = control<QTextBrowser>(page, "resourceDetail");
        QVERIFY(detail->toPlainText().contains("浏览器登录不会自动连通采集"));
        QVERIFY(!detail->openLinks());
        QVERIFY(!detail->openExternalLinks());
        control<QPushButton>(page, "openResourceButton")->click();
        QCOMPARE(browser.capture.requests, QList<QUrl>{QUrl(QString::fromStdString(official.url))});
        selectResource(page, "external");
        QVERIFY(detail->toPlainText().contains("学校官网推荐的外部资源，尚未验证"));
        control<QPushButton>(page, "openResourceButton")->click();
        QCOMPARE(browser.capture.requests.size(), 2);
        QCOMPARE(browser.capture.requests.back(), QUrl(QString::fromStdString(recommended.url)));
        for (const auto *id : {"spoofed", "unsafe", "empty-user", "empty-origin-user"}) {
            selectResource(page, id);
            QVERIFY(!control<QPushButton>(page, "openResourceButton")->isEnabled());
            control<QPushButton>(page, "openResourceButton")->click();
            QCOMPARE(browser.capture.requests.size(), 2);
        }
        selectResource(page, "external");
        QVERIFY(QMetaObject::invokeMethod(detail, "anchorClicked", Qt::DirectConnection,
                                          Q_ARG(QUrl, QUrl("https://evil.example/"))));
        QCOMPARE(browser.capture.requests.size(), 2);
        QVERIFY(
            control<QLabel>(page, "resourceStatus")->text().contains("尚未通过学校官方出处校验"));
        QVERIFY(QMetaObject::invokeMethod(
            detail, "anchorClicked", Qt::DirectConnection,
            Q_ARG(QUrl, QUrl(QString::fromStdString(recommended.discoveredFrom)))));
        QCOMPARE(browser.capture.requests.size(), 3);
    }
    void reviewedUsageEvidenceIsSeparateFromPublicEntranceAndSafeToOpen() {
        QTemporaryDir folder;
        Session s(folder.filePath("usage.sqlite"), folder.filePath("evidence"));
        BrowserGuard browser;
        auto checked = resource("reviewed", "【测试】期刊入口", "library", {"general"});
        checked.linkKind = "official_recommended";
        checked.url = "https://journals.example.org/";
        checked.accessNote = "学校2023年列出采购部分期刊；当前权限未知。<script>文本</script>";
        checked.accessEvidence = "https://lib.neepu.edu.cn/info/1/2.htm";
        s.resources.ingest({checked});
        ResourcePage page(s.school, s.resources, s.discovery);
        selectResource(page, "reviewed");
        auto *table = control<QTableView>(page, "resourceTable");
        auto *detail = control<QTextBrowser>(page, "resourceDetail");
        QCOMPARE(table->currentIndex().siblingAtColumn(3).data().toString(),
                 QString("外部官网推荐 · 入口可访问"));
        QVERIFY(detail->toPlainText().contains(QString::fromStdString(checked.accessNote)));
        QVERIFY(detail->toPlainText().contains("不代表个人已获得全文权限"));
        QVERIFY(detail->toHtml().contains("&lt;script&gt;"));
        QVERIFY(QMetaObject::invokeMethod(detail, "anchorClicked", Qt::DirectConnection,
                  Q_ARG(QUrl, QUrl(QString::fromStdString(checked.accessEvidence)))));
        QCOMPARE(browser.capture.requests.size(), 1);
        checked.accessEvidence = "https://evil.example/usage";
        s.resources.ingest({checked});
        control<QLineEdit>(page, "resourceSearch")->setText("采购部分期刊");
        QCOMPARE(visibleIds(page), QStringList{"reviewed"});
        QVERIFY(detail->toPlainText().contains("学校官方出处待核实"));
        QVERIFY(QMetaObject::invokeMethod(detail, "anchorClicked", Qt::DirectConnection,
                                         Q_ARG(QUrl, QUrl(checked.accessEvidence.c_str()))));
        QCOMPARE(browser.capture.requests.size(), 1);
        checked.status = "login_required";
        s.resources.ingest({checked});
        control<QLineEdit>(page, "resourceSearch")->clear();
        QVERIFY(detail->toPlainText().contains("按学校或提供方说明在官方页面登录"));
        QVERIFY(!detail->toPlainText().contains("使用学号"));
    }
    void firstEmptyActivationStartsOnceAndCancelKeepsPageResponsive() {
        QTemporaryDir folder;
        Session s(folder.filePath("discovery.sqlite"), folder.filePath("evidence"));
        ResourcePage page(s.school, s.resources, s.discovery);
        QSignalSpy started(&s.discovery, &ResourceDiscovery::started);
        QSignalSpy finished(&s.discovery, &ResourceDiscovery::finished);
        page.activation();
        page.activation();
        QCOMPARE(started.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 1000);
        QVERIFY(s.discovery.busy());
        QVERIFY(!control<QPushButton>(page, "discoverResourcesButton")->isEnabled());
        QVERIFY(control<QPushButton>(page, "cancelResourceDiscoveryButton")->isEnabled());
        control<QLineEdit>(page, "resourceSearch")->setText("可响应的筛选");
        choose(control<QComboBox>(page, "resourceStageFilter"), "unknown");
        QCOMPARE(control<QLineEdit>(page, "resourceSearch")->text(), QString("可响应的筛选"));
        control<QPushButton>(page, "cancelResourceDiscoveryButton")->click();
        QVERIFY(!s.discovery.busy());
        QCOMPARE(finished.count(), 1);
        QVERIFY(control<QPushButton>(page, "discoverResourcesButton")->isEnabled());
        QVERIFY(!control<QPushButton>(page, "cancelResourceDiscoveryButton")->isEnabled());
        QVERIFY(control<QLabel>(page, "resourceStatus")->text().contains("已取消"));
        page.activation();
        QApplication::processEvents();
        QCOMPARE(started.count(), 1);
        control<QPushButton>(page, "discoverResourcesButton")->click();
        QCOMPARE(started.count(), 2);
        control<QPushButton>(page, "cancelResourceDiscoveryButton")->click();
        QCOMPARE(finished.count(), 2);
        QVERIFY(s.resources.list().empty());
    }
    void repeatedDiscoveryChangesAllowFilteringAndCancelShowsLatestCache() {
        QTemporaryDir folder;
        Session s(folder.filePath("changes.sqlite"), folder.filePath("evidence"));
        s.resources.ingest({resource("saved", "【测试】原有通用文献检索", "library", {"general"})});
        s.resources.setFavorite("saved", true);
        ResourcePage page(s.school, s.resources, s.discovery);
        // Exercise the actual start/cancel lifecycle; cache notifications announce real DB writes.
        control<QPushButton>(page, "discoverResourcesButton")->click();
        QVERIFY(s.discovery.busy());
        s.resources.ingest({resource("new-a", "【测试】新文献入口甲", "library", {})});
        QVERIFY(QMetaObject::invokeMethod(&s.discovery, "changed", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(&s.discovery, "changed", Qt::DirectConnection));
        control<QLineEdit>(page, "resourceSearch")->setText("文献");
        choose(control<QComboBox>(page, "resourceStageFilter"), "unknown");
        QCOMPARE(visibleIds(page), QStringList{"new-a"});
        QVERIFY(control<QPushButton>(page, "cancelResourceDiscoveryButton")->isEnabled());
        s.resources.ingest({resource("new-b", "【测试】新文献入口乙", "library", {})});
        QVERIFY(QMetaObject::invokeMethod(&s.discovery, "changed", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(&s.discovery, "changed", Qt::DirectConnection));
        control<QPushButton>(page, "cancelResourceDiscoveryButton")->click();
        QVERIFY(!s.discovery.busy());
        QCOMPARE(visibleIds(page).size(), 2);
        QVERIFY(visibleIds(page).contains("new-a"));
        QVERIFY(visibleIds(page).contains("new-b"));
        QVERIFY(control<QLabel>(page, "resourceSummary")->text().contains("已缓存 3 项"));
        QVERIFY(control<QLabel>(page, "resourceStatus")->text().contains("已取消"));
        QTest::qWait(150);
        QCOMPARE(visibleIds(page).size(), 2);
        QVERIFY(control<QLabel>(page, "resourceStatus")->text().contains("已取消"));
        QCOMPARE(s.resources.list(ResourceQuery{.onlyFavorites = true}).size(), std::size_t(1));
    }
    void mainWindowDefersAiConfigUntilResourceDiscoveryIsIdle() {
        QTemporaryDir folder;
        Session s(folder.filePath("integrated.sqlite"), folder.filePath("evidence"));
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        SqliteRepository noticeRepository(s.db);
        SqliteSourceRepository sourceRepository(s.db);
        SqliteSubscriptionRepository subscriptionRepository(s.db);
        SqliteTaskRepository taskRepository(s.db);
        NoticeService notices(noticeRepository, s.school.id.toStdString());
        SourceService sources(s.school.id.toStdString(), s.school.catalog, sourceRepository);
        SubscriptionService subscriptions(s.school.id.toStdString(), subscriptionRepository,
                                          notices);
        TaskService tasks(s.school.id.toStdString(), s.school.timeZone.toStdString(),
                          taskRepository, notices);
        RefreshCoordinator coordinator(s.school, notices, sources);
        MainWindow window(s.school, notices, sources, coordinator, registry, subscriptions, tasks,
                          &s.resources, &s.discovery);
        QCOMPARE(control<QTabWidget>(window, "mainTabs")->count(), 8);
        auto *page = control<ResourcePage>(window, "resourcePage");
        QSignalSpy selected(&window, &MainWindow::universitySelected);
        control<QPushButton>(*page, "discoverResourcesButton")->click();
        QVERIFY(s.discovery.busy());
        QVERIFY(!control<QPushButton>(window, "loadUniversityButton")->isEnabled());
        QVERIFY(!control<QLineEdit>(window, "universityUrlInput")->isEnabled());
        auto *ai = control<AiSourcesPage>(window, "aiSourcesPage");
        const QString generated = QString::fromUtf8(SCHOOL_CONFIG_DIR) + "/neepu.example.json";
        // Deliver the configuration-ready notification only; no model or onboarding request runs.
        QVERIFY(QMetaObject::invokeMethod(ai, "configReady", Qt::DirectConnection,
                                          Q_ARG(QString, generated)));
        QCOMPARE(selected.count(), 0);
        control<QPushButton>(*page, "cancelResourceDiscoveryButton")->click();
        QVERIFY(!s.discovery.busy());
        QCOMPARE(selected.count(), 1);
        QCOMPARE(selected.at(0).at(0).toString(), generated);
        QVERIFY(control<QPushButton>(window, "loadUniversityButton")->isEnabled());
        QVERIFY(control<QLineEdit>(window, "universityUrlInput")->isEnabled());
        QApplication::processEvents();
        QCOMPARE(selected.count(), 1);
    }
};
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    ResourceDesktopTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ResourceDesktopTests.moc"
