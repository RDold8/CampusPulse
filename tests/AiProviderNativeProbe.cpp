#include "adapters/AiProviderConfig.h"
#include "adapters/AiProviderProbe.h"
#include "adapters/ArtifactWriter.h"
#include "adapters/RefreshCoordinator.h"
#include "adapters/UniversityRegistry.h"
#include "desktop/AiProviderDialog.h"
#include "desktop/AiSourcesPage.h"
#include "desktop/BrandTheme.h"
#include "desktop/DesktopWorkspace.h"
#include "storage/Database.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteSourceRepository.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <stdexcept>

using namespace campus;
namespace {
template <class T> T *control(QWidget &widget, const char *name) {
    auto *value = widget.findChild<T *>(name);
    if (!value)
        throw std::runtime_error(std::string("AI 界面验收控件缺失：") + name);
    return value;
}
} // namespace
// Production widgets with fresh test profiles, no credentials or network requests.
// --desktop-id is mandatory: this entry point never shows on the user's current desktop.
int main(int argc, char **argv) {
    qunsetenv("DEEPSEEK_API_KEY");
    QApplication application(argc, argv);
    application.setOrganizationName("CampusPulseAcceptance");
    application.setApplicationName("AiProviderNativeProbe");
    QCommandLineParser parser;
    parser.setApplicationDescription("CampusPulse AI provider UI acceptance; no API calls");
    parser.addHelpOption();
    parser.addOption({"desktop-id", "Target Windows virtual desktop; no desktop switch", "uuid"});
    parser.addOption({"output", "Fresh output directory (existing directories are refused)", "folder"});
    parser.process(application);
    try {
        if (parser.value("desktop-id").isEmpty() || parser.value("output").isEmpty())
            throw std::invalid_argument("请明确指定 --desktop-id 与新的 --output 目录；未显示窗口");
        const auto output = QFileInfo(parser.value("output")).absoluteFilePath();
        if (QFileInfo::exists(output))
            throw std::invalid_argument("验收输出目录已存在；没有覆盖文件或运行数据");
        if (!QDir().mkpath(output))
            throw std::runtime_error("无法创建验收输出目录");
        QTemporaryDir settings;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        QTemporaryDir data;
        SchoolPackage school;
        school.id = "cn-neepu";
        school.name = "东北电力大学 · 界面演示";
        school.officialHomepage = QUrl("https://www.neepu.edu.cn/");
        UniversityRegistry registry(data.path());
        Database db(data.filePath("probe.sqlite"));
        SqliteRepository noticeRepository(db);
        SqliteSourceRepository sourceRepository(db);
        NoticeService notices(noticeRepository, school.id.toStdString());
        SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
        RefreshCoordinator refresh(school, notices, sources);
        const auto providerDirectory = data.filePath("providers");
        AiProviderStore store(providerDirectory);
        store.load();
        store.upsert(AiProviderConfig::deepSeekPreset());
        AiProviderConfig example{"ui-demo-compatible", "【演示】自选模型路由",
            "https://route.example.com/v1/messages", "my-model-4.1", AiApiProtocol::DeepSeekNative};
        example.fullUrl = true;
        example.notes = "Route demo; no key or requests";
        example.website = "https://portal.example.com/";
        example.authMode = AiAuthMode::BearerToken;
        store.upsert(example);
        BrandTheme::installApplication(application);
        QMainWindow window;
        window.setWindowTitle("CampusPulse · AI 提供方界面验收（演示）");
        window.resize(1240, 820);
        auto *page = new AiSourcesPage(school, registry, refresh, &window, providerDirectory);
        window.setCentralWidget(page);
        BrandTheme::applyWindow(window);
        placeWindowOnDesktop(window, parser.value("desktop-id"));
        window.show();
        AiProviderDialog editor(store, example, &window);
        editor.setWindowTitle("CampusPulse · AI 配置表单验收（演示）");
        placeWindowOnDesktop(editor, parser.value("desktop-id"));
        QJsonObject evidence{{"kind", "native-production-ai-provider-widgets"},
            {"demo_only", true}, {"desktop_id", parser.value("desktop-id")},
            {"network_requests", 0}, {"credentials_supplied", false},
            {"normal_database_used", false}, {"global_settings_used", false},
            {"screenshot_method", "QWidget::grab of native production widgets"}};
        QTimer::singleShot(600, &application, [&] {
            try {
                const bool pageSaved = window.grab().save(output + "/ai-providers.png");
                evidence["provider_page_visible"] = page->isVisible();
                evidence["provider_count"] = control<QListWidget>(*page, "aiProviderList")->count();
                evidence["current_badge"] = control<QLabel>(*page, "aiActiveProvider")->text();
                evidence["key_masked"] = control<QLineEdit>(*page, "deepseekApiKey")->echoMode() ==
                                           QLineEdit::Password;
                evidence["remember_default_off"] = !control<QCheckBox>(*page, "aiRememberKey")->isChecked();
                evidence["provider_screenshot_saved"] = pageSaved;
                editor.show();
                QTimer::singleShot(500, &application, [&] {
                    try {
                        control<QPushButton>(editor, "aiProviderTestConnection")->click();
                        const bool guarded = control<QLabel>(editor, "aiProviderDialogStatus")
                            ->text().contains("尚未发起请求") &&
                            !editor.findChild<AiProviderProbe *>()->busy();
                        const bool dialogSaved = editor.grab().save(output + "/ai-provider-form.png");
                        evidence["editor_visible"] = editor.isVisible();
                        evidence["editor_no_key_guard"] = guarded;
                        evidence["editor_screenshot_saved"] = dialogSaved;
                        evidence["route_capability"] = control<QLabel>(editor, "aiProviderCapability")->text();
                        evidence["route_editable"] = !control<QLineEdit>(editor, "aiProviderBaseUrl")->isReadOnly();
                        evidence["full_url_enabled"] = control<QCheckBox>(editor, "aiProviderFullUrl")->isChecked();
                        evidence["custom_route_preserved"] = control<QLineEdit>(editor, "aiProviderBaseUrl")->text() == example.baseUrl;
                        evidence["passed"] = pageSaved && dialogSaved && guarded &&
                            evidence.value("key_masked").toBool() &&
                            evidence.value("remember_default_off").toBool() &&
                            evidence.value("provider_count").toInt() == 2 &&
                            evidence.value("route_editable").toBool() &&
                            evidence.value("custom_route_preserved").toBool();
                        writeArtifact(output + "/ai-provider-native.json", QJsonDocument(evidence).toJson());
                        const bool passed = evidence.value("passed").toBool();
                        editor.close();
                        window.close();
                        application.exit(passed ? 0 : 2);
                    } catch (const std::exception &error) {
                        QTextStream(stderr) << QString::fromUtf8(error.what()) << '\n';
                        application.exit(2);
                    }
                });
            } catch (const std::exception &error) {
                QTextStream(stderr) << QString::fromUtf8(error.what()) << '\n';
                application.exit(2);
            }
        });
        return application.exec();
    } catch (const std::exception &error) {
        QTextStream(stderr) << QString::fromUtf8(error.what()) << '\n';
        return 2;
    }
}
