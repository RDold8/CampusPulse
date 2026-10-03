#include "adapters/ArtifactWriter.h"
#include "adapters/RefreshCoordinator.h"
#include "adapters/ResourceClassifier.h"
#include "adapters/ResourceDiscovery.h"
#include "adapters/SchoolPackage.h"
#include "adapters/UniversityRegistry.h"
#include "application/NoticeService.h"
#include "application/ResourceService.h"
#include "application/SourceService.h"
#include "application/SubscriptionService.h"
#include "application/TaskService.h"
#include "desktop/BrandTheme.h"
#include "desktop/DesktopWorkspace.h"
#include "desktop/MainWindow.h"
#include "desktop/ResourcePage.h"
#include "storage/Database.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteResourceRepository.h"
#include "storage/SqliteSourceRepository.h"
#include "storage/SqliteSubscriptionRepository.h"
#include "storage/SqliteTaskRepository.h"
#include <QApplication>
#include <QBuffer>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <stdexcept>

using namespace campus;
namespace {
template <class T> T *control(QWidget &widget, const char *name) {
    auto *value = widget.findChild<T *>(name);
    if (!value)
        throw std::runtime_error(std::string("学校资源验收控件缺失：") + name);
    return value;
}
QByteArray fileHash(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("无法只读检查资源数据库：" + file.errorString().toStdString());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        throw std::runtime_error("无法计算资源数据库校验值");
    return hash.result().toHex();
}
struct Session {
    Database db;
    SqliteRepository noticeRepository;
    SqliteSourceRepository sourceRepository;
    SqliteSubscriptionRepository subscriptionRepository;
    SqliteTaskRepository taskRepository;
    SqliteResourceRepository resourceRepository;
    NoticeService notices;
    SourceService sources;
    SubscriptionService subscriptions;
    TaskService tasks;
    ResourceService resources;
    RefreshCoordinator coordinator;
    ResourceDiscovery discovery;
    Session(const QString &path, const SchoolPackage &school)
        : db(path), noticeRepository(db), sourceRepository(db), subscriptionRepository(db),
          taskRepository(db), resourceRepository(db),
          notices(noticeRepository, school.id.toStdString()),
          sources(school.id.toStdString(), school.catalog, sourceRepository),
          subscriptions(school.id.toStdString(), subscriptionRepository, notices),
          tasks(school.id.toStdString(), school.timeZone.toStdString(), taskRepository, notices),
          resources(resourceRepository, school.id.toStdString()),
          coordinator(school, notices, sources), discovery(school, resources) {}
};
bool expectedOpen(const SchoolResource &resource, const SchoolPackage &school) {
    const auto root = ResourceClassifier::officialRoot(school.officialHomepage);
    const auto official = [&](const QString &value) {
        const QUrl url(value, QUrl::StrictMode);
        return ResourceClassifier::isSafeHttps(value) && ResourceClassifier::isOfficial(url, root);
    };
    const auto url = QString::fromStdString(resource.url);
    if (!ResourceClassifier::isSafeHttps(url))
        return false;
    if (resource.linkKind == "official")
        return official(url);
    return resource.linkKind == "official_recommended" &&
           official(QString::fromStdString(resource.discoveredFrom));
}
QJsonArray strings(const std::vector<std::string> &values) {
    QJsonArray result;
    for (const auto &value : values)
        result.append(QString::fromStdString(value));
    return result;
}
} // namespace

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setOrganizationName("CampusPulseAcceptance");
    application.setApplicationName("ResourceNativeProbe");
    QCommandLineParser parser;
    parser.setApplicationDescription("CampusPulse production resource page acceptance probe");
    parser.addHelpOption();
    parser.addOption({"database", "Existing crawler resource database, copied before use", "file"});
    parser.addOption({"config", "School package matching the cached resources", "file"});
    parser.addOption({"desktop-id", "Target Windows virtual desktop UUID", "uuid"});
    parser.addOption({"evidence", "JSON evidence output", "file"});
    parser.addOption({"screenshot", "Production resource page PNG output", "file"});
    parser.process(application);
    try {
        for (const auto &option : {"database", "config", "desktop-id", "evidence", "screenshot"})
            if (parser.value(option).isEmpty())
                throw std::invalid_argument(std::string("缺少验收参数 --") + option);
        const auto originalPath = QFileInfo(parser.value("database")).canonicalFilePath();
        if (originalPath.isEmpty() || !QFileInfo(originalPath).isFile())
            throw std::invalid_argument("实际资源测试数据库不存在");
        if (QFileInfo(originalPath + "-wal").size() > 0)
            throw std::invalid_argument("资源数据库存在未合并 WAL，请先关闭采集连接再制作只读副本");
        const auto evidencePath = QFileInfo(parser.value("evidence")).absoluteFilePath();
        const auto screenshotPath = QFileInfo(parser.value("screenshot")).absoluteFilePath();
        for (const auto &path : {evidencePath, screenshotPath}) {
            if (path.compare(originalPath, Qt::CaseInsensitive) == 0)
                throw std::invalid_argument("验收输出不能覆盖原始资源数据库");
            if (!QDir().mkpath(QFileInfo(path).absolutePath()))
                throw std::runtime_error("无法创建学校资源验收输出目录");
        }
        if (evidencePath.compare(screenshotPath, Qt::CaseInsensitive) == 0)
            throw std::invalid_argument("JSON 和 PNG 必须使用不同输出路径");
        const auto school = SchoolPackage::load(parser.value("config"));
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        QTemporaryDir temporary;
        if (!temporary.isValid())
            throw std::runtime_error("无法创建独立资源验收数据库目录");
        const auto copiedPath = temporary.filePath("resource-acceptance.sqlite");
        const auto originalHash = fileHash(originalPath);
        if (!QFile::copy(originalPath, copiedPath) || fileHash(copiedPath) != originalHash)
            throw std::runtime_error("无法生成与原资源数据库一致的独立副本");
        Session session(copiedPath, school);
        const auto cached = session.resources.list();
        if (cached.empty())
            throw std::invalid_argument(
                "传入数据库没有当前学校的真实资源缓存，验收不会自动联网补充");
        auto chosen = std::find_if(cached.begin(), cached.end(), [](const auto &resource) {
            return resource.category == "library" && !resource.accessNote.empty();
        });
        if (chosen == cached.end())
            chosen = std::find_if(cached.begin(), cached.end(), [](const auto &resource) {
                return resource.status == "verified" && resource.category == "library";
            });
        if (chosen == cached.end())
            chosen = std::find_if(cached.begin(), cached.end(), [](const auto &resource) {
                return resource.status == "verified" && (resource.category == "study_plan" ||
                                                         resource.category == "course_material" ||
                                                         resource.category == "academic_support");
            });
        if (chosen == cached.end())
            throw std::invalid_argument("缓存没有图书馆或已验证学习资源，无法展示要求的真实详情");
        const auto selectedResource = *chosen;
        bool discoveryStarted = false, refreshStarted = false, detailRequested = false;
        QObject::connect(&session.discovery, &ResourceDiscovery::started, &application,
                         [&] { discoveryStarted = true; });
        QObject::connect(&session.coordinator, &RefreshCoordinator::started, &application,
                         [&] { refreshStarted = true; });
        QObject::connect(&session.coordinator, &RefreshCoordinator::detailStarted, &application,
                         [&] { detailRequested = true; });
        BrandTheme::installApplication(application);
        MainWindow window(school, session.notices, session.sources, session.coordinator, registry,
                          session.subscriptions, session.tasks, &session.resources,
                          &session.discovery);
        window.setWindowTitle(window.windowTitle() + " · 真实学校资源验收");
        window.setAttribute(Qt::WA_ShowWithoutActivating);
        auto *tabs = control<QTabWidget>(window, "mainTabs");
        auto *page = control<ResourcePage>(window, "resourcePage");
        auto *table = control<QTableView>(*page, "resourceTable");
        const auto pageIndex = tabs->indexOf(page);
        if (pageIndex < 0 || tabs->count() != 8)
            throw std::runtime_error("生产窗口没有包含独立学校资源页的八个标签");
        tabs->setCurrentIndex(pageIndex);
        int selectedRow = -1;
        for (int row = 0; row < table->model()->rowCount(); ++row)
            if (table->model()->index(row, 0).data(Qt::UserRole).toString() ==
                QString::fromStdString(selectedResource.id)) {
                selectedRow = row;
                break;
            }
        if (selectedRow < 0)
            throw std::runtime_error("生产列表没有呈现选定真实资源");
        table->setCurrentIndex(table->model()->index(selectedRow, 0));
        table->selectRow(selectedRow);
        table->scrollTo(table->currentIndex());
        placeWindowOnDesktop(window, parser.value("desktop-id"));
        window.show();
        QTimer::singleShot(500, &application, [&] {
            try {
                const auto actual = session.resources.list();
                QJsonObject categories, statuses;
                for (const auto &resource : actual) {
                    const auto category = QString::fromStdString(resource.category);
                    const auto status = QString::fromStdString(resource.status);
                    categories[category] = categories.value(category).toInt() + 1;
                    statuses[status] = statuses.value(status).toInt() + 1;
                }
                const auto selectedId =
                    table->currentIndex().siblingAtColumn(0).data(Qt::UserRole).toString();
                const auto detail = control<QTextBrowser>(*page, "resourceDetail")->toPlainText();
                const auto subtitle = control<QLabel>(*page, "subtitle")->text();
                const auto openEnabled =
                    control<QPushButton>(*page, "openResourceButton")->isEnabled();
                const auto openExpected = expectedOpen(selectedResource, school);
                const auto rows = table->model()->rowCount();
                const bool idle = !session.discovery.busy() && !session.coordinator.busy();
                const bool noNetwork = !discoveryStarted && !refreshStarted && !detailRequested;
                const bool originalUnchanged = fileHash(originalPath) == originalHash;
                QByteArray png;
                QBuffer pngBuffer(&png);
                if (!pngBuffer.open(QIODevice::WriteOnly) || !window.grab().save(&pngBuffer, "PNG"))
                    throw std::runtime_error("无法生成生产学校资源页截图");
                writeArtifact(screenshotPath, png);
                const bool screenshotSaved = QFileInfo(screenshotPath).size() == png.size();
                const bool detailsMatch =
                    detail.contains(QString::fromStdString(selectedResource.title)) &&
                    detail.contains(QString::fromStdString(selectedResource.url)) &&
                    (selectedResource.accessNote.empty() ||
                     (detail.contains(QString::fromStdString(selectedResource.accessNote)) &&
                      detail.contains(QString::fromStdString(selectedResource.accessEvidence))));
                const bool passed =
                    tabs->count() == 8 && tabs->currentWidget() == page && page->isVisible() &&
                    rows > 0 && rows == static_cast<int>(actual.size()) &&
                    selectedId == QString::fromStdString(selectedResource.id) && detailsMatch &&
                    subtitle.startsWith(school.name + " ·") && openEnabled == openExpected &&
                    idle && noNetwork && originalUnchanged && screenshotSaved && actual == cached;
                const QJsonObject proof{
                    {"passed", passed},
                    {"school_id", school.id},
                    {"school_name", school.name},
                    {"school_subtitle", subtitle},
                    {"production_tab_count", tabs->count()},
                    {"resource_tab_index", pageIndex},
                    {"resource_page_visible", page->isVisible()},
                    {"resource_count", static_cast<int>(actual.size())},
                    {"resource_table_rows", rows},
                    {"category_counts", categories},
                    {"status_counts", statuses},
                    {"selected_resource_id", selectedId},
                    {"selected_resource_title", QString::fromStdString(selectedResource.title)},
                    {"selected_resource_category",
                     QString::fromStdString(selectedResource.category)},
                    {"selected_resource_status", QString::fromStdString(selectedResource.status)},
                    {"selected_resource_url", QString::fromStdString(selectedResource.url)},
                    {"selected_resource_discovered_from",
                     QString::fromStdString(selectedResource.discoveredFrom)},
                    {"selected_resource_last_checked_at",
                     QString::fromStdString(selectedResource.lastCheckedAt)},
                    {"selected_resource_audiences", strings(selectedResource.audiences)},
                    {"resource_detail_matches", detailsMatch},
                    {"open_resource_enabled", openEnabled},
                    {"open_resource_expected", openExpected},
                    {"resource_discovery_busy", session.discovery.busy()},
                    {"resource_discovery_started", discoveryStarted},
                    {"refresh_started", refreshStarted},
                    {"detail_requested", detailRequested},
                    {"source_database", originalPath},
                    {"source_database_sha256", QString::fromLatin1(originalHash)},
                    {"source_database_unchanged", originalUnchanged},
                    {"database_copied_to_temporary", true},
                    {"resources_unchanged", actual == cached},
                    {"demo_resources_seeded", 0},
                    {"desktop_id", parser.value("desktop-id")},
                    {"screenshot", screenshotPath},
                    {"screenshot_saved", screenshotSaved},
                    {"model_calls", 0},
                    {"network_requests_requested", noNetwork ? 0 : -1},
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
                        << "学校资源页验收未通过，具体字段见 " << evidencePath << '\n';
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
