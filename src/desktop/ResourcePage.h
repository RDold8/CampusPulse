#pragma once
#include "adapters/SchoolPackage.h"
#include "application/ResourceService.h"
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStandardItemModel;
class QTableView;
class QTextBrowser;
class QTimer;

namespace campus {
class ResourceDiscovery;
class ResourcePage final : public QWidget {
    Q_OBJECT
  public:
    // The session owns both services; they must outlive this page.
    ResourcePage(const SchoolPackage &school, ResourceService &resources,
                 ResourceDiscovery &discovery, QWidget *parent = nullptr);
    // Called when this tab is selected. A cache is visible before discovery begins.
    void activation();
    void reload();

  private:
    SchoolPackage school_;
    ResourceService &resources_;
    ResourceDiscovery &discovery_;
    std::vector<SchoolResource> views_;
    QComboBox *category_, *stage_;
    QLineEdit *search_;
    QCheckBox *favorites_;
    QStandardItemModel *model_;
    QTableView *table_;
    QTextBrowser *detail_;
    QLabel *summary_, *status_;
    QPushButton *open_, *favorite_, *discover_, *cancel_;
    QTimer *refreshTimer_;
    bool activated_ = false;
    bool cancelRequested_ = false;
    QString failureReason_;
    const SchoolResource *selected() const;
    void updateSelection();
    void discover();
    void cancelDiscovery();
    void toggleFavorite();
    void openResource();
    void openOfficialOrigin(const QUrl &url);
    bool canOpen(const SchoolResource &resource) const;
    bool officialOrigin(const QUrl &url) const;
};
} // namespace campus
