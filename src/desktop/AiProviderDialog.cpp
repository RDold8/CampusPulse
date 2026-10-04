#include "desktop/AiProviderDialog.h"
#include "desktop/BrandTheme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <stdexcept>

namespace campus {
AiProviderDialog::AiProviderDialog(AiProviderStore &store, AiProviderConfig initial,
                                   QWidget *parent)
    : QDialog(parent), store_(store), initial_(std::move(initial)), probe_(this) {
    const bool adding = initial_.id.isEmpty();
    if (adding) {
        initial_ = AiProviderConfig::deepSeekPreset();
        initial_.id = "provider-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    setObjectName("aiProviderDialog");
    setWindowTitle(adding ? "添加 AI 提供方" : "编辑 AI 提供方");
    resize(840, 760);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    auto *heading = new QLabel(adding ? "添加 AI 提供方" : "编辑 AI 提供方");
    heading->setProperty("brandRole", "heading");
    layout->addWidget(heading);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 6, 0);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    preset_ = new QComboBox;
    preset_->setObjectName("aiProviderPreset");
    preset_->addItem("DeepSeek 官方预设");
    preset_->addItem("自定义 OpenAI 兼容接口");
    preset_->addItem("自定义 Anthropic 兼容路由");
    preset_->setCurrentIndex(initial_.protocol == AiApiProtocol::OpenAiCompatible ? 1
                            : (initial_.isOfficialDeepSeek() ? 0 : 2));
    form->addRow("快速配置", preset_);
    name_ = new QLineEdit(initial_.name);
    name_->setObjectName("aiProviderName");
    name_->setMaxLength(80);
    name_->setPlaceholderText("给这组配置起一个名称");
    auto *identityRow = new QHBoxLayout;
    identityRow->addWidget(name_, 1);
    notes_ = new QLineEdit(initial_.notes);
    notes_->setObjectName("aiProviderNotes");
    notes_->setMaxLength(500);
    notes_->setPlaceholderText("备注，例如：学习用账号 / 自建路由");
    identityRow->addWidget(notes_, 1);
    form->addRow("供应商名称 / 备注", identityRow);
    website_ = new QLineEdit(initial_.website);
    website_->setObjectName("aiProviderWebsite");
    website_->setMaxLength(2048);
    website_->setPlaceholderText("可选：供应商官网或 API Key 管理页面");
    form->addRow("官网链接", website_);
    protocol_ = new QComboBox;
    protocol_->setObjectName("aiProviderProtocol");
    protocol_->addItem("Anthropic Messages · 搜索工具模式", static_cast<int>(AiApiProtocol::DeepSeekNative));
    protocol_->addItem("OpenAI 兼容 · Chat Completions",
                       static_cast<int>(AiApiProtocol::OpenAiCompatible));
    protocol_->setCurrentIndex(initial_.protocol == AiApiProtocol::DeepSeekNative ? 0 : 1);
    baseUrl_ = new QLineEdit(initial_.baseUrl);
    baseUrl_->setObjectName("aiProviderBaseUrl");
    baseUrl_->setMaxLength(2048);
    baseUrl_->setPlaceholderText("填写服务基址，或启用完整 URL 后填写最终请求地址");

    auto *secretRow = new QHBoxLayout;
    key_ = new QLineEdit;
    key_->setObjectName("aiProviderKey");
    key_->setEchoMode(QLineEdit::Password);
    key_->setMaxLength(8192);
    key_->setPlaceholderText("仅用于你主动发起的请求，可稍后填写");
    QString keyLoadError;
    if (!adding) {
        try {
            key_->setText(store_.key(initial_.id));
        } catch (const std::exception &error) {
            keyLoadError = QString::fromUtf8(error.what());
        }
    }
    reveal_ = new QToolButton;
    reveal_->setObjectName("aiProviderRevealKey");
    reveal_->setText("显示");
    reveal_->setCheckable(true);
    reveal_->setAccessibleName("显示或隐藏 API Key");
    connect(reveal_, &QToolButton::toggled, this, [this](bool visible) {
        key_->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        reveal_->setText(visible ? "隐藏" : "显示");
    });
    secretRow->addWidget(key_, 1);
    secretRow->addWidget(reveal_);
    form->addRow("API Key", secretRow);
    remember_ = new QCheckBox("在此 Windows 用户下加密记住 Key");
    remember_->setObjectName("aiProviderRememberKey");
    remember_->setChecked(!adding && store_.keyIsRemembered(initial_.id));
    remember_->setEnabled(AiProviderStore::persistentSecretsSupported());
    remember_->setToolTip("默认不勾选：Key 仅在本次使用中保留。勾选后使用 Windows DPAPI 加密，"
                          "不写入普通配置、学校包或日志。取消勾选并保存会移除已保存的密钥。");
    form->addRow(QString(), remember_);

    auto *routeRow = new QHBoxLayout;
    routeRow->addWidget(baseUrl_, 1);
    fullUrl_ = new QCheckBox("完整 URL");
    fullUrl_->setObjectName("aiProviderFullUrl");
    fullUrl_->setChecked(initial_.fullUrl);
    fullUrl_->setToolTip("勾选：按输入地址直接请求，不追加路径。未勾选：按照协议拼接，并显示最终地址。");
    routeRow->addWidget(fullUrl_);
    form->addRow("请求地址", routeRow);
    contentLayout->addLayout(form);
    endpoint_ = new QLabel;
    endpoint_->setObjectName("aiProviderResolvedEndpoint");
    endpoint_->setWordWrap(true);
    endpoint_->setTextFormat(Qt::PlainText);
    endpoint_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    contentLayout->addWidget(endpoint_);

    auto *advanced = new QGroupBox("高级选项");
    auto *advancedForm = new QFormLayout(advanced);
    advancedForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    advancedForm->addRow("API 格式", protocol_);
    auth_ = new QComboBox;
    auth_->setObjectName("aiProviderAuthMode");
    auth_->addItem("按协议自动选择", static_cast<int>(AiAuthMode::Automatic));
    auth_->addItem("Authorization: Bearer（Token）", static_cast<int>(AiAuthMode::BearerToken));
    auth_->addItem("x-api-key（API Key）", static_cast<int>(AiAuthMode::ApiKeyHeader));
    auth_->setCurrentIndex(auth_->findData(static_cast<int>(initial_.authMode)));
    advancedForm->addRow("认证方式", auth_);

    auto *modelRow = new QHBoxLayout;
    model_ = new QComboBox;
    model_->setObjectName("aiProviderModel");
    model_->setEditable(true);
    model_->setInsertPolicy(QComboBox::NoInsert);
    model_->lineEdit()->setMaxLength(160);
    model_->lineEdit()->setPlaceholderText("获取列表后选择，或手动输入模型 ID");
    if (!initial_.model.isEmpty())
        model_->addItem(initial_.model);
    model_->setCurrentText(initial_.model);
    fetch_ = new QPushButton("获取模型");
    fetch_->setObjectName("aiProviderFetchModels");
    modelRow->addWidget(model_, 1);
    modelRow->addWidget(fetch_);
    advancedForm->addRow("实际请求模型", modelRow);
    contentLayout->addWidget(advanced);
    capability_ = new QLabel;
    capability_->setObjectName("aiProviderCapability");
    capability_->setWordWrap(true);
    capability_->setTextFormat(Qt::PlainText);
    contentLayout->addWidget(capability_);

    auto *previewHeading = new QLabel("配置预览（不包含 API Key）");
    contentLayout->addWidget(previewHeading);
    preview_ = new QPlainTextEdit;
    preview_->setObjectName("aiProviderConfigPreview");
    preview_->setReadOnly(true);
    preview_->setMaximumHeight(145);
    contentLayout->addWidget(preview_);

    auto *notice = new QLabel(
        "获取模型只读取接口目录。测试连接会发送一次短模型请求，可能消耗 token；"
        "测试成功只证明接口与该模型能回复，不证明联网搜索或学校栏目可用。保存配置不会发起请求。");
    notice->setWordWrap(true);
    contentLayout->addWidget(notice);
    test_ = new QPushButton("测试连接与模型");
    test_->setObjectName("aiProviderTestConnection");
    layout->addWidget(test_);
    status_ = new QLabel(keyLoadError.isEmpty() ? "尚未发送任何请求。"
        : keyLoadError + "；请重新填写当前提供方的 Key。尚未发送请求。");
    status_->setObjectName("aiProviderDialogStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    status_->setAccessibleName("提供方配置与测试结果");
    layout->addWidget(status_);
    auto *buttons = new QDialogButtonBox;
    save_ = buttons->addButton("保存配置", QDialogButtonBox::AcceptRole);
    save_->setObjectName("saveAiProviderButton");
    buttons->addButton("取消", QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &AiProviderDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(preset_, &QComboBox::currentIndexChanged, this, &AiProviderDialog::applyPreset);
    connect(protocol_, &QComboBox::currentIndexChanged, this, [this] {
        key_->clear();
        remember_->setChecked(false);
        updateProtocol();
        status_->setText("协议已改变，请重新填写此协议的 Key。尚未发送请求。");
    });
    connect(baseUrl_, &QLineEdit::textEdited, this, [this] {
        key_->clear();
        remember_->setChecked(false);
        status_->setText("接口地址已改变，请重新填写此接口的 Key。尚未发送请求。");
    });
    for (auto *line : {name_, notes_, website_, baseUrl_})
        connect(line, &QLineEdit::textChanged, this, &AiProviderDialog::updateProtocol);
    connect(model_, &QComboBox::currentTextChanged, this, &AiProviderDialog::updateProtocol);
    const auto routingChanged = [this] {
        key_->clear();
        remember_->setChecked(false);
        updateProtocol();
        status_->setText("路由或认证方式已改变，请重新填写本配置的 Key。尚未发送请求。");
    };
    connect(auth_, &QComboBox::currentIndexChanged, this, routingChanged);
    connect(fullUrl_, &QCheckBox::toggled, this, routingChanged);
    connect(fetch_, &QPushButton::clicked, this, [this] { startProbe(true); });
    connect(test_, &QPushButton::clicked, this, [this] { startProbe(false); });
    connect(&probe_, &AiProviderProbe::finished, this, [this](const AiProbeResult &result) {
        setBusy(false);
        if (result.success && result.operation == AiProbeOperation::Models) {
            const auto manual = model_->currentText();
            model_->clear();
            model_->addItems(result.modelIds);
            if (!manual.isEmpty() && model_->findText(manual) < 0)
                model_->addItem(manual);
            model_->setCurrentText(manual.isEmpty() && !result.modelIds.isEmpty()
                                       ? result.modelIds.first() : manual);
        }
        status_->setText(QString("%1 · HTTP %2 · %3 ms\n%4")
                             .arg(result.success ? "请求通过" : "请求失败")
                             .arg(result.httpStatus)
                             .arg(result.elapsedMs)
                             .arg(result.message));
        if (result.success && result.operation == AiProbeOperation::Connection) {
            const auto model = result.responseModel.isEmpty() ? "服务端未返回模型名"
                                                              : result.responseModel;
            status_->setText(status_->text() + "\n返回模型：" + model + "\n用量：" +
                             QString::fromUtf8(QJsonDocument(result.usage)
                                                   .toJson(QJsonDocument::Compact)));
        }
    });
    connect(this, &QDialog::finished, this, [this] {
        key_->setEchoMode(QLineEdit::Password);
        reveal_->setChecked(false);
        probe_.cancel();
    });
    updateProtocol();
    BrandTheme::applyWindow(*this);
}

AiProviderConfig AiProviderDialog::formProvider() const {
    auto result = initial_;
    result.name = name_->text().trimmed();
    result.baseUrl = baseUrl_->text().trimmed();
    result.model = model_->currentText().trimmed();
    result.protocol = static_cast<AiApiProtocol>(protocol_->currentData().toInt());
    result.notes = notes_->text().trimmed();
    result.website = website_->text().trimmed();
    result.fullUrl = fullUrl_->isChecked();
    result.authMode = static_cast<AiAuthMode>(auth_->currentData().toInt());
    return result;
}
void AiProviderDialog::updateProtocol() {
    const bool native = protocol_->currentData().toInt() ==
                        static_cast<int>(AiApiProtocol::DeepSeekNative);
    baseUrl_->setReadOnly(false);
    capability_->setText(native
        ? "使用你填写路由的 Anthropic Messages 接口。先测试连接；补充栏目要求服务支持搜索工具，"
          "不支持时明确报错。连接成功不证明网页搜索可用，返回候选还要由爬虫校验。"
        : "兼容接口：模型只能提出栏目候选，不等同于已执行网页搜索；不保证地址真实、完整或最新。"
          "候选仍需本校域名与真实网页校验。");
    const auto provider = formProvider();
    const auto error = AiProviderConfig::validationError(provider);
    endpoint_->setText(error.isEmpty() ? "最终请求地址：" + AiProviderConfig::requestEndpoint(provider).toString()
                                      : "请求地址待完善：" + error);
    preview_->setPlainText(QString::fromUtf8(QJsonDocument(provider.toJson()).toJson(QJsonDocument::Indented)));
}
void AiProviderDialog::applyPreset(int index) {
    key_->clear();
    remember_->setChecked(false);
    if (index == 0) {
        const auto preset = AiProviderConfig::deepSeekPreset();
        name_->setText(preset.name);
        protocol_->setCurrentIndex(0);
        baseUrl_->setText(preset.baseUrl);
        model_->clear();
        model_->addItem(preset.model);
    } else {
        name_->setText(index == 1 ? "自定义兼容接口" : "自定义模型路由");
        protocol_->setCurrentIndex(index == 1 ? 1 : 0);
        baseUrl_->clear();
        model_->clear();
    }
    fullUrl_->setChecked(false);
    auth_->setCurrentIndex(0);
    updateProtocol();
    status_->setText("已填入配置模板，请填写接口与 Key。尚未发送请求。");
}
void AiProviderDialog::setBusy(bool busy) {
    const QList<QWidget *> widgets{preset_, protocol_, auth_, name_, notes_, website_, baseUrl_, fullUrl_, key_, model_,
                                   reveal_, fetch_, test_, save_};
    for (auto *widget : widgets)
        widget->setEnabled(!busy);
    remember_->setEnabled(!busy && AiProviderStore::persistentSecretsSupported());
}
void AiProviderDialog::startProbe(bool models) {
    if (probe_.busy())
        return;
    const auto provider = formProvider();
    auto error = AiProviderConfig::validationError(provider);
    if (error.isEmpty() && key_->text().trimmed().isEmpty())
        error = "请先填写 API Key；尚未发起请求，不会消耗 token。";
    if (error.isEmpty() && !models && provider.model.isEmpty())
        error = "请先填写或获取模型 ID；尚未发起请求。";
    if (!error.isEmpty()) {
        status_->setText(error);
        return;
    }
    status_->setText(models ? "正在读取模型目录，不测试搜索能力。"
                           : "正在发送一次短模型请求，不测试搜索能力。不会自动重试。");
    setBusy(true);
    if (models)
        probe_.fetchModels(provider, key_->text().trimmed());
    else
        probe_.probe(provider, key_->text().trimmed());
}
void AiProviderDialog::save() {
    if (probe_.busy())
        return;
    const auto provider = formProvider();
    const auto error = AiProviderConfig::validationError(provider);
    if (!error.isEmpty()) {
        status_->setText(error);
        return;
    }
    try {
        store_.upsert(provider);
        store_.setKey(provider.id, key_->text().trimmed(), remember_->isChecked());
        saved_ = provider;
        accept();
    } catch (const std::exception &failure) {
        status_->setText("保存未完成，请检查配置与密钥状态：" +
                         QString::fromUtf8(failure.what()));
    }
}
} // namespace campus
