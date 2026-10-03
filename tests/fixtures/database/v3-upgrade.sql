-- Frozen v2-to-v3 extension. Never call production migrations to prepare this fixture.
ALTER TABLE notices ADD COLUMN tags TEXT NOT NULL DEFAULT '[]';
ALTER TABLE notices ADD COLUMN stages TEXT NOT NULL DEFAULT '[]';
UPDATE notices SET tags='["exam","retake_payment"]',stages='["payment"]';
CREATE UNIQUE INDEX notice_school_identity ON notices(school_id,id);
CREATE TABLE source_occurrence(school_id TEXT NOT NULL,notice_id TEXT NOT NULL,source_id TEXT NOT NULL,source_name TEXT NOT NULL,PRIMARY KEY(school_id,notice_id,source_id),FOREIGN KEY(school_id,notice_id) REFERENCES notices(school_id,id));
CREATE INDEX occurrence_sources ON source_occurrence(school_id,source_id,notice_id);
INSERT INTO source_occurrence SELECT school_id,id,source_id,source_name FROM notices;
CREATE TABLE subscription(id TEXT PRIMARY KEY,school_id TEXT NOT NULL,name TEXT NOT NULL,paused INTEGER NOT NULL DEFAULT 0 CHECK(paused IN (0,1)),source_ids TEXT NOT NULL,theme_keys TEXT NOT NULL,stage_keys TEXT NOT NULL,keyword_all TEXT NOT NULL,keyword_any TEXT NOT NULL,keyword_exclude TEXT NOT NULL,year_policy TEXT NOT NULL CHECK(year_policy IN ('current_year','all_years','fixed_year','unknown_date')),fixed_year INTEGER NOT NULL DEFAULT 0,created_at TEXT NOT NULL,updated_at TEXT NOT NULL,CHECK((year_policy='fixed_year' AND fixed_year BETWEEN 1 AND 9999) OR (year_policy<>'fixed_year' AND fixed_year=0)));
CREATE INDEX subscription_schools ON subscription(school_id,name,id);
INSERT INTO subscription VALUES('frozen-sub','cn-test','重修关注',1,'["academic"]','["retake_payment"]','[]','[]','[]','[]','current_year',0,'2026-09-16T00:00:00Z','2026-09-16T00:00:00Z');
PRAGMA user_version=3;
