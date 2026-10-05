#pragma once
#include "adapters/AiProviderConfig.h"
#include "adapters/AiProviderProbe.h"
#include <QDialog>
#include <QStringList>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace campus {
// A provider editor, not an importer of other applications' credentials.
class AiProviderDialog final : public QDialog {
    Q_OBJECT
  public:
    AiProviderDialog(AiProviderStore &store, AiProviderConfig initial,
                     QWidget *parent = nullptr);
    const AiProviderConfig &savedProvider() const { return saved_; }
    const QStringList &modelIds() const { return modelIds_; }
    bool connectionVerified() const { return connectionVerified_; }

  private:
    AiProviderStore &store_;
    AiProviderConfig initial_, saved_;
    AiProviderProbe probe_;
    QComboBox *model_;
    QLineEdit *name_, *baseUrl_, *key_;
    QCheckBox *remember_;
    QToolButton *reveal_, *advanced_;
    QLabel *status_;
    QPushButton *fetch_, *save_, *saveOnly_;
    QStringList modelIds_;
    bool connecting_ = false;
    bool connectionVerified_ = false;
    std::optional<AiProviderConfig> connectedProvider_;
    QByteArray connectedKeyDigest_;
    bool hasCurrentConnection() const;
    AiProviderConfig formProvider() const;
    void fetchModels();
    void connectAndSave();
    void setBusy(bool busy);
    bool persist(const AiProviderConfig &provider, bool activate);
    void saveOnly();
};
} // namespace campus
