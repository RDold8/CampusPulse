#include "adapters/HtmlAdapter.h"
#include "storage/SqliteRepository.h"
#include "application/NoticeService.h"
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <algorithm>
#include "desktop/NoticeFilter.h"
#include "desktop/NoticeListModel.h"

using namespace campus;
class PrototypeTests : public QObject {
    Q_OBJECT
  private:
    SourceConfig source(const char *key) {
        const auto school = SchoolPackage::load(CONFIG_FILE);
        for (const auto &s : school.sources)
            if (s.id == key)
                return s;
        throw std::runtime_error("测试来源缺失");
    }
    QByteArray fixture(const char *name) {
        QFile f(QString(FIXTURE_DIR) + "/" + name);
        if (!f.open(QIODevice::ReadOnly))
            throw std::runtime_error("测试样本无法读取");
        return f.readAll();
    }
  private slots:
    void sourceConfiguration() {
        const auto school = SchoolPackage::load(CONFIG_FILE);
        QCOMPARE(school.sources.size(), size_t(6));
        QVERIFY(!isAllowedUrl(QUrl("https://evil.invalid/info/123"), school.sources.front()));
        QVERIFY(!isAllowedUrl(QUrl("https://user:pass@jwc.neepu.edu.cn/"), school.sources.front()));
    }
    void listAndDetail() {
        const auto school = SchoolPackage::load(CONFIG_FILE);
        HtmlAdapter parser;
        const auto notices =
            parser.parseList(fixture("notices.html"), source("academic-affairs-notices"));
        QCOMPARE(notices.size(), size_t(20));
        QCOMPARE(
            parser.parseList(fixture("retake.html"), source("academic-retake-arrangements")).size(),
            size_t(20));
        auto found = std::find_if(notices.begin(), notices.end(), [](const auto &n) {
            return n.url.find("12248.htm") != std::string::npos;
        });
        QVERIFY(found != notices.end());
        QCOMPARE(QString::fromStdString(found->title),
                 QString("关于2026--2027学年第一学期重修缴费的通知"));
        QCOMPARE(QString::fromStdString(found->publishedDate), QString("2026-09-16"));
        const auto detail =
            parser.parseDetail(fixture("payment.html"), source("academic-affairs-notices"), *found);
        QVERIFY(QString::fromStdString(detail.body).contains("最终的重修名单"));
        QCOMPARE(detail.attachments.size(), size_t(1));
        QVERIFY(QString::fromStdString(detail.attachments[0].name).contains("缴费指南"));
        QVERIFY(!QString::fromStdString(detail.body).contains("getClickTimes"));
    }
    void expandedSources() {
        HtmlAdapter parser;
        struct Sample {
            const char *key;
            const char *file;
            size_t count;
        };
        for (const auto sample : {Sample{"main-announcements", "main.html", 15},
                                  Sample{"student-affairs-notices", "student.html", 10},
                                  Sample{"youth-league-notices", "youth.html", 5}}) {
            const auto rows = parser.parseList(fixture(sample.file), source(sample.key));
            QCOMPARE(rows.size(), sample.count);
            QVERIFY(std::all_of(rows.begin(), rows.end(), [](const auto &n) {
                return QDate::fromString(QString::fromStdString(n.publishedDate), Qt::ISODate)
                    .isValid();
            }));
            QVERIFY(!parser
                         .parseDetail(fixture(std::string(sample.key) == "main-announcements"
                                                  ? "main-detail.html"
                                              : std::string(sample.key) == "student-affairs-notices"
                                                  ? "student-detail.html"
                                                  : "youth-detail.html"),
                                      source(sample.key), rows.front())
                         .body.empty());
        }
        const auto student = source("student-affairs-notices");
        const auto next = parser.nextPage(fixture("student.html"), student, student.entry);
        QVERIFY(next.query().contains("a6p=2"));
        auto rows = parser.parseList(fixture("student-page2.html"), student);
        QCOMPARE(rows.size(), size_t(10));
        QVERIFY_THROWS_EXCEPTION(
            std::runtime_error,
            parser.nextPage("<a class='Next' href='https://evil.invalid/x'>next</a>", student,
                            student.entry));
        QVERIFY(std::any_of(rows.begin(), rows.end(), [](const auto &n) {
            return n.title.find("奖学金") != std::string::npos;
        }));
        rows = parser.parseList(fixture("youth.html"), source("youth-league-notices"));
        QVERIFY(QString::fromStdString(rows[0].title).startsWith("关于开展"));
        QVERIFY(!QString::fromStdString(rows[0].title).contains("2026-06-04"));
        QVERIFY(
            !parser.parseList(fixture("youth-activities.html"), source("youth-league-activities"))
                 .empty());
    }
    void publicationYearsAndCombinedFilters() {
        NoticeListModel model;
        NoticeFilter filter;
        filter.setSourceModel(&model);
        const int current = QDate::currentDate().year();
        std::vector<Notice> rows(5);
        for (auto &n : rows) {
            n.title = "奖学金申请";
            n.category = "scholarship";
        }
        rows[0].publishedDate = QDate(current, 5, 1).toString(Qt::ISODate).toStdString();
        rows[1].publishedDate = QDate(current - 1, 5, 1).toString(Qt::ISODate).toStdString();
        rows[1].title = std::to_string(current) + "学年奖学金申请";
        rows[2].publishedDate = "invalid";
        rows[3].publishedDate = "";
        rows[4].publishedDate = rows[0].publishedDate;
        rows[4].sourceName = "教务处";
        rows[0].sourceName = "学工";
        rows[0].sourceId = "student";
        rows[4].sourceId = "academic";
        rows[4].title = "考试安排";
        rows[4].category = "exam";
        model.setNotices(rows);
        QCOMPARE(filter.rowCount(), 2);
        filter.setQuery("奖学金");
        QCOMPARE(filter.rowCount(), 1);
        filter.setYear(current - 1);
        QCOMPARE(filter.rowCount(), 1);
        filter.setYear(-1);
        QCOMPARE(filter.rowCount(), 2);
        filter.setYear(0);
        QCOMPARE(filter.rowCount(), 4);
        filter.setCategory("考试");
        QCOMPARE(filter.rowCount(), 0);
        filter.setQuery({});
        QCOMPARE(filter.rowCount(), 1);
        filter.setSource("student");
        QCOMPARE(filter.rowCount(), 0);
        filter.setCategory({});
        QCOMPARE(filter.rowCount(), 1);
        filter.setSource({});
        filter.setYear(current + 1);
        QCOMPARE(filter.rowCount(), 0);
    }
    void rejectBrokenMarkup() {
        const auto source = this->source("academic-affairs-notices");
        HtmlAdapter parser;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 parser.parseList("<html>结构已变化</html>", source));
        auto invalid = source;
        invalid.itemSelector = "[";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 parser.parseList(fixture("notices.html"), invalid));
    }
    void idempotenceRevisionAndRestart() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto path = folder.path() + "/data.sqlite";
        const auto source = this->source("academic-affairs-notices");
        HtmlAdapter parser;
        auto rows = parser.parseList(fixture("notices.html"), source);
        {
            SqliteRepository db(path);
            NoticeService service(db);
            service.ingest(rows);
            service.ingest(rows);
            QCOMPARE(service.list().size(), size_t(20));
            QCOMPARE(db.revisionCount(rows[0].id), 1);
            QSqlQuery legacy(QSqlDatabase::database(QSqlDatabase::connectionNames().back()));
            legacy.prepare("UPDATE notices SET category='legacy' WHERE id=?");
            legacy.addBindValue(QString::fromStdString(rows[0].id));
            QVERIFY(legacy.exec());
            service.ingest(rows);
            const auto repaired = service.list();
            const auto first = std::find_if(repaired.begin(), repaired.end(),
                                            [&](const auto &n) { return n.id == rows[0].id; });
            QVERIFY(first != repaired.end());
            QCOMPARE(first->category, NoticeService::categoryHint(rows[0].title));
            QCOMPARE(db.revisionCount(rows[0].id), 1);
            rows[0].title += "（更正）";
            service.ingest(rows);
            QCOMPARE(db.revisionCount(rows[0].id), 2);
            auto found = std::find_if(rows.begin(), rows.end(), [](const auto &n) {
                return n.url.find("12248.htm") != std::string::npos;
            });
            const auto detail = parser.parseDetail(fixture("payment.html"), source, *found);
            service.saveDetail(detail);
            service.saveDetail(detail);
            QCOMPARE(db.revisionCount(detail.id), 2);
            auto broken = rows;
            broken[0].title += " rollback";
            broken[1].id.clear();
            // Force a later failure inside a batch and verify the earlier write rolls back.
            QSqlDatabase connection =
                QSqlDatabase::database(QSqlDatabase::connectionNames().back());
            QSqlQuery query(connection);
            QVERIFY(query.exec("CREATE TRIGGER reject_empty_id BEFORE INSERT ON notices WHEN "
                               "NEW.id='' BEGIN SELECT RAISE(ABORT,'empty identity'); END"));
            QVERIFY_THROWS_EXCEPTION(std::runtime_error, service.ingest(broken));
            QCOMPARE(db.revisionCount(rows[0].id), 2);
        }
        {
            SqliteRepository db(path);
            QCOMPARE(db.list().size(), size_t(20));
            auto all = db.list();
            QVERIFY(
                std::any_of(all.begin(), all.end(), [](const auto &n) { return !n.body.empty(); }));
        }
    }
};
QTEST_GUILESS_MAIN(PrototypeTests)
#include "PrototypeTests.moc"
