#pragma once
#include "desktop/ReminderAudio.h"
#include <QDialog>
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

namespace campus {
class ReminderSound;
class ReminderSoundDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit ReminderSoundDialog(QWidget *parent = nullptr);
    ~ReminderSoundDialog() override;
    const ReminderAudioOptions &options() const {
        return saved_;
    }
  signals:
    void settingsSaved();

  protected:
    void done(int result) override;

  private:
    ReminderAudioOptions saved_;
    ReminderSound *sound_;
    QComboBox *tone_;
    QSlider *volume_;
    QCheckBox *repeat_;
    QLabel *percentage_, *status_;
    QPushButton *stop_;
    ReminderAudioOptions currentOptions() const;
    bool stopPreview();
    void preview();
    void save();
    void settingsChanged();
};
} // namespace campus
