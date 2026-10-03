#include "adapters/ArtifactWriter.h"
#include "adapters/RefreshCoordinator.h"
#include "adapters/ReminderScheduler.h"
#include "adapters/UniversityRegistry.h"
#include "application/SubscriptionService.h"
#include "desktop/BrandTheme.h"
#include "desktop/CalendarPage.h"
#include "desktop/DesktopWorkspace.h"
#include "desktop/MainWindow.h"
#include "storage/Database.h"
#include "storage/SqliteReminderRepository.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteSourceRepository.h"
#include "storage/SqliteSubscriptionRepository.h"
#include "storage/SqliteTaskRepository.h"
#include <QApplication>
#include <QCalendarWidget>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimeZone>
#include <QTimer>
#include <stdexcept>

using namespace campus;
namespace {
struct Session {
    Database db;
    SqliteRepository noticeRepository;
    SqliteSourceRepository sourceRepository;
    SqliteSubscriptionRepository subscriptionRepository;
    SqliteTaskRepository taskRepository;
    SqliteReminderRepository reminderRepository;
    NoticeService notices;
    SourceService sources;
    SubscriptionService subscriptions;
    TaskService tasks;
    RefreshCoordinator coordinator;
    Session(const QString &path, const SchoolPackage &school)
        : db(path), noticeRepository(db), sourceRepository(db), subscriptionRepository(db),
          taskRepository(db), reminderRepository(db.connection()),
          notices(noticeRepository, school.id.toStdString()),
          sources(school.id.toStdString(), school.catalog, sourceRepository),
          subscriptions(school.id.toStdString(), subscriptionRepository, notices),
          tasks(school.id.toStdString(), school.timeZone.toStdString(), taskRepository, notices),
          coordinator(school, notices, sources, nullptr, {0, 1000}) {}
    void seed(const SchoolPackage &school, const QUrl &homepage, const QDateTime &exactTime) {
        const auto today =
            QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone(school.timeZone.toUtf8())).date();
        Notice notice;
        notice.id = "calendar-probe-notice";
        notice.schoolId = school.id.toStdString();
        notice.sourceId = "calendar-probe-source";
        notice.sourceName = "【演示】日历验收样本";
        notice.title = "【演示】重修缴费、竞赛报名和校园活动";
        notice.url = homepage.toString().toStdString();
        notice.publishedDate = today.toString(Qt::ISODate).toStdString();
        notices.ingest({notice});
        notice.body = "【演示数据】此通知和待办用于程序界面、ICS与提醒测试，"
                      "并非真实官网通告，不代表任何学校的缴费或竞赛时间。";
        notices.saveDetail(notice);
        auto dateTask = tasks.draft(notice.id);
        dateTask.title = "【演示】确认重修缴费材料";
        dateTask.action = "payment";
        dateTask.notes = "【演示】全天事项；不代表官网截止日期。";
        dateTask.time.precision = TimePrecision::DateOnly;
        dateTask.time.date = today.toString(Qt::ISODate).toStdString();
        dateTask.time.confirmation = TimeConfirmation::Personal;
        dateTask.time.evidence = "【演示】用户确认的个人计划日期";
        tasks.save(dateTask, true);
        auto exactTask = tasks.draft(notice.id);
        exactTask.title = "【演示】竞赛与活动准确时刻提醒";
        exactTask.action = "attendance";
        exactTask.time.precision = TimePrecision::DateTime;
        exactTask.time.utcDateTime =
            exactTime.toUTC().toString("yyyy-MM-ddTHH:mm:ssZ").toStdString();
        exactTask.time.confirmation = TimeConfirmation::Personal;
        exactTask.time.evidence = "【演示】测试开始后几秒的个人计划时刻";
        exactTask.notes = "【演示】测试提醒投递，不代表官网活动时间。";
        exactTask.reminder.enabled = true;
        exactTask.reminder.minutesBefore = 0;
        tasks.save(exactTask, true);
    }
};
} // namespace
// An explicit native acceptance entry point. All writes stay in the caller's
// fresh evidence database; user preferences and the normal application DB are untouched.
int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setOrganizationName("CampusPulseAcceptance");
    application.setApplicationName("CalendarNativeProbe");
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "CampusPulse production calendar acceptance probe (demo only)");
    parser.addHelpOption();
    parser.addOption({"config", "School config", "file", CONFIG_FILE});
    parser.addOption({"database", "Fresh evidence database; existing files are refused", "file"});
    parser.addOption({"desktop-id", "Windows target desktop UUID; no desktop switching", "uuid"});
    parser.addOption({"export", "ICS output path via the real CalendarPage", "file"});
    parser.addOption({"evidence", "JSON output path", "file"});
    parser.addOption({"screenshot", "Production calendar window PNG output", "file"});
    parser.addOption(
        {"self-exit-ms", "Exit delay; 0 leaves the window for UI inspection", "ms", "5000"});
    parser.process(application);
    try {
        QTemporaryDir fallback;
        const auto path = parser.isSet("database")
                              ? QFileInfo(parser.value("database")).absoluteFilePath()
                              : fallback.filePath("probe.sqlite");
        if (QFile::exists(path))
            throw std::invalid_argument("验收数据库已存在，请指定新的文件；未改动原有数据库");
        const auto folder = QFileInfo(path).absolutePath();
        if (!QDir().mkpath(folder))
            throw std::runtime_error("无法创建验收输出目录");
        const auto evidencePath = parser.isSet("evidence")
                                      ? parser.value("evidence")
                                      : QDir(folder).filePath("calendar-probe.json");
        const auto exportPath = parser.isSet("export")
                                    ? parser.value("export")
                                    : QDir(folder).filePath("calendar-probe.ics");
        const auto screenshotPath = parser.isSet("screenshot")
                                        ? parser.value("screenshot")
                                        : QDir(folder).filePath("calendar-probe.png");
        bool exitValid = false;
        const auto exitDelay = parser.value("self-exit-ms").toInt(&exitValid);
        if (!exitValid || exitDelay < 0 || (exitDelay > 0 && exitDelay < 4000))
            throw std::invalid_argument("自动退出需至少4000毫秒，或设置0保留窗口");
        const auto school = SchoolPackage::load(parser.value("config"));
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        QUrl homepage;
        for (const auto &university : registry.list())
            if (university.id == school.id)
                homepage = university.homepage;
        if (homepage.isEmpty())
            throw std::invalid_argument("验收学校不在本地已核验目录中");
        Session session(path, school);
        BrandTheme::installApplication(application);
        const auto fire = QDateTime::currentDateTimeUtc().addSecs(2);
        session.seed(school, homepage, fire);
        MainWindow window(school, session.notices, session.sources, session.coordinator, registry,
                          session.subscriptions, session.tasks);
        window.setWindowTitle(window.windowTitle() + " · 第4步演示验收");
        window.setAttribute(Qt::WA_ShowWithoutActivating);
        if (!parser.value("desktop-id").isEmpty())
            placeWindowOnDesktop(window, parser.value("desktop-id"));
        auto *calendar = window.findChild<CalendarPage *>("calendarPage");
        auto *tabs = window.findChild<QTabWidget *>("mainTabs");
        auto *month = window.findChild<QCalendarWidget *>("calendarMonth");
        if (!calendar || !tabs || !month)
            throw std::runtime_error("正式日历页面或控件缺失");
        month->setSelectedDate(fire.toTimeZone(QTimeZone(school.timeZone.toUtf8())).date());
        month->showSelectedDate();
        calendar->reload();
        tabs->setCurrentWidget(calendar);
        if (!calendar->exportToFile(exportPath, true, true))
            throw std::runtime_error(
                window.findChild<QLabel *>("calendarStatus")->text().toStdString());
        window.show();
        int delivered = 0;
        QString reminderError;
        ReminderScheduler scheduler(session.taskRepository, session.reminderRepository,
                                    [&](const PersonalTask &task) {
                                        ++delivered;
                                        window.showReminder(QString::fromStdString(task.title));
                                    });
        QObject::connect(&scheduler, &ReminderScheduler::failed, &application,
                         [&](const QString &error) { reminderError = error; });
        scheduler.start();
        QTimer testPoll;
        testPoll.setInterval(250);
        QObject::connect(&testPoll, &QTimer::timeout, &application,
                         [&] { scheduler.poll(QDateTime::currentDateTimeUtc()); });
        testPoll.start();
        bool evidenceSaved = false;
        const auto saveEvidence = [&] {
            if (evidenceSaved)
                return;
            evidenceSaved = true;
            const bool screenshotSaved = window.grab().save(screenshotPath);
            QFile file(exportPath);
            const bool readable = file.open(QIODevice::ReadOnly);
            const auto bytes = readable ? file.readAll() : QByteArray{};
            const auto banner = window.statusBar()->currentMessage();
            const bool passed = delivered == 1 && reminderError.isEmpty() && screenshotSaved &&
                                bytes.count("BEGIN:VEVENT") == 2 &&
                                bytes.count("BEGIN:VALARM") == 1 && banner.contains("【演示】");
            const QJsonObject proof{{"passed", passed},
                                    {"demo_only", true},
                                    {"database", path},
                                    {"desktop_id", parser.value("desktop-id")},
                                    {"calendar_ics", exportPath},
                                    {"screenshot", screenshotPath},
                                    {"task_count", static_cast<int>(session.tasks.list().size())},
                                    {"ics_events", static_cast<int>(bytes.count("BEGIN:VEVENT"))},
                                    {"ics_alarms", static_cast<int>(bytes.count("BEGIN:VALARM"))},
                                    {"delivered_count", delivered},
                                    {"in_app_reminder", banner},
                                    {"reminder_error", reminderError},
                                    {"network_requests_requested", 0},
                                    {"model_calls", 0},
                                    {"system_notification_tested", false},
                                    {"phone_import_tested", false}};
            writeArtifact(evidencePath, QJsonDocument(proof).toJson(QJsonDocument::Indented));
            QTextStream(stdout) << QString::fromUtf8(
                                       QJsonDocument(proof).toJson(QJsonDocument::Compact))
                                << '\n';
            if (exitDelay > 0)
                application.exit(passed ? 0 : 2);
        };
        QTimer::singleShot(exitDelay > 0 ? exitDelay : 5000, &application, [&] {
            try {
                saveEvidence();
            } catch (const std::exception &error) {
                QTextStream(stderr) << QString::fromUtf8(error.what()) << '\n';
                application.exit(2);
            }
        });
        return application.exec();
    } catch (const std::exception &error) {
        QTextStream(stderr) << QString::fromUtf8(error.what()) << '\n';
        return 2;
    }
}
