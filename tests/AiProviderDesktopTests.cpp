#include "adapters/AiProviderConfig.h"
#include "adapters/AiProviderProbe.h"
#include "adapters/RefreshCoordinator.h"
#include "adapters/UniversityRegistry.h"
#include "desktop/AiProviderDialog.h"
#include "desktop/AiSourcesPage.h"
#include "storage/Database.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteSourceRepository.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <functional>
#include <stdexcept>

using namespace campus;
namespace {
template <class T> T *control(QWidget &widget, const char *name) {
    auto *value = widget.findChild<T *>(name);
    if (!value)
        throw std::runtime_error(std::string("AI 界面控件缺失：") + name);
    return value;
}
SchoolPackage school() {
    SchoolPackage result;
    result.id = "ui-unregistered-university";
    result.name = "【测试】未收录学校";
    result.officialHomepage = QUrl("https://www.ui-school.edu.cn/");
    return result;
}
struct Session {
    SchoolPackage university = school();
    QTemporaryDir folder;
    UniversityRegistry registry;
    Database db;
    SqliteRepository noticeRepository;
    SqliteSourceRepository sourceRepository;
    NoticeService notices;
    SourceService sources;
    RefreshCoordinator refresh;
    Session()
        : registry(folder.path()), db(folder.filePath("ui.sqlite")),
          noticeRepository(db), sourceRepository(db),
          notices(noticeRepository, university.id.toStdString()),
          sources(university.id.toStdString(), university.catalog, sourceRepository),
          refresh(university, notices, sources) {}
    QString providersPath() const { return folder.filePath("providers"); }
};
bool editThroughButton(AiSourcesPage &page, const char *button,
                       const std::function<void(AiProviderDialog &)> &fill) {
    bool opened = false;
    QTimer::singleShot(0, &page, [&] {
        auto *dialog = qobject_cast<AiProviderDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        opened = true;
        QTimer::singleShot(3000, dialog, &QDialog::reject);
        fill(*dialog);
        control<QPushButton>(*dialog, "saveAiProviderButton")->click();
    });
    control<QPushButton>(page, button)->click();
    return opened;
}
QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace

class AiProviderDesktopTests final : public QObject {
    Q_OBJECT
  private slots:
    void defaultPageIsMaskedAndDoesNotRequestOrPersistCredentials() {
        Session s;
        AiSourcesPage page(s.university, s.registry, s.refresh, nullptr, s.providersPath());
        QCOMPARE(control<QListWidget>(page, "aiProviderList")->count(), 1);
        QVERIFY(control<QListWidget>(page, "aiProviderList")->item(0)->text().contains("当前启用"));
        QCOMPARE(control<QLineEdit>(page, "deepseekApiKey")->echoMode(), QLineEdit::Password);
        QVERIFY(!control<QCheckBox>(page, "aiRememberKey")->isChecked());
        QVERIFY(!control<QCheckBox>(page, "aiAutoSupplement")->isChecked());
        QVERIFY(!QFile::exists(s.providersPath() + "/providers.json"));
        QVERIFY(!QFile::exists(s.providersPath() + "/credentials.dpapi.json"));
        QVERIFY(!page.findChild<AiProviderProbe *>()->busy());
        QVERIFY(!s.refresh.busy());
        control<QPushButton>(page, "testAiProviderButton")->click();
        QVERIFY(control<QLabel>(page, "aiStatus")->text().contains("尚未发起请求"));
        control<QPushButton>(page, "aiSearchButton")->click();
        QVERIFY(control<QLabel>(page, "aiStatus")->text().contains("尚未发起请求"));
        QVERIFY(!page.findChild<AiProviderProbe *>()->busy());
    }

