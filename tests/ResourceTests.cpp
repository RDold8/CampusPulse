#include "application/ResourceService.h"
#include "storage/Database.h"
#include "storage/SqliteMigrations.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteResourceRepository.h"
#include "storage/SqliteTaskRepository.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <algorithm>

using namespace campus;
namespace {
struct RawDatabase {
    QString connection = QUuid::createUuid().toString();
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
    explicit RawDatabase(const QString &path) {
        db.setDatabaseName(path);
        if (!db.open())
            throw std::runtime_error(db.lastError().text().toStdString());
    }
    ~RawDatabase() {
        db.close();
        db = {};
        QSqlDatabase::removeDatabase(connection);
    }
};
void execSql(QSqlDatabase &database, const QString &sql) {
    QSqlQuery query(database);
    if (!query.exec(sql))
        throw std::runtime_error(query.lastError().text().toStdString());
}
QJsonArray queryRows(QSqlDatabase &database, const QString &sql) {
    QSqlQuery query(database);
    if (!query.exec(sql))
        throw std::runtime_error(query.lastError().text().toStdString());
    QJsonArray rows;
    while (query.next()) {
        QVariantList values;
        for (int i = 0; i < query.record().count(); ++i)
            values << query.value(i);
        rows.append(QJsonArray::fromVariantList(values));
    }
    return rows;
}
QJsonObject legacySnapshot(const QString &path) {
    RawDatabase raw(path);
    QJsonObject result;
    result["schema"] = queryRows(raw.db, "SELECT type,name,tbl_name,sql FROM sqlite_master "
                                         "WHERE tbl_name<>'school_resource' ORDER BY type,name");
    result["version"] = queryRows(raw.db, "PRAGMA user_version");
    for (const auto &table : QStringList{"notices", "notice_revisions", "source_preferences",
                                         "source_state", "fetch_run", "source_occurrence",
                                         "subscription", "personal_task", "reminder_delivery"})
        result[table] = queryRows(raw.db, "SELECT * FROM " + table + " ORDER BY rowid");
    return result;
}
void createVersion5(const QString &path, bool conflict = false) {
    RawDatabase raw(path);
    // Frozen schema captured from an actual v5 database, independent of today's migration code.
    const auto schema = QString::fromUtf8(R"SQL(
CREATE TABLE fetch_run(id INTEGER PRIMARY KEY,school_id TEXT NOT NULL,source_id TEXT NOT NULL,started_at TEXT NOT NULL,finished_at TEXT NOT NULL DEFAULT '',status TEXT NOT NULL DEFAULT 'updating',successful_pages INTEGER NOT NULL DEFAULT 0 CHECK(successful_pages>=0),row_count INTEGER NOT NULL DEFAULT 0 CHECK(row_count>=0),latest_published_date TEXT NOT NULL DEFAULT '',error TEXT NOT NULL DEFAULT '',FOREIGN KEY(school_id,source_id) REFERENCES source_state(school_id,source_id));
CREATE TABLE notice_revisions(id INTEGER PRIMARY KEY,notice_id TEXT NOT NULL REFERENCES notices(id),title TEXT NOT NULL,published_date TEXT NOT NULL,body TEXT NOT NULL,attachments TEXT NOT NULL,created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));
CREATE TABLE notices(id TEXT PRIMARY KEY,school_id TEXT NOT NULL,source_id TEXT NOT NULL,source_name TEXT NOT NULL,title TEXT NOT NULL,url TEXT NOT NULL,published_date TEXT NOT NULL,category TEXT NOT NULL,body TEXT NOT NULL DEFAULT '',attachments TEXT NOT NULL DEFAULT '[]', tags TEXT NOT NULL DEFAULT '[]', stages TEXT NOT NULL DEFAULT '[]');
CREATE TABLE personal_task(id TEXT PRIMARY KEY,school_id TEXT NOT NULL,notice_id TEXT NOT NULL,notice_revision_id INTEGER,title TEXT NOT NULL,notes TEXT NOT NULL,action TEXT NOT NULL CHECK(action IN ('registration','payment','application','check_result','attendance','other')),status TEXT NOT NULL CHECK(status IN ('not_started','in_progress','completed','cancelled')),time_precision TEXT NOT NULL CHECK(time_precision IN ('unknown','date','datetime')),time_date TEXT NOT NULL,time_utc TEXT NOT NULL,time_zone TEXT NOT NULL,time_evidence TEXT NOT NULL,time_source TEXT NOT NULL CHECK(time_source IN ('none','personal','original_text')),time_confirmed_at TEXT NOT NULL,reminder_enabled INTEGER NOT NULL CHECK(reminder_enabled IN(0,1)),reminder_minutes INTEGER NOT NULL CHECK(reminder_minutes BETWEEN 0 AND 525600),reminder_days INTEGER NOT NULL CHECK(reminder_days BETWEEN 0 AND 365),reminder_time TEXT NOT NULL,calendar_uid TEXT NOT NULL UNIQUE,created_at TEXT NOT NULL,updated_at TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>0),FOREIGN KEY(school_id,notice_id) REFERENCES notices(school_id,id),FOREIGN KEY(notice_id,notice_revision_id) REFERENCES notice_revisions(notice_id,id),CHECK((time_precision='unknown' AND time_date='' AND time_utc='' AND time_source='none' AND time_confirmed_at='' AND reminder_enabled=0) OR (time_precision='date' AND time_date<>'' AND time_utc='' AND time_source<>'none' AND time_confirmed_at<>'') OR (time_precision='datetime' AND time_date='' AND time_utc<>'' AND time_source<>'none' AND time_confirmed_at<>'')));
CREATE TABLE reminder_delivery(task_id TEXT NOT NULL REFERENCES personal_task(id),trigger_utc TEXT NOT NULL,task_revision INTEGER NOT NULL CHECK(task_revision>0),status TEXT NOT NULL CHECK(status IN('attempting','submitted','expired','failed','interrupted')),recorded_at TEXT NOT NULL,error TEXT NOT NULL,PRIMARY KEY(task_id,trigger_utc));
CREATE TABLE source_occurrence(school_id TEXT NOT NULL,notice_id TEXT NOT NULL,source_id TEXT NOT NULL,source_name TEXT NOT NULL,PRIMARY KEY(school_id,notice_id,source_id),FOREIGN KEY(school_id,notice_id) REFERENCES notices(school_id,id));
CREATE TABLE source_preferences(school_id TEXT NOT NULL,source_id TEXT NOT NULL,paused INTEGER NOT NULL DEFAULT 0 CHECK(paused IN (0,1)),PRIMARY KEY(school_id,source_id));
CREATE TABLE source_state(school_id TEXT NOT NULL,source_id TEXT NOT NULL,status TEXT NOT NULL DEFAULT 'never_checked',last_attempt_at TEXT NOT NULL DEFAULT '',last_success_at TEXT NOT NULL DEFAULT '',latest_published_date TEXT NOT NULL DEFAULT '',error TEXT NOT NULL DEFAULT '',successful_pages INTEGER NOT NULL DEFAULT 0 CHECK(successful_pages>=0),row_count INTEGER NOT NULL DEFAULT 0 CHECK(row_count>=0),PRIMARY KEY(school_id,source_id));
CREATE TABLE subscription(id TEXT PRIMARY KEY,school_id TEXT NOT NULL,name TEXT NOT NULL,paused INTEGER NOT NULL DEFAULT 0 CHECK(paused IN (0,1)),source_ids TEXT NOT NULL,theme_keys TEXT NOT NULL,stage_keys TEXT NOT NULL,keyword_all TEXT NOT NULL,keyword_any TEXT NOT NULL,keyword_exclude TEXT NOT NULL,year_policy TEXT NOT NULL CHECK(year_policy IN ('current_year','all_years','fixed_year','unknown_date')),fixed_year INTEGER NOT NULL DEFAULT 0,created_at TEXT NOT NULL,updated_at TEXT NOT NULL,CHECK((year_policy='fixed_year' AND fixed_year BETWEEN 1 AND 9999) OR (year_policy<>'fixed_year' AND fixed_year=0)));
CREATE INDEX fetch_run_sources ON fetch_run(school_id,source_id,id DESC);
CREATE INDEX notice_dates ON notices(published_date DESC);
CREATE UNIQUE INDEX notice_revision_identity ON notice_revisions(notice_id,id);
CREATE UNIQUE INDEX notice_school_identity ON notices(school_id,id);
CREATE INDEX notice_source_dates ON notices(school_id,source_id,published_date DESC);
CREATE INDEX occurrence_sources ON source_occurrence(school_id,source_id,notice_id);
CREATE INDEX subscription_schools ON subscription(school_id,name,id);
CREATE INDEX task_schools ON personal_task(school_id,status,updated_at DESC);
PRAGMA user_version=5;
)SQL");
    for (const auto &sql : schema.split(';'))
        if (!sql.trimmed().isEmpty())
            execSql(raw.db, sql);
    execSql(raw.db, "PRAGMA foreign_keys=ON");
    const auto data = QString::fromUtf8(R"SQL(
INSERT INTO notices(id,school_id,source_id,source_name,title,url,published_date,category,body,tags,stages) VALUES('legacy-notice','school-a','academic','教务处','原有重修缴费通知','https://school-a.edu.cn/info/1/2.htm','2026-10-01','exam','原通知正文，升级不能改写','["retake","payment"]','["payment"]');
INSERT INTO notice_revisions(id,notice_id,title,published_date,body,attachments,created_at) VALUES(1,'legacy-notice','原有重修缴费通知','2026-10-01','原通知正文，升级不能改写','[]','2026-10-01T00:00:00Z');
INSERT INTO source_preferences(school_id,source_id,paused) VALUES('school-a','academic',1);
INSERT INTO source_state(school_id,source_id,status,last_attempt_at,last_success_at,latest_published_date,successful_pages,row_count) VALUES('school-a','academic','success','2026-10-03T01:00:00Z','2026-10-03T01:01:00Z','2026-10-01',2,7);
INSERT INTO fetch_run(id,school_id,source_id,started_at,finished_at,status,successful_pages,row_count) VALUES(1,'school-a','academic','2026-10-03T01:00:00Z','2026-10-03T01:01:00Z','success',2,7);
INSERT INTO source_occurrence(school_id,notice_id,source_id,source_name) VALUES('school-a','legacy-notice','academic','教务处');
INSERT INTO subscription(id,school_id,name,paused,source_ids,theme_keys,stage_keys,keyword_all,keyword_any,keyword_exclude,year_policy,fixed_year,created_at,updated_at) VALUES('legacy-subscription','school-a','保留订阅',0,'["academic"]','["payment"]','[]','[]','[]','[]','current_year',0,'2026-10-03T02:00:00Z','2026-10-03T02:00:00Z');
INSERT INTO personal_task(id,school_id,notice_id,notice_revision_id,title,notes,action,status,time_precision,time_date,time_utc,time_zone,time_evidence,time_source,time_confirmed_at,reminder_enabled,reminder_minutes,reminder_days,reminder_time,calendar_uid,created_at,updated_at,revision) VALUES('legacy-task','school-a','legacy-notice',1,'保留重修缴费办理事项','完成状态与确认日期不能被迁移改写','payment','completed','date','2026-10-09','','Asia/Shanghai','原有个人计划','personal','2026-10-03T02:00:00Z',1,1440,1,'09:00','legacy-task@campuspulse','2026-10-03T02:00:00Z','2026-10-09T02:00:00Z',2);
INSERT INTO reminder_delivery(task_id,trigger_utc,task_revision,status,recorded_at,error) VALUES('legacy-task','2026-10-08T01:00:00Z',1,'submitted','2026-10-08T01:00:00Z','');
)SQL");
    for (const auto &sql : data.split(';'))
        if (!sql.trimmed().isEmpty())
            execSql(raw.db, sql);
    if (conflict)
        execSql(raw.db, "CREATE TABLE school_resource(unexpected TEXT)");
}
void createVersion6(const QString &path, bool conflict = false) {
    createVersion5(path);
    RawDatabase raw(path);
    // Frozen v6 resource schema: the migration must preserve these rows and personal favorites.
    const auto schema = QString::fromUtf8(R"SQL(
CREATE TABLE school_resource(id TEXT NOT NULL,school_id TEXT NOT NULL,title TEXT NOT NULL,url TEXT NOT NULL,description TEXT NOT NULL,category TEXT NOT NULL CHECK(category IN('study_plan','course_material','library','competition','academic_support','student_services','career','campus_life','other')),provider TEXT NOT NULL,discovered_from TEXT NOT NULL,last_checked_at TEXT NOT NULL,status TEXT NOT NULL CHECK(status IN('discovered','verified','login_required','unreachable')),link_kind TEXT NOT NULL CHECK(link_kind IN('official','official_recommended')),error TEXT NOT NULL,audiences TEXT NOT NULL DEFAULT '[]',tags TEXT NOT NULL DEFAULT '[]',favorite INTEGER NOT NULL DEFAULT 0 CHECK(favorite IN(0,1)),PRIMARY KEY(school_id,id),CHECK(length(school_id)>0 AND length(id)>0));
CREATE INDEX resource_schools ON school_resource(school_id,category,title,id);
INSERT INTO school_resource VALUES('legacy-library','school-a','原图书馆资源','https://school-a.edu.cn/library','原资源介绍','library','图书馆','https://school-a.edu.cn/','2026-10-03T01:00:00Z','login_required','official','学校账号登录说明','["general"]','["文献"]',1);
INSERT INTO school_resource VALUES('legacy-library','school-b','另一校同标识资源','https://school-b.edu.cn/library','另一校资源介绍','library','另一校图书馆','https://school-b.edu.cn/','2026-10-03T02:00:00Z','verified','official','','["postgraduate"]','["期刊"]',0);
PRAGMA user_version=6;
)SQL");
    for (const auto &sql : schema.split(';'))
        if (!sql.trimmed().isEmpty())
            execSql(raw.db, sql);
    if (conflict)
        execSql(raw.db,
                "ALTER TABLE school_resource ADD COLUMN access_evidence TEXT NOT NULL DEFAULT ''");
}
QJsonArray version6ResourceRows(const QString &path) {
    RawDatabase raw(path);
    return queryRows(raw.db,
                     "SELECT id,school_id,title,url,description,category,provider,discovered_from,"
                     "last_checked_at,status,link_kind,error,audiences,tags,favorite "
                     "FROM school_resource ORDER BY school_id,id");
}
SchoolResource sample(const std::string &id = "one", const std::string &school = "school-a") {
    SchoolResource resource;
    resource.id = id;
    resource.schoolId = school;
    resource.title = "本科培养方案";
    resource.url = "https://school-a.edu.cn/resource/" + id;
    resource.description = "专业课程要求与学分说明";
    resource.category = "study_plan";
    resource.provider = "本科生院";
    resource.discoveredFrom = "https://school-a.edu.cn/education.htm";
    resource.lastCheckedAt = "2026-10-03T02:00:00.123Z";
    resource.status = "verified";
    resource.audiences = {"undergraduate"};
    resource.tags = {"培养", "课程"};
    return resource;
}
const SchoolResource &byId(const std::vector<SchoolResource> &resources, const std::string &id) {
    const auto found = std::find_if(resources.begin(), resources.end(),
                                    [&](const auto &resource) { return resource.id == id; });
    if (found == resources.end())
        throw std::runtime_error("资源测试记录缺失");
    return *found;
}
} // namespace
class ResourceTests final : public QObject {
    Q_OBJECT
  private slots:
    void version5MigrationPreservesAllLegacyTablesAndTaskDates() {
        QTemporaryDir folder;
        const auto path = folder.filePath("upgrade.sqlite");
        createVersion5(path);
        auto before = legacySnapshot(path);
        QCOMPARE(before.value("version").toArray().at(0).toArray().at(0).toInt(), 5);
        {
            Database database(path);
            SqliteResourceRepository resources(database);
            QVERIFY(resources.list("school-a").empty());
            SqliteRepository notices(database);
            QCOMPARE(notices.list().at(0).publishedDate, std::string("2026-10-01"));
            SqliteTaskRepository tasks(database);
            const auto task = tasks.list("school-a").at(0);
            QCOMPARE(task.status, TaskStatus::Completed);
            QCOMPARE(task.time.date, std::string("2026-10-09"));
            QCOMPARE(task.revision, 2);
            QCOMPARE(task.calendarUid, std::string("legacy-task@campuspulse"));
            QVERIFY(queryRows(database.connection(), "PRAGMA foreign_key_check").isEmpty());
            const auto columns =
                queryRows(database.connection(), "PRAGMA table_info(school_resource)");
            for (const auto value : columns) {
                const auto name = value.toArray().at(1).toString();
                QVERIFY(name != "published_date");
                QVERIFY(!name.contains("deadline"));
            }
        }
        auto after = legacySnapshot(path);
        QCOMPARE(after.value("version").toArray().at(0).toArray().at(0).toInt(),
                 SqliteMigrations::CurrentVersion);
        before.remove("version");
        after.remove("version");
        QCOMPARE(after, before);
        {
            Database reopened(path);
            SqliteMigrations::apply(reopened.connection());
        }
        auto restarted = legacySnapshot(path);
        restarted.remove("version");
        QCOMPARE(restarted, before);
    }
    void migrationConflictRollsBackAndLeavesVersion5() {
        QTemporaryDir folder;
        const auto path = folder.filePath("conflict.sqlite");
        createVersion5(path, true);
        const auto before = legacySnapshot(path);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Database database(path));
        QCOMPARE(legacySnapshot(path), before);
    }
    void version6MigrationPreservesResourcesFavoritesAndAllLegacyTables() {
        QTemporaryDir folder;
        const auto path = folder.filePath("upgrade-v6.sqlite");
        createVersion6(path);
        auto before = legacySnapshot(path);
        const auto resourcesBefore = version6ResourceRows(path);
        QCOMPARE(before.value("version").toArray().at(0).toArray().at(0).toInt(), 6);
        {
            Database database(path);
            SqliteResourceRepository repository(database);
            const auto a = repository.list("school-a").at(0);
            const auto b = repository.list("school-b").at(0);
            QVERIFY(a.favorite);
            QVERIFY(!b.favorite);
            QCOMPARE(a.status, std::string("login_required"));
            QCOMPARE(a.error, std::string("学校账号登录说明"));
            QVERIFY(a.accessNote.empty());
            QVERIFY(a.accessEvidence.empty());
            QVERIFY(b.accessNote.empty());
            QVERIFY(b.accessEvidence.empty());
            QCOMPARE(queryRows(database.connection(),
                               "SELECT type,\"notnull\",dflt_value FROM "
                               "pragma_table_info('school_resource') "
                               "WHERE name IN('access_note','access_evidence') ORDER BY name"),
                     QJsonArray({QJsonArray({"TEXT", 1, "''"}),
                                 QJsonArray({"TEXT", 1, "''"})}));
            QVERIFY(queryRows(database.connection(), "PRAGMA foreign_key_check").isEmpty());
        }
        auto after = legacySnapshot(path);
        QCOMPARE(after.value("version").toArray().at(0).toArray().at(0).toInt(), 7);
        before.remove("version");
        after.remove("version");
        QCOMPARE(after, before);
        QCOMPARE(version6ResourceRows(path), resourcesBefore);
        {
            Database reopened(path);
            SqliteMigrations::apply(reopened.connection());
        }
        auto restarted = legacySnapshot(path);
        restarted.remove("version");
        QCOMPARE(restarted, before);
        QCOMPARE(version6ResourceRows(path), resourcesBefore);
    }
    void version6MigrationFailureRollsBackBothColumnsAndVersion() {
        QTemporaryDir folder;
        const auto path = folder.filePath("conflict-v6.sqlite");
        createVersion6(path, true);
        const auto before = legacySnapshot(path);
        QJsonArray schemaBefore, resourcesBefore;
        {
            RawDatabase raw(path);
            schemaBefore = queryRows(raw.db, "PRAGMA table_info(school_resource)");
            resourcesBefore = queryRows(raw.db, "SELECT * FROM school_resource ORDER BY school_id,id");
            QVERIFY_THROWS_EXCEPTION(std::runtime_error, SqliteMigrations::apply(raw.db));
            QCOMPARE(queryRows(raw.db, "PRAGMA table_info(school_resource)"), schemaBefore);
            QCOMPARE(queryRows(raw.db, "SELECT * FROM school_resource ORDER BY school_id,id"),
                     resourcesBefore);
        }
        QCOMPARE(legacySnapshot(path), before);
    }
    void schoolIsolationAndWholeBatchRollback() {
        QTemporaryDir folder;
        Database database(folder.filePath("scopes.sqlite"));
        SqliteResourceRepository repository(database);
        ResourceService a(repository, "school-a"), b(repository, "school-b");
        a.ingest({sample("shared"), sample("only-a")});
        auto foreign = sample("shared", "school-b");
        foreign.title = "另一校资源";
        b.ingest({foreign});
        QCOMPARE(a.list().size(), size_t(2));
        QCOMPARE(b.list().size(), size_t(1));
        QCOMPARE(b.list().at(0).title, std::string("另一校资源"));
        a.setFavorite("shared", true);
        QVERIFY(byId(a.list(), "shared").favorite);
        QVERIFY(!byId(b.list(), "shared").favorite);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b.setFavorite("only-a", true));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, a.ingest({sample("new"), foreign}));
        QCOMPARE(a.list().size(), size_t(2));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, repository.upsert("school-a", {foreign}));
        auto updated = sample("shared");
        updated.title = "此更新必须回滚";
        auto invalid = sample("bad");
        invalid.status = "invalid-state";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 repository.upsert("school-a", {updated, invalid}));
        QCOMPARE(byId(a.list(), "shared").title, std::string("本科培养方案"));
        QVERIFY(byId(a.list(), "shared").favorite);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, repository.list(""));
    }
    void collectionPreservesPersonalFavoritesAcrossRestart() {
        QTemporaryDir folder;
        const auto path = folder.filePath("favorite.sqlite");
        {
            Database database(path);
            SqliteResourceRepository repository(database);
            ResourceService service(repository, "school-a");
            auto resource = sample();
            resource.favorite = true;
            service.ingest({resource});
            QVERIFY(!service.list().at(0).favorite);
            service.setFavorite(resource.id, true);
            resource.favorite = false;
            resource.title = "培养方案更新";
            resource.status = "unreachable";
            resource.error = "HTTP 503；暂时无法检查，保留官方入口";
            service.ingest({resource});
            QVERIFY(service.list().at(0).favorite);
            QCOMPARE(service.list().at(0).title, resource.title);
            QCOMPARE(service.list().at(0).error, resource.error);
            resource.title = "直接仓储重采同样保留收藏";
            repository.upsert("school-a", {resource});
            QVERIFY(service.list().at(0).favorite);
        }
        {
            Database database(path);
            SqliteResourceRepository repository(database);
            ResourceService service(repository, "school-a");
            ResourceQuery query;
            query.onlyFavorites = true;
            QCOMPARE(service.list(query).size(), size_t(1));
            service.setFavorite("one", false);
            QVERIFY(service.list(query).empty());
            auto resource = sample();
            resource.favorite = true;
            service.ingest({resource});
            QVERIFY(!service.list().at(0).favorite);
        }
    }
    void reviewedAccessInstructionsSurviveDiscoveryRefreshAndRestart() {
        QTemporaryDir folder;
        const auto path = folder.filePath("access-notes.sqlite");
        const std::string note = "校外访问先登录学校 WebVPN，再从图书馆数据库导航进入。";
        const std::string evidence = "https://school-a.edu.cn/library/off-campus.html";
        {
            Database database(path);
            SqliteResourceRepository repository(database);
            ResourceService service(repository, "school-a");
            auto resource = sample("library");
            resource.category = "library";
            resource.accessNote = note;
            resource.accessEvidence = evidence;
            service.ingest({resource});
            service.setFavorite(resource.id, true);
            auto discovered = sample("library");
            discovered.category = "library";
            discovered.title = "图书馆入口检查更新";
            discovered.status = "login_required";
            service.ingest({discovered});
            QCOMPARE(service.list().at(0).title, discovered.title);
            QCOMPARE(service.list().at(0).status, discovered.status);
            QCOMPARE(service.list().at(0).accessNote, note);
            QCOMPARE(service.list().at(0).accessEvidence, evidence);
            QVERIFY(service.list().at(0).favorite);
            ResourceQuery query;
            query.keyword = "webvpn";
            QCOMPARE(service.list(query).size(), size_t(1));
            discovered.description = "直接仓储刷新也必须保留已审核说明";
            repository.upsert("school-a", {discovered});
            QCOMPARE(service.list().at(0).accessNote, note);
            QCOMPARE(service.list().at(0).accessEvidence, evidence);
            QCOMPARE(service.list().at(0).description, discovered.description);
        }
        const std::string updatedNote = "官方更新：校外通过 WebVPN 进入数据库后按读者账号登录。";
        const std::string updatedEvidence = "https://school-a.edu.cn/library/access-2026.html";
        {
            Database database(path);
            SqliteResourceRepository repository(database);
            ResourceService service(repository, "school-a");
            auto resource = service.list().at(0);
            QCOMPARE(resource.accessNote, note);
            QCOMPARE(resource.accessEvidence, evidence);
            QVERIFY(resource.favorite);
            resource.accessNote = updatedNote;
            resource.accessEvidence = updatedEvidence;
            service.ingest({resource});
            QCOMPARE(service.list().at(0).accessNote, updatedNote);
            QCOMPARE(service.list().at(0).accessEvidence, updatedEvidence);
            QVERIFY(service.list().at(0).favorite);
        }
        {
            Database database(path);
            SqliteResourceRepository repository(database);
            const auto resource = repository.list("school-a").at(0);
            QCOMPARE(resource.accessNote, updatedNote);
            QCOMPARE(resource.accessEvidence, updatedEvidence);
            QVERIFY(resource.favorite);
        }
    }
    void accessInstructionsRequirePairedHttpsEvidenceAndRespectLengthLimits() {
        QTemporaryDir folder;
        Database database(folder.filePath("access-validation.sqlite"));
        SqliteResourceRepository repository(database);
        ResourceService service(repository, "school-a");
        auto boundary = sample("boundary");
        boundary.accessNote = std::string(12000, 'n');
        boundary.accessEvidence = "https://school-a.edu.cn/";
        boundary.accessEvidence.append(8192 - boundary.accessEvidence.size(), 'a');
        service.ingest({boundary});
        QCOMPARE(service.list().at(0).accessNote, boundary.accessNote);
        QCOMPARE(service.list().at(0).accessEvidence, boundary.accessEvidence);
        auto invalid = boundary;
        invalid.id = "invalid";
        invalid.accessNote.push_back('n');
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.ingest({sample("new"), invalid}));
        invalid = boundary;
        invalid.accessEvidence.push_back('a');
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.ingest({invalid}));
        invalid = sample("invalid");
        invalid.accessNote = "账号登录说明";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.ingest({invalid}));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, repository.upsert("school-a", {invalid}));
        invalid.accessNote.clear();
        invalid.accessEvidence = "https://school-a.edu.cn/help";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.ingest({invalid}));
        invalid.accessNote = "账号登录说明";
        for (const auto &url : {"http://school-a.edu.cn/help", "https://", "https://:443/help",
                                "https://user@school-a.edu.cn/help", "https://school-a.edu.cn:bad/",
                                "https://school-a.edu.cn:65536/", "https://-bad.edu.cn/help",
                                "https://school-a.edu.cn/help%xy", "https://school-a.edu.cn/help\nmore",
                                "https://school-a.edu.cn/has space", "https://[1::2::3]/help"}) {
            invalid.accessEvidence = url;
            QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.ingest({invalid}));
        }
        invalid.accessEvidence = "https://school-a.edu.cn/help";
        invalid.accessEvidence.push_back('\0');
        invalid.accessEvidence += "hidden";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.ingest({invalid}));
        invalid.accessEvidence = "https://school-a.edu.cn/help";
        invalid.accessNote.push_back('\0');
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.ingest({invalid}));
        invalid.accessEvidence = " HTTPS://school-a.edu.cn:443/help%20page ";
        invalid.accessNote = "  账号登录说明  ";
        service.ingest({invalid});
        QCOMPARE(byId(service.list(), "invalid").accessNote, std::string("账号登录说明"));
        QCOMPARE(byId(service.list(), "invalid").accessEvidence,
                 std::string("HTTPS://school-a.edu.cn:443/help%20page"));
        QCOMPARE(service.list().size(), size_t(2));
        QCOMPARE(byId(service.list(), "boundary").accessNote, boundary.accessNote);
        auto ipv6Resource = sample("ipv6");
        ipv6Resource.accessNote = "IPv6 官方服务访问说明";
        ipv6Resource.accessEvidence = "https://[2001:db8::1]:443/help";
        service.ingest({ipv6Resource});
        QCOMPARE(byId(service.list(), "ipv6").accessEvidence, ipv6Resource.accessEvidence);
    }
    void categoryKeywordAndExplicitAudienceFilters() {
        QTemporaryDir folder;
        Database database(folder.filePath("filters.sqlite"));
        SqliteResourceRepository repository(database);
        ResourceService service(repository, "school-a");
        auto undergraduate = sample("undergraduate");
        auto postgraduate = sample("postgraduate");
        postgraduate.title = "研究生培养计划";
        postgraduate.audiences = {"postgraduate"};
        auto general = sample("library");
        general.title = "图书馆入口";
        general.description = "查看论文数据库";
        general.category = "library";
        general.provider = "图书馆";
        general.audiences = {"general"};
        general.tags = {"MOOC", "数据库"};
        auto unknown = sample("competition");
        unknown.title = "竞赛资源入口";
        unknown.description = "适用阶段尚待核实";
        unknown.category = "competition";
        unknown.provider = "竞赛中心";
        unknown.audiences.clear();
        service.ingest({undergraduate, postgraduate, general, unknown});
        ResourceQuery query;
        QCOMPARE(service.list().size(), size_t(4));
        query.stage = "undergraduate";
        QCOMPARE(service.list(query).size(), size_t(2));
        QVERIFY(byId(service.list(query), "library").audiences ==
                std::vector<std::string>{"general"});
        query.stage = "postgraduate";
        QCOMPARE(service.list(query).size(), size_t(2));
        query.stage = "general";
        QCOMPARE(service.list(query).size(), size_t(1));
        QCOMPARE(service.list(query).at(0).id, std::string("library"));
        query.stage = "unknown";
        QCOMPARE(service.list(query).size(), size_t(1));
        QCOMPARE(service.list(query).at(0).id, std::string("competition"));
        query.category = "library";
        QVERIFY(service.list(query).empty());
        query.stage.clear();
        QCOMPARE(service.list(query).size(), size_t(1));
        for (const auto &keyword : {" 图书馆 ", "论文", "mOoC", "数据库"}) {
            query.keyword = keyword;
            QCOMPARE(service.list(query).size(), size_t(1));
        }
        query.keyword = "竞赛中心";
        query.category.clear();
        QCOMPARE(service.list(query).at(0).id, std::string("competition"));
        query.keyword = "本科培养方案";
        QCOMPARE(service.list(query).at(0).id, std::string("undergraduate"));
        query.stage = "invented";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.list(query));
        query.stage.clear();
        query.category = "invented";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.list(query));
    }
    void failureAndRecommendedProvenanceRemainVisible() {
        QTemporaryDir folder;
        Database database(folder.filePath("status.sqlite"));
        SqliteResourceRepository repository(database);
        ResourceService service(repository, "school-a");
        auto failed = sample("failed");
        failed.status = "unreachable";
        failed.error = "检查超时，不等于学校没有此资源";
        auto login = sample("login");
        login.status = "login_required";
        login.error = "需要学校账号登录，未自动采集";
        auto recommended = sample("recommended");
        recommended.url = "https://learning.example.net/course";
        recommended.linkKind = "official_recommended";
        recommended.status = "discovered";
        recommended.lastCheckedAt.clear();
        recommended.audiences.clear();
        service.ingest({failed, login, recommended});
        QCOMPARE(service.list().size(), size_t(3));
        QCOMPARE(byId(service.list(), "failed").error, failed.error);
        QCOMPARE(byId(service.list(), "login").status, std::string("login_required"));
        QCOMPARE(byId(service.list(), "recommended").discoveredFrom, recommended.discoveredFrom);
        QVERIFY(byId(service.list(), "recommended").lastCheckedAt.empty());
        QVERIFY(byId(service.list(), "recommended").audiences.empty());
        recommended.discoveredFrom.clear();
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.ingest({recommended}));
        auto invalid = sample("invalid");
        invalid.audiences = {"unknown"};
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.ingest({invalid}));
        invalid = sample("invalid");
        invalid.url = "file:///C:/Windows";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.ingest({invalid}));
        invalid = sample("invalid");
        invalid.lastCheckedAt = "2026-02-30T02:00:00Z";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.ingest({invalid}));
        QCOMPARE(service.list().size(), size_t(3));
    }
};
QTEST_GUILESS_MAIN(ResourceTests)
#include "ResourceTests.moc"
