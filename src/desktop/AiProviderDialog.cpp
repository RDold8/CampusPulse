#include "desktop/AiProviderDialog.h"
#include <QCryptographicHash>
#include "desktop/BrandTheme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
        initial_.name.clear();
        initial_.model.clear();
    }
    setObjectName("aiProviderDialog");
    setWindowTitle(adding ? "连接 AI 接口" : "配置 AI 接口");
    resize(620, 400);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    auto *heading = new QLabel("填写地址和 Key，即可连接");
    heading->setProperty("brandRole", "heading");
    layout->addWidget(heading);
    auto *intro = new QLabel("模型会自动选择；连接后可在学校页面一键搜索。连接检查可能消耗少量 token。");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    baseUrl_ = new QLineEdit(initial_.baseUrl);
    baseUrl_->setObjectName("aiProviderBaseUrl");
    baseUrl_->setMaxLength(2048);
    baseUrl_->setPlaceholderText("https://api.deepseek.com，或你的服务接口地址");
    form->addRow("接口地址", baseUrl_);
    key_ = new QLineEdit;
    key_->setObjectName("aiProviderKey");
    key_->setEchoMode(QLineEdit::Password);
    key_->setMaxLength(8192);
    key_->setPlaceholderText("填写此接口的 API Key");
    QString keyLoadError;
    if (!adding) {
        try {
            key_->setText(store_.key(initial_.id));
        } catch (const std::exception &error) {
            keyLoadError = QString::fromUtf8(error.what());
        }
    }
    // Load the old credential binding first. Display normalization is not a disk migration.
    baseUrl_->setText(AiProviderConfig::automaticProfile(initial_).baseUrl);
    reveal_ = new QToolButton;
    reveal_->setObjectName("aiProviderRevealKey");
    reveal_->setText("显示");
    reveal_->setCheckable(true);
    reveal_->setAccessibleName("显示或隐藏 API Key");
    connect(reveal_, &QToolButton::toggled, this, [this](bool visible) {
        key_->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        reveal_->setText(visible ? "隐藏" : "显示");
    });
    auto *secretRow = new QHBoxLayout;
    secretRow->addWidget(key_, 1);
    secretRow->addWidget(reveal_);
    form->addRow("API Key", secretRow);
    remember_ = new QCheckBox("在此 Windows 用户下加密记住 Key");
    remember_->setObjectName("aiProviderRememberKey");
    remember_->setChecked(!adding && store_.keyIsRemembered(initial_.id));
    remember_->setEnabled(AiProviderStore::persistentSecretsSupported());
    remember_->setToolTip("未勾选时只在本次使用中保留；勾选后使用 Windows DPAPI 加密保存。");
    form->addRow(QString(), remember_);
    layout->addLayout(form);

    advanced_ = new QToolButton;
    advanced_->setObjectName("aiProviderAdvancedToggle");
    advanced_->setText("高级设置（可选）");
    advanced_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    advanced_->setArrowType(Qt::RightArrow);
    advanced_->setCheckable(true);
    layout->addWidget(advanced_);
    auto *advancedPanel = new QGroupBox;
    advancedPanel->setObjectName("aiProviderAdvancedPanel");
    auto *advancedForm = new QFormLayout(advancedPanel);
    advancedForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    name_ = new QLineEdit(initial_.name);
    name_->setObjectName("aiProviderName");
    name_->setMaxLength(80);
    name_->setPlaceholderText("留空时根据接口自动命名");
    advancedForm->addRow("名称", name_);
    model_ = new QComboBox;
    model_->setObjectName("aiProviderModel");
    model_->setEditable(true);
    model_->setInsertPolicy(QComboBox::NoInsert);
    model_->lineEdit()->setMaxLength(160);
    model_->lineEdit()->setPlaceholderText("自动选择；服务没有目录时可手动指定");
    if (!initial_.model.isEmpty())
        model_->addItem(initial_.model);
    model_->setCurrentText(initial_.model);
    fetch_ = new QPushButton("读取模型列表");
    fetch_->setObjectName("aiProviderFetchModels");
    auto *modelRow = new QHBoxLayout;
    modelRow->addWidget(model_, 1);
    modelRow->addWidget(fetch_);
    advancedForm->addRow("模型", modelRow);
    saveOnly_ = new QPushButton("仅保存配置，稍后连接");
    saveOnly_->setObjectName("saveAiProviderOnlyButton");
    advancedForm->addRow(QString(), saveOnly_);
    advancedPanel->hide();
    layout->addWidget(advancedPanel);
    connect(advanced_, &QToolButton::toggled, this, [this, advancedPanel](bool expanded) {
        advanced_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        advancedPanel->setVisible(expanded);
        resize(620, expanded ? 560 : 400);
    });
    status_ = new QLabel(keyLoadError.isEmpty()
        ? "尚未发起请求。只需填写接口地址与 API Key。"
        : keyLoadError + "；请重新填写 Key。尚未发起请求。");
    status_->setObjectName("aiProviderDialogStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    status_->setAccessibleName("AI 接口连接状态");
    layout->addWidget(status_);
    layout->addStretch(1);
    auto *buttons = new QDialogButtonBox;
    save_ = buttons->addButton("保存并连接", QDialogButtonBox::AcceptRole);
    save_->setObjectName("saveAiProviderButton");
    save_->setDefault(true);
    buttons->addButton("取消", QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &AiProviderDialog::connectAndSave);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(baseUrl_, &QLineEdit::textEdited, this, [this] {
        key_->clear();
        remember_->setChecked(false);
        status_->setText("地址已改变，请填写这个接口的 Key。尚未发起请求。");
    });
    connect(fetch_, &QPushButton::clicked, this, &AiProviderDialog::fetchModels);
    connect(saveOnly_, &QPushButton::clicked, this, &AiProviderDialog::saveOnly);
    connect(&probe_, &AiProviderProbe::progress, this, [this](const QString &message) {
        status_->setText(message);
    });
    connect(&probe_, &AiProviderProbe::finished, this, [this](const AiProbeResult &result) {
        setBusy(false);
        if (result.operation == AiProbeOperation::AutoConnect) {
            if (!connecting_)
                return;
            connecting_ = false;
            if (!result.success) {
                status_->setText("连接未完成：" + result.message + "\n请检查地址、Key 或服务状态后重试。");
                return;
            }
            const auto error = AiProviderConfig::validationError(result.provider);
            if (!error.isEmpty() || result.provider.id != initial_.id || result.provider.model.isEmpty()) {
                status_->setText("连接返回的配置不完整，本次没有保存；请检查接口的模型列表。");
                return;
            }
            modelIds_ = result.modelIds;
            model_->setCurrentText(result.provider.model);
            connectedProvider_ = result.provider;
            connectedKeyDigest_ = QCryptographicHash::hash(key_->text().trimmed().toUtf8(),
                                                         QCryptographicHash::Sha256);
            if (persist(result.provider, true)) {
                connectionVerified_ = true;
                accept();
            } else {
                status_->setText("连接已确认，本地保存未完成。填写内容已保留；重试保存不会再次调用模型。\n" +
                                 status_->text());
                save_->setText("重试保存");
            }
            return;
        }
        if (result.success && result.operation == AiProbeOperation::Models) {
            const auto manual = model_->currentText();
            model_->clear();
            model_->addItems(result.modelIds);
            if (!manual.isEmpty() && model_->findText(manual) < 0)
                model_->addItem(manual);
            model_->setCurrentText(manual);
            modelIds_ = result.modelIds;
        }
        status_->setText(QString("%1 · HTTP %2\n%3")
            .arg(result.success ? "模型列表已读取" : "模型列表读取失败")
            .arg(result.httpStatus).arg(result.message));
    });
    connect(this, &QDialog::finished, this, [this] {
        key_->setEchoMode(QLineEdit::Password);
        reveal_->setChecked(false);
        connecting_ = false;
        probe_.cancel();
    });
    BrandTheme::applyWindow(*this);
}

