#pragma once
#include "adapters/AiProviderConfig.h"
#include "adapters/AiProviderProbe.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
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

  private:
    AiProviderStore &store_;
    AiProviderConfig initial_, saved_;
    AiProviderProbe probe_;
    QComboBox *preset_, *protocol_, *model_, *auth_;
    QLineEdit *name_, *notes_, *website_, *baseUrl_, *key_;
    QCheckBox *remember_, *fullUrl_;
    QToolButton *reveal_;
    QLabel *status_, *capability_, *endpoint_;
    QPlainTextEdit *preview_;
    QPushButton *fetch_, *test_, *save_;
    AiProviderConfig formProvider() const;
    void updateProtocol();
    void applyPreset(int index);
    void startProbe(bool models);
    void setBusy(bool busy);
    void save();
};
} // namespace campus
