#include "adapters/RefreshCoordinator.h"
#include "adapters/UniversityRegistry.h"
#include "desktop/MainWindow.h"
#include "desktop/NoticePage.h"
#include "desktop/BrandTheme.h"
#include "desktop/OverviewPage.h"
#include "desktop/SourcePage.h"
#include "desktop/UniversityPage.h"
#include "storage/Database.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteSourceRepository.h"
#include "storage/SqliteSubscriptionRepository.h"
#include "storage/SqliteTaskRepository.h"
#include "application/TaskService.h"
#include "application/SubscriptionService.h"
#include <QtTest>
#include <QComboBox>
#include <QDate>
#include <QDir>
#include <QDialog>
#include <QDesktopServices>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTimer>
#include <QSplitter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBrowser>
#include <QAction>
#include <QToolButton>
#include <QTabBar>

using namespace campus;

// Intercept the browser request so this contract never launches a real browser.
class OfficialUrlCapture final : public QObject {
    Q_OBJECT
  public:
    OfficialUrlCapture() { QDesktopServices::setUrlHandler("https", this, "capture"); }
    ~OfficialUrlCapture() override { QDesktopServices::unsetUrlHandler("https"); }
    QUrl requested;
    int requests = 0;
  public slots:
    void capture(const QUrl &url) {
        requested = url;
        ++requests;
    }
};

