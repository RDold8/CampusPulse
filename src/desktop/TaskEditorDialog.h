#pragma once
#include "application/TaskService.h"
#include "adapters/SchoolPackage.h"
#include <QDialog>
#include <QTimeZone>
class QLineEdit;
class QComboBox;
class QTextEdit;
class QDateEdit;
class QDateTimeEdit;
class QCheckBox;
class QSpinBox;
class QTimeEdit;
class QLabel;
class QFormLayout;
class QToolButton;
namespace campus {
class TaskEditorDialog final : public QDialog {
    Q_OBJECT
  public:
    TaskEditorDialog(const SchoolPackage &school, TaskService &service, PersonalTask task,
                     const QString &noticeTitle, QWidget *parent = nullptr);
    const PersonalTask &saved() const {
        return saved_;
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    TaskService &service_;
    PersonalTask initial_, saved_;
    QTimeZone zone_;
    QLineEdit *title_;
    QComboBox *action_, *status_, *precision_, *source_, *reminderPreset_;
    QTextEdit *notes_, *evidence_;
    QDateEdit *date_;
    QDateTimeEdit *dateTime_;
    QCheckBox *confirmed_, *remind_, *scheduled_;
    QSpinBox *minutes_, *days_;
    QTimeEdit *reminderTime_;
    QLabel *error_, *preview_;
    QFormLayout *form_, *advancedForm_;
    QWidget *advanced_;
    QToolButton *more_;
    bool reminderChosen_ = false;
    void updateTimeControls();
    void rebuildReminderPresets();
    void applyReminderPreset();
    void updatePreview();
    void timeChanged();
    void save();
};
} // namespace campus
