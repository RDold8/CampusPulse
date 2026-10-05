#pragma once
#include "adapters/SchoolPackage.h"
#include <QJsonArray>
#include <QSet>
namespace campus {
struct AiSourceRepair {
    static QSet<QString> existing(const SchoolPackage &school);
    static QJsonArray pending(const SchoolPackage &school, const QString &sourceId = {});
};
}
