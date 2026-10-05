#include "desktop/AiSourcesPage.h"
#include "desktop/AiProviderDialog.h"
#include "adapters/SchoolOnboarding.h"
#include "adapters/UniversityRegistry.h"
#include "adapters/RefreshCoordinator.h"
#include "adapters/AiSearchTemplate.h"
#include <QCheckBox>
#include <QComboBox>
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
    const auto pages = usage.contains("pages_read")
        ? QString("已读取官网 %1 页、发现 %2 个链接 · ")
              .arg(usage.value("pages_read").toInt()).arg(usage.value("observed_links").toInt())
        : QString{};
    return pages + QString("输入 token %1 · 输出 token %2")
        .arg(input.isDouble() ? input.toVariant().toString() : "未返回",
             output.isDouble() ? output.toVariant().toString() : "未返回") +
        ((usage.value("search_limited").toBool() || usage.value("crawl_limited").toBool())
             ? " · 本次达到检索上限，展示部分结果" : "");
}
bool nativeWebSearch(const AiProviderConfig &provider) {
    return provider.isOfficialDeepSeek() && provider.nativeSearch();
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
    auto *heading = new QLabel("AI 搜索学校信息");
    heading->setProperty("brandRole", "heading");
    layout->addWidget(heading);
    auto *intro = new QLabel(
        "配置接口地址与 Key 后，点击一键搜索。默认综合查找，也可以指定重修缴费、竞赛或学习资源。"
        "AI 会消耗 token；找到的入口仍需官网校验。");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *connectionRow = new QHBoxLayout;
    active_ = new QLabel;
    active_->setObjectName("aiActiveProvider");
    active_->setTextFormat(Qt::PlainText);
    active_->setWordWrap(true);
    connectionRow->addWidget(active_, 1);
    configure_ = new QPushButton("配置接口");
    configure_->setObjectName("configureAiProviderButton");
    connectionRow->addWidget(configure_);
    layout->addLayout(connectionRow);
    connect(configure_, &QPushButton::clicked, this, [this] {
        editProvider(selectedProvider().id.isEmpty());
    });

    auto *advancedToggle = new QToolButton;
    advancedToggle->setObjectName("aiSearchAdvancedToggle");
    advancedToggle->setText("提供方与模型设置（可选）");
    advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    advancedToggle->setArrowType(Qt::RightArrow);
    advancedToggle->setCheckable(true);
    auto *advancedPanel = new QWidget;
    advancedPanel->setObjectName("aiSearchAdvancedPanel");
    auto *advancedLayout = new QVBoxLayout(advancedPanel);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedPanel->hide();
    connect(advancedToggle, &QToolButton::toggled, this, [advancedToggle, advancedPanel](bool expanded) {
        advancedToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        advancedPanel->setVisible(expanded);
    });

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

    auto *settingsPanel = new QGroupBox("模型选择");
    auto *settingsLayout = new QVBoxLayout(settingsPanel);
    selected_ = new QLabel;
    selected_->setObjectName("aiSelectedProvider");
    selected_->setTextFormat(Qt::PlainText);
    selected_->setWordWrap(true);
    settingsLayout->addWidget(selected_);
    auto *form = new QFormLayout;
    model_ = new QComboBox;
    model_->setEditable(true);
    model_->setInsertPolicy(QComboBox::NoInsert);
    model_->setObjectName("deepseekModel");
    model_->setAccessibleName("选择所选提供方的模型，也可输入模型 ID");
    model_->lineEdit()->setMaxLength(160);
    model_->lineEdit()->setPlaceholderText("连接时自动选择；也可手动指定");
    form->addRow("模型", model_);
    settingsLayout->addLayout(form);
    capability_ = new QLabel;
    capability_->setObjectName("aiProviderSearchCapability");
    capability_->setWordWrap(true);
    capability_->setTextFormat(Qt::PlainText);
    settingsLayout->addWidget(capability_);
    auto *settingsButtons = new QHBoxLayout;
    save_ = new QPushButton("保存模型");
    save_->setObjectName("saveAiProviderSettingsButton");
    fetch_ = new QPushButton("获取模型列表");
    fetch_->setObjectName("fetchAiProviderModelsButton");
    fetch_->setToolTip("读取所选提供方的模型目录；服务没有模型列表时可手动填写。连接测试在提供方编辑窗口中进行。");
    settingsButtons->addWidget(save_);
    settingsButtons->addWidget(fetch_);
    settingsLayout->addLayout(settingsButtons);
    panels->addWidget(settingsPanel, 3);
    advancedLayout->addLayout(panels);

    auto *templatePanel = new QGroupBox("搜索范围");
    auto *templateLayout = new QVBoxLayout(templatePanel);
    searchTemplate_ = new QComboBox;
    searchTemplate_->setObjectName("aiSearchTemplate");
    searchTemplate_->setAccessibleName("选择官网栏目搜索类别");
    for (const auto &entry : AiSearchTemplate::defaults())
        searchTemplate_->addItem(entry.name, entry.id);
    const auto templateId = QSettings().value("ai/searchTemplate/" + school_.id, "general").toString();
    const auto templateIndex = searchTemplate_->findData(templateId);
    searchTemplate_->setCurrentIndex(templateIndex < 0 ? 0 : templateIndex);
    templateDescription_ = new QLabel;
    templateDescription_->setObjectName("aiSearchTemplateDescription");
    templateDescription_->setTextFormat(Qt::PlainText);
    templateDescription_->setWordWrap(true);
    templateLayout->addWidget(searchTemplate_);
    templateLayout->addWidget(templateDescription_);
    layout->addWidget(templatePanel);
    connect(searchTemplate_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue("ai/searchTemplate/" + school_.id, searchTemplate_->currentData());
        updateTemplate();
    });
    updateTemplate();

    automatic_ = new QCheckBox("规则更新完成后自动补充（每校至少间隔 1 小时，可能消耗 token）");
    automatic_->setObjectName("aiAutoSupplement");
    automatic_->setChecked(QSettings().value("ai/enabled", false).toBool());
    connect(automatic_, &QCheckBox::toggled, this,
            [](bool checked) { QSettings().setValue("ai/enabled", checked); });
    advancedLayout->addWidget(automatic_);
    run_ = new QPushButton("一键搜索");
    run_->setObjectName("aiSearchButton");
    run_->setMinimumHeight(48);
    layout->addWidget(run_);
    status_ = new QLabel("先配置接口，然后一键搜索。模型自动选择；尚未发起请求。");
    status_->setObjectName("aiStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    layout->addWidget(advancedToggle);
    layout->addWidget(advancedPanel);
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
    connect(fetch_, &QPushButton::clicked, this, &AiSourcesPage::fetchModels);
    connect(run_, &QPushButton::clicked, this, &AiSourcesPage::run);
    connect(&probe_, &AiProviderProbe::finished, this, [this](const AiProbeResult &result) {
        updateEnabled();
        status_->setText(QString("%1 · HTTP %2 · %3 ms\n%4\n模型目录不代表联网搜索可用。")
                             .arg(result.success ? "模型列表读取成功" : "模型列表读取失败")
                             .arg(result.httpStatus)
                             .arg(result.elapsedMs)
                             .arg(result.message));
        if (result.success) {
            const auto current = model_->currentText();
            modelDirectories_[selectedProvider().id] = result.modelIds;
            for (const auto &id : result.modelIds)
                if (model_->findText(id) < 0)
                    model_->addItem(id);
            model_->setCurrentText(current);
        }
    });
    connect(&search_, &DeepSeekSearch::failed, this, [this](const QString &reason) {
        busy_ = false;
        updateEnabled();
        status_->setText(reason);
    });
    connect(&search_, &DeepSeekSearch::progress, this, [this](const QString &message) {
        status_->setText(message);
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
            QTimer::singleShot(0, this, [this] {
                if (automatic_->isChecked())
                    run();
            });
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
AiProviderConfig AiSourcesPage::selectedModelProvider() const {
    auto provider = selectedProvider();
    provider.model = model_->currentText().trimmed();
    return AiProviderConfig::automaticProfile(provider);
}
QString AiSourcesPage::providerKey(const AiProviderConfig &provider) const {
    if (provider.id.isEmpty())
        return {};
    try {
        auto key = providers_.key(provider.id);
        if (key.isEmpty() && provider.isOfficialDeepSeek())
            key = DeepSeekSearch::sessionKey();
        return key;
    } catch (const std::exception &) {
        return {};
    }
}
QString AiSourcesPage::activeKey() const {
    return providerKey(activeProvider());
}
void AiSourcesPage::updateTemplate() {
    const auto entry = AiSearchTemplate::byId(searchTemplate_->currentData().toString());
    templateDescription_->setText(entry.description + "\n" + entry.focus);
}
void AiSourcesPage::reloadProviders(QString selectedId) {
    if (selectedId.isEmpty())
        selectedId = selectedProvider().id;
    providerList_->blockSignals(true);
    providerList_->clear();
    int selectedRow = -1;
    for (const auto &provider : providers_.providers()) {
        const bool active = provider.id == providers_.activeId();
        const auto capability = nativeWebSearch(provider) ? "官网检索" : "官网采集与 AI 筛选";
        auto *item = new QListWidgetItem(
            (active ? "● 当前启用  " : "○ ") + provider.name + "\n" +
            (provider.model.isEmpty() ? "模型待选择" : provider.model) + " · " + capability,
            providerList_);
        item->setData(Qt::UserRole, provider.id);
        item->setToolTip(provider.name);
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
    run_->setText("一键搜索");
    loadSelected();
}
void AiSourcesPage::loadSelected() {
    const auto provider = selectedProvider();
    selected_->setText(provider.id.isEmpty() ? "请选择或添加提供方"
        : "所选：" + provider.name + (provider.id == providers_.activeId() ? "" : "（尚未启用）"));
    model_->clear();
    if (!provider.model.isEmpty())
        model_->addItem(provider.model);
    for (const auto &id : modelDirectories_.value(provider.id))
        if (model_->findText(id) < 0)
            model_->addItem(id);
    model_->setCurrentText(provider.model);
    capability_->setText("连接成功说明接口与模型可回复；搜索时另行核实检索能力，入口仍需官网校验。");
    updateEnabled();
}
void AiSourcesPage::updateEnabled() {
    const bool idle = storeReady_ && !busy_ && !refreshing_ && !probe_.busy();
    const auto selected = selectedProvider();
    const bool hasSelected = !selected.id.isEmpty();
    providerList_->setEnabled(idle);
    add_->setEnabled(idle);
    configure_->setEnabled(idle);
    for (auto *button : {edit_, remove_, save_, fetch_})
        button->setEnabled(idle && hasSelected);
    activate_->setEnabled(idle && hasSelected && selected.id != providers_.activeId());
    disable_->setEnabled(idle && !providers_.activeId().isEmpty());
    model_->setEnabled(idle && hasSelected);
    searchTemplate_->setEnabled(idle);
    automatic_->setEnabled(idle && !activeProvider().id.isEmpty());
    run_->setEnabled(idle && !activeProvider().id.isEmpty());
    run_->setText(busy_ ? "正在搜索…" : "一键搜索");
}
bool AiSourcesPage::editProvider(bool add) {
    if (!storeReady_ || busy_ || refreshing_ || probe_.busy())
        return false;
    AiProviderDialog dialog(providers_, add ? AiProviderConfig{} : selectedProvider(), this);
    if (dialog.exec() == QDialog::Accepted) {
        if (!dialog.modelIds().isEmpty())
            modelDirectories_[dialog.savedProvider().id] = dialog.modelIds();
        reloadProviders(dialog.savedProvider().id);
        status_->setText(dialog.connectionVerified()
            ? "接口已连接，模型已自动选择。点击一键搜索即可继续；检索能力将在搜索时核实。"
            : "配置已保存，尚未验证连接。点击配置接口完成连接后搜索。");
        return dialog.connectionVerified();
    }
    return false;
}
void AiSourcesPage::saveSelected() {
    const auto provider = selectedModelProvider();
    if (provider.id.isEmpty())
        return;
    try {
        providers_.upsert(provider);
        reloadProviders(provider.id);
        status_->setText("模型已保存，尚未发送请求。需要修改接口或密钥时，请在左侧编辑提供方。");
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
void AiSourcesPage::fetchModels() {
    const auto provider = selectedModelProvider();
    const auto key = providerKey(provider);
    auto error = AiProviderConfig::validationError(provider);
    if (error.isEmpty() && key.isEmpty())
        error = "请在左侧编辑所选提供方，填写密钥后获取模型。尚未发起请求。";
    if (!error.isEmpty()) {
        status_->setText(error);
        return;
    }
    status_->setText("正在读取所选提供方的模型目录，不测试联网搜索，不自动重试。");
    probe_.fetchModels(provider, key);
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
        provider = selectedModelProvider();
    provider = AiProviderConfig::automaticProfile(provider);
    if (root_.isEmpty()) {
        status_->setText("当前学校缺少已核验的官方首页；尚未发起请求，请先接入大学官网。");
        return;
    }
    const auto key = activeKey();
    if (key.isEmpty() || provider.model.isEmpty()) {
        // Configuration is prompted only by this explicit search action.
        // A completed connection continues this action; configuring separately never starts a search.
        reloadProviders(provider.id);
        if (editProvider(false))
            QTimer::singleShot(0, this, &AiSourcesPage::run);
        else
            status_->setText("连接未完成；尚未发起搜索请求。填写地址和 Key 并连接后即可搜索。");
        return;
    }
    auto error = AiProviderConfig::validationError(provider);
    if (!error.isEmpty()) {
        status_->setText(error);
        return;
    }
    busy_ = true;
    updateEnabled();
    results_->clear();
    requestedProvider_ = provider;
    requestedModel_ = provider.model;
    requestedTemplate_ = searchTemplate_->currentData().toString();
    QSettings().setValue("ai/lastAttempt/" + school_.id, QDateTime::currentSecsSinceEpoch());
    QSet<QString> known;
    for (const auto &source : school_.catalog) {
        QUrl url(QString::fromStdString(source.entryUrl));
        if (url.scheme() == "http")
            url.setScheme("https");
        known.insert(url.toString(QUrl::FullyEncoded));
    }
    status_->setText(nativeWebSearch(provider)
        ? "正在通过当前启用的提供方调用搜索工具。候选仍需官网校验；不会自动重试。"
        : "正在读取学校官网，再由 AI 从真实页面发现的链接中筛选。候选仍需官网校验。");
    search_.search(provider, key, school_.name, root_, known, requestedTemplate_, school_.officialHomepage);
}

void AiSourcesPage::validate(QJsonArray candidates, QJsonObject usage) {
    const auto auditFolder =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/ai-schools";
    const auto origin = usage.value("candidate_origin").toString(
        nativeWebSearch(requestedProvider_) ? "native_web_search" : "official_site_crawl_ai_selection");
    const QJsonObject audit{{"contract_version", origin == "native_web_search"
                                                        ? "search-v1" : "grounded-selection-v1"},
                            {"status", "candidate_only"},
                            {"candidate_origin", origin},
                            {"school_id", school_.id},
                            {"provider_id", requestedProvider_.id},
                            {"model", requestedModel_},
                            {"template_id", requestedTemplate_},
                            {"model_calls", usage.value("model_calls").toInt(1)},
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
