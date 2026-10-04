#pragma once
#include "adapters/SchoolPackage.h"
#include "adapters/DeepSeekSearch.h"
#include "adapters/AiProviderConfig.h"
#include "adapters/AiProviderProbe.h"
#include <QWidget>
class QLineEdit;
class QLabel;
class QPushButton;
class QCheckBox;
class QListWidget;
class QToolButton;
namespace campus {
class UniversityRegistry;
class RefreshCoordinator;
class AiSourcesPage final : public QWidget {
    Q_OBJECT
  public:
    AiSourcesPage(const SchoolPackage &school, const UniversityRegistry &registry,
                  RefreshCoordinator &refresh, QWidget *parent = nullptr,
                  QString providerDirectory = {});
  signals:
    void configReady(QString config);

  private:
    SchoolPackage school_;
    QString root_, requestedModel_;
    AiProviderConfig requestedProvider_;
    AiProviderStore providers_;
    AiProviderProbe probe_;
    DeepSeekSearch search_;
    QLineEdit *key_, *model_;
    QLabel *status_, *active_, *selected_, *capability_;
    QPushButton *run_, *add_, *edit_, *remove_, *activate_, *disable_, *save_, *test_;
    QToolButton *reveal_;
    QCheckBox *automatic_, *remember_;
    QListWidget *results_, *providerList_;
    bool busy_ = false;
    bool refreshing_ = false;
    bool storeReady_ = false;
    AiProviderConfig selectedProvider() const;
    AiProviderConfig activeProvider() const;
    QString activeKey() const;
    void reloadProviders(QString selectedId = {});
    void loadSelected();
    void updateEnabled();
    void editProvider(bool add);
    void saveSelected();
    void activateSelected();
    void removeSelected();
    void testSelected();
    void run();
    void validate(QJsonArray candidates, QJsonObject usage);
};
} // namespace campus
