#pragma once
#include "adapters/SchoolPackage.h"
#include "adapters/DeepSeekSearch.h"
#include <QWidget>
class QLineEdit;
class QLabel;
class QPushButton;
class QCheckBox;
class QListWidget;
namespace campus {
class UniversityRegistry;
class RefreshCoordinator;
class AiSourcesPage final : public QWidget {
    Q_OBJECT
  public:
    AiSourcesPage(const SchoolPackage &school, const UniversityRegistry &registry,
                  RefreshCoordinator &refresh, QWidget *parent = nullptr);
  signals:
    void configReady(QString config);

  private:
    SchoolPackage school_;
    QString root_, requestedModel_;
    DeepSeekSearch search_;
    QLineEdit *key_, *model_;
    QLabel *status_;
    QPushButton *run_;
    QCheckBox *automatic_;
    QListWidget *results_;
    bool busy_ = false;
    bool refreshing_ = false;
    void run();
    void validate(QJsonArray candidates, QJsonObject usage);
};
} // namespace campus
