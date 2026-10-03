#pragma once
#include "application/NoticeService.h"
#include "adapters/RefreshCoordinator.h"
#include "desktop/NoticeListModel.h"
#include <QWidget>
#include "desktop/NoticeFilter.h"

class QLineEdit;
class QComboBox;
class QTableView;
class QTextBrowser;
class QLabel;
class QPushButton;
namespace campus {
class NoticePage final : public QWidget {
    Q_OBJECT
  public:
    NoticePage(const SchoolPackage &school, NoticeService &service, RefreshCoordinator &coordinator,
               QWidget *parent = nullptr);
    void reload();
    bool selectContaining(const QString &keyword);
    bool showNotice(const QString &id);
    NoticeQuery currentQuery() const {
        return filter_.query();
    }
  signals:
    void saveSubscriptionRequested(campus::NoticeQuery query);
    void addTaskRequested(QString noticeId);

  private:
    NoticeService &service_;
    RefreshCoordinator &coordinator_;
    NoticeListModel model_;
    NoticeFilter filter_;
    QComboBox *year_;
    QTableView *table_;
    QTextBrowser *body_;
    QTextBrowser *attachments_;
    QLabel *title_;
    QLabel *count_;
    QLabel *status_;
    QPushButton *refresh_;
    QPushButton *addTask_, *refreshOriginal_;
    bool detailPending_ = false;
    QString selectedId_;
    Notice selected_;
    void updateCount();
    void selectedChanged();
    void renderDetail(const Notice &notice);
    void updateDetailActions();
};
} // namespace campus
