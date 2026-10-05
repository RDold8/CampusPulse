#include "desktop/MainWindow.h"
#include "desktop/NoticePage.h"
#include "desktop/SourcePage.h"
#include "desktop/UniversityPage.h"
#include "desktop/SubscriptionPage.h"
#include "desktop/TaskPage.h"
#include "desktop/AiSourcesPage.h"
#include "desktop/CalendarPage.h"
#include "desktop/ResourcePage.h"
#include "desktop/BrandTheme.h"
#include "adapters/SchoolPackage.h"
#include "adapters/RefreshCoordinator.h"
#include "adapters/ResourceDiscovery.h"
#include <stdexcept>
#include <memory>
#include <utility>
#include <QTabWidget>
#include <QStatusBar>

namespace campus {
MainWindow::MainWindow(const SchoolPackage &school, NoticeService &notices, SourceService &sources,
                       RefreshCoordinator &coordinator, const UniversityRegistry &registry,
                       SubscriptionService &subscriptions, TaskService &tasks,
                       ResourceService *resources, ResourceDiscovery *resourceDiscovery) {
    if ((resources == nullptr) != (resourceDiscovery == nullptr))
        throw std::invalid_argument("学校资源服务与发现器必须同时提供");
    setWindowTitle("CampusPulse · " + school.name);
    resize(1380, 880);
    auto *pages = new QTabWidget;
    pages->setObjectName("mainTabs");
    notices_ = new NoticePage(school, notices, coordinator);
    pages->addTab(notices_, "通知");
    pages->addTab(new SourcePage(school, sources, coordinator), "来源");
    auto *subscriptionPage = new SubscriptionPage(school, subscriptions, coordinator);
    pages->addTab(subscriptionPage, "我的订阅");
    tasks_ = new TaskPage(school, tasks, coordinator);
    pages->addTab(tasks_, "我的待办");
    connect(notices_, &NoticePage::addTaskRequested, tasks_, [this, pages](const QString &id) {
        if (tasks_->createForNotice(id))
            pages->setCurrentWidget(tasks_);
    });
    connect(tasks_, &TaskPage::noticeRequested, this, [this, pages](const QString &id) {
        if (notices_->showNotice(id))
            pages->setCurrentWidget(notices_);
    });
    connect(tasks_, &TaskPage::showNoticesRequested, this,
            [this, pages] { pages->setCurrentWidget(notices_); });
    connect(notices_, &NoticePage::saveSubscriptionRequested, subscriptionPage,
            [pages, subscriptionPage](NoticeQuery query) {
                if (subscriptionPage->createFromQuery(std::move(query)))
                    pages->setCurrentWidget(subscriptionPage);
            });
    connect(subscriptionPage, &SubscriptionPage::noticeRequested, this,
            [this, pages](const QString &id) {
                if (notices_->showNotice(id))
                    pages->setCurrentWidget(notices_);
            });
    auto *universities = new UniversityPage(registry, school.id);
    pages->addTab(universities, "大学");
    auto *ai = new AiSourcesPage(school, registry, coordinator);
    pages->addTab(ai, "AI补充");
    calendar_ = new CalendarPage(school, tasks);
    pages->addTab(calendar_, "日历");
    if (resources) {
        resources_ = new ResourcePage(school, *resources, *resourceDiscovery);
        pages->addTab(resources_, "学校资源");
    }
    connect(tasks_, &TaskPage::tasksChanged, calendar_, [this] { calendar_->reload(); });
    connect(calendar_, &CalendarPage::tasksChanged, tasks_, [this] { tasks_->reload(); });
    connect(calendar_, &CalendarPage::noticeRequested, this, [this, pages](const QString &id) {
        if (notices_->showNotice(id))
            pages->setCurrentWidget(notices_);
    });
    connect(calendar_, &CalendarPage::showTasksRequested, this,
            [this, pages] { pages->setCurrentWidget(tasks_); });
    connect(pages, &QTabWidget::currentChanged, this, [this, pages](int index) {
        if (pages->widget(index) == calendar_)
            calendar_->reload();
        if (resources_ && pages->widget(index) == resources_)
            resources_->activation();
    });
    const QStringList icons{"notices",      "sources", "subscriptions", "tasks",
                            "universities", "ai",      "calendar", "sources"};
    for (int i = 0; i < pages->count(); ++i)
        pages->setTabIcon(i, BrandTheme::navigationIcon(icons.at(i)));
    auto pendingAiConfig = std::make_shared<QString>();
    const auto applyPendingAiConfig = [this, &coordinator, resourceDiscovery, pendingAiConfig] {
        if (pendingAiConfig->isEmpty() || coordinator.busy() ||
            (resourceDiscovery && resourceDiscovery->busy()))
            return;
        const auto config = std::exchange(*pendingAiConfig, {});
        emit universitySelected(config);
    };
    connect(ai, &AiSourcesPage::configReady, this,
            [pendingAiConfig, applyPendingAiConfig](const QString &config) {
                *pendingAiConfig = config;
                applyPendingAiConfig();
            });
    connect(&coordinator, &RefreshCoordinator::finished, this,
            [applyPendingAiConfig](int, int) { applyPendingAiConfig(); });
    if (resourceDiscovery) {
        connect(resourceDiscovery, &ResourceDiscovery::finished, this,
                [applyPendingAiConfig](int, int, int) { applyPendingAiConfig(); });
        connect(resourceDiscovery, &ResourceDiscovery::failed, this,
                [applyPendingAiConfig](const QString &) { applyPendingAiConfig(); });
    }
    connect(universities, &UniversityPage::universitySelected, this,
            &MainWindow::universitySelected);
    connect(universities, &UniversityPage::homepageDiscoveryRequested, this,
            &MainWindow::universityHomepageRequested);
    const auto updateUniversityBusy = [universities, &coordinator, resourceDiscovery] {
        universities->setRefreshing(coordinator.busy() || (resourceDiscovery && resourceDiscovery->busy()));
    };
    connect(&coordinator, &RefreshCoordinator::started, universities,
            [universities] { universities->setRefreshing(true); });
    connect(&coordinator, &RefreshCoordinator::finished, universities,
            [updateUniversityBusy](int, int) { updateUniversityBusy(); });
    if (resourceDiscovery) {
        connect(resourceDiscovery, &ResourceDiscovery::started, universities,
                [universities] { universities->setRefreshing(true); });
        connect(resourceDiscovery, &ResourceDiscovery::finished, universities,
                [updateUniversityBusy](int, int, int) { updateUniversityBusy(); });
        connect(resourceDiscovery, &ResourceDiscovery::failed, universities,
                [updateUniversityBusy](const QString &) { updateUniversityBusy(); });
    }
    setCentralWidget(pages);
    BrandTheme::applyWindow(*this);
}
void MainWindow::reload() {
    notices_->reload();
    tasks_->reload();
    calendar_->reload();
    if (resources_)
        resources_->reload();
}
void MainWindow::showReminder(const QString &title) {
    statusBar()->showMessage("待办提醒：" + title, 60000);
}
bool MainWindow::selectContaining(const QString &keyword) {
    return notices_->selectContaining(keyword);
}
} // namespace campus
