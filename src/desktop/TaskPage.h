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
class QStackedWidget;
class QVBoxLayout;
class QScrollArea;
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
    void reminderTestRequested();

  private:
    SchoolPackage school_;
    TaskService &service_;
    std::vector<TaskView> views_;
    std::vector<QString> visibleIds_;
    QStandardItemModel *model_;
    QTableView *table_;
    QComboBox *filter_, *dateFilter_;
    QComboBox *viewMode_;
    QLineEdit *search_;
    QLabel *summary_, *status_;
    QStackedWidget *content_;
    QScrollArea *cards_;
    QWidget *cardContent_, *listActions_;
    QVBoxLayout *cardLayout_;
    QTextBrowser *detail_;
    QPushButton *edit_, *start_, *complete_, *undo_, *cancel_, *restore_, *review_, *open_;
    QString selectedId() const;
    QString selectedTaskId_;
    const TaskView *selected() const;
    void selectTask(const QString &id);
    void rebuildCards();
    void updateCardSelection();
    void selectionChanged();
    void edit();
    void changeStatus(TaskStatus status);
    void acknowledge();
};
} // namespace campus
