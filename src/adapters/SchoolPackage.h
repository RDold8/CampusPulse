#pragma once
#include "domain/Source.h"
#include <QString>
#include <QStringList>
#include <QUrl>
#include <vector>

namespace campus {
struct SourceConfig {
    QString schoolId;
    QString id;
    QString name;
    QUrl entry;
    QStringList allowedHosts;
    QString itemSelector;
    QString titleSelector;
    QString dateSelector;
    QString bodySelector;
    QString attachmentSelector;
    QString nextPageSelector;
    int maxPages = 1;
    bool autoDetect = false;
    bool allowUnknownDates = false;
};
struct SchoolPackage {
    QString id;
    QString name;
    std::vector<SourceConfig> sources;
    // Full community catalog, including entries whose adapter is not ready yet.
    std::vector<SourceDescription> catalog;
    QString timeZone = "Asia/Shanghai";
    QStringList discoveryEntries;
    int discoveryLimit = 16;
    QString configFile;
    QUrl officialHomepage;
    QStringList resourceDiscoveryEntries;
    int resourceDiscoveryLimit = 32;
    bool automaticallyIdentified = false;
    static SchoolPackage load(const QString &filename);
};
bool isAllowedUrl(const QUrl &url, const SourceConfig &source);
} // namespace campus
