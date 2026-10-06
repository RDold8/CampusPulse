#include "desktop/ReminderSoundDialog.h"
#include "desktop/BrandTheme.h"
#include "desktop/ReminderSound.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <stdexcept>

namespace campus {
ReminderSoundDialog::ReminderSoundDialog(QWidget *parent)
    : QDialog(parent), sound_(new ReminderSound(this)) {
    setObjectName("reminderSoundDialog");
    setWindowTitle("提醒声音");
    resize(470, 360);
    setMinimumWidth(400);
    QString loadError;
    try {
        saved_ = ReminderAudioOptions::load();
    } catch (const std::exception &error) {
        loadError = "读取声音设置失败：" + QString::fromUtf8(error.what());
    }
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(12);
    auto *heading = new QLabel("提醒声音");
    heading->setObjectName("heading");
    layout->addWidget(heading);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setVerticalSpacing(12);
    tone_ = new QComboBox;
    tone_->setObjectName("reminderSoundTone");
    tone_->setAccessibleName("提醒铃声");
    tone_->addItem("轻柔铃声", "gentle");
    tone_->addItem("双响提示", "double");
    tone_->addItem("闹钟提示", "alarm");
    tone_->addItem("静音", "mute");
    tone_->setCurrentIndex(tone_->findData(saved_.tone));
    form->addRow("铃声", tone_);
    auto *volumeRow = new QHBoxLayout;
    volume_ = new QSlider(Qt::Horizontal);
    volume_->setObjectName("reminderSoundVolume");
    volume_->setAccessibleName("提醒音量，百分比");
    volume_->setRange(0, 100);
    volume_->setSingleStep(5);
    volume_->setPageStep(10);
    volume_->setValue(saved_.volume);
    percentage_ = new QLabel(QString::number(volume_->value()) + "%");
    percentage_->setObjectName("reminderSoundVolumePercent");
    percentage_->setMinimumWidth(40);
    percentage_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    volumeRow->addWidget(volume_, 1);
    volumeRow->addWidget(percentage_);
    form->addRow("音量", volumeRow);
    layout->addLayout(form);
    repeat_ = new QCheckBox("未关闭提醒时重复提示（最多1分钟）");
    repeat_->setObjectName("reminderSoundRepeat");
    repeat_->setChecked(saved_.repeat);
    layout->addWidget(repeat_);
    auto *previewRow = new QHBoxLayout;
    auto *previewButton = new QPushButton("试听");
    previewButton->setObjectName("previewReminderSoundButton");
    previewButton->setToolTip("试听当前铃声一次，不修改待办或已保存设置。");
    stop_ = new QPushButton("停止");
    stop_->setObjectName("stopReminderSoundButton");
    stop_->setEnabled(false);
    previewRow->addWidget(previewButton);
    previewRow->addWidget(stop_);
    previewRow->addStretch();
    layout->addLayout(previewRow);
    status_ = new QLabel;
    status_->setObjectName("reminderSoundStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setMinimumHeight(status_->fontMetrics().height() * 2);
    status_->setText(loadError.isEmpty()
                         ? "点击“试听”检查声音。保存后用于待办提醒。"
                         : loadError);
    layout->addWidget(status_);
    layout->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("保存");
    buttons->button(QDialogButtonBox::Save)->setObjectName("saveReminderSoundButton");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    buttons->button(QDialogButtonBox::Cancel)->setObjectName("cancelReminderSoundButton");
    layout->addWidget(buttons);
    connect(previewButton, &QPushButton::clicked, this, &ReminderSoundDialog::preview);
    connect(stop_, &QPushButton::clicked, this, [this] {
        if (!stopPreview())
            return;
        status_->setText("试听已停止。");
    });
    connect(tone_, &QComboBox::currentIndexChanged, this,
            [this] { settingsChanged(); });
    connect(volume_, &QSlider::valueChanged, this, [this](int value) {
        percentage_->setText(QString::number(value) + "%");
        settingsChanged();
    });
    connect(repeat_, &QCheckBox::toggled, this, [this] { settingsChanged(); });
    connect(sound_, &ReminderSound::started, this, [this](const QString &) {
        stop_->setEnabled(true);
        status_->setText("正在试听“" + tone_->currentText() + "”（播放一次）。");
    });
    connect(sound_, &ReminderSound::finished, this, [this] {
        stop_->setEnabled(false);
        status_->setText("试听结束。保存后用于待办提醒。");
    });
    connect(sound_, &ReminderSound::failed, this, [this](const QString &error) {
        stop_->setEnabled(sound_->isPlaying());
        status_->setText(error.isEmpty() ? QString("提示音播放失败。")
                                         : "提示音播放失败：" + error);
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &ReminderSoundDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    BrandTheme::applyWindow(*this);
}
ReminderSoundDialog::~ReminderSoundDialog() {
    sound_->stop();
}
ReminderAudioOptions ReminderSoundDialog::currentOptions() const {
    const auto tone = tone_->currentData().toString();
    if (tone != "gentle" && tone != "double" && tone != "alarm" && tone != "mute")
        throw std::invalid_argument("请选择有效的提醒铃声");
    const auto volume = volume_->value();
    if (volume < 0 || volume > 100)
        throw std::invalid_argument("提醒音量必须在 0 到 100 之间");
    return {tone, volume, repeat_->isChecked()};
}
bool ReminderSoundDialog::stopPreview() {
    bool nativeFailure = false;
    const auto failure = connect(sound_, &ReminderSound::failed, this,
                                 [&](const QString &) { nativeFailure = true; });
    sound_->stop();
    disconnect(failure);
    if (nativeFailure || sound_->isPlaying()) {
        stop_->setEnabled(true);
        const auto error = sound_->errorString();
        status_->setText(error.isEmpty() ? QString("提示音仍在播放，未能停止。请点击“停止”重试。")
                                         : "提示音停止失败：" + error);
        return false;
    }
    stop_->setEnabled(false);
    return true;
}
void ReminderSoundDialog::settingsChanged() {
    if (!stopPreview())
        return;
    status_->setText(tone_->currentData().toString() == "mute" || volume_->value() == 0
                         ? "当前为静音。"
                         : "设置已修改，点击“试听”检查声音；保存后生效。");
}
void ReminderSoundDialog::preview() {
    if (!stopPreview())
        return;
    try {
        const auto options = currentOptions();
        if (options.tone == "mute" || options.volume == 0) {
            status_->setText("当前为静音。");
            return;
        }
        if (!sound_->play(options.tone, options.volume)) {
            const auto error = sound_->errorString();
            status_->setText(error.isEmpty() ? QString("提示音播放失败。")
                                             : "提示音播放失败：" + error);
        }
    } catch (const std::exception &error) {
        status_->setText("试听失败：" + QString::fromUtf8(error.what()));
    }
}
void ReminderSoundDialog::save() {
    if (!stopPreview())
        return;
    try {
        const auto options = currentOptions();
        options.save();
        saved_ = options;
        emit settingsSaved();
        accept();
    } catch (const std::exception &error) {
        status_->setText("声音设置保存失败：" + QString::fromUtf8(error.what()));
    }
}
void ReminderSoundDialog::done(int result) {
    if (!stopPreview() && result == QDialog::Accepted)
        return;
    QDialog::done(result);
}
} // namespace campus
