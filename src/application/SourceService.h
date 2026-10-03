#pragma once

#include "domain/Source.h"
#include <map>
#include <optional>

namespace campus {

class SourceService {
  public:
    SourceService(std::string schoolId, std::vector<SourceDescription> sources,
                  SourceRepository &repository);
    std::vector<SourceView> list() const;
    std::optional<SourceView> find(const std::string &sourceId) const;
    bool isRunnable(const std::string &sourceId) const;
    void setPaused(const std::string &sourceId, bool paused);
    std::int64_t begin(const std::string &sourceId, const std::string &timestamp);
    void page(std::int64_t runId, int rows, const std::string &latestDate);
    void finish(std::int64_t runId, const std::string &error, const std::string &timestamp);
    void recover(const std::string &timestamp);

  private:
    std::string schoolId_;
    std::vector<SourceDescription> sources_;
    SourceRepository &repository_;
    std::map<std::int64_t, std::string> activeRuns_;
};

} // namespace campus
