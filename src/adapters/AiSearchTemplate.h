#pragma once
#include <QList>
#include <QString>

namespace campus {
// Shared, fixed prompts for discovering public university column entrances.
// A selected category changes the focus, not request budgets or validation rules.
struct AiSearchTemplate {
    QString id;
    QString name;
    QString description;
    QString focus;

    static QList<AiSearchTemplate> defaults();
    // Reject unknown IDs instead of silently broadening a category request.
    static AiSearchTemplate byId(const QString &id);
};
} // namespace campus
