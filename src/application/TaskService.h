#pragma once
#include "application/NoticeService.h"
#include "domain/PersonalTask.h"
#include <functional>

namespace campus {
class TaskService {
  public:
    using Clock = std::function<std::string()>;
    TaskService(std::string schoolId, std::string timeZone, TaskRepository &repository,
                NoticeService &notices, Clock clock = {});
    std::vector<PersonalTask> list() const;
    std::vector<TaskView> views() const;
    PersonalTask find(const std::string &id) const;
    PersonalTask draft(const std::string &noticeId) const;
    PersonalTask save(PersonalTask task, bool confirmTime = false);
    PersonalTask setStatus(const std::string &id, TaskStatus status);
    PersonalTask acknowledgeNotice(const std::string &id);
    static bool active(TaskStatus status);
    static bool overdue(const PersonalTask &task, const std::string &schoolDate,
                        const std::string &nowUtc);
    static bool validUtc(const std::string &value);
    static std::string nowUtc();

  private:
    std::string schoolId_, timeZone_;
    TaskRepository &repository_;
    NoticeService &notices_;
    Clock clock_;
    Notice notice(const std::string &id) const;
};
} // namespace campus
