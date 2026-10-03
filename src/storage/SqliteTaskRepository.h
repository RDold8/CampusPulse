#pragma once
#include "domain/PersonalTask.h"
#include "storage/Database.h"
namespace campus {
class SqliteTaskRepository final : public TaskRepository {
  public:
    explicit SqliteTaskRepository(Database &database);
    std::vector<PersonalTask> list(const std::string &schoolId) const override;
    std::optional<PersonalTask> find(const std::string &schoolId,
                                     const std::string &id) const override;
    PersonalTask save(const PersonalTask &task, int expectedRevision) override;

  private:
    QSqlDatabase db_;
};
} // namespace campus