    void editorTemplatesManualModelAndExplicitNoKeyGuards() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        AiProviderDialog editor(store, {});
        QCOMPARE(control<QComboBox>(editor, "aiProviderPreset")->currentIndex(), 0);
        QVERIFY(!control<QLineEdit>(editor, "aiProviderBaseUrl")->isReadOnly());
        QVERIFY(!control<QCheckBox>(editor, "aiProviderRememberKey")->isChecked());
        auto *key = control<QLineEdit>(editor, "aiProviderKey");
        QCOMPARE(key->echoMode(), QLineEdit::Password);
        key->setText("synthetic-ui-key");
        control<QToolButton>(editor, "aiProviderRevealKey")->click();
        QCOMPARE(key->echoMode(), QLineEdit::Normal);
        control<QComboBox>(editor, "aiProviderPreset")->setCurrentIndex(1);
        QVERIFY(key->text().isEmpty());
        QVERIFY(!control<QLineEdit>(editor, "aiProviderBaseUrl")->isReadOnly());
        control<QLineEdit>(editor, "aiProviderBaseUrl")->setText("https://api.example.com/v1");
        control<QComboBox>(editor, "aiProviderModel")->setCurrentText("manual-model");
        control<QPushButton>(editor, "aiProviderFetchModels")->click();
        QVERIFY(control<QLabel>(editor, "aiProviderDialogStatus")->text().contains("尚未发起请求"));
        control<QPushButton>(editor, "aiProviderTestConnection")->click();
        QVERIFY(control<QLabel>(editor, "aiProviderDialogStatus")->text().contains("尚未发起请求"));
        QVERIFY(!editor.findChild<AiProviderProbe *>()->busy());
        QVERIFY(control<QLabel>(editor, "aiProviderCapability")->text().contains("不等同于"));
    }