AiProviderConfig AiProviderDialog::formProvider() const {
    auto result = initial_;
    result.name = name_->text().trimmed();
    result.baseUrl = baseUrl_->text().trimmed();
    result.model = model_->currentText().trimmed();
    return AiProviderConfig::automaticProfile(result);
}
bool AiProviderDialog::hasCurrentConnection() const {
    return connectedProvider_ && connectedProvider_->toJson() == formProvider().toJson() &&
           connectedKeyDigest_ == QCryptographicHash::hash(key_->text().trimmed().toUtf8(),
                                                          QCryptographicHash::Sha256);
}
void AiProviderDialog::setBusy(bool busy) {
    const QList<QWidget *> widgets{baseUrl_, key_, reveal_, name_, model_, fetch_, save_, saveOnly_, advanced_};
    for (auto *widget : widgets)
        widget->setEnabled(!busy);
    remember_->setEnabled(!busy && AiProviderStore::persistentSecretsSupported());
    save_->setText(busy ? "正在连接…" : "保存并连接");
}
void AiProviderDialog::connectAndSave() {
    if (probe_.busy() || connecting_)
        return;
    const auto provider = formProvider();
    auto error = AiProviderConfig::endpointError(baseUrl_->text().trimmed());
    if (error.isEmpty())
        error = AiProviderConfig::validationError(provider);
    if (error.isEmpty() && key_->text().trimmed().isEmpty())
        error = "请填写 API Key；尚未发起请求。";
    if (!error.isEmpty()) {
        status_->setText(error);
        return;
    }
    try {
        store_.checkWritable();
    } catch (const std::exception &failure) {
        status_->setText("本地保存检查未通过，尚未发起 API 请求。填写内容已保留。\n" +
                         QString::fromUtf8(failure.what()));
        return;
    }
    if (hasCurrentConnection()) {
        if (persist(provider, true)) {
            connectionVerified_ = true;
            accept();
        }
        return;
    }
    connecting_ = true;
    setBusy(true);
    status_->setText("正在连接并自动选择模型，请稍候…");
    probe_.connectProvider(provider, key_->text().trimmed());
}
void AiProviderDialog::fetchModels() {
    if (probe_.busy() || connecting_)
        return;
    const auto provider = formProvider();
    auto error = AiProviderConfig::endpointError(baseUrl_->text().trimmed());
    if (error.isEmpty())
        error = AiProviderConfig::validationError(provider);
    if (error.isEmpty() && key_->text().trimmed().isEmpty())
        error = "请填写 API Key；尚未发起请求。";
    if (!error.isEmpty()) {
        status_->setText(error);
        return;
    }
    setBusy(true);
    status_->setText("正在读取模型列表；尚未验证搜索能力。");
    probe_.fetchModels(provider, key_->text().trimmed());
}
bool AiProviderDialog::persist(const AiProviderConfig &provider, bool activate) {
    try {
        store_.saveProvider(provider, key_->text().trimmed(), remember_->isChecked(), activate);
        saved_ = provider;
        return true;
    } catch (const std::exception &failure) {
        status_->setText("本地保存失败：本次配置、凭据和启用状态均未提交。填写内容已保留。\n" +
                         QString::fromUtf8(failure.what()));
        return false;
    }
}
void AiProviderDialog::saveOnly() {
    if (probe_.busy() || connecting_)
        return;
    const auto provider = formProvider();
    auto error = AiProviderConfig::endpointError(baseUrl_->text().trimmed());
    if (error.isEmpty())
        error = AiProviderConfig::validationError(provider);
    if (!error.isEmpty()) {
        status_->setText(error);
        return;
    }
    const bool connected = hasCurrentConnection();
    if (persist(provider, connected)) {
        connectionVerified_ = connected;
        accept();
    }
}
} // namespace campus
