#pragma once
#include <QWidget>

class QLineEdit;
class QPushButton;
class QLabel;
class QListWidget;
class QProgressBar;

namespace campus {
class UniversityRegistry;

class UniversityPage final : public QWidget {
    Q_OBJECT
  public:
    UniversityPage(const UniversityRegistry &registry, const QString &currentSchoolId,
                   QWidget *parent = nullptr);
    void setBusy(bool busy);
    void setRefreshing(bool refreshing);
    void setFeedback(const QString &message);

  signals:
    void universitySelected(QString configFile);
    void homepageDiscoveryRequested(QString homepage);

  private:
    const UniversityRegistry &registry_;
    QLineEdit *url_;
    QPushButton *load_;
    QLabel *feedback_;
    QListWidget *directory_;
    QProgressBar *progress_;
    bool busy_ = false;
    bool onboardingBusy_ = false;
    bool refreshing_ = false;
    void applyBusy();
    void loadUniversity();
};
} // namespace campus
