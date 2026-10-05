#pragma once
#include <QJsonObject>
#include <QString>
namespace campus {
struct AiSearchHistory {
    static QJsonObject load(const QString &directory, const QString &schoolId);
    static void save(const QString &directory, const QString &schoolId, const QJsonObject &report);
};
}
