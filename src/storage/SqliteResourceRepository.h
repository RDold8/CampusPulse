#pragma once
#include "domain/SchoolResource.h"
#include "storage/Database.h"
#include <QSqlDatabase>

namespace campus {
class SqliteResourceRepository final : public ResourceRepository {
  public:
    explicit SqliteResourceRepository(Database &database);
    std::vector<SchoolResource> list(const std::string &schoolId) const override;
    void upsert(const std::string &schoolId, const std::vector<SchoolResource> &resources) override;
    void setFavorite(const std::string &schoolId, const std::string &id, bool favorite) override;

  private:
    QSqlDatabase db_;
};
} // namespace campus
