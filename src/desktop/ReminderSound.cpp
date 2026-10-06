#include "desktop/ReminderSound.h"
#include <QCoreApplication>
#include <QDebug>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <numbers>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmeapi.h>
#endif

namespace campus {
namespace {
#ifdef Q_OS_WIN
constexpr int SampleRate = 44100;

struct Pulse { double start, length, frequency; };

std::vector<std::int16_t> synthesize(const QString &tone, int volume) {
    std::vector<Pulse> pulses;
    double duration = 0;
    if (tone == "gentle") {
        pulses = {{0.02, 0.78, 880.0}};
        duration = 0.85;
    } else if (tone == "double") {
        pulses = {{0.02, 0.36, 784.0}, {0.43, 0.51, 1046.5}};
        duration = 1.05;
    } else {
        pulses = {{0.02, 0.24, 880.0}, {0.35, 0.24, 1174.7},
                  {0.68, 0.24, 880.0}, {1.01, 0.24, 1174.7}};
        duration = 1.35;
    }
    std::vector<std::int16_t> pcm(std::size_t(std::ceil(duration * SampleRate)), 0);
    const double gain = 0.58 * double(volume) / 100.0;
    constexpr double tau = 2 * std::numbers::pi;
    for (std::size_t index = 0; index < pcm.size(); ++index) {
        const double time = double(index) / SampleRate;
        double sample = 0;
        for (const auto &pulse : pulses) {
            const double local = time - pulse.start;
            if (local < 0 || local >= pulse.length) continue;
            // Short cosine attack/release removes discontinuities. These 784-
            // 1175 Hz fundamentals remain audible on ordinary laptop speakers.
            const double attack = std::min(1.0, local / 0.015);
            const double release = std::min(1.0, (pulse.length - local) / 0.07);
            const double envelope = (0.5 - 0.5 * std::cos(std::numbers::pi * attack)) *
                                    (0.5 - 0.5 * std::cos(std::numbers::pi * release)) *
                                    std::exp(-2.0 * local);
            const double fundamental = std::sin(tau * pulse.frequency * local);
            const double overtone = std::sin(tau * pulse.frequency * 2.0 * local);
            sample += envelope * (0.86 * fundamental + 0.14 * overtone);
        }
        // Pulses do not overlap. Clamp additionally protects future tone edits;
        // full volume has headroom and never clips signed 16-bit PCM.
        pcm[index] = std::int16_t(std::lround(std::clamp(sample * gain, -0.95, 0.95) * 32767.0));
    }
    return pcm;
}

QString nativeError(const char *operation, MMRESULT result) {
    wchar_t description[MAXERRORLENGTH]{};
    const auto lookup = waveOutGetErrorTextW(result, description, MAXERRORLENGTH);
    auto text = QString::fromLatin1(operation) + " 失败（代码 " + QString::number(result) + "）";
    if (lookup == MMSYSERR_NOERROR) text += ": " + QString::fromWCharArray(description);
    else text += "; waveOutGetErrorTextW 失败（代码 " + QString::number(lookup) + "）";
    return text;
}

// The native buffer/header have a stable allocation until unprepare succeeds.
// Normally this object deletes itself immediately after cleanup. If stop fails
// while a driver still owns the buffer, the application owns it until the driver
// returns WHDR_DONE; deleting ReminderSound does not free queued PCM memory.
class NativePlayback final : public QObject {
  public:
    using Callback = std::function<void()>;
    using ErrorCallback = std::function<void(const QString &)>;

    NativePlayback(std::vector<std::int16_t> pcm, Callback complete, ErrorCallback error)
        : QObject(QCoreApplication::instance()), pcm_(std::move(pcm)),
          complete_(std::move(complete)), error_(std::move(error)),
          header_(std::make_unique<WAVEHDR>()) {
        timer_.setInterval(20);
        connect(&timer_, &QTimer::timeout, this, [this] {
            if (!queued_ || done()) {
                const auto reason = release(false);
                if (!reason.isEmpty()) {
                    timer_.stop(); // A deterministic cleanup error is not retried forever.
                    if (error_) error_(reason);
                }
            }
        });
    }

