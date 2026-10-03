#pragma once
#include "domain/SchoolResource.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace campus::resource_probe {
inline QJsonArray array(const std::vector<std::string> &values) {
    QJsonArray result;
    for (const auto &value : values)
        result.append(QString::fromStdString(value));
    return result;
}
inline QJsonObject json(const SchoolResource &resource) {
    return {{"id", QString::fromStdString(resource.id)},
            {"school_id", QString::fromStdString(resource.schoolId)},
            {"title", QString::fromStdString(resource.title)},
            {"url", QString::fromStdString(resource.url)},
            {"description", QString::fromStdString(resource.description)},
            {"access_note", QString::fromStdString(resource.accessNote)},
            {"access_evidence", QString::fromStdString(resource.accessEvidence)},
            {"category", QString::fromStdString(resource.category)},
            {"provider", QString::fromStdString(resource.provider)},
            {"discovered_from", QString::fromStdString(resource.discoveredFrom)},
            {"last_checked_at", QString::fromStdString(resource.lastCheckedAt)},
            {"status", QString::fromStdString(resource.status)},
            {"link_kind", QString::fromStdString(resource.linkKind)},
            {"error", QString::fromStdString(resource.error)},
            {"audiences", array(resource.audiences)}, {"tags", array(resource.tags)},
            {"favorite", resource.favorite}};
}
inline SchoolResource resource(const QJsonObject &object) {
    SchoolResource result;
    const auto string = [&](const char *key) { return object.value(key).toString().toStdString(); };
    result.id = string("id");
    result.schoolId = string("school_id");
    result.title = string("title");
    result.url = string("url");
    result.description = string("description");
    result.accessNote = string("access_note");
    result.accessEvidence = string("access_evidence");
    result.category = string("category");
    result.provider = string("provider");
    result.discoveredFrom = string("discovered_from");
    result.lastCheckedAt = string("last_checked_at");
    result.status = string("status");
    result.linkKind = string("link_kind");
    result.error = string("error");
    for (const auto value : object.value("audiences").toArray())
        result.audiences.push_back(value.toString().toStdString());
    for (const auto value : object.value("tags").toArray())
        result.tags.push_back(value.toString().toStdString());
    return result;
}
} // namespace campus::resource_probe
