#pragma once
#include <QMainWindow>

namespace campus {
struct SchoolPackage;
class NoticeService;
class SourceService;
class RefreshCoordinator;
class NoticePage;
class UniversityRegistry;
class SubscriptionService;
class TaskService;
class TaskPage;
class CalendarPage;
class ResourceService;
class ResourceDiscovery;
class ResourcePage;

class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    MainWindow(const SchoolPackage &school, NoticeService &notices, SourceService &sources,
               RefreshCoordinator &coordinator, const UniversityRegistry &registry,
               SubscriptionService &subscriptions, TaskService &tasks,
               ResourceService *resources = nullptr, ResourceDiscovery *resourceDiscovery = nullptr);
    void reload();
    bool selectContaining(const QString &keyword);
    void showReminder(const QString &title);
    void showTask(const QString &taskId = {});
    void setBackgroundReminders(bool enabled);
  signals:
    void universitySelected(QString configFile);
    void universityHomepageRequested(QString homepage);
    void reminderTestRequested();
    void reminderSoundSettingsRequested();

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    NoticePage *notices_;
    TaskPage *tasks_;
    CalendarPage *calendar_;
    ResourcePage *resources_ = nullptr;
    bool backgroundReminders_ = false;
};
} // namespace campus