class DesktopTests final : public QObject {
    Q_OBJECT
  private:
    template <class T> T *control(QWidget &window, const char *name) {
        auto *result = window.findChild<T *>(name);
        if (!result)
            throw std::runtime_error(std::string("界面控件缺失：") + name);
        return result;
    }
    void seed(NoticeService &notices) {
        Notice notice;
        notice.id = "desktop-test-payment";
        notice.schoolId = "cn-neepu";
        notice.sourceId = "academic-affairs-notices";
        notice.sourceName = "教务处通知公告";
        notice.title = "重修缴费通知";
        notice.url = "https://jwc.neepu.edu.cn/info/1014/12248.htm";
        notice.publishedDate = QDate::currentDate().toString(Qt::ISODate).toStdString();
        notices.ingest({notice});
        notice.body = "已经保存的官方正文";
        notices.saveDetail(notice);
    }
    void writeEvidence(const QJsonObject &result) {
        if (!QDir(EVIDENCE_DIR).exists())
            return;
        QFile output(QString(EVIDENCE_DIR) + "/stage1-desktop-tests.json");
        if (!output.open(QIODevice::WriteOnly))
            throw std::runtime_error("无法写入界面验收证据");
        output.write(QJsonDocument(result).toJson());
    }
  private slots:
    void overviewUsesCacheAndOpensSelectedNotice() {
        BrandTheme::installApplication(*qobject_cast<QApplication *>(QCoreApplication::instance()));
        const auto school = SchoolPackage::load(CONFIG_FILE);
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        QTemporaryDir folder;
        Database database(folder.filePath("overview.sqlite"));
        SqliteRepository repository(database);
        SqliteSourceRepository sourceRepository(database);
        SqliteSubscriptionRepository subscriptionRepository(database);
        SqliteTaskRepository taskRepository(database);
        NoticeService notices(repository, school.id.toStdString());
        SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
        SubscriptionService subscriptions(school.id.toStdString(), subscriptionRepository, notices);
        TaskService tasks(school.id.toStdString(), school.timeZone.toStdString(), taskRepository, notices);
        RefreshCoordinator network(school, notices, sources);
        seed(notices);
        auto task = tasks.draft("desktop-test-payment");
        task.title = "核对重修缴费结果";
        task = tasks.save(task);
        MainWindow window(school, notices, sources, network, registry, subscriptions, tasks);
        auto *tabs = control<QTabWidget>(window, "mainTabs");
        QCOMPARE(tabs->currentWidget()->objectName(), QString("overviewPage"));
        QCOMPARE(tabs->tabText(0), QString("通知"));
        QCOMPARE(tabs->tabText(1), QString("来源"));
        QCOMPARE(tabs->tabText(2), QString("我的订阅"));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(tabs->tabBar()->isHidden());
        auto *overviewButton = control<QPushButton>(window, "navigateOverviewButton");
        auto *noticeButton = control<QPushButton>(window, "navigateNoticesButton");
        auto *taskButton = control<QPushButton>(window, "navigateTasksButton");
        QVERIFY(overviewButton->isChecked());
        QVERIFY(!control<QPushButton>(window, "navigateResourcesButton")->isEnabled());
        QVERIFY(!control<QPushButton>(window, "overviewResourcesButton")->isEnabled());
        noticeButton->setFocus();
        QTest::keyClick(noticeButton, Qt::Key_Space);
        QCOMPARE(tabs->currentIndex(), 0);
        QVERIFY(noticeButton->isChecked());
        QVERIFY(!overviewButton->isChecked());
        taskButton->click();
        QCOMPARE(tabs->currentIndex(), 3);
        QVERIFY(taskButton->isChecked());
        for (const auto &[name, index] : {std::pair{"navigateSourcesAction", 1},
                                         {"navigateSubscriptionsAction", 2},
                                         {"navigateUniversityAction", 4},
                                         {"navigateAiAction", 5},
                                         {"navigateCalendarAction", 6}}) {
            auto *action = window.findChild<QAction *>(QString::fromLatin1(name));
            QVERIFY(action);
            action->trigger();
            QCOMPARE(tabs->currentIndex(), index);
            QVERIFY(!taskButton->isChecked());
            QVERIFY(!noticeButton->isChecked());
        }
        overviewButton->click();
        QCOMPARE(tabs->currentWidget()->objectName(), QString("overviewPage"));
        // Cancel the real manual editor: navigation must not pretend a task was saved.
        QTimer::singleShot(0, &window, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        control<QPushButton>(window, "overviewNewReminderButton")->click();
        QCOMPARE(tabs->currentWidget()->objectName(), QString("overviewPage"));
        QCOMPARE(tasks.views().size(), size_t(1));
        // Offscreen does not reactivate the owner when its modal editor closes.
        // Establish the active-window precondition before testing keyboard focus.
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *overview = control<OverviewPage>(window, "overviewPage");
        auto *splitter = control<QSplitter>(window, "overviewSplitter");
        window.resize(600, 600);
        QTRY_COMPARE(window.width(), 600);
        QTRY_COMPARE(splitter->orientation(), Qt::Vertical);
        auto largeFont = overview->font();
        largeFont.setPointSizeF((largeFont.pointSizeF() > 0 ? largeFont.pointSizeF() : 10.5) * 1.5);
        overview->setFont(largeFont);
        auto *scroll = control<QScrollArea>(window, "overviewScroll");
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QVERIFY(scroll->widget()->width() <= scroll->viewport()->width());
        overview->setFont(window.font());
        window.resize(1800, 1000);
        QTRY_COMPARE(splitter->orientation(), Qt::Horizontal);
        QVERIFY(control<QLabel>(window, "overviewSummary")->text().contains("1 项待办未完成"));
        const auto captures = QString(EVIDENCE_DIR) + "/upgrade-2026-10-14";
        QVERIFY(QDir().mkpath(captures));
        QVERIFY(window.grab().save(captures + "/qt-overview.png"));
        auto *recent = control<QListWidget>(window, "overviewNotices");
        QCOMPARE(recent->count(), 1);
        recent->itemActivated(recent->item(0));
        QCOMPARE(tabs->currentWidget()->objectName(), QString("noticePage"));
        QVERIFY(!network.busy());
        auto *page = control<NoticePage>(window, "noticePage");
        auto *noticeSplitter = control<QSplitter>(window, "noticeSplitter");
        window.resize(600, 600);
        QTRY_COMPARE(noticeSplitter->orientation(), Qt::Vertical);
        QVERIFY(window.grab().save(captures + "/qt-notice-narrow.png"));
        page->setFont(largeFont);
        auto *noticeScroll = control<QScrollArea>(window, "noticeScroll");
        QTRY_VERIFY(noticeScroll->verticalScrollBar()->maximum() > 0);
        QVERIFY(noticeScroll->widget()->width() <= noticeScroll->viewport()->width());
        auto *table = control<QTableView>(window, "noticeTable");
        table->setFocus();
        QSignalSpy activated(table, &QTableView::activated);
        QTest::keyClick(table, Qt::Key_Return);
        QCOMPARE(activated.count(), 1);
        QTRY_VERIFY(control<QTextBrowser>(window, "noticeBody")->hasFocus());
        QVERIFY(control<QPushButton>(window, "openNoticeOriginalButton")->isEnabled());
        control<QLineEdit>(window, "keywordSearch")->setText("不会匹配的测试关键词");
        QTRY_COMPARE(table->model()->rowCount(), 0);
        QVERIFY(!control<QPushButton>(window, "openNoticeOriginalButton")->isEnabled());
        QVERIFY(!control<QPushButton>(window, "addTaskButton")->isEnabled());
        control<QPushButton>(window, "resetNoticeFiltersButton")->click();
        QTRY_COMPARE(table->model()->rowCount(), 1);
        page->setFont(window.font());
        window.resize(1800, 1000);
        QTRY_COMPARE(noticeSplitter->orientation(), Qt::Horizontal);
        tasks.setStatus(task.id, TaskStatus::Completed);
        control<OverviewPage>(window, "overviewPage")->reload();
        QVERIFY(control<QLabel>(window, "overviewSummary")->text().contains("0 项待办未完成"));
        const auto sourceId = school.sources.front().id.toStdString();
        const auto run = sources.begin(sourceId, "2026-10-14T00:00:00Z");
        overview->reload();
        QVERIFY(control<QLabel>(window, "overviewSourceHealth")->text().contains("1 个正在更新"));
        sources.finish(run, {}, "2026-10-14T00:00:01Z");
        sources.setPaused(sourceId, true);
        overview->reload();
        QVERIFY(control<QLabel>(window, "overviewSourceHealth")->text().contains("1 个已暂停"));
        QVERIFY(control<QLabel>(window, "overviewSourceHealth")->text().contains("0 个正在更新"));
    }
    void resetClearsSourceAndEmptyStateOffersYearRecovery() {
        BrandTheme::installApplication(*qobject_cast<QApplication *>(QCoreApplication::instance()));
        const auto school = SchoolPackage::load(CONFIG_FILE);
        QTemporaryDir folder;
        Database database(folder.filePath("filter-recovery.sqlite"));
        SqliteRepository repository(database);
        SqliteSourceRepository sourceRepository(database);
        NoticeService notices(repository, school.id.toStdString());
        SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
        RefreshCoordinator network(school, notices, sources);
        seed(notices);
        NoticePage page(school, notices, network);
        page.resize(1280, 800);
        page.show();
        auto *source = control<QComboBox>(page, "noticeSourceSelector");
        auto *search = control<QLineEdit>(page, "keywordSearch");
        auto *year = control<QComboBox>(page, "yearSelector");
        auto *table = control<QTableView>(page, "noticeTable");
        auto *empty = control<QWidget>(page, "noticeEmptyState");
        auto *allYears = control<QPushButton>(page, "showAllNoticeYearsButton");
        const int academic = source->findData("academic-affairs-notices");
        QVERIFY(academic > 0);
        source->setCurrentIndex(academic);
        search->setText("不会匹配的关键词");
        QCOMPARE(table->model()->rowCount(), 0);
        QVERIFY(!empty->isHidden());
        control<QPushButton>(page, "resetNoticeFiltersButton")->click();
        QVERIFY(page.currentQuery().sourceIds.empty());
        QVERIFY(page.currentQuery().keywordAll.empty());
        QCOMPARE(source->currentIndex(), 0);
        QCOMPARE(table->model()->rowCount(), 1);
        QVERIFY(empty->isHidden());
        year->addItem("无通知的历史年", QDate::currentDate().year() - 20);
        year->setCurrentIndex(year->count() - 1);
        QCOMPARE(table->model()->rowCount(), 0);
        QVERIFY(!allYears->isHidden());
        allYears->click();
        QCOMPARE(page.currentQuery().yearPolicy, YearPolicy::AllYears);
        QCOMPARE(table->model()->rowCount(), 1);
        QVERIFY(!network.busy());
        const auto captureDirectory = QString(EVIDENCE_DIR) + "/upgrade-2026-10-13";
        QVERIFY(QDir().mkpath(captureDirectory));
        QVERIFY(page.grab().save(captureDirectory + "/notice-page.png"));
    }
    void pendingSourceOffersAiWithoutStartingSearch() {
        auto school = SchoolPackage::load(CONFIG_FILE);
        SourceDescription pending;
        pending.schoolId = school.id.toStdString(); pending.id = "test-pending";
        pending.name = "【测试】待接入"; pending.entryUrl = "https://www.neepu.edu.cn/tzgg/";
        auto login = pending;
        login.id = "test-login"; login.requiresLogin = true;
        school.catalog = {pending, login};
        QTemporaryDir folder;
        Database database(folder.filePath("repair.sqlite"));
        SqliteRepository repository(database);
        SqliteSourceRepository sourceRepository(database);
        NoticeService notices(repository, school.id.toStdString());
        SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
        RefreshCoordinator network(school, notices, sources);
        SourcePage page(school, sources, network);
        auto *table = control<QTableView>(page, "sourceTable");
        auto *button = control<QPushButton>(page, "aiSupplementSourceButton");
        QSignalSpy requested(&page, &SourcePage::aiSupplementRequested);
        table->setCurrentIndex(table->model()->index(0, 0));
        QVERIFY(button->isEnabled());
        button->click();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.first().first().toString(), QString("test-pending"));
        QVERIFY(!network.busy());
        table->setCurrentIndex(table->model()->index(1, 0));
        QVERIFY(!button->isEnabled());
    }
    void loginAccessCardAndOfficialBrowserRequest() {
        auto school = SchoolPackage::load(CONFIG_FILE);
        SourceDescription restricted;
        restricted.schoolId = school.id.toStdString();
        restricted.id = "account-protected-academic";
        restricted.name = "教务账号通知";
        restricted.entryUrl = "https://jwc.neepu.edu.cn/";
        restricted.loginUrl = restricted.entryUrl;
        restricted.requiresLogin = true;
        restricted.pendingReason = "此入口需要登录";
        school.catalog.push_back(restricted);
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        QTemporaryDir folder;
        Database database(folder.filePath("login-ui.sqlite"));
        SqliteRepository repository(database);
        SqliteSourceRepository sourceRepository(database);
        NoticeService notices(repository, school.id.toStdString());
        SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
        RefreshCoordinator network(school, notices, sources, nullptr, {0, 100});
        SqliteSubscriptionRepository subscriptionRepository(database);
        SubscriptionService subscriptions(school.id.toStdString(), subscriptionRepository, notices);
        SqliteTaskRepository taskRepository(database);
        TaskService tasks(school.id.toStdString(), school.timeZone.toStdString(), taskRepository, notices);
        MainWindow window(school, notices, sources, network, registry, subscriptions, tasks);
        control<QTabWidget>(window, "mainTabs")->setCurrentIndex(1);
        window.show();
        auto *table = control<QTableView>(window, "sourceTable");
        auto *panel = control<QWidget>(window, "sourceLoginPanel");
        auto *openLogin = control<QPushButton>(window, "openOfficialLoginButton");
        QVERIFY(!panel->isVisible());
        control<QPushButton>(window, "showLoginSourceButton")->click();
        QCOMPARE(table->currentIndex().row(), static_cast<int>(school.catalog.size()) - 1);
        QCOMPARE(table->model()->index(table->currentIndex().row(), 1).data().toString(), QString("需要登录"));
        QVERIFY(panel->isVisible());
        QVERIFY(openLogin->isEnabled());
        QVERIFY(!control<QPushButton>(window, "updateSourceButton")->isEnabled());
        QVERIFY(!control<QPushButton>(window, "pauseSourceButton")->isEnabled());
        QVERIFY(control<QLabel>(window, "sourceLoginExplanation")->text().contains("不自动采集"));
        OfficialUrlCapture browser;
        openLogin->click();
        QCOMPARE(browser.requests, 1);
        QCOMPARE(browser.requested, QUrl("https://jwc.neepu.edu.cn/"));
        QVERIFY(control<QLabel>(window, "sourceStatus")->text().contains("不会自动启用"));
        QVERIFY(!sources.isRunnable(restricted.id));
        control<SourcePage>(window, "sourcePage")->reload();
        QVERIFY(panel->isVisible());
        table->setCurrentIndex(table->model()->index(0, 0));
        QVERIFY(!panel->isVisible());
        QVERIFY(!openLogin->isEnabled());
        openLogin->click();
        QCOMPARE(browser.requests, 1);
        QCOMPARE(notices.list().size(), size_t(0));
        QVERIFY(!network.busy());
    }

