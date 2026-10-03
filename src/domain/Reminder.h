#pragma once
#include <string>
#include <vector>
namespace campus {
struct ReminderRecord {
    std::string taskId, triggerUtc, status, recordedAt, error;
    int taskRevision = 0;
};
class ReminderRepository {
  public:
    virtual ~ReminderRepository() = default;
    virtual std::vector<std::string> schools() const = 0;
    virtual bool claim(const ReminderRecord &) = 0;
    virtual void finish(const ReminderRecord &) = 0;
    virtual void recover(const std::string &nowUtc) = 0;
};
} // namespace campus
