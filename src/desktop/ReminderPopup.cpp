#include "desktop/ReminderPopup.h"
#include "desktop/BrandTheme.h"
#include "adapters/ReminderScheduler.h"
#include <QApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimeZone>
#include <QVBoxLayout>

namespace campus {
ReminderPopup::ReminderPopup(QWidget *parent)
    : QDialog(parent), sound_(this),
      audio_([this](const QString &tone, int volume) { return sound_.play(tone, volume); },
             [this] { sound_.stop(); }, this), audioOptions_(ReminderAudioOptions::load()) {
    setObjectName("reminderPopup");
    setWindowTitle("CampusPulse · 待办提醒");
    setWindowFlag(Qt::WindowStaysOnTopHint);
    setModal(false);
    resize(520, 330);
    auto *layout = new QVBoxLayout(this);
    heading_ = new QLabel("到时间了", this);
    heading_->setObjectName("reminderPopupHeading");
    heading_->setStyleSheet("font-size: 24px; font-weight: bold;");
    layout->addWidget(heading_);
    auto *hint = new QLabel("提醒会保留到你关闭。查看待办后，可以标记完成或修改时间。", this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    items_ = new QListWidget(this);
    items_->setObjectName("reminderPopupItems");
    items_->setWordWrap(true);
    items_->setSpacing(8);
    layout->addWidget(items_);
    audioStatus_ = new QLabel(this);
    audioStatus_->setObjectName("reminderAudioStatus");
    audioStatus_->setTextFormat(Qt::PlainText);
    audioStatus_->setWordWrap(true);
    layout->addWidget(audioStatus_);
    auto *buttons = new QDialogButtonBox(this);
    auto *silence = buttons->addButton("停止声音", QDialogButtonBox::ActionRole);
    silence->setObjectName("stopPopupSoundButton");
    auto *soundSettings = buttons->addButton("声音设置", QDialogButtonBox::ActionRole);
    soundSettings->setObjectName("popupSoundSettingsButton");
    auto *open = buttons->addButton("查看待办", QDialogButtonBox::ActionRole);
    open->setObjectName("openReminderTaskButton");
    auto *dismiss = buttons->addButton("知道了", QDialogButtonBox::AcceptRole);
    dismiss->setObjectName("dismissReminderButton");
    layout->addWidget(buttons);
    connect(silence, &QPushButton::clicked, this, &ReminderPopup::stopSound);
    connect(soundSettings, &QPushButton::clicked, this, &ReminderPopup::soundSettingsRequested);
    connect(&sound_, &ReminderSound::failed, this, [this](const QString &reason) {
        audioStatus_->setText("提示音无法播放：" + reason + "。弹窗提醒仍保留。");
    });
    connect(open, &QPushButton::clicked, this, [this] {
        const auto *item = items_->currentItem();
        if (item && !item->data(Qt::UserRole).toString().isEmpty())
            emit taskRequested(item->data(Qt::UserRole + 1).toString(),
                               item->data(Qt::UserRole).toString());
    });
    connect(dismiss, &QPushButton::clicked, this, [this] {
        const int row = items_->currentRow();
        delete items_->takeItem(row < 0 ? 0 : row);
        if (items_->count() == 0) { stopSound(); hide(); }
        else items_->setCurrentRow(0);
    });
    BrandTheme::applyWindow(*this);
}
void ReminderPopup::showTask(const PersonalTask &task, const QString &schoolName) {
    const auto trigger = ReminderScheduler::trigger(task);
    const auto key = QString::fromStdString(task.id) + ":" + trigger.toString(Qt::ISODate);
    if (displayed_.contains(key)) return;
    displayed_.insert(key);
    heading_->setText("到时间了");
    const QTimeZone zone(QByteArray::fromStdString(task.time.timeZone));
    const auto time = trigger.toTimeZone(zone).toString("yyyy-MM-dd HH:mm");
    const auto label = schoolName.isEmpty() ? QString::fromStdString(task.schoolId) : schoolName;
    auto *item = new QListWidgetItem(QString::fromStdString(task.title) + "\n" +
                                    label + " · 提醒时间 " + time, items_);
    item->setData(Qt::UserRole, QString::fromStdString(task.id));
    item->setData(Qt::UserRole + 1, QString::fromStdString(task.schoolId));
    items_->setCurrentItem(item);
    present();
    ring();
}
void ReminderPopup::showTest() {
    heading_->setText("测试提醒已弹出");
    auto *item = new QListWidgetItem("这是一条测试提醒。真实待办到点也会在这里显示。\n"
                                    "测试不会创建待办或修改你的数据。", items_);
    items_->setCurrentItem(item);
    present();
    ring();
}
void ReminderPopup::showFailure(const QString &reason) {
    if (failures_.contains(reason)) return;
    failures_.insert(reason);
    heading_->setText("提醒遇到问题");
    auto *item = new QListWidgetItem("请检查待办与本机存储：\n" + reason, items_);
    items_->setCurrentItem(item);
    present();
    ring();
}
void ReminderPopup::present() {
    show();
    raise();
    activateWindow();
    QApplication::alert(this, 0);
}
void ReminderPopup::ring() {
    const bool muted = audioOptions_.tone == "mute" || audioOptions_.volume == 0;
    audioStatus_->setText(muted ? "提示音已静音，弹窗仍保留。"
                              : audioOptions_.repeat ? "未关闭时会重复提示，最多1分钟。"
                                                     : "播放一次提示音，弹窗仍保留。");
    audio_.begin(audioOptions_);
}
void ReminderPopup::stopSound() {
    bool failed = false;
    const auto failure = connect(&sound_, &ReminderSound::failed, this,
                                 [&](const QString &) { failed = true; });
    audio_.stop();
    disconnect(failure);
    if (!failed && !sound_.isPlaying()) audioStatus_->setText("声音已停止，待办状态未改变。");
}
void ReminderPopup::reloadAudioOptions() {
    audioOptions_ = ReminderAudioOptions::load();
    if (items_->count() > 0) ring();
}
int ReminderPopup::reminderCount() const { return items_->count(); }
void ReminderPopup::done(int result) {
    stopSound();
    items_->clear();
    QDialog::done(result);
}
} // namespace campus
