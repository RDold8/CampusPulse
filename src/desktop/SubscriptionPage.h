#pragma once
#include "application/SubscriptionService.h"
#include "adapters/RefreshCoordinator.h"
#include "desktop/NoticeListModel.h"
#include <QWidget>
class QListWidget;
class QTableView;
class QLabel;
class QPushButton;
namespace campus {
class SubscriptionPage final : public QWidget {
    Q_OBJECT
  public:
    SubscriptionPage(const SchoolPackage &school, SubscriptionService &service,
                     RefreshCoordinator &coordinator, QWidget *parent = nullptr);
    bool createFromQuery(NoticeQuery query);
    void reload(const QString &selectId = {});
  signals:
    void noticeRequested(QString id);

  private:
    SchoolPackage school_;
    SubscriptionService &service_;
    NoticeListModel matches_;
    QListWidget *list_;
    QTableView *table_;
    QLabel *summary_, *rules_, *status_;
    QPushButton *edit_, *pause_, *remove_, *open_;
    QString selectedId() const;
    void selectionChanged();
    void edit();
    void togglePaused();
    void remove();
    void openNotice();
};
} // namespace campus
