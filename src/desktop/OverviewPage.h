#pragma once
#include <QWidget>

class QLabel;
class QListWidget;
class QSplitter;
class QBoxLayout;
namespace campus {
struct SchoolPackage;
class NoticeService;
class SourceService;
class TaskService;
class OverviewPage final : public QWidget {
    Q_OBJECT
  public:
    OverviewPage(const SchoolPackage &school, NoticeService &notices, SourceService &sources,
                 TaskService &tasks, QWidget *parent = nullptr);
    void reload();
    void setResourcesAvailable(bool available);
  signals:
    void noticesRequested();
    void sourcesRequested();
    void tasksRequested();
    void resourcesRequested();
    void newReminderRequested();
    void noticeRequested(QString id);
    void taskRequested(QString id);
  protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
  private:
    void updateLayout();
    QSplitter *splitter_;
    QBoxLayout *actions_;
    NoticeService &notices_;
    SourceService &sources_;
    TaskService &tasks_;
    QString timeZone_;
    QLabel *date_, *summary_, *health_, *error_;
    QListWidget *taskList_, *noticeList_;
};
} // namespace campus
