#include "application/TaskService.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteTaskRepository.h"
#include "storage/SqliteMigrations.h"
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <QTemporaryDir>
#include <QUuid>
using namespace campus;
namespace {
Notice sample(std::string id = "one", std::string school = "cn-test") {
    Notice n;
    n.id = id;
    n.schoolId = school;
    n.sourceId = "academic";
    n.sourceName = "教务处";
    n.title = "重修缴费通知";
    n.url = "https://school.edu.cn/" + id;
    n.publishedDate = "2026-09-16";
    return n;
}
struct Store {
    Database db;
    SqliteRepository noticesRepo;
    SqliteTaskRepository tasksRepo;
    NoticeService notices;
    std::string now = "2026-10-02T02:00:00Z";
    TaskService tasks;
    explicit Store(const QString &file)
        : db(file), noticesRepo(db), tasksRepo(db), notices(noticesRepo, "cn-test"),
          tasks("cn-test", "Asia/Shanghai", tasksRepo, notices, [this] { return now; }) {}
    void seed() {
        auto n = sample();
        notices.ingest({n});
        n.body = "旧原文，请在原通知核对办理时间。";
        notices.saveDetail(n);
    }
};
QJsonObject snapshot(const QString &path, const QStringList &statements = {}) {
    const auto key = QUuid::createUuid().toString();
    QJsonObject result;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", key);
        db.setDatabaseName(path);
        if (!db.open())
            throw std::runtime_error(db.lastError().text().toStdString());
        {
            QSqlQuery q(db);
            for (const auto &sql : statements)
                if (!q.exec(sql))
                    throw std::runtime_error(q.lastError().text().toStdString());
            if (!q.exec("PRAGMA user_version") || !q.next())
                throw std::runtime_error("Version read failed");
            result["version"] = q.value(0).toInt();
            for (const auto &query : QStringList{
                     "SELECT * FROM notices ORDER BY id",
                     "SELECT * FROM notice_revisions ORDER BY id",
                     "SELECT * FROM source_preferences ORDER BY school_id,source_id",
                     "SELECT * FROM source_state ORDER BY school_id,source_id",
                     "SELECT * FROM fetch_run ORDER BY id",
                     "SELECT * FROM source_occurrence ORDER BY school_id,notice_id,source_id",
                     "SELECT * FROM subscription ORDER BY id"}) {
                if (!q.exec(query))
                    throw std::runtime_error(q.lastError().text().toStdString());
                QJsonArray rows;
                while (q.next()) {
                    QVariantList row;
                    for (int i = 0; i < q.record().count(); ++i)
                        row << q.value(i);
                    rows.append(QJsonArray::fromVariantList(row));
                }
                result[query] = rows;
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(key);
    return result;
}
void freeze(const QString &path, bool conflict = false) {
    QStringList commands;
    for (const auto &name : QStringList{"v2.sql", "v3-upgrade.sql"}) {
        QFile file(QString(DATABASE_FIXTURES) + "/" + name);
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("Fixture missing");
        for (const auto &sql : QString::fromUtf8(file.readAll()).split(';'))
            if (!sql.trimmed().isEmpty())
                commands << sql;
    }
    if (conflict)
        commands << "CREATE TABLE personal_task(unexpected TEXT)";
    snapshot(path, commands);
}
void sameLegacy(QJsonObject before, QJsonObject after) {
    before.remove("version");
    after.remove("version");
    QCOMPARE(after, before);
}
} // namespace
class TaskTests final : public QObject {
    Q_OBJECT
  private slots:
    void draftUnknownDatesSeparateTasksAndRestart() {
        QTemporaryDir folder;
        const auto file = folder.filePath("saved.sqlite");
        std::string first, second;
        {
            Store s(file);
            s.seed();
            auto registration = s.tasks.draft("one");
            QCOMPARE(registration.time.precision, TimePrecision::Unknown);
            QVERIFY(registration.time.date.empty());
            QVERIFY(registration.time.utcDateTime.empty());
            registration.action = "registration";
            registration.reminder.enabled = true;
            auto saved = s.tasks.save(registration);
            first = saved.id;
            QVERIFY(!saved.reminder.enabled);
            QCOMPARE(saved.revision, 1);
            QCOMPARE(saved.calendarUid, saved.id + "@campuspulse");
            auto payment = s.tasks.draft("one");
            payment.action = "payment";
            second = s.tasks.save(payment).id;
            QVERIFY(second != first);
            s.tasks.setStatus(first, TaskStatus::Completed);
            QCOMPARE(s.tasks.find(second).status, TaskStatus::NotStarted);
        }
        {
            Store s(file);
            QCOMPARE(s.tasks.list().size(), size_t(2));
            QCOMPARE(s.tasks.find(first).status, TaskStatus::Completed);
            QCOMPARE(s.tasks.find(second).action, std::string("payment"));
        }
    }
    void confirmationPrecisionAndOriginalEvidence() {
        QTemporaryDir dir;
        Store s(dir.filePath("data.sqlite"));
        s.seed();
        auto t = s.tasks.draft("one");
        t.time.precision = TimePrecision::DateOnly;
        t.time.date = "2026-10-03";
        t.time.confirmation = TimeConfirmation::OriginalText;
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, s.tasks.save(t, true));
        t.time.evidence = "用户摘录：10月3日前提交。";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, s.tasks.save(t));
        const auto saved = s.tasks.save(t, true);
        QCOMPARE(saved.time.date, std::string("2026-10-03"));
        QVERIFY(saved.time.utcDateTime.empty());
        QCOMPARE(saved.time.confirmedAt, s.now);
        auto invalid = saved;
        invalid.time.date = "2026-02-29";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, s.tasks.save(invalid, true));
        invalid = saved;
        invalid.time.date = "2024-02-29";
        s.tasks.save(invalid, true);
        QVERIFY(TaskService::validUtc("2026-10-02T01:02:03Z"));
        QVERIFY(!TaskService::validUtc("2026-10-02T24:00:00Z"));
        QVERIFY(!TaskService::validUtc("2026-10-02T09:00:00+08:00"));
    }
    void editIdentityNoOpVersionsAndExactUtc() {
        QTemporaryDir dir;
        Store s(dir.filePath("data.sqlite"));
        s.seed();
        auto t = s.tasks.draft("one");
        t.time.precision = TimePrecision::DateTime;
        t.time.utcDateTime = "2026-10-03T01:00:00Z";
        t.time.confirmation = TimeConfirmation::Personal;
        const auto first = s.tasks.save(t, true);
        s.now = "2026-10-02T03:00:00Z";
        QCOMPARE(s.tasks.save(first), first);
        t = first;
        t.title = "我需要完成缴费";
        const auto edited = s.tasks.save(t);
        QCOMPARE(edited.id, first.id);
        QCOMPARE(edited.calendarUid, first.calendarUid);
        QCOMPARE(edited.createdAt, first.createdAt);
        QCOMPARE(edited.revision, 2);
        QCOMPARE(edited.time.confirmedAt, first.time.confirmedAt);
        t = edited;
        t.time.utcDateTime = "2026-10-04T01:00:00Z";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, s.tasks.save(t));
        const auto changed = s.tasks.save(t, true);
        QCOMPARE(changed.id, first.id);
        QCOMPARE(changed.revision, 3);
        QCOMPARE(changed.time.confirmedAt, s.now);
        QVERIFY(changed.time.date.empty());
    }
    void overdueIsDerivedAndTerminalStatesCanBeReopened() {
        QTemporaryDir dir;
        Store s(dir.filePath("data.sqlite"));
        s.seed();
        auto t = s.tasks.draft("one");
        t.time.precision = TimePrecision::DateOnly;
        t.time.date = "2026-10-02";
        t.time.confirmation = TimeConfirmation::Personal;
        auto saved = s.tasks.save(t, true);
        QVERIFY(!TaskService::overdue(saved, "2026-10-02", "2026-10-02T23:59:59Z"));
        QVERIFY(TaskService::overdue(saved, "2026-10-03", "2026-10-02T16:00:00Z"));
        QCOMPARE(saved.status, TaskStatus::NotStarted);
        saved = s.tasks.setStatus(saved.id, TaskStatus::InProgress);
        saved = s.tasks.setStatus(saved.id, TaskStatus::Completed);
        QVERIFY(!TaskService::overdue(saved, "2026-10-03", "2026-10-03T00:00:00Z"));
        saved = s.tasks.setStatus(saved.id, TaskStatus::NotStarted);
        saved = s.tasks.setStatus(saved.id, TaskStatus::Cancelled);
        QVERIFY(!TaskService::overdue(saved, "2026-10-03", "2026-10-03T00:00:00Z"));
        saved = s.tasks.setStatus(saved.id, TaskStatus::NotStarted);
        QVERIFY(TaskService::overdue(saved, "2026-10-03", "2026-10-03T00:00:00Z"));
        saved.time.precision = TimePrecision::DateTime;
        saved.time.utcDateTime = "2026-10-03T01:00:00Z";
        saved = s.tasks.save(saved, true);
        QVERIFY(!TaskService::overdue(saved, "2026-10-03", "2026-10-03T01:00:00Z"));
        QVERIFY(TaskService::overdue(saved, "2026-10-03", "2026-10-03T01:00:01Z"));
    }
    void officialChangeRequiresReviewAndNeverOverwritesPersonalState() {
        QTemporaryDir dir;
        Store s(dir.filePath("data.sqlite"));
        s.seed();
        auto t = s.tasks.draft("one");
        t.time.precision = TimePrecision::DateOnly;
        t.time.date = "2026-10-03";
        t.time.confirmation = TimeConfirmation::Personal;
        auto saved = s.tasks.save(t, true);
        saved = s.tasks.setStatus(saved.id, TaskStatus::Completed);
        const auto original = saved;
        auto n = s.notices.list().front();
        n.body = "原文更新了说明，需要用户复核。";
        s.notices.saveDetail(n);
        QVERIFY(s.tasks.views().front().needsReview());
        QCOMPARE(s.tasks.find(saved.id), original);
        QCOMPARE(s.tasks.save(saved), saved);
        QVERIFY(s.tasks.views().front().needsReview());
        const auto checked = s.tasks.acknowledgeNotice(saved.id);
        QVERIFY(!s.tasks.views().front().needsReview());
        QCOMPARE(checked.time, original.time);
        QCOMPARE(checked.status, TaskStatus::Completed);
        QCOMPARE(checked.calendarUid, original.calendarUid);
        QCOMPARE(s.tasks.acknowledgeNotice(checked.id), checked);
        n = s.notices.list().front();
        n.sourceId = "other-column";
        s.notices.ingest({n});
        QVERIFY(!s.tasks.views().front().needsReview());
        n.title = "重修缴费通知更新标题";
        s.notices.ingest({n});
        QVERIFY(s.tasks.views().front().needsReview());
    }
    void schoolIsolationStaleEditAndTransactionFailure() {
        QTemporaryDir dir;
        Store s(dir.filePath("data.sqlite"));
        s.seed();
        const auto saved = s.tasks.save(s.tasks.draft("one"));
        auto stale = saved;
        auto changed = saved;
        changed.title = "已更新";
        const auto latest = s.tasks.save(changed);
        stale.title = "不应覆盖";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, s.tasks.save(stale));
        NoticeService otherNotices(s.noticesRepo, "cn-other");
        TaskService other("cn-other", "Asia/Shanghai", s.tasksRepo, otherNotices);
        QVERIFY(other.list().empty());
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, other.find(saved.id));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, other.save(saved));
        QSqlQuery q(s.db.connection());
        QVERIFY(q.exec("CREATE TRIGGER reject_task BEFORE UPDATE ON personal_task BEGIN SELECT "
                       "RAISE(ABORT,'injected failure'); END"));
        changed = latest;
        changed.title = "保存失败";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, s.tasks.save(changed));
        QCOMPARE(s.tasks.find(saved.id), latest);
        QVERIFY(q.exec("DROP TRIGGER reject_task"));
        auto another = sample("another");
        s.notices.ingest({another});
        auto forged = latest;
        forged.id.clear();
        forged.noticeRevisionId = s.notices.list().front().revisionId;
        if (forged.noticeRevisionId == latest.noticeRevisionId)
            forged.noticeRevisionId = s.notices.list().back().revisionId;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, s.tasksRepo.save(forged, 0));
        QCOMPARE(s.tasks.list().size(), size_t(1));
    }
    void frozenV3MigrationAndRollback() {
        QTemporaryDir dir;
        const auto path = dir.filePath("old.sqlite");
        freeze(path);
        const auto before = snapshot(path);
        {
            Store s(path);
            QVERIFY(s.tasks.list().empty());
            QCOMPARE(s.notices.list().front().body, std::string("原有缓存正文"));
        }
        const auto after = snapshot(path);
        QCOMPARE(after["version"].toInt(), SqliteMigrations::CurrentVersion);
        sameLegacy(before, after);
        const auto broken = dir.filePath("broken.sqlite");
        freeze(broken, true);
        const auto unchanged = snapshot(broken);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Database{broken});
        QCOMPARE(snapshot(broken), unchanged);
    }
    void actualV3SnapshotMigrationPreservesAllLegacyTables() {
        if (!QFile::exists(REAL_V3_DB))
            QSKIP("Optional local v3 snapshot absent; frozen v3 fixture always runs");
        const auto before = snapshot(REAL_V3_DB);
        QCOMPARE(before["version"].toInt(), 3);
        QTemporaryDir dir;
        const auto path = dir.filePath("migrated.sqlite");
        QVERIFY(QFile::copy(REAL_V3_DB, path));
        {
            Database db(path);
            SqliteTaskRepository repo(db);
            QVERIFY(repo.list("cn-neepu").empty());
        }
        const auto after = snapshot(path);
        QCOMPARE(after["version"].toInt(), SqliteMigrations::CurrentVersion);
        sameLegacy(before, after);
        {
            Database restart(path);
        }
        QCOMPARE(snapshot(path), after);
        QFile proof(QString(EVIDENCE_DIR) + "/stage3-migration-proof.json");
        QVERIFY(proof.open(QIODevice::WriteOnly));
        proof.write(QJsonDocument(QJsonObject{{"passed", true},
                                              {"old_version", 3},
                                              {"new_version", SqliteMigrations::CurrentVersion},
                                              {"legacy_tables_equal", true}})
                        .toJson());
    }
};
QTEST_GUILESS_MAIN(TaskTests)
#include "TaskTests.moc"