    void unverifiedLoginAddressDoesNotOfferBrowserAction() {
        auto school = SchoolPackage::load(CONFIG_FILE);
        for (auto &source : school.catalog) {
            source.requiresLogin = true;
            source.loginUrl.clear();
        }
        QTemporaryDir folder;
        Database database(folder.filePath("no-login-url.sqlite"));
        SqliteRepository repository(database);
        SqliteSourceRepository sourceRepository(database);
        NoticeService notices(repository, school.id.toStdString());
        SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
        RefreshCoordinator network(school, notices, sources, nullptr, {0, 100});
        SourcePage page(school, sources, network);
        page.show();
        QVERIFY(control<QWidget>(page, "sourceLoginPanel")->isVisible());
        QVERIFY(!control<QPushButton>(page, "openOfficialLoginButton")->isEnabled());
        QVERIFY(control<QLabel>(page, "sourceLoginAddress")->text().contains("尚未核验"));
        OfficialUrlCapture browser;
        control<QPushButton>(page, "openOfficialLoginButton")->click();
        QCOMPARE(browser.requests, 0);
    }

    void pauseResumeAndPendingControls() {
        const auto school = SchoolPackage::load(CONFIG_FILE);
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        QTemporaryDir folder;
        Database database(folder.filePath("ui.sqlite"));
        SqliteRepository repository(database);
        SqliteSourceRepository sourceRepository(database);
        NoticeService notices(repository, school.id.toStdString());
        seed(notices);
        SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
        RefreshCoordinator network(school, notices, sources, nullptr, {0, 100});
        SqliteSubscriptionRepository subscriptionRepository(database);
        SubscriptionService subscriptions(school.id.toStdString(), subscriptionRepository, notices);
        SqliteTaskRepository taskRepository(database);
        TaskService tasks(school.id.toStdString(), school.timeZone.toStdString(), taskRepository,
                          notices);
        MainWindow window(school, notices, sources, network, registry, subscriptions, tasks);
        window.show();
        auto *table = control<QTableView>(window, "sourceTable");
        auto *pause = control<QPushButton>(window, "pauseSourceButton");
        auto *update = control<QPushButton>(window, "updateSourceButton");
        QCOMPARE(table->model()->rowCount(), 7);
        table->setCurrentIndex(table->model()->index(6, 0));
        QCOMPARE(table->model()->index(6, 1).data().toString(), QString("待接入"));
        QVERIFY(!pause->isEnabled());
        QVERIFY(!update->isEnabled());
        table->setCurrentIndex(table->model()->index(0, 0));
        QVERIFY(pause->isEnabled());
        QVERIFY(update->isEnabled());
        pause->click();
        QVERIFY(sources.find("main-announcements")->state.paused);
        QCOMPARE(pause->text(), QString("恢复更新"));
        QVERIFY(!update->isEnabled());
        QCOMPARE(notices.list().size(), size_t(1));
        pause->click();
        QVERIFY(!sources.find("main-announcements")->state.paused);
        QVERIFY(update->isEnabled());
        auto *theme = control<QComboBox>(window, "themeSelector");
        theme->setCurrentText("重修 / 缴费");
        QCOMPARE(control<QTableView>(window, "noticeTable")->model()->rowCount(), 1);
        QCOMPARE(control<QComboBox>(window, "yearSelector")->currentData().toInt(),
                 QDate::currentDate().year());
        QVERIFY(!window.findChild<QLineEdit *>("deepseekApiKey"));
        // Search now opens basic setup when no Key exists. Explicitly dismiss
        // that modal UI; this contract never authorizes a model request.
        QTimer::singleShot(0, &window, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        control<QPushButton>(window, "aiSearchButton")->click();
        QVERIFY(control<QLabel>(window, "aiStatus")->text().contains("尚未"));
    }
    void allPausedRestartAndCachedNotices() {
        const auto school = SchoolPackage::load(CONFIG_FILE);
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        QTemporaryDir folder;
        const auto filename = folder.filePath("restart.sqlite");
        const bool realSnapshot = QFile::exists(REAL_V1_DB);
        if (realSnapshot)
            QVERIFY(QFile::copy(REAL_V1_DB, filename));
        int expected = 1;
        {
            Database database(filename);
            SqliteRepository repository(database);
            SqliteSourceRepository sourceRepository(database);
            NoticeService notices(repository, school.id.toStdString());
            if (!realSnapshot)
                seed(notices);
            expected = static_cast<int>(notices.list().size());
            SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
            RefreshCoordinator network(school, notices, sources, nullptr, {0, 100});
            SqliteSubscriptionRepository subscriptionRepository(database);
            SubscriptionService subscriptions(school.id.toStdString(), subscriptionRepository,
                                              notices);
            SqliteTaskRepository taskRepository(database);
            TaskService tasks(school.id.toStdString(), school.timeZone.toStdString(),
                              taskRepository, notices);
            MainWindow window(school, notices, sources, network, registry, subscriptions, tasks);
            auto *table = control<QTableView>(window, "sourceTable");
            for (int row = 0; row < 6; ++row) {
                table->setCurrentIndex(table->model()->index(row, 0));
                control<QPushButton>(window, "pauseSourceButton")->click();
            }
            QCOMPARE(control<QLabel>(window, "sourceSummary")->text(),
                     QString("共 7 个来源 · 已接入 6 个 · 本机暂停 6 个 · 待接入 1 个"));
            QSignalSpy finished(&network, &RefreshCoordinator::finished);
            control<QPushButton>(window, "primary")->click();
            QCOMPARE(finished.count(), 1);
            QCOMPARE(finished.front().at(0).toInt(), 0);
            QCOMPARE(finished.front().at(1).toInt(), 0);
            QVERIFY(control<QPushButton>(window, "primary")->isEnabled());
            QCOMPARE(notices.list().size(), static_cast<size_t>(expected));
        }
        {
            Database database(filename);
            SqliteRepository repository(database);
            SqliteSourceRepository sourceRepository(database);
            NoticeService notices(repository, school.id.toStdString());
            SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
            RefreshCoordinator network(school, notices, sources, nullptr, {0, 100});
            SqliteSubscriptionRepository subscriptionRepository(database);
            SubscriptionService subscriptions(school.id.toStdString(), subscriptionRepository,
                                              notices);
            SqliteTaskRepository taskRepository(database);
            TaskService tasks(school.id.toStdString(), school.timeZone.toStdString(),
                              taskRepository, notices);
            MainWindow window(school, notices, sources, network, registry, subscriptions, tasks);
            window.show();
            QCOMPARE(notices.list().size(), static_cast<size_t>(expected));
            QCOMPARE(control<QLabel>(window, "sourceSummary")->text(),
                     QString("共 7 个来源 · 已接入 6 个 · 本机暂停 6 个 · 待接入 1 个"));
            auto *table = control<QTableView>(window, "sourceTable");
            for (int row = 0; row < 6; ++row)
                QCOMPARE(table->model()->index(row, 1).data().toString(), QString("已暂停"));
            QVERIFY(control<QLabel>(window, "noticeCount")
                        ->text()
                        .contains(QString("本地共 %1 条").arg(expected)));
            writeEvidence({{"all_paused_restart", true},
                           {"cached_notices", expected},
                           {"real_v1_snapshot", realSnapshot},
                           {"source_rows", 7}});
        }
    }
    void refreshCompletionDoesNotUnlockSchoolOnboarding() {
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        QVERIFY(!registry.list().empty());
        UniversityPage page(registry, registry.list().front().id);
        page.show();
        auto *input = control<QLineEdit>(page, "universityUrlInput");
        auto *load = control<QPushButton>(page, "loadUniversityButton");
        auto *directory = control<QListWidget>(page, "universityDirectory");
        auto *feedback = control<QLabel>(page, "universityFeedback");
        auto *progress = control<QProgressBar>(page, "universityProgress");
        const QString phase = "正在发现新学校的公开栏目……";

        page.setBusy(true);
        page.setFeedback(phase);
        page.setRefreshing(true);
        page.setRefreshing(false);
        QVERIFY(progress->isVisible());
        QCOMPARE(progress->maximum(), 0);
        QVERIFY(!input->isEnabled());
        QVERIFY(!load->isEnabled());
        QVERIFY(!directory->isEnabled());
        QCOMPARE(feedback->text(), phase);
        page.setBusy(false);
        QVERIFY(progress->isHidden());
        QVERIFY(input->isEnabled());

        // The opposite completion order must retain the refresh lock too.
        page.setRefreshing(true);
        page.setBusy(true);
        page.setFeedback(phase);
        page.setBusy(false);
        QVERIFY(progress->isVisible());
        QCOMPARE(progress->maximum(), 0);
        QVERIFY(!input->isEnabled());
        QVERIFY(!load->isEnabled());
        QVERIFY(!directory->isEnabled());
        QCOMPARE(feedback->text(), phase);
        page.setRefreshing(false);
        QVERIFY(progress->isHidden());
        QCOMPARE(progress->maximum(), 100);
        QVERIFY(input->isEnabled());
        QVERIFY(load->isEnabled());
        QVERIFY(directory->isEnabled());
    }
    void schoolOnboardingBusyRestoresControls_data() {
        QTest::addColumn<QString>("terminalMessage");
        QTest::newRow("success") << QString("学校接入完成，已加载公开通知与资源。");
        QTest::newRow("failure") << QString("官网暂时无法访问，请稍后重试。");
    }
    void schoolOnboardingBusyRestoresControls() {
        QFETCH(QString, terminalMessage);
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        QVERIFY(!registry.list().empty());
        UniversityPage page(registry, registry.list().front().id);
        page.show();
        auto *input = control<QLineEdit>(page, "universityUrlInput");
        auto *load = control<QPushButton>(page, "loadUniversityButton");
        auto *directory = control<QListWidget>(page, "universityDirectory");
        auto *feedback = control<QLabel>(page, "universityFeedback");
        auto *progress = control<QProgressBar>(page, "universityProgress");
        QSignalSpy selected(&page, &UniversityPage::universitySelected);
        QSignalSpy discovered(&page, &UniversityPage::homepageDiscoveryRequested);
        QVERIFY(progress->isHidden());

        page.setBusy(true);
        QVERIFY(progress->isVisible());
        QCOMPARE(progress->minimum(), 0);
        QCOMPARE(progress->maximum(), 0);
        QVERIFY(!input->isEnabled());
        QVERIFY(!load->isEnabled());
        QVERIFY(!directory->isEnabled());
        QVERIFY(!progress->accessibleName().isEmpty());
        const QString phase = "已识别学校，正在发现公开栏目……";
        page.setFeedback(phase);
        // Repeated busy signals must not erase the backend's current phase.
        page.setBusy(true);
        QCOMPARE(feedback->text(), phase);
        QCOMPARE(feedback->accessibleDescription(), phase);
        QCOMPARE(progress->accessibleDescription(), phase);
        load->click();
        QVERIFY(QMetaObject::invokeMethod(input, "returnPressed", Qt::DirectConnection));
        QCOMPARE(selected.count(), 0);
        QCOMPARE(discovered.count(), 0);

        page.setBusy(false);
        page.setFeedback(terminalMessage);
        QVERIFY(progress->isHidden());
        QCOMPARE(progress->minimum(), 0);
        QCOMPARE(progress->maximum(), 100);
        QVERIFY(progress->accessibleDescription().isEmpty());
        QVERIFY(input->isEnabled());
        QVERIFY(load->isEnabled());
        QVERIFY(directory->isEnabled());
        QCOMPARE(load->text(), QString("接入 / 切换大学"));
        QCOMPARE(feedback->textFormat(), Qt::PlainText);
        QCOMPARE(feedback->text(), terminalMessage);
        // A later idle notification must keep the success or failure visible.
        page.setBusy(false);
        QCOMPARE(feedback->text(), terminalMessage);
        load->click();
        QCOMPARE(selected.count(), 1);
        QCOMPARE(discovered.count(), 0);
        QCOMPARE(selected.front().front().toString(), registry.list().front().configFile);
    }
    void officialHomepageInputAndRejection() {
        const auto school = SchoolPackage::load(CONFIG_FILE);
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        QTemporaryDir folder;
        Database database(folder.filePath("input.sqlite"));
        SqliteRepository repository(database);
        SqliteSourceRepository sourceRepository(database);
        NoticeService notices(repository, school.id.toStdString());
        SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
        RefreshCoordinator network(school, notices, sources, nullptr, {0, 100});
        SqliteSubscriptionRepository subscriptionRepository(database);
        SubscriptionService subscriptions(school.id.toStdString(), subscriptionRepository, notices);
        SqliteTaskRepository taskRepository(database);
        TaskService tasks(school.id.toStdString(), school.timeZone.toStdString(), taskRepository,
                          notices);
        MainWindow window(school, notices, sources, network, registry, subscriptions, tasks);
        window.show();
        auto *input = control<QLineEdit>(window, "universityUrlInput");
        auto *load = control<QPushButton>(window, "loadUniversityButton");
        auto *feedback = control<QLabel>(window, "universityFeedback");
        QSignalSpy selected(&window, &MainWindow::universitySelected);
        for (const auto &url : {"https://example.com/", "https://www.neepu.edu.cn.example.com/",
                                "http://127.0.0.1/", "https://user:password@www.neepu.edu.cn/"}) {
            input->setText(url);
            load->click();
            QCOMPARE(selected.count(), 0);
            QVERIFY(!feedback->text().isEmpty());
            QCOMPARE(feedback->textFormat(), Qt::PlainText);
        }
        input->setText("www.neepu.edu.cn");
        load->click();
        QCOMPARE(selected.count(), 1);
        QCOMPARE(selected.front().front().toString(),
                 registry.resolve("www.neepu.edu.cn").configFile);
        QVERIFY(feedback->text().contains("东北电力大学"));
        // Updating locks the selector until the run completes, including zero eligible sources.
        emit network.started();
        QVERIFY(!input->isEnabled());
        QVERIFY(!load->isEnabled());
        emit network.finished(0, 0);
        QVERIFY(input->isEnabled());
        QVERIFY(load->isEnabled());
    }
};

QTEST_MAIN(DesktopTests)
#include "DesktopTests.moc"
