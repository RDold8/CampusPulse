#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

namespace campus {
// Original short PCM chimes through Windows waveOut. Volume changes only the
// samples for this playback; the device and system volume are never changed.
class ReminderSound final : public QObject {
    Q_OBJECT
  public:
    explicit ReminderSound(QObject *parent = nullptr);
    ~ReminderSound() override;
    bool play(const QString &tone, int volumePercent);
    void stop();
    bool isPlaying() const;
    QString errorString() const;
    static QStringList tones();
  signals:
    void started(QString tone);
    void finished();
    void failed(QString reason);
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void setError(const QString &reason);
};
} // namespace campus
