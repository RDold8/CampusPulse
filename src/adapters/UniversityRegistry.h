#pragma once

#include <QString>
#include <QUrl>
#include <vector>

namespace campus {

struct SchoolPackage;

struct RegisteredUniversity {
    QString id;
    QString name;
    QUrl homepage;
    QString configFile;
    bool automaticallyIdentified = false;
};

// Community packages and separately identified local drafts remain distinct.
// Resolving input itself never performs network requests.
class UniversityRegistry {
  public:
    explicit UniversityRegistry(const QString &directory, const QString &discoveredDirectory = {});
    const std::vector<RegisteredUniversity> &list() const;
    RegisteredUniversity resolve(const QString &input) const;
    SchoolPackage loadSessionPackage(const QString &configFile, bool allowUnregistered = false) const;
    void addLocalDiscoveredPackage(const QString &configFile);

  private:
    std::vector<RegisteredUniversity> universities_;
};

} // namespace campus
