#pragma once

#include "domain/Source.h"
#include "storage/Database.h"

namespace campus {

class SqliteSourceRepository final : public SourceRepository {
  public:
    explicit SqliteSourceRepository(Database &database);
    SourceState state(const std::string &schoolId, const std::string &sourceId) const override;
    void setPaused(const std::string &schoolId, const std::string &sourceId, bool paused) override;
    std::int64_t beginRun(const std::string &schoolId, const std::string &sourceId,
                          const std::string &timestamp) override;
    void recordPage(std::int64_t runId, int rows, const std::string &latestDate) override;
    void finishRun(std::int64_t runId, const std::string &error,
                   const std::string &timestamp) override;
    void recoverInterrupted(const std::string &schoolId, const std::string &timestamp) override;

  private:
    QSqlDatabase db_;
};

} // namespace campus
