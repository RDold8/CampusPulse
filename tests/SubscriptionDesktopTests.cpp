#include "adapters/RefreshCoordinator.h"
#include "adapters/UniversityRegistry.h"
#include "application/SubscriptionService.h"
#include "desktop/MainWindow.h"
#include "desktop/SubscriptionEditorDialog.h"
#include "storage/Database.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteSourceRepository.h"
#include "storage/SqliteSubscriptionRepository.h"
#include "storage/SqliteTaskRepository.h"
#include "application/TaskService.h"
#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>

using namespace campus;
namespace {
template <class T> T *control(QWidget &window, const char *name) {
    auto *result = window.findChild<T *>(name);
    if (!result)
        throw std::runtime_error(std::string("界面控件缺失：") + name);
    return result;
}
Notice sample(const std::string &id, const std::string &title, int year,
              const std::string &source = "academic-affairs-notices") {
    Notice value;
    value.id = id;
    value.schoolId = "cn-neepu";
    value.sourceId = source;
    value.sourceName = source == "academic-affairs-notices" ? "教务处通知公告" : "学生工作处";
    value.title = title;
    value.url = "https://jwc.neepu.edu.cn/" + id;
    value.publishedDate = std::to_string(year) + "-09-16";
    return value;
}
void seed(NoticeService &notices) {
    const auto year = QDate::currentDate().year();
    for (auto value :
         {sample("ui-pay", "重修缴费通知", year), sample("ui-arrangement", "重修考试安排", year),
          sample("ui-scholarship", "奖学金申请通知", year, "student-notices"),
          sample("ui-old", "重修缴费通知", year - 1)}) {
        notices.ingest({value});
        value.body = "界面测试使用的缓存正文；这些示例通知不代表官网实时公告。";
        notices.saveDetail(value);
    }
}
SubscriptionEditorDialog *activeEditor() {
    auto *dialog = qobject_cast<SubscriptionEditorDialog *>(QApplication::activeModalWidget());
    if (dialog)
        QTimer::singleShot(5000, dialog, &QDialog::reject);
    return dialog;
}
} // namespace

class SubscriptionDesktopTests final : public QObject {
    Q_OBJECT
  private slots:
    void currentFiltersSaveEditPauseRefreshAndRestart() {
        const auto school = SchoolPackage::load(CONFIG_FILE);
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        QTemporaryDir folder;
        const auto filename = folder.filePath("subscriptions.sqlite");
        std::string id, createdAt;
        {
            Database db(filename);
            SqliteRepository repository(db);
            SqliteSourceRepository sourceRepository(db);
            SqliteSubscriptionRepository subscriptionRepository(db);
            NoticeService notices(repository, school.id.toStdString());
            seed(notices);
            SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
            SubscriptionService subscriptions(school.id.toStdString(), subscriptionRepository,
                                              notices);
            RefreshCoordinator coordinator(school, notices, sources, nullptr, {0, 100});
            SqliteTaskRepository taskRepository(db);
            TaskService tasks(school.id.toStdString(), school.timeZone.toStdString(),
                              taskRepository, notices);
            MainWindow window(school, notices, sources, coordinator, registry, subscriptions,
                              tasks);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto *tabs = control<QTabWidget>(window, "mainTabs");
            QCOMPARE(tabs->count(), 7);
            control<QComboBox>(window, "themeSelector")->setCurrentText("重修 / 缴费");
            control<QLineEdit>(window, "keywordSearch")->setText("重修 缴费");
            auto *source = control<QComboBox>(window, "noticeSourceSelector");
            source->setCurrentIndex(source->findData("academic-affairs-notices"));
            QCOMPARE(control<QTableView>(window, "noticeTable")->model()->rowCount(), 1);
            QCOMPARE(
                control<QTableView>(window, "noticeTable")->model()->index(0, 2).data().toString(),
                QString("重修 / 缴费"));

            // Cancelling the modal editor preserves both the filters and the current tab.
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = activeEditor();
                QVERIFY(dialog);
                dialog->reject();
            });
            control<QPushButton>(window, "saveSubscriptionButton")->click();
            QCOMPARE(tabs->currentIndex(), 0);
            QVERIFY(subscriptions.list().empty());

