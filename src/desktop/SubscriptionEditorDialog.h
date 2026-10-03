#pragma once
#include "application/SubscriptionService.h"
#include "adapters/SchoolPackage.h"
#include <QDialog>
#include <vector>
class QLineEdit;
class QComboBox;
class QSpinBox;
class QListWidget;
class QLabel;
class QCheckBox;
namespace campus {
class SubscriptionEditorDialog final : public QDialog {
    Q_OBJECT
  public:
    SubscriptionEditorDialog(const SchoolPackage &school, SubscriptionService &service,
                             Subscription initial, QWidget *parent = nullptr);
    const Subscription &saved() const {
        return saved_;
    }

  private:
    SubscriptionService &service_;
    Subscription initial_, saved_;
    QLineEdit *name_, *all_, *any_, *exclude_;
    QComboBox *year_;
    QSpinBox *fixedYear_;
    QListWidget *sources_;
    std::vector<QCheckBox *> themes_, stages_;
    QLabel *preview_, *error_;
    NoticeQuery readQuery() const;
    void updatePreview();
    void save();
};
} // namespace campus
