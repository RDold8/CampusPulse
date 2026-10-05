#include "adapters/RefreshCoordinator.h"
#include "adapters/UniversityRegistry.h"
#include "desktop/MainWindow.h"
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
