#include "storage/SqliteRepository.h"
#include "storage/SqliteJson.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <stdexcept>
#include <map>

namespace campus {
namespace {
void execute(QSqlQuery &query) {
    if (!query.exec())
        throw std::runtime_error(query.lastError().text().toStdString());
}
QString attachmentsJson(const std::vector<Attachment> &attachments) {
    QJsonArray array;
    for (const auto &a : attachments)
        array.append(QJsonObject{{"name", QString::fromStdString(a.name)},
                                 {"url", QString::fromStdString(a.url)}});
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
void recordRevision(QSqlDatabase &db, const Notice &notice) {
    QSqlQuery query(db);
    query.prepare("INSERT INTO notice_revisions(notice_id,title,published_date,body,attachments) "
                  "VALUES(?,?,?,?,?)");
    query.addBindValue(QString::fromStdString(notice.id));
    query.addBindValue(QString::fromStdString(notice.title));
    query.addBindValue(QString::fromStdString(notice.publishedDate));
    query.addBindValue(QString::fromStdString(notice.body));
    query.addBindValue(attachmentsJson(notice.attachments));
    execute(query);
}
void saveDerivedAndOccurrence(QSqlDatabase &db, const Notice &notice) {
    QSqlQuery tags(db);
    tags.prepare("UPDATE notices SET tags=?,stages=? WHERE id=? AND school_id=?");
    tags.addBindValue(stringsJson(notice.tags));
    tags.addBindValue(stringsJson(notice.stages));
    tags.addBindValue(QString::fromStdString(notice.id));
    tags.addBindValue(QString::fromStdString(notice.schoolId));
    execute(tags);
    if (!notice.sourceId.empty()) {
        QSqlQuery occurrence(db);
        occurrence.prepare(
            "INSERT INTO source_occurrence(school_id,notice_id,source_id,source_name) "
            "VALUES(?,?,?,?) ON CONFLICT(school_id,notice_id,source_id) "
            "DO UPDATE SET source_name=excluded.source_name");
        for (const auto *value :
             {&notice.schoolId, &notice.id, &notice.sourceId, &notice.sourceName})
            occurrence.addBindValue(QString::fromStdString(*value));
        execute(occurrence);
    }
}
} // namespace
SqliteRepository::SqliteRepository(const QString &filename)
    : ownedDatabase_(std::make_unique<Database>(filename)), db_(ownedDatabase_->connection()) {}

SqliteRepository::SqliteRepository(Database &database) : db_(database.connection()) {}

SqliteRepository::~SqliteRepository() = default;
std::vector<Notice> SqliteRepository::list() const {
    QSqlQuery query(db_);
    if (!query.exec(
            "SELECT "
            "id,school_id,source_id,source_name,title,url,published_date,category,body,"
            "attachments,tags,stages,(SELECT COALESCE(MAX(r.id),0) FROM notice_revisions r "
            "WHERE r.notice_id=notices.id) FROM notices ORDER BY published_date DESC,title ASC"))
        throw std::runtime_error(query.lastError().text().toStdString());
    std::vector<Notice> result;
    while (query.next()) {
        Notice n;
        n.id = query.value(0).toString().toStdString();
        n.schoolId = query.value(1).toString().toStdString();
        n.sourceId = query.value(2).toString().toStdString();
        n.sourceName = query.value(3).toString().toStdString();
        n.title = query.value(4).toString().toStdString();
        n.url = query.value(5).toString().toStdString();
        n.publishedDate = query.value(6).toString().toStdString();
        n.category = query.value(7).toString().toStdString();
        n.body = query.value(8).toString().toStdString();
        for (const auto a : QJsonDocument::fromJson(query.value(9).toString().toUtf8()).array())
            n.attachments.push_back({a.toObject().value("name").toString().toStdString(),
                                     a.toObject().value("url").toString().toStdString()});
        n.tags = stringsFromJson(query.value(10).toString());
        n.stages = stringsFromJson(query.value(11).toString());
        n.revisionId = query.value(12).toLongLong();
        result.push_back(std::move(n));
    }
    QSqlQuery occurrences(db_);
    if (!occurrences.exec("SELECT notice_id,source_id FROM source_occurrence ORDER BY source_id"))
        throw std::runtime_error(occurrences.lastError().text().toStdString());
    std::map<std::string, size_t> indices;
    for (size_t i = 0; i < result.size(); ++i)
        indices.emplace(result[i].id, i);
    while (occurrences.next()) {
        const auto found = indices.find(occurrences.value(0).toString().toStdString());
        if (found != indices.end())
            result[found->second].sourceIds.push_back(
                occurrences.value(1).toString().toStdString());
    }
    return result;
}
void SqliteRepository::upsertBatch(const std::vector<Notice> &notices) {
    if (!db_.transaction())
        throw std::runtime_error("无法开始保存事务");
    try {
        for (const auto &n : notices) {
            QSqlQuery old(db_);
            old.prepare("SELECT title,published_date,body,attachments,category,school_id,url FROM "
                        "notices WHERE id=?");
            old.addBindValue(QString::fromStdString(n.id));
            execute(old);
            const bool exists = old.next();
            if (exists && (old.value(5).toString() != QString::fromStdString(n.schoolId) ||
                           old.value(6).toString() != QString::fromStdString(n.url)))
                throw std::runtime_error("通知标识与已保存的学校或URL冲突");
            const bool changed = !exists ||
                                 old.value(0).toString() != QString::fromStdString(n.title) ||
                                 old.value(1).toString() != QString::fromStdString(n.publishedDate);
            if (!changed) {
                // Keyword classification is derived metadata, not a change to official content.
                if (old.value(4).toString() != QString::fromStdString(n.category)) {
                    QSqlQuery category(db_);
                    category.prepare("UPDATE notices SET category=? WHERE id=?");
                    category.addBindValue(QString::fromStdString(n.category));
                    category.addBindValue(QString::fromStdString(n.id));
                    execute(category);
                }
                saveDerivedAndOccurrence(db_, n);
                continue;
            }
            Notice revision = n;
            if (exists) {
                revision.body = old.value(2).toString().toStdString();
                for (const auto a :
                     QJsonDocument::fromJson(old.value(3).toString().toUtf8()).array())
                    revision.attachments.push_back(
                        {a.toObject().value("name").toString().toStdString(),
                         a.toObject().value("url").toString().toStdString()});
            }
            QSqlQuery q(db_);
            q.prepare("INSERT INTO "
                      "notices(id,school_id,source_id,source_name,title,url,published_date,"
                      "category) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
                      "title=excluded.title,published_date=excluded.published_date,category="
                      "excluded.category");
            for (const auto *field : {&n.id, &n.schoolId, &n.sourceId, &n.sourceName, &n.title,
                                      &n.url, &n.publishedDate, &n.category})
                q.addBindValue(QString::fromStdString(*field));
            execute(q);
            saveDerivedAndOccurrence(db_, n);
            recordRevision(db_, revision);
        }
        if (!db_.commit())
            throw std::runtime_error("通知事务提交失败");
    } catch (...) {
        db_.rollback();
        throw;
    }
}
void SqliteRepository::saveDetail(const Notice &n) {
    if (!db_.transaction())
        throw std::runtime_error("无法开始正文保存事务");
    try {
        QSqlQuery old(db_);
        old.prepare("SELECT body,attachments FROM notices WHERE id=? AND school_id=?");
        old.addBindValue(QString::fromStdString(n.id));
        old.addBindValue(QString::fromStdString(n.schoolId));
        execute(old);
        if (!old.next())
            throw std::runtime_error("正文没有对应通知");
        if (old.value(0).toString() != QString::fromStdString(n.body) ||
            old.value(1).toString() != attachmentsJson(n.attachments)) {
            QSqlQuery q(db_);
            q.prepare("UPDATE notices SET body=?,attachments=? WHERE id=?");
            q.addBindValue(QString::fromStdString(n.body));
            q.addBindValue(attachmentsJson(n.attachments));
            q.addBindValue(QString::fromStdString(n.id));
            execute(q);
            recordRevision(db_, n);
        }
        if (!db_.commit())
            throw std::runtime_error("正文保存提交失败");
    } catch (...) {
        db_.rollback();
        throw;
    }
}
int SqliteRepository::revisionCount(const std::string &id) const {
    QSqlQuery q(db_);
    q.prepare("SELECT count(*) FROM notice_revisions WHERE notice_id=?");
    q.addBindValue(QString::fromStdString(id));
    execute(q);
    q.next();
    return q.value(0).toInt();
}
} // namespace campus
