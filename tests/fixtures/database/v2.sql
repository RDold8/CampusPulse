-- Frozen schema shipped by stage 1. Keep independent of current migration code.
CREATE TABLE notices(id TEXT PRIMARY KEY,school_id TEXT NOT NULL,source_id TEXT NOT NULL,source_name TEXT NOT NULL,title TEXT NOT NULL,url TEXT NOT NULL,published_date TEXT NOT NULL,category TEXT NOT NULL,body TEXT NOT NULL DEFAULT '',attachments TEXT NOT NULL DEFAULT '[]');
CREATE TABLE notice_revisions(id INTEGER PRIMARY KEY,notice_id TEXT NOT NULL REFERENCES notices(id),title TEXT NOT NULL,published_date TEXT NOT NULL,body TEXT NOT NULL,attachments TEXT NOT NULL,created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));
CREATE INDEX notice_dates ON notices(published_date DESC);
CREATE TABLE source_preferences(school_id TEXT NOT NULL,source_id TEXT NOT NULL,paused INTEGER NOT NULL DEFAULT 0 CHECK(paused IN (0,1)),PRIMARY KEY(school_id,source_id));
CREATE TABLE source_state(school_id TEXT NOT NULL,source_id TEXT NOT NULL,status TEXT NOT NULL DEFAULT 'never_checked',last_attempt_at TEXT NOT NULL DEFAULT '',last_success_at TEXT NOT NULL DEFAULT '',latest_published_date TEXT NOT NULL DEFAULT '',error TEXT NOT NULL DEFAULT '',successful_pages INTEGER NOT NULL DEFAULT 0 CHECK(successful_pages>=0),row_count INTEGER NOT NULL DEFAULT 0 CHECK(row_count>=0),PRIMARY KEY(school_id,source_id));
CREATE TABLE fetch_run(id INTEGER PRIMARY KEY,school_id TEXT NOT NULL,source_id TEXT NOT NULL,started_at TEXT NOT NULL,finished_at TEXT NOT NULL DEFAULT '',status TEXT NOT NULL DEFAULT 'updating',successful_pages INTEGER NOT NULL DEFAULT 0 CHECK(successful_pages>=0),row_count INTEGER NOT NULL DEFAULT 0 CHECK(row_count>=0),latest_published_date TEXT NOT NULL DEFAULT '',error TEXT NOT NULL DEFAULT '',FOREIGN KEY(school_id,source_id) REFERENCES source_state(school_id,source_id));
CREATE INDEX fetch_run_sources ON fetch_run(school_id,source_id,id DESC);
CREATE INDEX notice_source_dates ON notices(school_id,source_id,published_date DESC);
INSERT INTO notices VALUES('frozen-v2','cn-test','academic','教务处','重修缴费通知','https://school.edu.cn/frozen-v2','2026-09-16','exam','原有缓存正文','[]');
INSERT INTO notice_revisions VALUES(1,'frozen-v2','重修缴费通知','2026-09-16','原有缓存正文','[]','2026-09-16T00:00:00Z');
INSERT INTO source_preferences VALUES('cn-test','academic',1);
INSERT INTO source_state VALUES('cn-test','academic','success','2026-09-16T00:00:00Z','2026-09-16T00:00:00Z','2026-09-16','',1,1);
INSERT INTO fetch_run VALUES(1,'cn-test','academic','2026-09-16T00:00:00Z','2026-09-16T00:00:01Z','success',1,1,'2026-09-16','');
PRAGMA user_version=2;
