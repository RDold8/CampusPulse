#include "adapters/AiProviderConfig.h"
#include "adapters/AiProviderProbe.h"
#include "adapters/AiSearchTemplate.h"
#include "adapters/AiSearchHistory.h"
#include "adapters/AiSourceRepair.h"
#include "adapters/DeepSeekSearch.h"
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
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
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
        control<QPushButton>(*dialog, "saveAiProviderOnlyButton")->click();
    });
    control<QPushButton>(page, button)->click();
    return opened;
}
QByteArray readFile(const QString &path) {
    const auto dbPath = QFileInfo(path).dir().filePath("ai-providers.sqlite");
    if (QFile::exists(dbPath)) {
        const auto name = "desktop-read-" + QUuid::createUuid().toString();
        QByteArray result;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(dbPath);
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            if (db.open()) {
                QSqlQuery query(db);
                query.prepare("SELECT payload FROM documents WHERE name=?");
                query.addBindValue(QFileInfo(path).fileName());
                if (query.exec() && query.next()) result = query.value(0).toByteArray();
            }
        }
        QSqlDatabase::removeDatabase(name);
        if (!result.isEmpty()) return result;
    }
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace

class AiProviderDesktopTests final : public QObject {
    Q_OBJECT
  private slots:
    void pendingPlanExcludesCollectedAndLoginSources() {
        auto university = school();
        SourceDescription source;
        source.id = "pending"; source.name = "待接入教务";
        source.entryUrl = "https://jwc.ui-school.edu.cn/tzgg/";
        university.catalog.push_back(source);
        source.id = "ready"; source.entryUrl = "https://lib.ui-school.edu.cn/";
        source.ready = source.configuredEnabled = true;
        university.catalog.push_back(source);
        source.id = "login"; source.requiresLogin = true;
        source.entryUrl = "https://sso.ui-school.edu.cn/";
        university.catalog.push_back(source);
        source = {}; source.id = "outside"; source.entryUrl = "https://evil.org/";
        university.catalog.push_back(source);
        QCOMPARE(AiSourceRepair::existing(university).size(), 1);
        const auto targets = AiSourceRepair::pending(university);
        QCOMPARE(targets.size(), 1);
        QCOMPARE(targets.first().toObject().value("source_id").toString(), QString("pending"));
        QVERIFY(!AiSourceRepair::existing(university).contains(targets.first().toObject().value("url").toString()));
        QVERIFY(AiSourceRepair::pending(university, "ready").isEmpty());
    }
    void realCandidatesRemainVisibleAfterValidationFailureAndPageRecreation() {
        Session s;
        const auto candidate = QJsonObject{{"title", "【测试】待接入教务"},
            {"url", "https://jwc.ui-school.edu.cn/tzgg/"}};
        {
            AiSourcesPage page(s.university, s.registry, s.refresh, nullptr, s.providersPath());
            page.findChild<DeepSeekSearch *>()->finished({candidate}, {{"model_calls", 1}, {"input_tokens", 42}});
            const auto *rows = control<QListWidget>(page, "aiCandidates");
            QCOMPARE(rows->count(), 1);
            QVERIFY(rows->item(0)->text().contains("仍待接入"));
            QVERIFY(rows->item(0)->text().contains(candidate.value("url").toString()));
            QVERIFY(control<QPlainTextEdit>(page, "aiSearchFeedback")->toPlainText().contains("无法读取当前学校配置"));
        }
        AiSourcesPage restored(s.university, s.registry, s.refresh, nullptr, s.providersPath());
        const auto *rows = control<QListWidget>(restored, "aiCandidates");
        QCOMPARE(rows->count(), 1);
        QVERIFY(rows->item(0)->text().contains("【测试】待接入教务"));
        const auto report = AiSearchHistory::load(s.providersPath(), s.university.id);
        QCOMPARE(report.value("phase").toString(), QString("failed"));
        QVERIFY(AiSearchHistory::load(s.providersPath(), "another-school").isEmpty());
        QCOMPARE(report.value("added_sources").toInt(), 0);
        restored.resize(1250, 880);
        restored.show();
        QCoreApplication::processEvents();
        const auto evidence = qEnvironmentVariable("CAMPUSPULSE_UI_EVIDENCE");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            QVERIFY(restored.grab().save(QDir(evidence).filePath("ai-results-offline.png")));
        }
    }
    void unwritableStorageStopsBeforeAnyConnectionAndPreservesDraft() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        QVERIFY(QDir().mkdir(folder.filePath("ai-providers.sqlite")));
        AiProviderDialog editor(store, {});
        control<QLineEdit>(editor, "aiProviderKey")->setText("synthetic-retained-draft");
        auto *probe = editor.findChild<AiProviderProbe *>();
        QSignalSpy progress(probe, &AiProviderProbe::progress);
        control<QPushButton>(editor, "saveAiProviderButton")->click();
        QVERIFY(!probe->busy());
        QCOMPARE(progress.count(), 0);
        QVERIFY(control<QLabel>(editor, "aiProviderDialogStatus")->text().contains("尚未发起 API"));
        QCOMPARE(control<QLineEdit>(editor, "aiProviderKey")->text(), QString("synthetic-retained-draft"));
        QCOMPARE(editor.result(), int(QDialog::Rejected));
    }
    void successfulConnectionWithFailedSaveCanRetryLocallyWithChosenModel() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        auto provider = AiProviderConfig::deepSeekPreset();
        store.upsert(provider);
        const auto before = readFile(folder.filePath("providers.json"));
        AiProviderDialog editor(store, provider);
        control<QLineEdit>(editor, "aiProviderKey")->setText("synthetic-retained-connect-key");
        auto *probe = editor.findChild<AiProviderProbe *>();
        control<QPushButton>(editor, "saveAiProviderButton")->click();
        QVERIFY(probe->busy());
        // No event loop is entered: the pending network operation cannot dispatch HTTP.
        const auto connectionName = "ui-write-failure-" + QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(folder.filePath("ai-providers.sqlite"));
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TRIGGER reject_credentials BEFORE UPDATE ON documents "
                               "WHEN NEW.name='credentials.dpapi.json' "
                               "BEGIN SELECT RAISE(ABORT,'synthetic-write-denied'); END"));
        }
        QSqlDatabase::removeDatabase(connectionName);
        provider.model = "server-chosen-model";
        AiProbeResult response;
        response.operation = AiProbeOperation::AutoConnect;
        response.success = true;
        response.provider = provider;
        response.modelIds = {provider.model};
        probe->finished(response);
        probe->cancel();
        QVERIFY(!editor.connectionVerified());
        QVERIFY(control<QLabel>(editor, "aiProviderDialogStatus")->text().contains("连接已确认"));
        QCOMPARE(control<QComboBox>(editor, "aiProviderModel")->currentText(), provider.model);
        QCOMPARE(control<QLineEdit>(editor, "aiProviderKey")->text(), QString("synthetic-retained-connect-key"));
        QCOMPARE(readFile(folder.filePath("providers.json")), before);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(folder.filePath("ai-providers.sqlite"));
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("DROP TRIGGER reject_credentials"));
        }
        QSqlDatabase::removeDatabase(connectionName);
        QSignalSpy progress(probe, &AiProviderProbe::progress);
        control<QPushButton>(editor, "saveAiProviderButton")->click();
        QCOMPARE(progress.count(), 0);
        QVERIFY(!probe->busy());
        QVERIFY(editor.connectionVerified());
        QCOMPARE(editor.result(), int(QDialog::Accepted));
        QCOMPARE(editor.savedProvider().model, QString("server-chosen-model"));
        AiProviderStore reopened(folder.path());
        reopened.load();
        QCOMPARE(reopened.providers().first().model, provider.model);
    }
    void basicEditorRequiresOnlyRouteAndKey() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        AiProviderDialog editor(store, {});
        editor.show();
        QVERIFY(control<QLineEdit>(editor, "aiProviderBaseUrl")->isVisible());
        QVERIFY(control<QLineEdit>(editor, "aiProviderKey")->isVisible());
        QCOMPARE(control<QLineEdit>(editor, "aiProviderBaseUrl")->text(), QString("https://api.deepseek.com"));
        QCOMPARE(control<QLineEdit>(editor, "aiProviderKey")->echoMode(), QLineEdit::Password);
        QVERIFY(!control<QWidget>(editor, "aiProviderAdvancedPanel")->isVisible());
        QVERIFY(!control<QLineEdit>(editor, "aiProviderName")->isVisible());
        QVERIFY(!control<QComboBox>(editor, "aiProviderModel")->isVisible());
        QVERIFY(control<QComboBox>(editor, "aiProviderModel")->currentText().isEmpty());
        QVERIFY(!editor.findChild<QComboBox *>("aiProviderProtocol"));
        QVERIFY(!editor.findChild<QComboBox *>("aiProviderAuthMode"));
        QVERIFY(!editor.findChild<QPlainTextEdit *>("aiProviderConfigPreview"));
        QCOMPARE(control<QPushButton>(editor, "saveAiProviderButton")->text(), QString("保存并连接"));
        QVERIFY(!control<QCheckBox>(editor, "aiProviderRememberKey")->isChecked());
        QVERIFY(!editor.findChild<AiProviderProbe *>()->busy());
        QVERIFY(!QFile::exists(folder.filePath("providers.json")));
        control<QPushButton>(editor, "saveAiProviderButton")->click();
        QVERIFY(control<QLabel>(editor, "aiProviderDialogStatus")->text().contains("API Key"));
        QVERIFY(!editor.findChild<AiProviderProbe *>()->busy());
    }

    void defaultSearchPageHasOneClickScopeAndKeepsConfigurationCollapsed() {
        Session s;
        AiSourcesPage page(s.university, s.registry, s.refresh, nullptr, s.providersPath());
        page.show();
        QVERIFY(control<QPushButton>(page, "configureAiProviderButton")->isVisible());
        QVERIFY(control<QComboBox>(page, "aiSearchTemplate")->isVisible());
        QCOMPARE(control<QComboBox>(page, "aiSearchTemplate")->currentData().toString(), QString("general"));
        QCOMPARE(control<QPushButton>(page, "aiSearchButton")->text(), QString("一键搜索"));
        QVERIFY(control<QPushButton>(page, "aiSearchButton")->isVisible());
        QVERIFY(!control<QWidget>(page, "aiSearchAdvancedPanel")->isVisible());
        QVERIFY(!control<QComboBox>(page, "deepseekModel")->isVisible());
        QVERIFY(!control<QListWidget>(page, "aiProviderList")->isVisible());
        QVERIFY(!page.findChild<QLineEdit *>("aiProviderBaseUrl"));
        QVERIFY(!page.findChild<QLineEdit *>("deepseekApiKey"));
        QVERIFY(!control<QLabel>(page, "aiActiveProvider")->text().contains("https://"));
        QVERIFY(!control<QCheckBox>(page, "aiAutoSupplement")->isChecked());
        bool connectionPrompted = false;
        QTimer::singleShot(0, &page, [&] {
            if (auto *dialog = qobject_cast<AiProviderDialog *>(QApplication::activeModalWidget())) {
                connectionPrompted = true;
                dialog->reject();
            }
        });
        control<QPushButton>(page, "aiSearchButton")->click();
        QVERIFY(connectionPrompted);
        QVERIFY(control<QLabel>(page, "aiStatus")->text().contains("尚未发起搜索请求"));
        QVERIFY(!page.findChild<AiProviderProbe *>()->busy());
        QVERIFY(!s.refresh.busy());
        control<QToolButton>(page, "aiSearchAdvancedToggle")->click();
        QVERIFY(control<QListWidget>(page, "aiProviderList")->isVisible());
        QVERIFY(control<QComboBox>(page, "deepseekModel")->isVisible());
    }

    void advancedSaveAutoNamesAndKeepsUnexposedMetadataWithoutRequests() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        auto provider = AiProviderConfig::deepSeekPreset();
        provider.name.clear();
        provider.baseUrl = "https://route.example.com/v1/messages";
        provider.model = "manual-model";
        provider.notes = "Existing private route note";
        provider.website = "https://portal.example.com/";
        provider.authMode = AiAuthMode::BearerToken;
        provider.fullUrl = true;
        AiProviderDialog editor(store, provider);
        control<QLineEdit>(editor, "aiProviderKey")->setText("synthetic-route-key");
        control<QPushButton>(editor, "saveAiProviderOnlyButton")->click();
        QCOMPARE(editor.result(), int(QDialog::Accepted));
        const auto saved = editor.savedProvider();
        QVERIFY(!saved.name.isEmpty());
        QCOMPARE(saved.notes, provider.notes);
        QCOMPARE(saved.website, provider.website);
        QCOMPARE(saved.authMode, AiAuthMode::BearerToken);
        QVERIFY(saved.fullUrl);
        QCOMPARE(saved.model, QString("manual-model"));
        QCOMPARE(store.key(saved.id), QString("synthetic-route-key"));
        QVERIFY(!editor.connectionVerified());
        QVERIFY(!readFile(folder.filePath("providers.json")).contains("synthetic-route-key"));
        QVERIFY(!editor.findChild<AiProviderProbe *>()->busy());
    }

    void legacyMixedDeepSeekProfileLoadsKeyBeforeNormalizingWithoutDiskMigration() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        auto legacy = AiProviderConfig::deepSeekPreset();
        legacy.baseUrl = "https://api.deepseek.com/anthropic/v1";
        legacy.protocol = AiApiProtocol::OpenAiCompatible;
        store.upsert(legacy);
        store.setKey(legacy.id, "synthetic-legacy-binding-key", false);
        const auto before = readFile(folder.filePath("providers.json"));
        AiProviderDialog editor(store, legacy);
        QCOMPARE(control<QLineEdit>(editor, "aiProviderBaseUrl")->text(), QString("https://api.deepseek.com"));
        QCOMPARE(control<QLineEdit>(editor, "aiProviderKey")->text(), QString("synthetic-legacy-binding-key"));
        QCOMPARE(control<QLineEdit>(editor, "aiProviderKey")->echoMode(), QLineEdit::Password);
        QCOMPARE(readFile(folder.filePath("providers.json")), before);
        QVERIFY(!editor.findChild<AiProviderProbe *>()->busy());
        editor.reject();
        QCOMPARE(readFile(folder.filePath("providers.json")), before);
        QCOMPARE(store.key(legacy.id), QString("synthetic-legacy-binding-key"));
    }

    void typedAddressInvalidatesLoadedKeyButAdvancedToggleDoesNot() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        auto provider = AiProviderConfig::deepSeekPreset();
        store.setKey(provider.id, "synthetic-ui-key", false);
        AiProviderDialog editor(store, provider);
        auto *key = control<QLineEdit>(editor, "aiProviderKey");
        QCOMPARE(key->text(), QString("synthetic-ui-key"));
        control<QToolButton>(editor, "aiProviderAdvancedToggle")->click();
        QCOMPARE(key->text(), QString("synthetic-ui-key"));
        control<QToolButton>(editor, "aiProviderRevealKey")->click();
        QCOMPARE(key->echoMode(), QLineEdit::Normal);
        auto *route = control<QLineEdit>(editor, "aiProviderBaseUrl");
        route->selectAll();
        QTest::keyClicks(route, "https://route.example.com/v1");
        QVERIFY(key->text().isEmpty());
        QVERIFY(!control<QCheckBox>(editor, "aiProviderRememberKey")->isChecked());
        QVERIFY(!editor.findChild<AiProviderProbe *>()->busy());
        editor.reject();
        QCOMPARE(key->echoMode(), QLineEdit::Password);
    }

    void invalidRouteCannotConnectOrSave() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        AiProviderDialog editor(store, {});
        control<QLineEdit>(editor, "aiProviderBaseUrl")->setText("http://127.0.0.1:8080/v1");
        control<QLineEdit>(editor, "aiProviderKey")->setText("synthetic-ui-key");
        control<QPushButton>(editor, "saveAiProviderButton")->click();
        QVERIFY(!editor.findChild<AiProviderProbe *>()->busy());
        QVERIFY(control<QLabel>(editor, "aiProviderDialogStatus")->text().contains("HTTPS"));
        control<QPushButton>(editor, "saveAiProviderOnlyButton")->click();
        QVERIFY(editor.result() != QDialog::Accepted);
        QVERIFY(!QFile::exists(folder.filePath("providers.json")));
    }

    void connectWithNoManualModelFreezesFormAndCanCancelBeforeAnyApiResponse() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        AiProviderDialog editor(store, {});
        // Cancel in this same event-loop turn, before any DNS completion can dispatch an HTTP request.
        // The example.com hostname and synthetic key are test-only; no real model service is used.
        control<QLineEdit>(editor, "aiProviderBaseUrl")->setText("https://provider.example.com/v1");
        control<QLineEdit>(editor, "aiProviderKey")->setText("synthetic-cancel-key");
        QVERIFY(control<QComboBox>(editor, "aiProviderModel")->currentText().isEmpty());
        control<QPushButton>(editor, "saveAiProviderButton")->click();
        QVERIFY(editor.findChild<AiProviderProbe *>()->busy());
        QVERIFY(!control<QLineEdit>(editor, "aiProviderBaseUrl")->isEnabled());
        QVERIFY(!control<QLineEdit>(editor, "aiProviderKey")->isEnabled());
        QVERIFY(!control<QPushButton>(editor, "saveAiProviderButton")->isEnabled());
        QVERIFY(control<QLabel>(editor, "aiProviderDialogStatus")->text().contains("模型"));
        editor.reject();
        QVERIFY(!editor.findChild<AiProviderProbe *>()->busy());
        QVERIFY(!editor.connectionVerified());
        QVERIFY(!QFile::exists(folder.filePath("providers.json")));
    }

    void syntheticAutoConnectCompletionPersistsChosenModelAndActivates() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        auto provider = AiProviderConfig::deepSeekPreset();
        provider.baseUrl = "https://provider.example.com/v1";
        provider.model.clear();
        provider = AiProviderConfig::automaticProfile(provider);
        AiProviderDialog editor(store, provider);
        control<QLineEdit>(editor, "aiProviderKey")->setText("synthetic-connect-key");
        control<QPushButton>(editor, "saveAiProviderButton")->click();
        QVERIFY(editor.findChild<AiProviderProbe *>()->busy());
        // This is a typed synthetic completion for UI persistence, not a live model connection.
        AiProbeResult response;
        response.operation = AiProbeOperation::AutoConnect;
        response.success = true;
        response.httpStatus = 200;
        response.provider = provider;
        response.provider.model = "server-default-model";
        response.modelIds = {"server-default-model", "another-model"};
        auto *probe = editor.findChild<AiProviderProbe *>();
        QVERIFY(QMetaObject::invokeMethod(probe, "finished", Qt::DirectConnection,
                                          Q_ARG(campus::AiProbeResult, response)));
        QCOMPARE(editor.result(), int(QDialog::Accepted));
        QVERIFY(editor.connectionVerified());
        QCOMPARE(editor.savedProvider().model, QString("server-default-model"));
        QCOMPARE(store.activeId(), provider.id);
        QCOMPARE(store.key(provider.id), QString("synthetic-connect-key"));
        QCOMPARE(editor.modelIds(), response.modelIds);
        QVERIFY(!readFile(folder.filePath("providers.json")).contains("synthetic-connect-key"));
        QVERIFY(!probe->busy());
    }

    void syntheticConnectionFailureLeavesEditableFormAndNoSavedSecret() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        AiProviderDialog editor(store, {});
        control<QLineEdit>(editor, "aiProviderBaseUrl")->setText("https://provider.example.com/v1");
        control<QLineEdit>(editor, "aiProviderKey")->setText("synthetic-error-key");
        control<QPushButton>(editor, "saveAiProviderButton")->click();
        auto *probe = editor.findChild<AiProviderProbe *>();
        AiProbeResult response;
        response.operation = AiProbeOperation::AutoConnect;
        response.httpStatus = 403;
        response.message = "HTTP 403: service denied";
        QVERIFY(QMetaObject::invokeMethod(probe, "finished", Qt::DirectConnection,
                                          Q_ARG(campus::AiProbeResult, response)));
        QVERIFY(!editor.connectionVerified());
        QVERIFY(editor.result() != QDialog::Accepted);
        QVERIFY(control<QLineEdit>(editor, "aiProviderBaseUrl")->isEnabled());
        QVERIFY(control<QPushButton>(editor, "saveAiProviderButton")->isEnabled());
        QVERIFY(control<QLabel>(editor, "aiProviderDialogStatus")->text().contains("403"));
        QVERIFY(!QFile::exists(folder.filePath("providers.json")));
        QVERIFY(!QFile::exists(folder.filePath("credentials.dpapi.json")));
        editor.reject();
        QVERIFY(!probe->busy());
    }

    void optionalModelsDirectoryPreservesManualSelectionWithoutCredentialExposure() {
        QTemporaryDir folder;
        AiProviderStore store(folder.path());
        store.load();
        AiProviderDialog editor(store, {});
        auto *model = control<QComboBox>(editor, "aiProviderModel");
        model->setCurrentText("my-manual-model");
        AiProbeResult response;
        response.operation = AiProbeOperation::Models;
        response.success = true;
        response.httpStatus = 200;
        response.modelIds = {"catalog-model-a", "catalog-model-b"};
        auto *probe = editor.findChild<AiProviderProbe *>();
        QVERIFY(QMetaObject::invokeMethod(probe, "finished", Qt::DirectConnection,
                                          Q_ARG(campus::AiProbeResult, response)));
        QCOMPARE(model->currentText(), QString("my-manual-model"));
        QVERIFY(model->findText("catalog-model-a") >= 0);
        QVERIFY(!probe->busy());
        QVERIFY(!QFile::exists(folder.filePath("providers.json")));
    }

    void providerLibraryStillSupportsAddEditActivateStopDeleteAndReload() {
        Session s;
        AiSourcesPage page(s.university, s.registry, s.refresh, nullptr, s.providersPath());
        QVERIFY(editThroughButton(page, "addAiProviderButton", [](AiProviderDialog &editor) {
            control<QLineEdit>(editor, "aiProviderName")->setText("【测试】兼容提供方");
            control<QLineEdit>(editor, "aiProviderBaseUrl")->setText("https://api.example.com/v1");
            control<QComboBox>(editor, "aiProviderModel")->setCurrentText("manual-model");
        }));
        auto *list = control<QListWidget>(page, "aiProviderList");
        QCOMPARE(list->count(), 2);
        const auto addedId = list->currentItem()->data(Qt::UserRole).toString();
        QVERIFY(addedId != "deepseek-official");
        control<QPushButton>(page, "activateAiProviderButton")->click();
        QVERIFY(control<QLabel>(page, "aiActiveProvider")->text().contains("兼容提供方"));
        QCOMPARE(control<QPushButton>(page, "aiSearchButton")->text(), QString("一键搜索"));
        QVERIFY(editThroughButton(page, "editAiProviderButton", [](AiProviderDialog &editor) {
            control<QLineEdit>(editor, "aiProviderName")->setText("【测试】已改名");
            control<QComboBox>(editor, "aiProviderModel")->setCurrentText("changed-model");
        }));
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), addedId);
        QCOMPARE(control<QComboBox>(page, "deepseekModel")->currentText(), QString("changed-model"));
        control<QPushButton>(page, "disableAiProviderButton")->click();
        QVERIFY(!control<QPushButton>(page, "aiSearchButton")->isEnabled());
        AiProviderStore reopened(s.providersPath());
        reopened.load();
        QCOMPARE(reopened.providers().size(), 2);
        QVERIFY(reopened.activeId().isEmpty());
        control<QPushButton>(page, "activateAiProviderButton")->click();
        QTimer::singleShot(0, &page, [] {
            if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                dialog->button(QMessageBox::Yes)->click();
        });
        control<QPushButton>(page, "removeAiProviderButton")->click();
        QCOMPARE(list->count(), 1);
        reopened.load();
        QCOMPARE(reopened.providers().size(), 1);
        QVERIFY(reopened.activeId().isEmpty());
    }

    void modelOnlySaveAndSchoolReloadKeepSessionKeyPrivate() {
        Session s;
        AiSourcesPage page(s.university, s.registry, s.refresh, nullptr, s.providersPath());
        QVERIFY(editThroughButton(page, "editAiProviderButton", [](AiProviderDialog &editor) {
            control<QLineEdit>(editor, "aiProviderKey")->setText("synthetic-ui-key");
        }));
        control<QComboBox>(page, "deepseekModel")->setCurrentText("chosen-model");
        control<QPushButton>(page, "saveAiProviderSettingsButton")->click();
        AiProviderStore reopened(s.providersPath());
        reopened.load();
        QCOMPARE(reopened.key(reopened.activeId()), QString("synthetic-ui-key"));
        QCOMPARE(reopened.providers().first().model, QString("chosen-model"));
        QVERIFY(!readFile(s.providersPath() + "/providers.json").contains("synthetic-ui-key"));
        QVERIFY(!QFile::exists(s.providersPath() + "/credentials.dpapi.json"));
        AiSourcesPage newPage(s.university, s.registry, s.refresh, nullptr, s.providersPath());
        QVERIFY(!newPage.findChild<QLineEdit *>("deepseekApiKey"));
        QCOMPARE(control<QComboBox>(newPage, "deepseekModel")->currentText(), QString("chosen-model"));
    }

    void categoryFocusIsOptionalAndPersistsPerSchoolWithoutApi() {
        Session s;
        AiSourcesPage page(s.university, s.registry, s.refresh, nullptr, s.providersPath());
        auto *templates = control<QComboBox>(page, "aiSearchTemplate");
        QCOMPARE(templates->count(), AiSearchTemplate::defaults().size());
        templates->setCurrentIndex(templates->findData("retake-payment"));
        QVERIFY(control<QLabel>(page, "aiSearchTemplateDescription")->text().contains("缴费"));
        templates->setCurrentIndex(templates->findData("study-resources"));
        QVERIFY(control<QLabel>(page, "aiSearchTemplateDescription")->text().contains("图书馆"));
        AiSourcesPage same(s.university, s.registry, s.refresh, nullptr, s.providersPath());
        QCOMPARE(control<QComboBox>(same, "aiSearchTemplate")->currentData().toString(), QString("study-resources"));
        auto otherSchool = s.university;
        otherSchool.id = "another-school";
        AiSourcesPage other(otherSchool, s.registry, s.refresh, nullptr, s.providersPath());
        QCOMPARE(control<QComboBox>(other, "aiSearchTemplate")->currentData().toString(), QString("general"));
        QVERIFY(!page.findChild<AiProviderProbe *>()->busy());
    }

    void disablingAutomaticSupplementCancelsQueuedAttempt() {
        Session s;
        AiSourcesPage page(s.university, s.registry, s.refresh, nullptr, s.providersPath());
        QVERIFY(editThroughButton(page, "editAiProviderButton", [](AiProviderDialog &editor) {
            control<QLineEdit>(editor, "aiProviderKey")->setText("synthetic-automatic-key");
        }));
        QSettings().setValue("ai/lastAttempt/" + s.university.id, 0);
        auto *automatic = control<QCheckBox>(page, "aiAutoSupplement");
        automatic->setChecked(true);
        auto *status = control<QLabel>(page, "aiStatus");
        const auto before = status->text();
        QVERIFY(QMetaObject::invokeMethod(&s.refresh, "finished", Qt::DirectConnection,
                                         Q_ARG(int, 1), Q_ARG(int, 0)));
        automatic->setChecked(false);
        QCoreApplication::processEvents();
        QCOMPARE(status->text(), before);
        QCOMPARE(QSettings().value("ai/lastAttempt/" + s.university.id).toLongLong(), 0);
        QVERIFY(!page.findChild<AiProviderProbe *>()->busy());
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
