#include "desktop/ReminderAudio.h"
#include <QSettings>
#include <QStringList>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace campus {
namespace {
bool validTone(const QString &tone) {
    return QStringList{"gentle", "double", "alarm", "mute"}.contains(tone);
}
}
ReminderAudioOptions ReminderAudioOptions::load() {
    QSettings settings;
    ReminderAudioOptions result;
    const auto tone = settings.value("reminders/soundTone", result.tone).toString();
    if (validTone(tone)) result.tone = tone;
    bool parsed = false;
    const auto volume = settings.value("reminders/soundVolume", result.volume).toInt(&parsed);
    if (parsed) result.volume = std::clamp(volume, 0, 100);
    result.repeat = settings.value("reminders/repeatSound", result.repeat).toBool();
    return result;
}
void ReminderAudioOptions::save() const {
    if (!validTone(tone) || volume < 0 || volume > 100)
        throw std::invalid_argument("请选择有效的提示音和0至100的音量");
    QSettings settings;
    settings.setValue("reminders/soundTone", tone);
    settings.setValue("reminders/soundVolume", volume);
    settings.setValue("reminders/repeatSound", repeat);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        throw std::runtime_error("提醒声音设置保存失败，请检查本机设置目录");
}
ReminderAudio::ReminderAudio(Delivery delivery, Stop stop, QObject *parent,
                             int intervalMs, int durationMs)
    : QObject(parent), delivery_(std::move(delivery)), stop_(std::move(stop)), durationMs_(durationMs) {
    if (!delivery_ || !stop_ || intervalMs <= 0 || durationMs <= 0)
        throw std::invalid_argument("提示音播放器或时间参数无效");
    repeat_.setInterval(intervalMs);
    deadline_.setSingleShot(true);
    deadline_.setInterval(durationMs);
    connect(&repeat_, &QTimer::timeout, this, [this] {
        if (elapsed_.elapsed() >= durationMs_) this->stop();
        else play();
    });
    connect(&deadline_, &QTimer::timeout, this, &ReminderAudio::stop);
}
void ReminderAudio::begin(const ReminderAudioOptions &options) {
    stop();
    if (!validTone(options.tone) || options.volume < 0 || options.volume > 100)
        throw std::invalid_argument("提示音设置无效");
    options_ = options;
    if (options_.tone == "mute" || options_.volume == 0) return;
    elapsed_.start();
    if (options_.repeat) {
        repeat_.start();
        deadline_.start();
    }
    play();
}
void ReminderAudio::play() {
    if (!delivery_(options_.tone, options_.volume)) stop();
}
void ReminderAudio::stop() {
    repeat_.stop();
    deadline_.stop();
    stop_();
}
bool ReminderAudio::repeating() const { return repeat_.isActive(); }
} // namespace campus
