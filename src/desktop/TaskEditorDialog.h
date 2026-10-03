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
namespace campus {
class TaskEditorDialog final : public QDialog {
    Q_OBJECT
  public:
    TaskEditorDialog(const SchoolPackage &school, TaskService &service, PersonalTask task,
                     const QString &noticeTitle, QWidget *parent = nullptr);
    const PersonalTask &saved() const {
        return saved_;
    }

  private:
    TaskService &service_;
    PersonalTask initial_, saved_;
    QTimeZone zone_;
    QLineEdit *title_;
    QComboBox *action_, *status_, *precision_, *source_;
    QTextEdit *notes_, *evidence_;
    QDateEdit *date_;
    QDateTimeEdit *dateTime_;
    QCheckBox *confirmed_, *remind_;
    QSpinBox *minutes_, *days_;
    QTimeEdit *reminderTime_;
    QLabel *error_;
    void updateTimeControls();
    void timeChanged();
    void save();
};
} // namespace campus
