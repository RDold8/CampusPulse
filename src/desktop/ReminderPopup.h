#pragma once
#include "domain/PersonalTask.h"
#include "desktop/ReminderAudio.h"
#include "desktop/ReminderSound.h"
#include <QDialog>
#include <QSet>
class QListWidget;
class QLabel;
namespace campus {
// Persistent, non-modal delivery surface. Closing acknowledges the visible batch;
// it does not change task status or claim that the user completed the task.
class ReminderPopup final : public QDialog {
    Q_OBJECT
  public:
    explicit ReminderPopup(QWidget *parent = nullptr);
    void showTask(const PersonalTask &task, const QString &schoolName = {});
    void showTest();
    void showFailure(const QString &reason);
    int reminderCount() const;
    void stopSound();
    void reloadAudioOptions();
  signals:
    void taskRequested(QString schoolId, QString taskId);
    void soundSettingsRequested();
  protected:
    void done(int result) override;
  private:
    QListWidget *items_;
    QLabel *heading_;
    QLabel *audioStatus_;
    ReminderSound sound_;
    ReminderAudio audio_;
    ReminderAudioOptions audioOptions_;
    QSet<QString> displayed_;
    QSet<QString> failures_;
    void present();
    void ring();
};
} // namespace campus
