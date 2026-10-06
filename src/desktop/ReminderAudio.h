#pragma once
#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>

namespace campus {
struct ReminderAudioOptions {
    QString tone = "gentle";
    int volume = 60;
    bool repeat = true;
    static ReminderAudioOptions load();
    void save() const;
};

// Repetition is independent of the platform playback device. An injected
// delivery allows deterministic rules tests without pretending sound was heard.
class ReminderAudio final : public QObject {
    Q_OBJECT
  public:
    using Delivery = std::function<bool(const QString &, int)>;
    using Stop = std::function<void()>;
    ReminderAudio(Delivery delivery, Stop stop, QObject *parent = nullptr,
                  int intervalMs = 12000, int durationMs = 60000);
    void begin(const ReminderAudioOptions &options);
    void stop();
    bool repeating() const;
  private:
    Delivery delivery_;
    Stop stop_;
    ReminderAudioOptions options_;
    QTimer repeat_, deadline_;
    QElapsedTimer elapsed_;
    int durationMs_;
    void play();
};
} // namespace campus
