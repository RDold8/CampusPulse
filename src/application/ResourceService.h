#pragma once
#include "domain/SchoolResource.h"

namespace campus {
class ResourceService {
  public:
    ResourceService(ResourceRepository &repository, std::string schoolId);
    std::vector<SchoolResource> list(const ResourceQuery &query = {}) const;
    void ingest(std::vector<SchoolResource> resources);
    void setFavorite(const std::string &id, bool favorite);

  private:
    ResourceRepository &repository_;
    std::string schoolId_;
};
} // namespace campus
