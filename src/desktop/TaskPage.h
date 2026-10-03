#pragma once
#include "application/TaskService.h"
#include "adapters/RefreshCoordinator.h"
#include <QWidget>
class QComboBox;
class QLineEdit;
class QLabel;
class QTableView;
class QStandardItemModel;
class QPushButton;
class QTextBrowser;
namespace campus {
class TaskPage final : public QWidget {
    Q_OBJECT
  public:
    TaskPage(const SchoolPackage &school, TaskService &service, RefreshCoordinator &coordinator,
             QWidget *parent = nullptr);
    bool createForNotice(const QString &noticeId);
    void reload(const QString &selectId = {});
  signals:
    void tasksChanged();
    void noticeRequested(QString id);
    void showNoticesRequested();

  private:
    SchoolPackage school_;
    TaskService &service_;
    std::vector<TaskView> views_;
    QStandardItemModel *model_;
    QTableView *table_;
    QComboBox *filter_, *dateFilter_;
    QLineEdit *search_;
    QLabel *summary_, *status_;
    QTextBrowser *detail_;
    QPushButton *edit_, *start_, *complete_, *undo_, *cancel_, *restore_, *review_, *open_;
    QString selectedId() const;
    const TaskView *selected() const;
    void selectionChanged();
    void edit();
    void changeStatus(TaskStatus status);
    void acknowledge();
};
} // namespace campus
