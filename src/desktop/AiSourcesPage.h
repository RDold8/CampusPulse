#pragma once
#include "adapters/SchoolPackage.h"
#include "adapters/DeepSeekSearch.h"
#include "adapters/AiProviderConfig.h"
#include "adapters/AiProviderProbe.h"
#include <QWidget>
#include <QHash>
class QComboBox;
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
                  RefreshCoordinator &refresh, QWidget *parent = nullptr,
                  QString providerDirectory = {});
  signals:
    void configReady(QString config);

  private:
    SchoolPackage school_;
    QString root_, requestedModel_, requestedTemplate_;
    AiProviderConfig requestedProvider_;
    AiProviderStore providers_;
    AiProviderProbe probe_;
    DeepSeekSearch search_;
    QComboBox *model_, *searchTemplate_;
    QLabel *status_, *active_, *selected_, *capability_, *templateDescription_;
    QPushButton *run_, *configure_, *add_, *edit_, *remove_, *activate_, *disable_, *save_, *fetch_;
    QCheckBox *automatic_;
    QListWidget *results_, *providerList_;
    QHash<QString, QStringList> modelDirectories_;
    bool busy_ = false;
    bool refreshing_ = false;
    bool storeReady_ = false;
    AiProviderConfig selectedProvider() const;
    AiProviderConfig selectedModelProvider() const;
    AiProviderConfig activeProvider() const;
    QString providerKey(const AiProviderConfig &provider) const;
    QString activeKey() const;
    void reloadProviders(QString selectedId = {});
    void loadSelected();
    void updateEnabled();
    bool editProvider(bool add);
    void saveSelected();
    void activateSelected();
    void removeSelected();
    void fetchModels();
    void updateTemplate();
    void run();
    void validate(QJsonArray candidates, QJsonObject usage);
};
} // namespace campus
