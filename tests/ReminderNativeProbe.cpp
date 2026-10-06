#include "adapters/ArtifactWriter.h"
#include "adapters/ReminderScheduler.h"
#include "application/TaskService.h"
#include "desktop/BrandTheme.h"
#include "desktop/DesktopWorkspace.h"
#include "desktop/ReminderPopup.h"
#include "storage/Database.h"
#include "storage/SqliteReminderRepository.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteTaskRepository.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <stdexcept>

using namespace campus;

// Native acceptance uses production scheduling and the production popup only.
// Its temporary database contains one explicitly marked test notice/task. It
// never loads ordinary settings, providers, API keys, or a school/network client.
int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setOrganizationName("CampusPulseAcceptance");
    application.setApplicationName("ReminderNativeProbe");
    application.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.setApplicationDescription("CampusPulse isolated native reminder acceptance (test only)");
    parser.addHelpOption();
    parser.addOption({"evidence-dir", "JSON and PNG output directory; no user database is opened", "dir"});
    parser.addOption({"desktop-id", "Optional Windows desktop UUID; no desktop switching", "uuid"});
    parser.process(application);

    try {
        if (!parser.isSet("evidence-dir"))
            throw std::invalid_argument("请使用 --evidence-dir 指定验收输出目录");
        const auto output = QFileInfo(parser.value("evidence-dir")).absoluteFilePath();
        if (!QDir().mkpath(output))
            throw std::runtime_error("无法创建提醒验收输出目录");
        QTemporaryDir temporary;
        if (!temporary.isValid())
            throw std::runtime_error("无法创建独立的测试数据库目录");
        const auto databasePath = temporary.filePath("reminder-native.sqlite");
        Database database(databasePath);
        SqliteRepository noticeRepository(database);
        SqliteTaskRepository taskRepository(database);
        SqliteReminderRepository reminderRepository(database.connection());
        NoticeService notices(noticeRepository, "cn-reminder-probe");
        TaskService tasks("cn-reminder-probe", "Asia/Shanghai", taskRepository, notices);
        Notice notice;
        notice.id = "reminder-native-test-notice";
        notice.schoolId = "cn-reminder-probe";
        notice.sourceId = "test-source";
        notice.sourceName = "[测试] 本地提醒验收";
        notice.title = "[测试] 提醒界面验收，不是真实学校通知";
        notice.url = "https://school.example.edu.cn/test.htm";
        notice.publishedDate = QDateTime::currentDateTimeUtc().date().toString(Qt::ISODate)
                                   .toStdString();
        notices.ingest({notice});
        const auto fire = QDateTime::currentDateTimeUtc().addSecs(6);
        auto task = tasks.draft(notice.id);
        task.title = "[测试] 到点提醒：这是独立验收样例";
        task.time.precision = TimePrecision::DateTime;
        task.time.utcDateTime = fire.toUTC().toString(Qt::ISODate).toStdString();
        task.time.confirmation = TimeConfirmation::Personal;
        task.reminder.enabled = true;
        task.reminder.minutesBefore = 0;
        task = tasks.save(task, true);

        BrandTheme::installApplication(application);
        ReminderPopup popup;
        popup.setWindowTitle("CampusPulse · 待办提醒 · [测试] 原生验收");
        if (parser.isSet("desktop-id"))
            placeWindowOnDesktop(popup, parser.value("desktop-id"));
        QElapsedTimer elapsed;
        elapsed.start();
        int deliveries = 0;
        qint64 firstDeliveryMs = -1;
        QString failure;
        ReminderScheduler scheduler(taskRepository, reminderRepository,
                                    [&](const PersonalTask &due) {
                                        ++deliveries;
                                        if (firstDeliveryMs < 0) firstDeliveryMs = elapsed.elapsed();
                                        popup.showTask(due, "[测试] 学校");
                                    });
        QObject::connect(&scheduler, &ReminderScheduler::failed, &application,
                         [&](const QString &error) { failure = error; });
        scheduler.start();
        bool passed = false;
        const auto jsonPath = QDir(output).filePath("reminder-native.json");
        const auto screenshotPath = QDir(output).filePath("reminder-native.png");
        QTimer::singleShot(10000, &application, [&] {
            try {
                QSqlQuery query(database.connection());
                if (!query.exec("SELECT COUNT(*) FROM reminder_delivery WHERE status='submitted'") ||
                    !query.next())
                    throw std::runtime_error(query.lastError().text().toStdString());
                const int submitted = query.value(0).toInt();
                const bool screenshotSaved = popup.isVisible() && popup.grab().save(screenshotPath);
                auto *items = popup.findChild<QListWidget *>("reminderPopupItems");
                const bool testTitleVisible = items && items->count() == 1 &&
                                              items->item(0)->text().contains("[测试] 到点提醒");
                passed = deliveries == 1 && submitted == 1 && popup.isVisible() &&
                         popup.reminderCount() == 1 && testTitleVisible && screenshotSaved &&
                         failure.isEmpty() && firstDeliveryMs >= 0 && firstDeliveryMs <= 8000;
                const QJsonObject proof{{"passed", passed},
                                        {"test_only", true},
                                        {"temporary_database", databasePath},
                                        {"temporary_database_auto_removed_on_exit", true},
                                        {"user_data_accessed", false},
                                        {"desktop_id", parser.value("desktop-id")},
                                        {"trigger_utc", QString::fromStdString(task.time.utcDateTime)},
                                        {"delivery_count", deliveries},
                                        {"first_delivery_elapsed_ms", double(firstDeliveryMs)},
                                        {"submitted_records", submitted},
                                        {"popup_visible", popup.isVisible()},
                                        {"popup_item_count", popup.reminderCount()},
                                        {"popup_test_title_visible", testTitleVisible},
                                        {"non_modal", !popup.isModal()},
                                        {"window_stays_on_top", popup.windowFlags().testFlag(Qt::WindowStaysOnTopHint)},
                                        {"window_id", QString::number(qulonglong(popup.winId()))},
                                        {"screenshot", screenshotPath},
                                        {"screenshot_saved", screenshotSaved},
                                        {"error", failure},
                                        {"network_requests", 0},
                                        {"model_calls", 0},
                                        {"system_notification_tested", false},
                                        {"auto_exit_ms", 60000}};
                writeArtifact(jsonPath, QJsonDocument(proof).toJson(QJsonDocument::Indented));
                QTextStream(stdout) << QString::fromUtf8(QJsonDocument(proof).toJson(QJsonDocument::Compact))
                                    << '\n';
            } catch (const std::exception &error) {
                QTextStream(stderr) << QString::fromUtf8(error.what()) << '\n';
                application.exit(2);
            }
        });
        // Leave the real popup available for an independent native-window capture.
        QTimer::singleShot(60000, &application, [&] { application.exit(passed ? 0 : 2); });
        return application.exec();
    } catch (const std::exception &error) {
        QTextStream(stderr) << QString::fromUtf8(error.what()) << '\n';
        return 2;
    }
}