            QTimer::singleShot(0, &window, [&] {
                auto *dialog = activeEditor();
                QVERIFY(dialog);
                QCOMPARE(
                    control<QComboBox>(*dialog, "subscriptionYearPolicy")->currentData().toString(),
                    QString("current_year"));
                QCOMPARE(control<QLineEdit>(*dialog, "subscriptionKeywordAll")->text(),
                         QString("重修 缴费"));
                QVERIFY(
                    control<QCheckBox>(*dialog, "subscriptionTheme_retake_payment")->isChecked());
                QVERIFY(
                    control<QLabel>(*dialog, "subscriptionPreview")->text().contains("匹配 1 条"));
                control<QLineEdit>(*dialog, "subscriptionName")->setText("今年重修缴费");
                control<QPushButton>(*dialog, "saveSubscriptionDialogButton")->click();
                QCOMPARE(dialog->result(), int(QDialog::Accepted));
            });
            control<QPushButton>(window, "saveSubscriptionButton")->click();
            QCOMPARE(tabs->tabText(tabs->currentIndex()), QString("我的订阅"));
            QCOMPARE(subscriptions.list().size(), size_t(1));
            const auto stored = subscriptions.list().front();
            id = stored.id;
            createdAt = stored.createdAt;
            QCOMPARE(stored.query.sourceIds,
                     std::vector<std::string>({"academic-affairs-notices"}));
            QCOMPARE(stored.query.keywordAll, std::vector<std::string>({"重修", "缴费"}));
            QCOMPARE(stored.query.yearPolicy, YearPolicy::CurrentYear);
            auto *matches = control<QTableView>(window, "subscriptionNoticeTable");
            QCOMPARE(matches->model()->rowCount(), 1);
            QCOMPARE(matches->model()->index(0, 2).data().toString(), QString("重修 / 缴费"));

            QTimer::singleShot(0, &window, [&] {
                auto *dialog = activeEditor();
                QVERIFY(dialog);
                control<QLineEdit>(*dialog, "subscriptionName")->setText("重修缴费关注");
                control<QLineEdit>(*dialog, "subscriptionKeywordExclude")->setText("公示 结果");
                control<QCheckBox>(*dialog, "subscriptionStage_payment")->setChecked(true);
                QVERIFY(
                    control<QLabel>(*dialog, "subscriptionPreview")->text().contains("匹配 1 条"));
                if (QDir(EVIDENCE_DIR).exists())
                    QVERIFY(dialog->grab().save(QString(EVIDENCE_DIR) + "/stage2-editor-demo.png"));
                control<QPushButton>(*dialog, "saveSubscriptionDialogButton")->click();
            });
            control<QPushButton>(window, "editSubscriptionButton")->click();
            QCOMPARE(subscriptions.find(id).name, std::string("重修缴费关注"));
            QCOMPARE(subscriptions.find(id).createdAt, createdAt);
            QCOMPARE(subscriptions.find(id).query.stageKeys, std::vector<std::string>({"payment"}));
            control<QPushButton>(window, "pauseSubscriptionButton")->click();
            QVERIFY(subscriptions.find(id).paused);
            QCOMPARE(matches->model()->rowCount(), 0);
            control<QPushButton>(window, "pauseSubscriptionButton")->click();
            QVERIFY(!subscriptions.find(id).paused);
            QCOMPARE(matches->model()->rowCount(), 1);

            // The coordinator emits this same signal after committed crawl changes.
            auto newNotice = sample("ui-new", "第二批重修缴费通知", QDate::currentDate().year());
            notices.ingest({newNotice});
            newNotice.body = "另一个已缓存正文";
            notices.saveDetail(newNotice);
            emit coordinator.changed();
            QCOMPARE(matches->model()->rowCount(), 2);
            QVERIFY(control<QLabel>(window, "subscriptionStatus")->text().contains("命中 2 条"));
            if (QDir(EVIDENCE_DIR).exists())
                QVERIFY(
                    window.grab().save(QString(EVIDENCE_DIR) + "/stage2-subscriptions-demo.png"));

