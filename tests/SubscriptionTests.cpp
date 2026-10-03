#include "application/NoticeClassifier.h"
#include "application/NoticeMatcher.h"
#include "application/SubscriptionService.h"
#include "desktop/NoticeFilter.h"
#include "desktop/NoticeListModel.h"
#include "storage/Database.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteMigrations.h"
#include "storage/SqliteSubscriptionRepository.h"
#include "storage/SqliteSourceRepository.h"
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>

using namespace campus;
namespace {
Notice notice(const std::string &id, const std::string &title, const std::string &date,
              const std::string &source = "academic") {
    Notice result;
    result.id = id;
    result.schoolId = "cn-test";
    result.sourceId = source;
    result.sourceName = source == "academic" ? "教务处" : "学工";
    result.title = title;
    result.url = "https://school.edu.cn/" + id;
    result.publishedDate = date;
    return result;
}
struct Snapshot {
    int version;
    QList<QVariantList> notices, revisions, preferences, states, runs;
};
Snapshot snapshot(const QString &filename) {
    const auto name = QUuid::createUuid().toString();
    Snapshot result;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(filename);
        if (!db.open())
            throw std::runtime_error(db.lastError().text().toStdString());
        {
            QSqlQuery query(db);
            query.exec("PRAGMA user_version");
            query.next();
            result.version = query.value(0).toInt();
            const auto read = [&](const QString &sql, int columns, QList<QVariantList> &rows) {
                if (!query.exec(sql))
                    throw std::runtime_error(query.lastError().text().toStdString());
                while (query.next()) {
                    QVariantList values;
                    for (int i = 0; i < columns; ++i)
                        values << query.value(i);
                    rows << values;
                }
            };
            read("SELECT "
                 "id,school_id,source_id,source_name,title,url,published_date,category,body,"
                 "attachments "
                 "FROM notices ORDER BY id",
                 10, result.notices);
            read("SELECT * FROM notice_revisions ORDER BY id", 7, result.revisions);
            read("SELECT * FROM source_preferences ORDER BY school_id,source_id", 3,
                 result.preferences);
            read("SELECT * FROM source_state ORDER BY school_id,source_id", 9, result.states);
            read("SELECT * FROM fetch_run ORDER BY id", 10, result.runs);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return result;
}
void frozenVersionTwo(const QString &filename, bool conflict = false) {
    QFile fixture(V2_FIXTURE_FILE);
    if (!fixture.open(QIODevice::ReadOnly))
        throw std::runtime_error("无法读取冻结v2数据库夹具");
    const auto name = QUuid::createUuid().toString();
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(filename);
        if (!db.open())
            throw std::runtime_error(db.lastError().text().toStdString());
        {
            QSqlQuery query(db);
            for (const auto &statement : QString::fromUtf8(fixture.readAll()).split(';'))
                if (!statement.trimmed().isEmpty() && !query.exec(statement))
                    throw std::runtime_error(query.lastError().text().toStdString());
            if (conflict && !query.exec("CREATE TABLE subscription(unexpected TEXT)"))
                throw std::runtime_error(query.lastError().text().toStdString());
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
}
} // namespace
class SubscriptionTests final : public QObject {
    Q_OBJECT
  private slots:
    void dimensionAndOrAndExclusionSemantics() {
        auto sample = notice("one", "CET 补考报名与重修缴费通知", "2026-09-16");
        sample.sourceIds = {"academic", "exams"};
        NoticeQuery query;
        query.schoolId = "cn-test";
        query.sourceIds = {"student", "exams"};
        query.themeKeys = {"scholarship", "retake_payment"};
        query.stageKeys = {"registration", "payment"};
        query.keywordAll = {"cet", "重修"};
        query.keywordAny = {"奖学金", "缴费"};
        QVERIFY(NoticeMatcher::matches(sample, query, 2026));
        query.keywordExclude = {"缴费"};
        QVERIFY(!NoticeMatcher::matches(sample, query, 2026));
        query.keywordExclude.clear();
        query.sourceIds = {"student"};
        QVERIFY(!NoticeMatcher::matches(sample, query, 2026));
        query.sourceIds.clear();
        query.keywordAny = {"奖学金", "竞赛"};
        QVERIFY(!NoticeMatcher::matches(sample, query, 2026));
        query.keywordAny.clear();
        query.keywordAll = {"重修", "不存在"};
        QVERIFY(!NoticeMatcher::matches(sample, query, 2026));
        query.keywordAll.clear();
        query.schoolId = "another-school";
        QVERIFY(!NoticeMatcher::matches(sample, query, 2026));
    }
    void currentYearRollsWhileFixedYearDoesNot() {
        const auto older = notice("old", "2027学年重修", "2026-09-16");
        const auto newer = notice("new", "重修", "2027-09-16");
        NoticeQuery query;
        QVERIFY(NoticeMatcher::matches(older, query, 2026));
        QVERIFY(!NoticeMatcher::matches(older, query, 2027));
        QVERIFY(NoticeMatcher::matches(newer, query, 2027));
        query.yearPolicy = YearPolicy::FixedYear;
        query.fixedYear = 2026;
        QVERIFY(NoticeMatcher::matches(older, query, 2027));
        QVERIFY(!NoticeMatcher::matches(newer, query, 2027));
        query.yearPolicy = YearPolicy::UnknownDate;
        QVERIFY(NoticeMatcher::matches(notice("unknown", "考试", "2026-02-29"), query, 2026));
        QVERIFY(!NoticeMatcher::matches(notice("leap", "考试", "2024-02-29"), query, 2026));
        QVERIFY(NoticeMatcher::matches(notice("empty", "考试", ""), query, 2026));
    }
    void classifierKeepsRetakeScopeAndSeparatesOutcomes() {
        const auto payment = NoticeClassifier::classify("关于2026—2027学年第一学期重修缴费的通知");
        QCOMPARE(payment.primaryCategory, std::string("retake_payment"));
        QVERIFY(std::find(payment.tags.begin(), payment.tags.end(), "exam") != payment.tags.end());
        const auto publicResult = NoticeClassifier::classify("奖学金申请评审结果公示");
        QVERIFY(std::find(publicResult.tags.begin(), publicResult.tags.end(), "scholarship") !=
                publicResult.tags.end());
        QCOMPARE(publicResult.stages, std::vector<std::string>({"result", "publicity"}));
        const auto multi = NoticeClassifier::classify("竞赛报名与重修缴费通知");
        QCOMPARE(multi.primaryCategory, std::string("competition"));
        QVERIFY(std::find(multi.tags.begin(), multi.tags.end(), "exam") != multi.tags.end());
        NoticeQuery query;
        query.yearPolicy = YearPolicy::AllYears;
        query.themeKeys = {"retake_payment"};
        QVERIFY(NoticeMatcher::matches(notice("retake", "重修安排", ""), query, 2026));
        QVERIFY(!NoticeMatcher::matches(notice("fee", "宿舍缴费", ""), query, 2026));
        query.themeKeys = {"scholarship"};
        query.stageKeys = {"application"};
        QVERIFY(!NoticeMatcher::matches(notice("publicity", "奖学金申请评审结果公示", ""), query,
                                        2026));
        QVERIFY(NoticeMatcher::matches(notice("application", "奖学金申请通知", ""), query, 2026));
        QCOMPARE(NoticeClassifier::classify("关于进一步做好有关工作的通知").stages,
                 std::vector<std::string>({"unknown"}));
    }
    void cachedTitleTopicsDisplayWithoutRewritingCategory() {
        auto cached = notice("cached", "关于2026—2027学年第一学期重修缴费的通知", "2026-09-16");
        cached.category = "exam";
        cached.tags = {"exam", "retake_payment"};
        NoticeListModel model;
        model.setNotices({cached});
        QCOMPARE(model.index(0, 2).data().toString(), QString("重修 / 缴费"));
        QCOMPARE(model.notice(0).category, std::string("exam"));
        NoticeQuery query;
        query.themeKeys = {"exam"};
        QVERIFY(NoticeMatcher::matches(cached, query, 2026));
        query.themeKeys = {"retake_payment"};
        QVERIFY(NoticeMatcher::matches(cached, query, 2026));
        auto retake = cached;
        retake.title = "重修课程表";
        auto makeup = cached;
        makeup.title = "补考安排通知";
        auto fee = cached;
        fee.title = "宿舍缴费通知";
        auto combined = cached;
        combined.title = "关于缴费、补考及重修重修的通知";
        auto other = cached;
        other.title = "奖学金申请通知";
        other.category = "scholarship";
        model.setNotices({retake, makeup, fee, combined, other});
        QCOMPARE(model.index(0, 2).data().toString(), QString("重修"));
        QCOMPARE(model.index(1, 2).data().toString(), QString("补考"));
        QCOMPARE(model.index(2, 2).data().toString(), QString("缴费"));
        QCOMPARE(model.index(3, 2).data().toString(), QString("重修 / 补考 / 缴费"));
        QCOMPARE(model.index(4, 2).data().toString(), QString("奖助学金"));
        for (int row = 0; row < 4; ++row)
            QCOMPARE(model.notice(row).category, std::string("exam"));
    }
    void crossColumnNoticeKeepsOneIdentityAndBothSubscriptions() {
        QTemporaryDir folder;
        Database db(folder.filePath("data.sqlite"));
        SqliteRepository repository(db);
        NoticeService notices(repository, "cn-test");
        SqliteSubscriptionRepository subscriptionRepository(db);
        SubscriptionService subscriptions("cn-test", subscriptionRepository, notices);
        auto sample = notice("one", "重修缴费", "2026-09-16");
        notices.ingest({sample});
        sample.body = "缓存正文";
        notices.saveDetail(sample);
        const int revisions = repository.revisionCount(sample.id);
        sample.sourceId = "exams";
        sample.sourceName = "考试中心";
        notices.ingest({sample});
        notices.ingest({sample});
        const auto saved = notices.list();
        QCOMPARE(saved.size(), size_t(1));
        QCOMPARE(saved.front().sourceId, std::string("academic"));
        QCOMPARE(saved.front().sourceIds, std::vector<std::string>({"academic", "exams"}));
        QCOMPARE(saved.front().body, std::string("缓存正文"));
        QCOMPARE(repository.revisionCount(sample.id), revisions);
        SqliteSourceRepository sources(db);
        QCOMPARE(sources.state("cn-test", "exams").latestPublishedDate, std::string("2026-09-16"));
        for (const auto &source : {"academic", "exams"}) {
            Subscription subscription;
            subscription.name = source;
            subscription.query.sourceIds = {source};
            const auto stored = subscriptions.save(subscription);
            QCOMPARE(subscriptions.matches(stored.id, 2026).size(), size_t(1));
        }
    }
    void savedRulesAndNoticeFilterHaveIdenticalResults() {
        QTemporaryDir folder;
        Database db(folder.filePath("data.sqlite"));
        SqliteRepository repository(db);
        NoticeService notices(repository, "cn-test");
        const auto thisYear = std::to_string(QDate::currentDate().year());
        notices.ingest({notice("pay", "重修缴费", thisYear + "-09-16"),
                        notice("result", "奖学金申请结果公示", thisYear + "-09-16", "student"),
                        notice("apply", "奖学金申请通知", thisYear + "-09-16", "student"),
                        notice("old", "重修缴费", "2025-09-16")});
        SqliteSubscriptionRepository storage(db);
        SubscriptionService subscriptions("cn-test", storage, notices);
        std::vector<NoticeQuery> rules(4);
        rules[0].themeKeys = {"retake_payment"};
        rules[0].keywordAll = {"缴费"};
        rules[1].sourceIds = {"student"};
        rules[1].keywordExclude = {"公示"};
        rules[2].themeKeys = {"scholarship", "exam"};
        rules[2].keywordAny = {"缴费", "申请"};
        rules[3].yearPolicy = YearPolicy::AllYears;
        rules[3].stageKeys = {"publicity"};
        for (auto &query : rules) {
            query.schoolId = "cn-test";
            Subscription subscription;
            subscription.name = "匹配一致";
            subscription.query = query;
            const auto stored = subscriptions.save(subscription);
            NoticeListModel model;
            model.setNotices(notices.list());
            NoticeFilter filter;
            filter.setSourceModel(&model);
            filter.setRule(stored.query);
            std::vector<std::string> filtered;
            for (int row = 0; row < filter.rowCount(); ++row)
                filtered.push_back(model.notice(filter.mapToSource(filter.index(row, 0)).row()).id);
            std::vector<std::string> matched;
            for (const auto &value : subscriptions.matches(stored.id, QDate::currentDate().year()))
                matched.push_back(value.id);
            QCOMPARE(filtered, matched);
        }
    }
    void editPauseDeleteAndRestartRemainSchoolScoped() {
        QTemporaryDir folder;
        const auto filename = folder.filePath("saved.sqlite");
        std::string id, created;
        {
            Database db(filename);
            SqliteRepository repository(db);
            NoticeService notices(repository, "cn-test");
            notices.ingest({notice("one", "重修缴费", "2026-09-16")});
            SqliteSubscriptionRepository storage(db);
            SubscriptionService service("cn-test", storage, notices);
            Subscription subscription;
            subscription.name = "  重修关注  ";
            subscription.query.keywordAll = {" 重修 ", "重修", ""};
            auto stored = service.save(subscription);
            id = stored.id;
            created = stored.createdAt;
            QVERIFY(!id.empty());
            QCOMPARE(stored.name, std::string("重修关注"));
            QCOMPARE(stored.query.keywordAll, std::vector<std::string>({"重修"}));
            stored.name = "重修缴费";
            stored.query.keywordExclude = {"公示"};
            const auto edited = service.save(stored);
            QCOMPARE(edited.id, id);
            QCOMPARE(edited.createdAt, created);
            service.setPaused(id, true);
            QVERIFY(service.matches(id, 2026).empty());
            NoticeService otherNotices(repository, "cn-other");
            SubscriptionService other("cn-other", storage, otherNotices);
            QVERIFY(other.list().empty());
            QVERIFY_THROWS_EXCEPTION(std::invalid_argument, other.find(id));
            QVERIFY_THROWS_EXCEPTION(std::invalid_argument, other.remove(id));
            QVERIFY_THROWS_EXCEPTION(std::invalid_argument, other.save(stored));
        }
        {
            Database db(filename);
            SqliteRepository repository(db);
            NoticeService notices(repository, "cn-test");
            SqliteSubscriptionRepository storage(db);
            SubscriptionService service("cn-test", storage, notices);
            QCOMPARE(service.list().size(), size_t(1));
            QVERIFY(service.find(id).paused);
            QCOMPARE(service.find(id).query.keywordExclude, std::vector<std::string>({"公示"}));
            service.setPaused(id, false);
            QCOMPARE(service.matches(id, 2026).size(), size_t(1));
            service.remove(id);
            QVERIFY(service.list().empty());
            QCOMPARE(notices.list().size(), size_t(1));
        }
    }
    void invalidSaveAndDatabaseFailureLeaveExistingRulesUntouched() {
        QTemporaryDir folder;
        Database db(folder.filePath("data.sqlite"));
        SqliteRepository repository(db);
        NoticeService notices(repository, "cn-test");
        SqliteSubscriptionRepository storage(db);
        SubscriptionService service("cn-test", storage, notices);
        Subscription sub;
        sub.name = "有效订阅";
        const auto saved = service.save(sub);
        sub.name.clear();
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.save(sub));
        sub.name = "无效";
        sub.query.themeKeys = {"伪造主题"};
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.save(sub));
        sub.query.themeKeys.clear();
        sub.query.yearPolicy = YearPolicy::FixedYear;
        sub.query.fixedYear = 0;
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.save(sub));
        QSqlQuery trigger(db.connection());
        QVERIFY(
            trigger.exec("CREATE TRIGGER reject_subscription_edit BEFORE UPDATE ON subscription "
                         "BEGIN SELECT RAISE(ABORT,'injected save failure'); END"));
        auto changed = saved;
        changed.name = "不应写入";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, service.save(changed));
        QCOMPARE(service.find(saved.id).name, saved.name);
        QCOMPARE(service.list().size(), size_t(1));
    }
    void frozenVersionTwoMigrationAndFailureRollback() {
        QTemporaryDir folder;
        const auto good = folder.filePath("good.sqlite");
        frozenVersionTwo(good);
        const auto before = snapshot(good);
        {
            Database db(good);
            SqliteRepository repository(db);
            const auto saved = repository.list().front();
            QCOMPARE(saved.sourceIds, std::vector<std::string>({"academic"}));
            QCOMPARE(saved.stages, std::vector<std::string>({"payment"}));
        }
        const auto after = snapshot(good);
        QCOMPARE(after.version, SqliteMigrations::CurrentVersion);
        QCOMPARE(after.notices, before.notices);
        QCOMPARE(after.revisions, before.revisions);
        QCOMPARE(after.preferences, before.preferences);
        QCOMPARE(after.states, before.states);
        QCOMPARE(after.runs, before.runs);

        const auto broken = folder.filePath("broken.sqlite");
        frozenVersionTwo(broken, true);
        const auto unchanged = snapshot(broken);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Database{broken});
        const auto rolledBack = snapshot(broken);
        QCOMPARE(rolledBack.version, 2);
        QCOMPARE(rolledBack.notices, unchanged.notices);
        QCOMPARE(rolledBack.revisions, unchanged.revisions);
        const auto name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(broken);
            QVERIFY(db.open());
            {
                QSqlQuery query(db);
                QVERIFY(query.exec("PRAGMA table_info(notices)"));
                int columns = 0;
                while (query.next())
                    ++columns;
                QCOMPARE(columns, 10);
                QVERIFY(query.exec(
                    "SELECT COUNT(*) FROM sqlite_master WHERE "
                    "name IN ('source_occurrence','notice_school_identity','occurrence_sources')"));
                QVERIFY(query.next());
                QCOMPARE(query.value(0).toInt(), 0);
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void actualVersionTwoMigrationPreservesCacheAndSourceState() {
        if (!QFile::exists(REAL_V2_DB))
            QSKIP("本机v2快照未随仓库分发；v1冻结迁移回归继续运行");
        const auto before = snapshot(REAL_V2_DB);
        QCOMPARE(before.version, 2);
        QTemporaryDir folder;
        const auto filename = folder.filePath("migrated.sqlite");
        QVERIFY(QFile::copy(REAL_V2_DB, filename));
        {
            Database db(filename);
            SqliteRepository notices(db);
            QCOMPARE(notices.list().size(), size_t(95));
            SqliteSubscriptionRepository subscriptions(db);
            QVERIFY(subscriptions.list("cn-neepu").empty());
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM source_occurrence"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 95);
            for (const auto &value : notices.list()) {
                QVERIFY(!value.tags.empty());
                QVERIFY(!value.stages.empty());
            }
        }
        const auto after = snapshot(filename);
        QCOMPARE(after.version, SqliteMigrations::CurrentVersion);
        QCOMPARE(after.notices, before.notices);
        QCOMPARE(after.revisions, before.revisions);
        QCOMPARE(after.preferences, before.preferences);
        QCOMPARE(after.states, before.states);
        QCOMPARE(after.runs, before.runs);
        {
            Database restart(filename);
        }
        QCOMPARE(snapshot(filename).revisions, before.revisions);
        QCOMPARE(snapshot(REAL_V2_DB).version, 2);
        QFile proof(QString(EVIDENCE_DIR) + "/stage3-v2-forward-migration-proof.json");
        if (proof.open(QIODevice::WriteOnly))
            proof.write(QJsonDocument(QJsonObject{{"passed", true},
                                                  {"old_version", 2},
                                                  {"new_version", SqliteMigrations::CurrentVersion},
                                                  {"notices", 95},
                                                  {"revisions", before.revisions.size()},
                                                  {"cache_and_source_state_equal", true}})
                            .toJson());
    }
};
QTEST_GUILESS_MAIN(SubscriptionTests)
#include "SubscriptionTests.moc"
