#include "adapters/ArtifactWriter.h"
#include "adapters/RefreshCoordinator.h"
#include "adapters/UniversityRegistry.h"
#include "application/SubscriptionService.h"
#include "application/TaskService.h"
#include "desktop/BrandTheme.h"
#include "desktop/DesktopWorkspace.h"
#include "desktop/MainWindow.h"
#include "desktop/SourcePage.h"
#include "storage/Database.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteSourceRepository.h"
#include "storage/SqliteSubscriptionRepository.h"
#include "storage/SqliteTaskRepository.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <stdexcept>

using namespace campus;
namespace {
template <class T> T *control(QWidget &widget, const char *name) {
    auto *value = widget.findChild<T *>(name);
    if (!value)
        throw std::runtime_error(std::string("来源登录验收控件缺失：") + name);
    return value;
}
struct Session {
    Database db;
    SqliteRepository noticeRepository;
    SqliteSourceRepository sourceRepository;
    SqliteSubscriptionRepository subscriptionRepository;
    SqliteTaskRepository taskRepository;
    NoticeService notices;
    SourceService sources;
    SubscriptionService subscriptions;
    TaskService tasks;
    RefreshCoordinator coordinator;
    Session(const QString &path, const SchoolPackage &school)
        : db(path), noticeRepository(db), sourceRepository(db), subscriptionRepository(db),
          taskRepository(db), notices(noticeRepository, school.id.toStdString()),
          sources(school.id.toStdString(), school.catalog, sourceRepository),
          subscriptions(school.id.toStdString(), subscriptionRepository, notices),
          tasks(school.id.toStdString(), school.timeZone.toStdString(), taskRepository, notices),
          coordinator(school, notices, sources, nullptr, {0, 1000}) {}
};
bool officialLoginUrl(const QUrl &login, const QUrl &homepage) {
    auto root = homepage.host().toLower();
    if (root.startsWith("www."))
        root.remove(0, 4);
    const auto host = login.host().toLower();
    return !root.isEmpty() && login.isValid() && login.userInfo().isEmpty() &&
           (login.scheme() == "https" || login.scheme() == "http") &&
           (host == root || host.endsWith("." + root));
}
} // namespace

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setOrganizationName("CampusPulseAcceptance");
    application.setApplicationName("SourceAccessNativeProbe");
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "CampusPulse production login-required source acceptance probe");
    parser.addHelpOption();
    parser.addOption(
        {"config", "School config containing a login-required catalog source", "file"});
    parser.addOption({"desktop-id", "Target Windows virtual desktop UUID", "uuid"});
    parser.addOption({"evidence", "JSON evidence output", "file"});
    parser.addOption({"screenshot", "Production source page PNG output", "file"});
    parser.process(application);
    try {
        for (const auto &option : {"config", "desktop-id", "evidence", "screenshot"})
            if (parser.value(option).isEmpty())
                throw std::invalid_argument(std::string("缺少验收参数 --") + option);
        const auto evidencePath = QFileInfo(parser.value("evidence")).absoluteFilePath();
        const auto screenshotPath = QFileInfo(parser.value("screenshot")).absoluteFilePath();
        for (const auto &path : {evidencePath, screenshotPath})
            if (!QDir().mkpath(QFileInfo(path).absolutePath()))
                throw std::runtime_error("无法创建来源登录验收输出目录");
        const auto school = SchoolPackage::load(parser.value("config"));
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        QUrl homepage;
        for (const auto &university : registry.list())
            if (university.id == school.id)
                homepage = university.homepage;
        if (homepage.isEmpty())
            throw std::invalid_argument("学校不在本地已核验目录中，无法验证官方登录入口");
        QTemporaryDir temporary;
        if (!temporary.isValid())
            throw std::runtime_error("无法创建独立验收数据库目录");
        Session session(temporary.filePath("source-access.sqlite"), school);
        BrandTheme::installApplication(application);
        MainWindow window(school, session.notices, session.sources, session.coordinator, registry,
                          session.subscriptions, session.tasks);
        window.setWindowTitle(window.windowTitle() + " · 来源登录入口验收");
        window.setAttribute(Qt::WA_ShowWithoutActivating);
        auto *tabs = control<QTabWidget>(window, "mainTabs");
        auto *page = control<SourcePage>(window, "sourcePage");
        auto *table = control<QTableView>(*page, "sourceTable");
        auto *chooseLogin = control<QPushButton>(*page, "showLoginSourceButton");
        if (tabs->widget(1) != page)
            throw std::runtime_error("生产来源页不在预期的来源标签中");
        tabs->setCurrentIndex(1);
        chooseLogin->click();
        const auto row = table->currentIndex().row();
        if (row < 0)
            throw std::runtime_error("未能选择需登录来源");
        const auto sourceId = table->model()->index(row, 0).data(Qt::ToolTipRole).toString();
        const SourceDescription *chosen = nullptr;
        for (const auto &source : school.catalog)
            if (QString::fromStdString(source.id) == sourceId)
                chosen = &source;
        if (!chosen || !chosen->requiresLogin)
            throw std::runtime_error("查看需登录来源未选择到需登录的配置条目");
        const auto chosenSource = *chosen;
        placeWindowOnDesktop(window, parser.value("desktop-id"));
        window.show();
        QTimer::singleShot(500, &application, [&] {
            try {
                auto *panel = control<QWidget>(*page, "sourceLoginPanel");
                auto *openLogin = control<QPushButton>(*page, "openOfficialLoginButton");
                auto *update = control<QPushButton>(*page, "updateSourceButton");
                auto *pause = control<QPushButton>(*page, "pauseSourceButton");
                const auto explanation = control<QLabel>(*page, "sourceLoginExplanation")->text();
                const auto address = control<QLabel>(*page, "sourceLoginAddress")->text();
                const auto status =
                    table->model()->index(table->currentIndex().row(), 1).data().toString();
                const auto loginUrl = QString::fromStdString(chosenSource.loginUrl);
                const bool official = officialLoginUrl(QUrl(loginUrl, QUrl::StrictMode), homepage);
                const bool screenshotSaved = window.grab().save(screenshotPath);
                const bool explanationValid =
                    explanation.contains("不自动采集") && explanation.contains("学校账号");
                const auto noticeCount = static_cast<int>(session.notices.list().size());
                const auto taskCount = static_cast<int>(session.tasks.list().size());
                const bool passed =
                    panel->isVisible() && openLogin->isEnabled() && status == "需要登录" &&
                    !update->isEnabled() && !pause->isEnabled() && explanationValid && official &&
                    address.contains(loginUrl) && screenshotSaved && !session.coordinator.busy() &&
                    noticeCount == 0 && taskCount == 0;
                const QJsonObject proof{{"passed", passed},
                                        {"school_id", school.id},
                                        {"school_name", school.name},
                                        {"source_id", sourceId},
                                        {"source_name", QString::fromStdString(chosenSource.name)},
                                        {"source_status", status},
                                        {"requires_login", chosenSource.requiresLogin},
                                        {"login_url", loginUrl},
                                        {"official_homepage", homepage.toString()},
                                        {"official_login_url", official},
                                        {"source_login_panel_visible", panel->isVisible()},
                                        {"open_official_login_enabled", openLogin->isEnabled()},
                                        {"update_source_enabled", update->isEnabled()},
                                        {"pause_source_enabled", pause->isEnabled()},
                                        {"login_explanation", explanation},
                                        {"login_explanation_valid", explanationValid},
                                        {"login_address", address},
                                        {"desktop_id", parser.value("desktop-id")},
                                        {"screenshot", screenshotPath},
                                        {"screenshot_saved", screenshotSaved},
                                        {"notice_count", noticeCount},
                                        {"task_count", taskCount},
                                        {"model_calls", 0},
                                        {"network_requests_requested", 0},
                                        {"browser_opened", false},
                                        {"system_reminder_started", false},
                                        {"default_database_used", false},
                                        {"default_qsettings_used", false}};
                writeArtifact(evidencePath, QJsonDocument(proof).toJson(QJsonDocument::Indented));
                QTextStream(stdout)
                    << QString::fromUtf8(QJsonDocument(proof).toJson(QJsonDocument::Compact))
                    << '\n';
                if (!passed)
                    QTextStream(stderr)
                        << "来源登录提示验收未通过，具体字段见 " << evidencePath << '\n';
                application.exit(passed ? 0 : 1);
            } catch (const std::exception &error) {
                QTextStream(stderr) << QString::fromUtf8(error.what()) << '\n';
                application.exit(1);
            }
        });
        return application.exec();
    } catch (const std::exception &error) {
        QTextStream(stderr) << QString::fromUtf8(error.what()) << '\n';
        return 1;
    }
}
