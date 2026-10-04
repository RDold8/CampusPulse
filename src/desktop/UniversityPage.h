#pragma once
#include <QWidget>

class QLineEdit;
class QPushButton;
class QLabel;

namespace campus {
class UniversityRegistry;

class UniversityPage final : public QWidget {
    Q_OBJECT
  public:
    UniversityPage(const UniversityRegistry &registry, const QString &currentSchoolId,
                   QWidget *parent = nullptr);
    void setBusy(bool busy);
    void setFeedback(const QString &message);

  signals:
    void universitySelected(QString configFile);
    void homepageDiscoveryRequested(QString homepage);

  private:
    const UniversityRegistry &registry_;
    QLineEdit *url_;
    QPushButton *load_;
    QLabel *feedback_;
    void loadUniversity();
};
} // namespace campus