    ~NativePlayback() override {
        timer_.stop();
        if (!handle_) return;
        auto reason = cleanup(true);
        if (!reason.isEmpty()) {
            // Final application teardown can race a broken driver's reset. Only
            // this exceptional path waits, for at most the longest chime plus a
            // small device margin; ordinary play/stop and completion are async.
            for (int count = 0; queued_ && !done() && count < 160; ++count)
                QThread::msleep(10);
            if (!queued_ || done()) reason = cleanup(false);
            if (!reason.isEmpty()) {
                qCritical().noquote() << "CampusPulse 提示音清理失败:" << reason;
                if (prepared_) {
                    // Never free a header/buffer still owned by a malfunctioning
                    // driver. At application teardown Windows reclaims the
                    // process-owned allocation/handle; the error stays visible.
                    header_.release();
                    auto *retained = new std::vector<std::int16_t>(std::move(pcm_));
                    Q_UNUSED(retained);
                }
            }
        }
    }

    QString begin() {
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = SampleRate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = 2;
        format.nAvgBytesPerSec = SampleRate * format.nBlockAlign;
        // No WAVE_ALLOWSYNC: a synchronous driver must fail explicitly rather
        // than blocking the GUI. No global/device volume API is called.
        auto result = waveOutOpen(&handle_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
        if (result != MMSYSERR_NOERROR) {
            handle_ = nullptr;
            return nativeError("waveOutOpen", result);
        }
        *header_ = WAVEHDR{};
        header_->lpData = reinterpret_cast<LPSTR>(pcm_.data());
        header_->dwBufferLength = DWORD(pcm_.size() * sizeof(std::int16_t));
        result = waveOutPrepareHeader(handle_, header_.get(), sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) return nativeError("waveOutPrepareHeader", result);
        prepared_ = true;
        result = waveOutWrite(handle_, header_.get(), sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) return nativeError("waveOutWrite", result);
        queued_ = true;
        playing_ = true;
        started_ = true;
        timer_.start();
        return {};
    }

    QString release(bool terminate) {
        const auto reason = cleanup(terminate);
        if (!reason.isEmpty()) {
            // Reset failure while queued can still recover when actual playback
            // finishes. Header/PCM remain alive throughout that wait.
            if (queued_ && !done()) timer_.start();
            return reason;
        }
        timer_.stop();
        const bool notify = std::exchange(started_, false);
        deleteLater();
        if (notify && complete_) complete_();
        return {};
    }

    bool isPlaying() const { return playing_; }
    bool hasHandle() const { return handle_ != nullptr; }
    void detach() { complete_ = {}; error_ = {}; }

  private:
    std::vector<std::int16_t> pcm_;
    Callback complete_;
    ErrorCallback error_;
    std::unique_ptr<WAVEHDR> header_;
    HWAVEOUT handle_ = nullptr;
    QTimer timer_;
    bool prepared_ = false, queued_ = false, playing_ = false, started_ = false;

    bool done() const {
        const volatile DWORD *flags = &header_->dwFlags;
        return (*flags & WHDR_DONE) != 0;
    }

    QString cleanup(bool terminate) {
        if (!handle_) { playing_ = false; return {}; }
        if (queued_ && !done()) {
            if (!terminate) return "waveOut: 音频缓冲区尚未归还";
            const auto result = waveOutReset(handle_);
            if (result != MMSYSERR_NOERROR) return nativeError("waveOutReset", result);
        }
        queued_ = false;
        playing_ = false;
        if (prepared_) {
            const auto result = waveOutUnprepareHeader(handle_, header_.get(), sizeof(WAVEHDR));
            if (result != MMSYSERR_NOERROR) return nativeError("waveOutUnprepareHeader", result);
            prepared_ = false;
        }
        const auto result = waveOutClose(handle_);
        if (result != MMSYSERR_NOERROR) return nativeError("waveOutClose", result);
        handle_ = nullptr;
        pcm_.clear();
        return {};
    }
};
#endif
} // namespace

struct ReminderSound::Impl {
    QString error;
    bool changing = false;
#ifdef Q_OS_WIN
    QPointer<NativePlayback> playback;
#endif
};

ReminderSound::ReminderSound(QObject *parent) : QObject(parent), impl_(std::make_unique<Impl>()) {}

ReminderSound::~ReminderSound() {
#ifdef Q_OS_WIN
    if (impl_->playback) {
        auto *playback = impl_->playback.data();
        playback->detach();
        const auto reason = playback->release(true);
        if (!reason.isEmpty()) {
            impl_->error = reason;
            qWarning().noquote() << "CampusPulse 提示音停止失败:" << reason;
        }
    }
#endif
}

QStringList ReminderSound::tones() { return {"gentle", "double", "alarm"}; }

void ReminderSound::setError(const QString &reason) {
    impl_->error = reason;
    emit failed(reason);
}

bool ReminderSound::play(const QString &tone, int volumePercent) {
    if (!tones().contains(tone)) { setError("未知提示音：" + tone); return false; }
    if (volumePercent < 0 || volumePercent > 100) {
        setError("提示音音量应为 0 到 100");
        return false;
    }
#ifdef Q_OS_WIN
    if (impl_->changing) {
        setError("提示音正在切换，请稍后重试");
        return false;
    }
    if (QThread::currentThread() != thread()) {
        setError("提示音必须在其所属的 Qt 线程中播放");
        return false;
    }
    if (!QCoreApplication::instance()) {
        setError("提示音需要 Qt 应用事件循环");
        return false;
    }
    if (thread() != QCoreApplication::instance()->thread()) {
        setError("提示音需要在 Qt 应用线程中使用");
        return false;
    }
    QPointer<ReminderSound> guard(this);
    impl_->changing = true;
    stop();
    if (!guard) return false;
    if (impl_->playback && impl_->playback->hasHandle()) {
        impl_->changing = false;
        return false;
    }
    impl_->error.clear();
    NativePlayback *playback = nullptr;
    try {
        playback = new NativePlayback(
            synthesize(tone, volumePercent),
            [guard] {
                if (!guard) return;
                guard->impl_->playback = nullptr;
                emit guard->finished();
            },
            [guard](const QString &error) { if (guard) guard->setError(error); });
    } catch (const std::exception &error) {
        impl_->changing = false;
        setError("无法生成提示音：" + QString::fromUtf8(error.what()));
        return false;
    }
    impl_->playback = playback;
    const auto reason = playback->begin();
    if (!reason.isEmpty()) {
        const auto cleanup = playback->release(true);
        if (!playback->hasHandle()) impl_->playback = nullptr;
        impl_->changing = false;
        setError(cleanup.isEmpty() ? reason : reason + "\n清理时：" + cleanup);
        return false;
    }
    impl_->changing = false;
    emit started(tone);
    return true;
#else
    Q_UNUSED(tone);
    Q_UNUSED(volumePercent);
    setError("当前平台尚未接入提示音播放；Windows 原生音频仅在 Windows 上可用");
    return false;
#endif
}

void ReminderSound::stop() {
#ifdef Q_OS_WIN
    if (QThread::currentThread() != thread()) {
        setError("提示音必须在其所属的 Qt 线程中停止");
        return;
    }
    if (!impl_->playback) return;
    const QPointer<ReminderSound> guard(this);
    const auto playback = impl_->playback;
    const auto reason = playback->release(true);
    if (!guard) return;
    if (!reason.isEmpty()) setError(reason);
    else if (impl_->playback == playback) impl_->playback = nullptr;
#endif
}

bool ReminderSound::isPlaying() const {
#ifdef Q_OS_WIN
    return impl_->playback && impl_->playback->isPlaying();
#else
    return false;
#endif
}

QString ReminderSound::errorString() const { return impl_->error; }
} // namespace campus
