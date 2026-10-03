#pragma once
#include "application/SourceService.h"
#include <QWidget>
#include <vector>

class QTableView;
class QStandardItemModel;
class QTextBrowser;
class QLabel;
class QPushButton;

namespace campus {
struct SchoolPackage;
class RefreshCoordinator;

class SourcePage final : public QWidget {
    Q_OBJECT
  public:
    SourcePage(const SchoolPackage &school, SourceService &sources, RefreshCoordinator &coordinator,
               QWidget *parent = nullptr);
    void reload();

  private:
    SourceService &sources_;
    RefreshCoordinator &coordinator_;
    std::vector<SourceView> views_;
    QTableView *table_;
    QStandardItemModel *model_;
    QTextBrowser *detail_;
    QLabel *summary_;
    QLabel *status_;
    QPushButton *update_;
    QPushButton *pause_;
    QWidget *loginPanel_;
    QLabel *loginSummary_;
    QLabel *loginExplanation_;
    QLabel *loginAddress_;
    QPushButton *showLogin_;
    QPushButton *openLogin_;
    const SourceView *selected() const;
    void updateSelection();
    void updateSelected();
    void togglePause();
    void selectLoginSource();
    void openOfficialLogin();
};
} // namespace campus
