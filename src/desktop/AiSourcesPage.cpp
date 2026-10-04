#include "desktop/AiSourcesPage.h"
#include "desktop/AiProviderDialog.h"
#include "adapters/SchoolOnboarding.h"
#include "adapters/UniversityRegistry.h"
#include "adapters/RefreshCoordinator.h"
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <stdexcept>

namespace campus {
namespace {
QString providerFolder(const QString &specified) {
    return specified.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/ai-providers"
        : specified;
}
QString usageText(const QJsonObject &usage) {
    const auto input = usage.contains("input_tokens") ? usage.value("input_tokens")
                                                      : usage.value("prompt_tokens");
    const auto output = usage.contains("output_tokens") ? usage.value("output_tokens")
                                                        : usage.value("completion_tokens");
    return QString("输入 token %1 · 输出 token %2")
        .arg(input.isDouble() ? input.toVariant().toString() : "未返回",
             output.isDouble() ? output.toVariant().toString() : "未返回") +
        (usage.value("search_limited").toBool() ? " · 搜索预算已达上限，采用已返回的部分实际结果" : "");
}
} // namespace

AiSourcesPage::AiSourcesPage(const SchoolPackage &school, const UniversityRegistry &registry,
                             RefreshCoordinator &refresh, QWidget *parent,
                             QString providerDirectory)
    : QWidget(parent), school_(school), providers_(providerFolder(providerDirectory)),
      probe_(this), search_(this) {
    setObjectName("aiSourcesPage");
    for (const auto &entry : registry.list())
        if (entry.id == school.id)
            root_ = entry.homepage.host();
    if (root_.isEmpty())
        root_ = school_.officialHomepage.host();
    root_ = root_.toLower();
    if (root_.startsWith("www."))
        root_.remove(0, 4);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    auto *heading = new QLabel("AI 补充官网栏目");
    heading->setProperty("brandRole", "heading");
    layout->addWidget(heading);
    auto *intro = new QLabel(
        "先运行规则爬虫，再用所选提供方补充公开栏目候选。普通采集不调用模型；"
        "AI 请求可能消耗 token，仅使用学校名称、官网域与已有公开栏目。候选必须通过本校域名、列表和正文校验，"
        "不能据此判断账号权限或资源全文是否可读。");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *panels = new QHBoxLayout;
    auto *providerPanel = new QGroupBox("我的 AI 提供方");
    auto *providerLayout = new QVBoxLayout(providerPanel);
    providerList_ = new QListWidget;
    providerList_->setObjectName("aiProviderList");
    providerList_->setAccessibleName("AI 提供方列表，当前启用项有标记");
    providerList_->setMinimumHeight(120);
    providerLayout->addWidget(providerList_, 1);
    auto *editRow = new QHBoxLayout;
    add_ = new QPushButton("添加");
    add_->setObjectName("addAiProviderButton");
    edit_ = new QPushButton("编辑");
    edit_->setObjectName("editAiProviderButton");
    remove_ = new QPushButton("删除");
    remove_->setObjectName("removeAiProviderButton");
    for (auto *button : {add_, edit_, remove_})
        editRow->addWidget(button);
    providerLayout->addLayout(editRow);
    auto *activateRow = new QHBoxLayout;
    activate_ = new QPushButton("启用所选");
    activate_->setObjectName("activateAiProviderButton");
    disable_ = new QPushButton("停用 AI");
    disable_->setObjectName("disableAiProviderButton");
    activateRow->addWidget(activate_);
    activateRow->addWidget(disable_);
    providerLayout->addLayout(activateRow);
    panels->addWidget(providerPanel, 2);

    auto *settingsPanel = new QGroupBox("所选配置");
    auto *settingsLayout = new QVBoxLayout(settingsPanel);
    active_ = new QLabel;
    active_->setObjectName("aiActiveProvider");
    active_->setTextFormat(Qt::PlainText);
    active_->setWordWrap(true);
    settingsLayout->addWidget(active_);
    selected_ = new QLabel;
    selected_->setObjectName("aiSelectedProvider");
    selected_->setTextFormat(Qt::PlainText);
    selected_->setWordWrap(true);
    settingsLayout->addWidget(selected_);
    auto *form = new QFormLayout;
    auto *keyRow = new QHBoxLayout;
    key_ = new QLineEdit;
    key_->setEchoMode(QLineEdit::Password);
    key_->setObjectName("deepseekApiKey");
    key_->setAccessibleName("所选提供方的 API Key");
    key_->setMaxLength(8192);
    key_->setPlaceholderText("本次使用；可选择加密记住");
    reveal_ = new QToolButton;
    reveal_->setObjectName("aiRevealKeyButton");
    reveal_->setCheckable(true);
    reveal_->setText("显示");
    reveal_->setAccessibleName("显示或隐藏 API Key");
    keyRow->addWidget(key_, 1);
    keyRow->addWidget(reveal_);
    form->addRow("API Key", keyRow);
    model_ = new QLineEdit;
    model_->setObjectName("deepseekModel");
    model_->setAccessibleName("所选提供方的模型 ID");
    model_->setMaxLength(160);
    model_->setPlaceholderText("可手动输入；在编辑配置中获取模型列表");
    form->addRow("模型", model_);
    settingsLayout->addLayout(form);
    remember_ = new QCheckBox("在此 Windows 用户下加密记住 Key");
    remember_->setObjectName("aiRememberKey");
    remember_->setToolTip("默认关闭。勾选并保存后使用 Windows DPAPI 加密；"
                          "不勾选并保存会移除已保存的密钥。Key 不写入学校包、普通配置或日志。");
    settingsLayout->addWidget(remember_);
    capability_ = new QLabel;
    capability_->setObjectName("aiProviderSearchCapability");
    capability_->setWordWrap(true);
    capability_->setTextFormat(Qt::PlainText);
    settingsLayout->addWidget(capability_);
    auto *settingsButtons = new QHBoxLayout;
    save_ = new QPushButton("保存模型与 Key");
    save_->setObjectName("saveAiProviderSettingsButton");
    test_ = new QPushButton("测试所选连接与模型");
    test_->setObjectName("testAiProviderButton");
    test_->setToolTip("显式发送一次短模型请求，可能消耗 token；成功不代表联网搜索可用。");
    settingsButtons->addWidget(save_);
    settingsButtons->addWidget(test_);
    settingsLayout->addLayout(settingsButtons);
    panels->addWidget(settingsPanel, 3);
    layout->addLayout(panels);

    automatic_ = new QCheckBox("规则更新完成后自动补充（每校至少间隔 1 小时，可能消耗 token）");
    automatic_->setObjectName("aiAutoSupplement");
    automatic_->setChecked(QSettings().value("ai/enabled", false).toBool());
    connect(automatic_, &QCheckBox::toggled, this,
            [](bool checked) { QSettings().setValue("ai/enabled", checked); });
    layout->addWidget(automatic_);
    run_ = new QPushButton("补充栏目候选并校验接入");
    run_->setObjectName("aiSearchButton");
    layout->addWidget(run_);
    status_ = new QLabel("尚未发起请求。连接测试只验证接口与模型回复，不证明搜索或官网覆盖。");
    status_->setObjectName("aiStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    results_ = new QListWidget;
    results_->setObjectName("aiCandidates");
    layout->addWidget(results_, 1);
    connect(providerList_, &QListWidget::currentRowChanged, this, &AiSourcesPage::loadSelected);
    connect(add_, &QPushButton::clicked, this, [this] { editProvider(true); });
    connect(edit_, &QPushButton::clicked, this, [this] { editProvider(false); });
    connect(providerList_, &QListWidget::itemDoubleClicked, this,
            [this] { editProvider(false); });
    connect(activate_, &QPushButton::clicked, this, &AiSourcesPage::activateSelected);
    connect(remove_, &QPushButton::clicked, this, &AiSourcesPage::removeSelected);
    connect(disable_, &QPushButton::clicked, this, [this] {
        try {
            providers_.setActive({});
            automatic_->setChecked(false);
            reloadProviders();
            status_->setText("AI 已停用，普通官网采集不受影响。尚未发送请求。");
        } catch (const std::exception &error) {
            status_->setText(QString::fromUtf8(error.what()));
        }
    });
    connect(save_, &QPushButton::clicked, this, &AiSourcesPage::saveSelected);
    connect(test_, &QPushButton::clicked, this, &AiSourcesPage::testSelected);
    connect(reveal_, &QToolButton::toggled, this, [this](bool visible) {
        key_->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        reveal_->setText(visible ? "隐藏" : "显示");
    });
    connect(run_, &QPushButton::clicked, this, &AiSourcesPage::run);
    connect(&probe_, &AiProviderProbe::finished, this, [this](const AiProbeResult &result) {
        updateEnabled();
        status_->setText(QString("%1 · HTTP %2 · %3 ms\n%4\n连接测试不代表联网搜索可用。")
                             .arg(result.success ? "接口测试通过" : "接口测试失败")
                             .arg(result.httpStatus)
                             .arg(result.elapsedMs)
                             .arg(result.message));
        if (result.success) {
            status_->setText(status_->text() + "\n返回模型：" +
                             (result.responseModel.isEmpty() ? "未返回" : result.responseModel) +
                             " · " + usageText(result.usage));
        }
    });
    connect(&search_, &DeepSeekSearch::failed, this, [this](const QString &reason) {
        busy_ = false;
        updateEnabled();
        status_->setText(reason);
    });
    connect(&search_, &DeepSeekSearch::finished, this, &AiSourcesPage::validate);
    connect(&refresh, &RefreshCoordinator::started, this, [this] {
        refreshing_ = true;
        updateEnabled();
    });
    connect(&refresh, &RefreshCoordinator::finished, this, [this](int good, int) {
        refreshing_ = false;
        updateEnabled();
        const auto last = QSettings().value("ai/lastAttempt/" + school_.id).toLongLong();
        if (storeReady_ && !probe_.busy() && automatic_->isChecked() && good > 0 &&
            !activeProvider().id.isEmpty() && !activeKey().isEmpty() &&
            QDateTime::currentSecsSinceEpoch() - last >= 3600)
            QTimer::singleShot(0, this, &AiSourcesPage::run);
    });
    try {
        providers_.load();
        storeReady_ = true;
        reloadProviders(providers_.activeId());
    } catch (const std::exception &error) {
        status_->setText("AI 配置读取失败，未覆盖原文件：" + QString::fromUtf8(error.what()));
        updateEnabled();
    }
}

AiProviderConfig AiSourcesPage::selectedProvider() const {
    if (const auto *item = providerList_->currentItem())
        for (const auto &provider : providers_.providers())
            if (provider.id == item->data(Qt::UserRole).toString())
                return provider;
    return {};
}
AiProviderConfig AiSourcesPage::activeProvider() const {
    for (const auto &provider : providers_.providers())
        if (provider.id == providers_.activeId())
            return provider;
    return {};
}
QString AiSourcesPage::activeKey() const {
    const auto provider = activeProvider();
    if (provider.id.isEmpty())
        return {};
    if (provider.id == selectedProvider().id)
        return key_->text().trimmed();
    try {
        auto key = providers_.key(provider.id);
        if (key.isEmpty() && provider.isOfficialDeepSeek())
            key = DeepSeekSearch::sessionKey();
        return key;
    } catch (const std::exception &) {
        return {};
    }
}
void AiSourcesPage::reloadProviders(QString selectedId) {
    if (selectedId.isEmpty())
        selectedId = selectedProvider().id;
    providerList_->blockSignals(true);
    providerList_->clear();
    int selectedRow = -1;
    for (const auto &provider : providers_.providers()) {
        const bool active = provider.id == providers_.activeId();
        const auto capability = provider.nativeSearch() ? "搜索工具模式" : "兼容接口 · 候选建议";
        auto *item = new QListWidgetItem(
            (active ? "● 当前启用  " : "○ ") + provider.name + "\n" +
            (provider.model.isEmpty() ? "模型待选择" : provider.model) + " · " + capability,
            providerList_);
        item->setData(Qt::UserRole, provider.id);
        item->setToolTip(provider.baseUrl);
        if (provider.id == selectedId)
            selectedRow = providerList_->count() - 1;
    }
    if (selectedRow < 0 && providerList_->count())
        selectedRow = 0;
    providerList_->setCurrentRow(selectedRow);
    providerList_->blockSignals(false);
    const auto active = activeProvider();
    active_->setText(active.id.isEmpty() ? "当前启用：无（AI 已停用）"
                                        : "当前启用：" + active.name);
    run_->setText(active.nativeSearch() ? "通过所选路由搜索栏目并校验"
                                      : "让兼容模型提出栏目候选并校验");
    loadSelected();
}
void AiSourcesPage::loadSelected() {
    const auto provider = selectedProvider();
    selected_->setText(provider.id.isEmpty() ? "请选择或添加提供方"
        : "所选：" + provider.name + "\n" + provider.baseUrl);
    model_->setText(provider.model);
    key_->clear();
    reveal_->setChecked(false);
    remember_->setChecked(!provider.id.isEmpty() && providers_.keyIsRemembered(provider.id));
    capability_->setText(provider.nativeSearch()
        ? "工具搜索：使用所选路由；接口与模型须支持搜索工具，连接成功不代表搜索可用。"
        : "候选建议：兼容模型不代表联网搜索，提出的地址还要由爬虫实查。");
    if (!provider.id.isEmpty())
        try {
            auto key = providers_.key(provider.id);
            if (key.isEmpty() && provider.isOfficialDeepSeek())
                key = DeepSeekSearch::sessionKey();
            key_->setText(key);
        } catch (const std::exception &error) {
            status_->setText(QString::fromUtf8(error.what()));
        }
    updateEnabled();
}
void AiSourcesPage::updateEnabled() {
    const bool idle = storeReady_ && !busy_ && !refreshing_ && !probe_.busy();
    const auto selected = selectedProvider();
    const bool hasSelected = !selected.id.isEmpty();
    providerList_->setEnabled(idle);
    add_->setEnabled(idle);
    for (auto *button : {edit_, remove_, save_, test_})
        button->setEnabled(idle && hasSelected);
    activate_->setEnabled(idle && hasSelected && selected.id != providers_.activeId());
    disable_->setEnabled(idle && !providers_.activeId().isEmpty());
    key_->setEnabled(idle && hasSelected);
    model_->setEnabled(idle && hasSelected);
    reveal_->setEnabled(idle && hasSelected);
    remember_->setEnabled(idle && hasSelected && AiProviderStore::persistentSecretsSupported());
    automatic_->setEnabled(idle && !activeProvider().id.isEmpty());
    run_->setEnabled(idle && !activeProvider().id.isEmpty());
}
void AiSourcesPage::editProvider(bool add) {
    if (!storeReady_ || busy_ || refreshing_ || probe_.busy())
        return;
    AiProviderDialog dialog(providers_, add ? AiProviderConfig{} : selectedProvider(), this);
    if (dialog.exec() == QDialog::Accepted) {
        reloadProviders(dialog.savedProvider().id);
        status_->setText("配置已保存。需要时点击启用所选；保存不会发起 API 请求。");
    }
}
void AiSourcesPage::saveSelected() {
    auto provider = selectedProvider();
    if (provider.id.isEmpty())
        return;
    provider.model = model_->text().trimmed();
    const bool remember = remember_->isChecked();
    const auto key = key_->text().trimmed();
    try {
        providers_.upsert(provider);
        providers_.setKey(provider.id, key, remember);
        reloadProviders(provider.id);
        status_->setText(key.isEmpty()
            ? "模型已保存，当前 Key 已清空，未保留凭据。尚未发送请求。"
            : remember
            ? "模型已保存，Key 已用 Windows 当前用户密钥加密。尚未发送请求。"
            : "模型已保存，Key 仅供本次使用，未保留加密凭据。尚未发送请求。");
    } catch (const std::exception &error) {
        status_->setText("保存未完成，请检查配置与密钥状态：" + QString::fromUtf8(error.what()));
    }
}
void AiSourcesPage::activateSelected() {
    const auto provider = selectedProvider();
    if (provider.id.isEmpty())
        return;
    try {
        providers_.setActive(provider.id);
        reloadProviders(provider.id);
        status_->setText("已启用 " + provider.name + "。尚未发起请求。连接测试与栏目补充需要单独点击。");
    } catch (const std::exception &error) {
        status_->setText(QString::fromUtf8(error.what()));
    }
}
void AiSourcesPage::removeSelected() {
    const auto provider = selectedProvider();
    if (provider.id.isEmpty())
        return;
    if (QMessageBox::question(this, "删除提供方", "删除“" + provider.name +
            "”及其本机密钥？学校通知、资源和待办会保留。") != QMessageBox::Yes)
        return;
    try {
        providers_.remove(provider.id);
        if (providers_.activeId().isEmpty())
            automatic_->setChecked(false);
        reloadProviders();
        status_->setText("提供方已删除。尚未发送 API 请求。");
    } catch (const std::exception &error) {
        status_->setText(QString::fromUtf8(error.what()));
    }
}
void AiSourcesPage::testSelected() {
    auto provider = selectedProvider();
    provider.model = model_->text().trimmed();
    auto error = AiProviderConfig::validationError(provider);
    if (error.isEmpty() && key_->text().trimmed().isEmpty())
        error = "请填写所选提供方的 API Key；尚未发起请求，不会消耗 token。";
    if (error.isEmpty() && provider.model.isEmpty())
        error = "请填写模型 ID；尚未发起请求。";
    if (!error.isEmpty()) {
        status_->setText(error);
        return;
    }
    status_->setText("正在发送一次短模型请求，可能消耗 token；不测试联网搜索，不自动重试。");
    probe_.probe(provider, key_->text().trimmed());
    updateEnabled();
}
void AiSourcesPage::run() {
    if (!storeReady_ || busy_ || refreshing_ || probe_.busy())
        return;
    auto provider = activeProvider();
    if (provider.id.isEmpty()) {
        status_->setText("请先启用提供方；尚未发起请求。");
        return;
    }
    if (provider.id == selectedProvider().id)
        provider.model = model_->text().trimmed();
    const auto key = activeKey();
    auto error = AiProviderConfig::validationError(provider);
    if (error.isEmpty() && key.isEmpty())
        error = "请填写当前提供方的 API Key；尚未发起请求，不会消耗 token。";
    if (error.isEmpty() && provider.model.isEmpty())
        error = "请填写当前提供方的模型 ID；尚未发起请求。";
    if (error.isEmpty() && root_.isEmpty())
        error = "当前学校缺少已核验的官方首页，无法限定候选域名；尚未发起请求。";
    if (!error.isEmpty()) {
        status_->setText(error);
        return;
    }
    busy_ = true;
    updateEnabled();
    results_->clear();
    requestedProvider_ = provider;
    requestedModel_ = provider.model;
    QSettings().setValue("ai/lastAttempt/" + school_.id, QDateTime::currentSecsSinceEpoch());
    QSet<QString> known;
    for (const auto &source : school_.catalog) {
        QUrl url(QString::fromStdString(source.entryUrl));
        if (url.scheme() == "http")
            url.setScheme("https");
        known.insert(url.toString(QUrl::FullyEncoded));
    }
    status_->setText(provider.nativeSearch()
        ? "正在通过所选路由调用搜索工具。候选仍需官网校验；不会自动重试。"
        : "正在让兼容模型提出栏目候选。这不是联网搜索证据，后续由爬虫验证；不会自动重试。");
    search_.search(provider, key, school_.name, root_, known);
}

void AiSourcesPage::validate(QJsonArray candidates, QJsonObject usage) {
    const auto auditFolder =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/ai-schools";
    const QJsonObject audit{{"contract_version", requestedProvider_.nativeSearch()
                                                        ? "search-v1" : "suggestion-v1"},
                            {"status", "candidate_only"},
                            {"candidate_origin", requestedProvider_.nativeSearch()
                                                        ? "native_web_search" : "compatible_model_suggestion"},
                            {"school_id", school_.id},
                            {"provider_id", requestedProvider_.id},
                            {"model", requestedModel_},
                            {"model_calls", 1},
                            {"usage", usage},
                            {"candidates", candidates},
                            {"searched_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    QSaveFile auditFile(auditFolder + "/" + school_.id + ".search.json");
    const auto auditBytes = QJsonDocument(audit).toJson();
    if (!QDir().mkpath(auditFolder) || !auditFile.open(QIODevice::WriteOnly) ||
        auditFile.write(auditBytes) != auditBytes.size() || !auditFile.commit()) {
        busy_ = false;
        updateEnabled();
        status_->setText("模型已返回，但候选记录保存失败；未添加来源。");
        return;
    }
    const auto use = usageText(usage);
    if (candidates.empty()) {
        busy_ = false;
        updateEnabled();
        status_->setText("补充完成，没有新的本校栏目候选。" + use);
        return;
    }
    QStringList urls;
    for (const auto &value : candidates) {
        const auto hit = value.toObject();
        urls.append(hit.value("url").toString());
        results_->addItem(hit.value("title").toString() + "\n" + urls.back());
    }
    try {
        QFile current(school_.configFile);
        if (!current.open(QIODevice::ReadOnly))
            throw std::runtime_error("无法读取当前学校配置");
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(current.readAll(), &parseError);
        current.close();
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            throw std::runtime_error("当前学校配置不是有效 JSON");
        auto seed = document.object();
        auto schoolIdentity = seed.value("school").toObject();
        if (schoolIdentity.value("official_homepage").toString().isEmpty() &&
            !school_.officialHomepage.isEmpty()) {
            schoolIdentity["official_homepage"] = school_.officialHomepage.toString();
            seed["school"] = schoolIdentity;
        }
        seed["auto_discovery"] = QJsonObject{{"department_urls", QJsonArray{}}, {"max_pages", 24}};
        const auto folder =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/ai-schools";
        if (!QDir().mkpath(folder))
            throw std::runtime_error("无法创建 AI 补充缓存目录");
        const auto seedFile = folder + "/" + school_.id + ".seed.json";
        QSaveFile file(seedFile);
        const auto bytes = QJsonDocument(seed).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            throw std::runtime_error("无法保存 AI 补充种子");
        auto *scan = new SchoolOnboarding(seedFile, folder, this, 3000, urls);
        connect(scan, &SchoolOnboarding::progress, this,
                [this, use](const QString &message) { status_->setText(use + "\n" + message); });
        connect(scan, &SchoolOnboarding::failed, this, [this, scan, use](const QString &error) {
            busy_ = false;
            updateEnabled();
            status_->setText(use + "\n" + error);
            scan->deleteLater();
        });
        connect(scan, &SchoolOnboarding::finished, this,
                [this, scan, use](const QString &config, int count, int) {
                    busy_ = false;
                    updateEnabled();
                    status_->setText(QString("新增 %1 个已校验来源。%2").arg(count).arg(use));
                    scan->deleteLater();
                    emit configReady(config);
                });
        scan->start();
    } catch (const std::exception &error) {
        busy_ = false;
        updateEnabled();
        status_->setText(use + "\n" + QString::fromUtf8(error.what()));
    }
}
} // namespace campus