    void nativeRouteCanBeChosenWithFullUrlAuthAndPrivatePreview() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        AiProviderDialog editor(store, AiProviderConfig::deepSeekPreset());
        auto *route = control<QLineEdit>(editor, "aiProviderBaseUrl");
        QVERIFY(!route->isReadOnly());
        auto *key = control<QLineEdit>(editor, "aiProviderKey");
        key->setText("synthetic-route-secret");
        route->selectAll();
        QTest::keyClicks(route, "https://route.example.com/custom/messages");
        QVERIFY(key->text().isEmpty());
        control<QCheckBox>(editor, "aiProviderFullUrl")->setChecked(true);
        auto *protocol = control<QComboBox>(editor, "aiProviderProtocol");
        protocol->setCurrentIndex(1);
        QCOMPARE(route->text(), QString("https://route.example.com/custom/messages"));
        protocol->setCurrentIndex(0);
        QCOMPARE(route->text(), QString("https://route.example.com/custom/messages"));
        control<QComboBox>(editor, "aiProviderAuthMode")->setCurrentIndex(1);
        control<QLineEdit>(editor, "aiProviderNotes")->setText("Route test");
        control<QLineEdit>(editor, "aiProviderWebsite")->setText("https://portal.example.com/");
        control<QComboBox>(editor, "aiProviderModel")->setCurrentText("my-model-4.1");
        const auto resolved = control<QLabel>(editor, "aiProviderResolvedEndpoint")->text();
        QVERIFY(resolved.endsWith("https://route.example.com/custom/messages"));
        key->setText("synthetic-route-secret");
        const auto preview = control<QPlainTextEdit>(editor, "aiProviderConfigPreview")->toPlainText();
        QVERIFY(preview.contains("my-model-4.1"));
        QVERIFY(!preview.contains("synthetic-route-secret"));
        QVERIFY(!preview.contains("api_key"));
        control<QPushButton>(editor, "saveAiProviderButton")->click();
        QCOMPARE(editor.result(), int(QDialog::Accepted));
        const auto saved = editor.savedProvider();
        QCOMPARE(saved.baseUrl, QString("https://route.example.com/custom/messages"));
        QVERIFY(saved.fullUrl);
        QVERIFY(saved.nativeSearch());
        QVERIFY(!saved.isOfficialDeepSeek());
        QCOMPARE(saved.authMode, AiAuthMode::BearerToken);
        QCOMPARE(saved.notes, QString("Route test"));
        QCOMPARE(store.key(saved.id), QString("synthetic-route-secret"));
        AiProviderStore reopened(folder.path());
        reopened.load();
        QCOMPARE(reopened.providers().first().baseUrl, saved.baseUrl);
        QVERIFY(!readFile(folder.filePath("providers.json")).contains("synthetic-route-secret"));
    }

    void actualAddEditActivateStopDeleteAndReload() {
        Session s;
        AiSourcesPage page(s.university, s.registry, s.refresh, nullptr, s.providersPath());
        QVERIFY(editThroughButton(page, "addAiProviderButton", [](AiProviderDialog &editor) {
            control<QComboBox>(editor, "aiProviderPreset")->setCurrentIndex(1);
            control<QLineEdit>(editor, "aiProviderName")->setText("【测试】兼容提供方");
            control<QLineEdit>(editor, "aiProviderBaseUrl")->setText("https://api.example.com/v1");
            control<QComboBox>(editor, "aiProviderModel")->setCurrentText("manual-model");
        }));
        auto *list = control<QListWidget>(page, "aiProviderList");
        QCOMPARE(list->count(), 2);
        const auto addedId = list->currentItem()->data(Qt::UserRole).toString();
        QVERIFY(addedId != "deepseek-official");
        QVERIFY(control<QLabel>(page, "aiActiveProvider")->text().contains("DeepSeek"));
        QVERIFY(control<QLabel>(page, "aiProviderSearchCapability")->text().contains("不代表联网搜索"));
        control<QPushButton>(page, "activateAiProviderButton")->click();
        QVERIFY(control<QLabel>(page, "aiActiveProvider")->text().contains("兼容提供方"));
        QVERIFY(list->currentItem()->text().contains("当前启用"));
        QVERIFY(control<QPushButton>(page, "aiSearchButton")->text().contains("提出栏目候选"));
        QVERIFY(editThroughButton(page, "editAiProviderButton", [](AiProviderDialog &editor) {
            control<QLineEdit>(editor, "aiProviderName")->setText("【测试】已改名");
            control<QComboBox>(editor, "aiProviderModel")->setCurrentText("changed-model");
        }));
        QCOMPARE(list->count(), 2);
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), addedId);
        QCOMPARE(control<QLineEdit>(page, "deepseekModel")->text(), QString("changed-model"));
        control<QPushButton>(page, "disableAiProviderButton")->click();
        QVERIFY(!control<QPushButton>(page, "aiSearchButton")->isEnabled());
        QVERIFY(!control<QCheckBox>(page, "aiAutoSupplement")->isEnabled());
        AiProviderStore saved(s.providersPath());
        saved.load();
        QCOMPARE(saved.providers().size(), 2);
        QVERIFY(saved.activeId().isEmpty());
        control<QPushButton>(page, "activateAiProviderButton")->click();
        QTimer::singleShot(0, &page, [] {
            if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                dialog->button(QMessageBox::Yes)->click();
        });
        control<QPushButton>(page, "removeAiProviderButton")->click();
        QCOMPARE(list->count(), 1);
        QVERIFY(!control<QPushButton>(page, "aiSearchButton")->isEnabled());
        saved.load();
        QCOMPARE(saved.providers().size(), 1);
        QVERIFY(saved.activeId().isEmpty());
    }

    void sessionKeyStaysMaskedAndNeverEntersPlainConfiguration() {
        Session s;
        AiSourcesPage page(s.university, s.registry, s.refresh, nullptr, s.providersPath());
        auto *key = control<QLineEdit>(page, "deepseekApiKey");
        key->setText("synthetic-ui-key");
        control<QPushButton>(page, "saveAiProviderSettingsButton")->click();
        QVERIFY(key->text() == "synthetic-ui-key");
        QCOMPARE(key->echoMode(), QLineEdit::Password);
        QVERIFY(!readFile(s.providersPath() + "/providers.json").contains("synthetic-ui-key"));
        QVERIFY(!QFile::exists(s.providersPath() + "/credentials.dpapi.json"));
        // A new page for another university gets the shared process session, without a disk secret.
        AiSourcesPage reopened(s.university, s.registry, s.refresh, nullptr, s.providersPath());
        QVERIFY(control<QLineEdit>(reopened, "deepseekApiKey")->text() == "synthetic-ui-key");
        QCOMPARE(control<QLineEdit>(reopened, "deepseekApiKey")->echoMode(), QLineEdit::Password);
    }

    void changedProtocolAndTypedEndpointInvalidateLoadedKeys() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        const auto provider = AiProviderConfig::deepSeekPreset();
        store.setKey(provider.id, "synthetic-ui-key", false);
        AiProviderDialog editor(store, provider);
        QVERIFY(control<QLineEdit>(editor, "aiProviderKey")->text() == "synthetic-ui-key");
        control<QComboBox>(editor, "aiProviderProtocol")->setCurrentIndex(1);
        QVERIFY(control<QLineEdit>(editor, "aiProviderKey")->text().isEmpty());
        QVERIFY(!control<QCheckBox>(editor, "aiProviderRememberKey")->isChecked());
        auto *key = control<QLineEdit>(editor, "aiProviderKey");
        key->setText("synthetic-ui-key");
        auto *url = control<QLineEdit>(editor, "aiProviderBaseUrl");
        url->selectAll();
        QTest::keyClicks(url, "https://api.example.com/v1");
        QVERIFY(key->text().isEmpty());
    }

    void returnedModelDirectoryCanBeSelectedWithoutNetworkOrOverwritingManualEntry() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        AiProviderDialog editor(store, {});
        auto *model = control<QComboBox>(editor, "aiProviderModel");
        model->setCurrentText("my-manual-model");
        AiProbeResult result;
        result.operation = AiProbeOperation::Models;
        result.success = true;
        result.httpStatus = 200;
        result.modelIds = {"directory-model-a", "directory-model-b"};
        result.message = "【测试响应】模型目录读取成功，不认证搜索能力";
        auto *probe = editor.findChild<AiProviderProbe *>();
        QVERIFY(QMetaObject::invokeMethod(probe, "finished", Qt::DirectConnection,
                                          Q_ARG(campus::AiProbeResult, result)));
        QCOMPARE(model->currentText(), QString("my-manual-model"));
        QVERIFY(model->findText("directory-model-a") >= 0);
        model->setCurrentIndex(model->findText("directory-model-b"));
        QCOMPARE(model->currentText(), QString("directory-model-b"));
        QVERIFY(!probe->busy());
    }

    void invalidEndpointCannotBeSavedOrTested() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        AiProviderDialog editor(store, {});
        control<QComboBox>(editor, "aiProviderPreset")->setCurrentIndex(1);
        control<QLineEdit>(editor, "aiProviderBaseUrl")->setText("http://127.0.0.1:8080/v1");
        control<QComboBox>(editor, "aiProviderModel")->setCurrentText("manual-model");
        control<QLineEdit>(editor, "aiProviderKey")->setText("synthetic-ui-key");
        control<QPushButton>(editor, "saveAiProviderButton")->click();
        QVERIFY(editor.result() != QDialog::Accepted);
        QCOMPARE(store.providers().size(), 1);
        control<QPushButton>(editor, "aiProviderTestConnection")->click();
        QVERIFY(!editor.findChild<AiProviderProbe *>()->busy());
        QVERIFY(control<QLabel>(editor, "aiProviderDialogStatus")->text().contains("HTTPS"));
        QVERIFY(!QFile::exists(folder.filePath("providers.json")));
    }
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qunsetenv("DEEPSEEK_API_KEY");
    QApplication application(argc, argv);
    application.setOrganizationName("CampusPulseUiTests");
    application.setApplicationName("AiProviderDesktopTests");
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    AiProviderDesktopTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "AiProviderDesktopTests.moc"
