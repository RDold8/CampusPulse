#include "storage/SqliteResourceRepository.h"
#include "storage/SqliteJson.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <stdexcept>

namespace campus {
namespace {
constexpr auto fields =
    "id,school_id,title,url,description,category,provider,discovered_from,last_checked_at,"
    "status,link_kind,error,audiences,tags,favorite,access_note,access_evidence";
void execute(QSqlQuery &query) {
    if (!query.exec())
        throw std::runtime_error(query.lastError().text().toStdString());
}
SchoolResource read(const QSqlQuery &query) {
    SchoolResource resource;
    resource.id = query.value(0).toString().toStdString();
    resource.schoolId = query.value(1).toString().toStdString();
    resource.title = query.value(2).toString().toStdString();
    resource.url = query.value(3).toString().toStdString();
    resource.description = query.value(4).toString().toStdString();
    resource.category = query.value(5).toString().toStdString();
    resource.provider = query.value(6).toString().toStdString();
    resource.discoveredFrom = query.value(7).toString().toStdString();
    resource.lastCheckedAt = query.value(8).toString().toStdString();
    resource.status = query.value(9).toString().toStdString();
    resource.linkKind = query.value(10).toString().toStdString();
    resource.error = query.value(11).toString().toStdString();
    resource.audiences = stringsFromJson(query.value(12).toString());
    resource.tags = stringsFromJson(query.value(13).toString());
    resource.favorite = query.value(14).toBool();
    resource.accessNote = query.value(15).toString().toStdString();
    resource.accessEvidence = query.value(16).toString().toStdString();
    return resource;
}
} // namespace
SqliteResourceRepository::SqliteResourceRepository(Database &database)
    : db_(database.connection()) {}
std::vector<SchoolResource> SqliteResourceRepository::list(const std::string &schoolId) const {
    if (schoolId.empty())
        throw std::invalid_argument("学校资源缺少读取学校范围");
    QSqlQuery query(db_);
    query.prepare(
        QString("SELECT %1 FROM school_resource WHERE school_id=? ORDER BY category,title,id")
            .arg(fields));
    query.addBindValue(QString::fromStdString(schoolId));
    execute(query);
    std::vector<SchoolResource> result;
    while (query.next())
        result.push_back(read(query));
    return result;
}
void SqliteResourceRepository::upsert(const std::string &schoolId,
                                      const std::vector<SchoolResource> &resources) {
    if (schoolId.empty())
        throw std::invalid_argument("学校资源缺少保存学校范围");
    for (const auto &resource : resources)
        if (resource.schoolId != schoolId || resource.id.empty())
            throw std::invalid_argument("学校资源批次包含无效或跨校标识，未保存");
        else if (resource.accessNote.empty() != resource.accessEvidence.empty())
            throw std::invalid_argument("学校资源使用说明与官方出处必须成对保存，未保存本批次");
    if (resources.empty())
        return;
    if (!db_.transaction())
        throw std::runtime_error("无法开始学校资源保存事务");
    try {
        for (const auto &resource : resources) {
            QSqlQuery query(db_);
            query.prepare(
                QString(
                    "INSERT INTO school_resource(%1) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,0,?,?) "
                    "ON CONFLICT(school_id,id) DO UPDATE SET title=excluded.title,"
                    "url=excluded.url,description=excluded.description,category=excluded.category,"
                    "provider=excluded.provider,discovered_from=excluded.discovered_from,"
                    "last_checked_at=excluded.last_checked_at,status=excluded.status,"
                    "link_kind=excluded.link_kind,error=excluded.error,"
                    "audiences=excluded.audiences,tags=excluded.tags,"
                    "access_note=CASE WHEN excluded.access_note<>'' AND "
                    "excluded.access_evidence<>'' THEN excluded.access_note "
                    "ELSE school_resource.access_note END,"
                    "access_evidence=CASE WHEN excluded.access_note<>'' AND "
                    "excluded.access_evidence<>'' THEN excluded.access_evidence "
                    "ELSE school_resource.access_evidence END")
                    .arg(fields));
            for (const auto *value :
                 {&resource.id, &resource.schoolId, &resource.title, &resource.url,
                  &resource.description, &resource.category, &resource.provider,
                  &resource.discoveredFrom, &resource.lastCheckedAt, &resource.status,
                  &resource.linkKind, &resource.error})
                query.addBindValue(QString::fromStdString(*value));
            query.addBindValue(stringsJson(resource.audiences));
            query.addBindValue(stringsJson(resource.tags));
            query.addBindValue(QString::fromStdString(resource.accessNote));
            query.addBindValue(QString::fromStdString(resource.accessEvidence));
            execute(query);
        }
        if (!db_.commit())
            throw std::runtime_error("学校资源保存提交失败");
    } catch (...) {
        db_.rollback();
        throw;
    }
}
void SqliteResourceRepository::setFavorite(const std::string &schoolId, const std::string &id,
                                           bool favorite) {
    if (schoolId.empty() || id.empty())
        throw std::invalid_argument("学校资源收藏缺少学校范围或标识");
    QSqlQuery query(db_);
    query.prepare("UPDATE school_resource SET favorite=? WHERE school_id=? AND id=?");
    query.addBindValue(favorite ? 1 : 0);
    query.addBindValue(QString::fromStdString(schoolId));
    query.addBindValue(QString::fromStdString(id));
    execute(query);
    if (query.numRowsAffected() != 1)
        throw std::invalid_argument("学校资源不存在或不属于当前学校");
}
} // namespace campus
