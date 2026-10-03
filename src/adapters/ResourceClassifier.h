#pragma once
#include "domain/SchoolResource.h"
#include <QByteArray>
#include <QString>
#include <QUrl>

namespace campus {
class ResourceClassifier {
  public:
    // No request is made by these helpers; production and fixtures share them.
    static bool isSafeHttps(const QUrl &url);
    static bool isSafeHttps(const QString &rawUrl);
    static QString officialRoot(const QUrl &homepage);
    static bool isOfficial(const QUrl &url, const QString &officialRoot);
    static QUrl canonicalUrl(QUrl url);
    static bool isDownload(const QUrl &url);
    // Uses a known resource-page category only for explicit version labels.
    static QString labelInContext(const QString &label, const QString &parentCategory = {});
    static bool isPractical(const QString &title, const QUrl &url);
    static SchoolResource describe(const QString &schoolId, const QString &title, const QUrl &url,
                                   const QUrl &discoveredFrom);
    static bool isLoginPage(const QByteArray &html, const QString &title, const QUrl &url);
    static QString staticText(const QByteArray &html);
    static QString unverifiedReason(const QByteArray &html, const QString &text,
                                    bool hasUsefulLinks);
};
} // namespace campus
