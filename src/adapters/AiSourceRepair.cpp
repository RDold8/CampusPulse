#include "adapters/AiSourceRepair.h"
#include "adapters/HtmlAdapter.h"
#include "adapters/SchoolOnboarding.h"
#include <QJsonObject>
#include <QRegularExpression>
namespace campus {
namespace {
QUrl normalized(const std::string &value) {
    QUrl url(QString::fromStdString(value));
    if (url.scheme() == "http") url.setScheme("https");
    url.setFragment({});
    return url;
}
}
QSet<QString> AiSourceRepair::existing(const SchoolPackage &school) {
    QSet<QString> result;
    for (const auto &source : school.catalog)
        if (source.ready && source.configuredEnabled && !source.requiresLogin)
            result.insert(normalized(source.entryUrl).toString(QUrl::FullyEncoded));
    return result;
}
QJsonArray AiSourceRepair::pending(const SchoolPackage &school, const QString &sourceId) {
    QJsonArray result;
    auto root = school.officialHomepage.host().toLower();
    if (root.startsWith("www.")) root.remove(0, 4);
    QSet<QString> seen;
    for (const auto &source : school.catalog) {
        if ((source.ready && source.configuredEnabled) || source.requiresLogin ||
            (!sourceId.isEmpty() && sourceId != QString::fromStdString(source.id))) continue;
        auto url = normalized(source.discoveryUrl.empty() ? source.entryUrl : source.discoveryUrl);
        const auto wire = url.toString(QUrl::FullyEncoded);
        if (!SchoolOnboarding::withinUniversity(url, root) || HtmlAdapter::isArticleUrl(url) ||
            QRegularExpression("login|signin|oauth|sso|passport|[?&](token|key|password|ticket)=",
                QRegularExpression::CaseInsensitiveOption).match(wire).hasMatch() || seen.contains(wire)) continue;
        seen.insert(wire);
        result.append(QJsonObject{{"source_id", QString::fromStdString(source.id)},
            {"title", QString::fromStdString(source.name).left(200)}, {"url", wire},
            {"reason", QString::fromStdString(source.pendingReason).left(400)}});
        if (result.size() >= 12) break;
    }
    return result;
}
}