            matches->setCurrentIndex(matches->model()->index(0, 0));
            control<QPushButton>(window, "openSubscriptionNoticeButton")->click();
            QCOMPARE(tabs->currentIndex(), 0);
            QVERIFY(control<QLabel>(window, "detailTitle")->text().contains("重修缴费"));
            QCOMPARE(control<QTableView>(window, "noticeTable")->model()->rowCount(), 5);
            tabs->setCurrentIndex(2);
            control<QPushButton>(window, "pauseSubscriptionButton")->click();
            QVERIFY(subscriptions.find(id).paused);
        }
        {
            Database db(filename);
            SqliteRepository repository(db);
            SqliteSourceRepository sourceRepository(db);
            SqliteSubscriptionRepository subscriptionRepository(db);
            NoticeService notices(repository, school.id.toStdString());
            SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
            SubscriptionService subscriptions(school.id.toStdString(), subscriptionRepository,
                                              notices);
            RefreshCoordinator coordinator(school, notices, sources, nullptr, {0, 100});
            SqliteTaskRepository taskRepository(db);
            TaskService tasks(school.id.toStdString(), school.timeZone.toStdString(),
                              taskRepository, notices);
            MainWindow window(school, notices, sources, coordinator, registry, subscriptions,
                              tasks);
            window.show();
            control<QTabWidget>(window, "mainTabs")->setCurrentIndex(2);
            QCOMPARE(subscriptions.find(id).createdAt, createdAt);
            QCOMPARE(control<QListWidget>(window, "subscriptionList")->count(), 1);
            QCOMPARE(control<QPushButton>(window, "pauseSubscriptionButton")->text(),
                     QString("恢复订阅"));
            auto *matches = control<QTableView>(window, "subscriptionNoticeTable");
            QCOMPARE(matches->model()->rowCount(), 0);
            control<QPushButton>(window, "pauseSubscriptionButton")->click();
            QCOMPARE(matches->model()->rowCount(), 2);
            for (const auto answer : {QMessageBox::No, QMessageBox::Yes}) {
                QTimer::singleShot(0, &window, [answer] {
                    auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                    QVERIFY(question);
                    question->button(answer)->click();
                });
                control<QPushButton>(window, "deleteSubscriptionButton")->click();
                QCOMPARE(subscriptions.list().size(),
                         answer == QMessageBox::No ? size_t(1) : size_t(0));
            }
            QCOMPARE(control<QListWidget>(window, "subscriptionList")->count(), 0);
            QCOMPARE(matches->model()->rowCount(), 0);
            QVERIFY(!control<QPushButton>(window, "editSubscriptionButton")->isEnabled());
            QVERIFY(!control<QPushButton>(window, "pauseSubscriptionButton")->isEnabled());
            QVERIFY(control<QLabel>(window, "subscriptionStatus")->text().contains("暂无订阅"));
            QCOMPARE(notices.list().size(), size_t(5));
        }
        if (QDir(EVIDENCE_DIR).exists()) {
            QFile output(QString(EVIDENCE_DIR) + "/stage2-desktop-proof.json");
            QVERIFY(output.open(QIODevice::WriteOnly));
            output.write(QJsonDocument(QJsonObject{{"passed", true},
                                                   {"save_current_filters", true},
                                                   {"cancel_keeps_notice_page", true},
                                                   {"edit_stable_id", true},
                                                   {"pause_restart_resume", true},
                                                   {"refresh_recomputes_matches", true},
                                                   {"open_cached_notice_detail", true},
                                                   {"delete_preserves_cache", true},
                                                   {"cached_notices", 5},
                                                   {"demo_data", true}})
                             .toJson());
        }
    }
    void editorValidationYearsAndOutcomePreview() {
        const auto school = SchoolPackage::load(CONFIG_FILE);
        QTemporaryDir folder;
        Database db(folder.filePath("editor.sqlite"));
        SqliteRepository repository(db);
        SqliteSubscriptionRepository subscriptionRepository(db);
        NoticeService notices(repository, school.id.toStdString());
        seed(notices);
        notices.ingest({sample("ui-publicity", "奖学金申请结果公示", QDate::currentDate().year(),
                               "student-notices")});
        SubscriptionService subscriptions(school.id.toStdString(), subscriptionRepository, notices);
        Subscription initial;
        SubscriptionEditorDialog dialog(school, subscriptions, initial);
        dialog.show();
        auto *year = control<QComboBox>(dialog, "subscriptionYearPolicy");
        auto *fixed = control<QSpinBox>(dialog, "subscriptionFixedYear");
        QVERIFY(!fixed->isEnabled());
        year->setCurrentIndex(year->findData("fixed_year"));
        QVERIFY(fixed->isEnabled());
        fixed->setValue(QDate::currentDate().year() - 1);
        QVERIFY(control<QLabel>(dialog, "subscriptionPreview")->text().contains("匹配 1 条"));
        year->setCurrentIndex(year->findData("current_year"));
        control<QCheckBox>(dialog, "subscriptionTheme_scholarship")->setChecked(true);
        control<QCheckBox>(dialog, "subscriptionStage_application")->setChecked(true);
        QVERIFY(control<QLabel>(dialog, "subscriptionPreview")->text().contains("匹配 1 条"));
        control<QPushButton>(dialog, "saveSubscriptionDialogButton")->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QVERIFY(!control<QLabel>(dialog, "subscriptionError")->text().isEmpty());
        QVERIFY(subscriptions.list().empty());
        control<QLineEdit>(dialog, "subscriptionName")->setText("奖学金申请");
        control<QPushButton>(dialog, "saveSubscriptionDialogButton")->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(subscriptions.list().size(), size_t(1));
        QCOMPARE(subscriptions.matches(dialog.saved().id, QDate::currentDate().year()).front().id,
                 std::string("ui-scholarship"));
    }
};
QTEST_MAIN(SubscriptionDesktopTests)
#include "SubscriptionDesktopTests.moc"
