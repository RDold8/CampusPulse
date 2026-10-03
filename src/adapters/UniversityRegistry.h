#pragma once

#include <QString>
#include <QUrl>
#include <vector>

namespace campus {

struct RegisteredUniversity {
    QString id;
    QString name;
    QUrl homepage;
    QString configFile;
};

// Only locally installed, community-reviewed school packages are addressable.
// Resolving user input never fetches a page or constructs a new crawler configuration.
class UniversityRegistry {
  public:
    explicit UniversityRegistry(const QString &directory);
    const std::vector<RegisteredUniversity> &list() const;
    RegisteredUniversity resolve(const QString &input) const;

  private:
    std::vector<RegisteredUniversity> universities_;
};

} // namespace campus
