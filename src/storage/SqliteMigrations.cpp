#include "storage/SqliteMigrations.h"
#include "storage/SqliteJson.h"
#include "application/NoticeClassifier.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <stdexcept>

namespace campus {
namespace {

void execute(QSqlDatabase &database, const QString &sql) {
    QSqlQuery query(database);
    if (!query.exec(sql))
        throw std::runtime_error(query.lastError().text().toStdString());
}

void migrateToVersion1(QSqlDatabase &database) {
    execute(database, "CREATE TABLE notices(id TEXT PRIMARY KEY,school_id TEXT NOT NULL,source_id "
                      "TEXT NOT NULL,source_name TEXT NOT NULL,title TEXT NOT NULL,url TEXT NOT "
                      "NULL,published_date TEXT NOT NULL,category TEXT NOT NULL,body TEXT NOT NULL "
                      "DEFAULT '',attachments TEXT NOT NULL DEFAULT '[]')");
    execute(database, "CREATE TABLE notice_revisions(id INTEGER PRIMARY KEY,notice_id TEXT NOT "
                      "NULL REFERENCES notices(id),title TEXT NOT NULL,published_date TEXT NOT "
                      "NULL,body TEXT NOT NULL,attachments TEXT NOT NULL,created_at TEXT NOT NULL "
                      "DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')))");
    execute(database, "CREATE INDEX notice_dates ON notices(published_date DESC)");
}

void migrateToVersion2(QSqlDatabase &database) {
    execute(database,
            "CREATE TABLE source_preferences(school_id TEXT NOT NULL,source_id TEXT NOT NULL,"
            "paused INTEGER NOT NULL DEFAULT 0 CHECK(paused IN (0,1)),"
            "PRIMARY KEY(school_id,source_id))");
    execute(
        database,
        "CREATE TABLE source_state(school_id TEXT NOT NULL,source_id TEXT NOT NULL,"
        "status TEXT NOT NULL DEFAULT 'never_checked',last_attempt_at TEXT NOT NULL DEFAULT '',"
        "last_success_at TEXT NOT NULL DEFAULT '',latest_published_date TEXT NOT NULL DEFAULT '',"
        "error TEXT NOT NULL DEFAULT '',successful_pages INTEGER NOT NULL DEFAULT 0 "
        "CHECK(successful_pages>=0),row_count INTEGER NOT NULL DEFAULT 0 CHECK(row_count>=0),"
        "PRIMARY KEY(school_id,source_id))");
    execute(database,
            "CREATE TABLE fetch_run(id INTEGER PRIMARY KEY,school_id TEXT NOT NULL,"
            "source_id TEXT NOT NULL,started_at TEXT NOT NULL,finished_at TEXT NOT NULL DEFAULT '',"
            "status TEXT NOT NULL DEFAULT 'updating',successful_pages INTEGER NOT NULL DEFAULT 0 "
            "CHECK(successful_pages>=0),row_count INTEGER NOT NULL DEFAULT 0 CHECK(row_count>=0),"
            "latest_published_date TEXT NOT NULL DEFAULT '',error TEXT NOT NULL DEFAULT '',"
            "FOREIGN KEY(school_id,source_id) REFERENCES source_state(school_id,source_id))");
    execute(database, "CREATE INDEX fetch_run_sources ON fetch_run(school_id,source_id,id DESC)");
    execute(database,
            "CREATE INDEX notice_source_dates ON notices(school_id,source_id,published_date DESC)");
    // v1 has no crawl timestamps. Leave attempt/success unknown rather than fabricate history.
}

void migrateToVersion3(QSqlDatabase &database) {
    execute(database, "ALTER TABLE notices ADD COLUMN tags TEXT NOT NULL DEFAULT '[]'");
    execute(database, "ALTER TABLE notices ADD COLUMN stages TEXT NOT NULL DEFAULT '[]'");
    execute(database, "CREATE UNIQUE INDEX notice_school_identity ON notices(school_id,id)");
    execute(database,
            "CREATE TABLE source_occurrence(school_id TEXT NOT NULL,notice_id TEXT NOT NULL,"
            "source_id TEXT NOT NULL,source_name TEXT NOT NULL,"
            "PRIMARY KEY(school_id,notice_id,source_id),"
            "FOREIGN KEY(school_id,notice_id) REFERENCES notices(school_id,id))");
    execute(database,
            "CREATE INDEX occurrence_sources ON source_occurrence(school_id,source_id,notice_id)");
    execute(database, "INSERT INTO source_occurrence(school_id,notice_id,source_id,source_name) "
                      "SELECT school_id,id,source_id,source_name FROM notices WHERE source_id<>''");
    execute(
        database,
        "CREATE TABLE subscription(id TEXT PRIMARY KEY,school_id TEXT NOT NULL,"
        "name TEXT NOT NULL,paused INTEGER NOT NULL DEFAULT 0 CHECK(paused IN (0,1)),"
        "source_ids TEXT NOT NULL,theme_keys TEXT NOT NULL,stage_keys TEXT NOT NULL,"
        "keyword_all TEXT NOT NULL,keyword_any TEXT NOT NULL,keyword_exclude TEXT NOT NULL,"
        "year_policy TEXT NOT NULL CHECK(year_policy IN "
        "('current_year','all_years','fixed_year','unknown_date')),"
        "fixed_year INTEGER NOT NULL DEFAULT 0,created_at TEXT NOT NULL,updated_at TEXT NOT NULL,"
        "CHECK((year_policy='fixed_year' AND fixed_year BETWEEN 1 AND 9999) OR "
        "(year_policy<>'fixed_year' AND fixed_year=0)))");
    execute(database, "CREATE INDEX subscription_schools ON subscription(school_id,name,id)");
    QSqlQuery notices(database);
    if (!notices.exec("SELECT id,title FROM notices"))
        throw std::runtime_error(notices.lastError().text().toStdString());
    while (notices.next()) {
        const auto classified =
            NoticeClassifier::classify(notices.value(1).toString().toStdString());
        QSqlQuery update(database);
        update.prepare("UPDATE notices SET tags=?,stages=? WHERE id=?");
        update.addBindValue(stringsJson(classified.tags));
        update.addBindValue(stringsJson(classified.stages));
        update.addBindValue(notices.value(0));
        if (!update.exec())
            throw std::runtime_error(update.lastError().text().toStdString());
    }
}
void migrateToVersion4(QSqlDatabase &database) {
    execute(database,
            "CREATE UNIQUE INDEX notice_revision_identity ON notice_revisions(notice_id,id)");
    execute(
        database,
        "CREATE TABLE personal_task(id TEXT PRIMARY KEY,school_id TEXT NOT NULL,notice_id TEXT NOT "
        "NULL,"
        "notice_revision_id INTEGER,title TEXT NOT NULL,notes TEXT NOT NULL,action TEXT NOT NULL "
        "CHECK(action IN "
        "('registration','payment','application','check_result','attendance','other')),"
        "status TEXT NOT NULL CHECK(status IN "
        "('not_started','in_progress','completed','cancelled')),"
        "time_precision TEXT NOT NULL CHECK(time_precision IN ('unknown','date','datetime')),"
        "time_date TEXT NOT NULL,time_utc TEXT NOT NULL,time_zone TEXT NOT NULL,time_evidence TEXT "
        "NOT NULL,"
        "time_source TEXT NOT NULL CHECK(time_source IN "
        "('none','personal','original_text')),time_confirmed_at TEXT NOT NULL,"
        "reminder_enabled INTEGER NOT NULL CHECK(reminder_enabled IN(0,1)),"
        "reminder_minutes INTEGER NOT NULL CHECK(reminder_minutes BETWEEN 0 AND 525600),"
        "reminder_days INTEGER NOT NULL CHECK(reminder_days BETWEEN 0 AND 365),reminder_time TEXT "
        "NOT NULL,"
        "calendar_uid TEXT NOT NULL UNIQUE,created_at TEXT NOT NULL,updated_at TEXT NOT NULL,"
        "revision INTEGER NOT NULL CHECK(revision>0),"
        "FOREIGN KEY(school_id,notice_id) REFERENCES notices(school_id,id),"
        "FOREIGN KEY(notice_id,notice_revision_id) REFERENCES notice_revisions(notice_id,id),"
        "CHECK((time_precision='unknown' AND time_date='' AND time_utc='' AND time_source='none' "
        "AND time_confirmed_at='' AND reminder_enabled=0) OR "
        "(time_precision='date' AND time_date<>'' AND time_utc='' AND time_source<>'none' AND "
        "time_confirmed_at<>'') OR "
        "(time_precision='datetime' AND time_date='' AND time_utc<>'' AND time_source<>'none' AND "
        "time_confirmed_at<>'')))");
    execute(database,
            "CREATE INDEX task_schools ON personal_task(school_id,status,updated_at DESC)");
}
void migrateToVersion5(QSqlDatabase &database) {
    execute(database, "CREATE TABLE reminder_delivery(task_id TEXT NOT NULL REFERENCES "
                      "personal_task(id),trigger_utc TEXT NOT NULL,task_revision INTEGER NOT NULL "
                      "CHECK(task_revision>0),status TEXT NOT NULL CHECK(status "
                      "IN('attempting','submitted','expired','failed','interrupted')),recorded_at "
                      "TEXT NOT NULL,error TEXT NOT NULL,PRIMARY KEY(task_id,trigger_utc))");
}
void migrateToVersion6(QSqlDatabase &database) {
    execute(
        database,
        "CREATE TABLE school_resource(id TEXT NOT NULL,school_id TEXT NOT NULL,"
        "title TEXT NOT NULL,url TEXT NOT NULL,description TEXT NOT NULL,"
        "category TEXT NOT NULL CHECK(category IN('study_plan','course_material','library',"
        "'competition','academic_support','student_services','career','campus_life','other')),"
        "provider TEXT NOT NULL,discovered_from TEXT NOT NULL,last_checked_at TEXT NOT NULL,"
        "status TEXT NOT NULL CHECK(status "
        "IN('discovered','verified','login_required','unreachable')),"
        "link_kind TEXT NOT NULL CHECK(link_kind IN('official','official_recommended')),"
        "error TEXT NOT NULL,audiences TEXT NOT NULL DEFAULT '[]',tags TEXT NOT NULL DEFAULT '[]',"
        "favorite INTEGER NOT NULL DEFAULT 0 CHECK(favorite IN(0,1)),"
        "PRIMARY KEY(school_id,id),CHECK(length(school_id)>0 AND length(id)>0))");
    execute(database,
            "CREATE INDEX resource_schools ON school_resource(school_id,category,title,id)");
}
void migrateToVersion7(QSqlDatabase &database) {
    execute(database, "ALTER TABLE school_resource ADD COLUMN access_note TEXT NOT NULL DEFAULT ''");
    execute(database,
            "ALTER TABLE school_resource ADD COLUMN access_evidence TEXT NOT NULL DEFAULT ''");
}
} // namespace

void SqliteMigrations::apply(QSqlDatabase &database) {
    execute(database, "PRAGMA foreign_keys=ON");
    execute(database, "PRAGMA busy_timeout=5000");
    int version = 0;
    {
        QSqlQuery query(database);
        if (!query.exec("PRAGMA user_version") || !query.next())
            throw std::runtime_error("无法读取数据库版本");
        version = query.value(0).toInt();
    }
    if (version > CurrentVersion)
        throw std::runtime_error("数据库来自更新版本，请升级软件");
    if (version == CurrentVersion)
        return;
    if (!database.transaction())
        throw std::runtime_error("无法开始迁移事务");
    try {
        if (version < 1)
            migrateToVersion1(database);
        if (version < 2)
            migrateToVersion2(database);
        if (version < 3)
            migrateToVersion3(database);
        if (version < 4)
            migrateToVersion4(database);
        if (version < 5)
            migrateToVersion5(database);
        if (version < 6)
            migrateToVersion6(database);
        if (version < 7)
            migrateToVersion7(database);
        execute(database, QString("PRAGMA user_version=%1").arg(CurrentVersion));
        if (!database.commit())
            throw std::runtime_error("迁移提交失败");
    } catch (...) {
        database.rollback();
        throw;
    }
}

} // namespace campus
