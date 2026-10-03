#pragma once
#include "domain/Theme.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace campus {
enum class TaskStatus { NotStarted, InProgress, Completed, Cancelled };
enum class TimePrecision { Unknown, DateOnly, DateTime };
enum class TimeConfirmation { None, Personal, OriginalText };
std::string taskStatusKey(TaskStatus value);
TaskStatus taskStatusFromKey(const std::string &value);
std::string timePrecisionKey(TimePrecision value);
TimePrecision timePrecisionFromKey(const std::string &value);
std::string confirmationKey(TimeConfirmation value);
TimeConfirmation confirmationFromKey(const std::string &value);
inline constexpr std::array<NamedKey, 6> TaskActions{{{"registration", "报名"},
                                                      {"payment", "缴费"},
                                                      {"application", "申请"},
                                                      {"check_result", "核对结果"},
                                                      {"attendance", "参加活动"},
                                                      {"other", "其他事项"}}};
struct TaskTime {
    TimePrecision precision = TimePrecision::Unknown;
    std::string date;        // Date only: YYYY-MM-DD, no invented time.
    std::string utcDateTime; // Exact time: YYYY-MM-DDTHH:MM:SSZ.
    std::string timeZone;    // School IANA zone, retained with the task.
    std::string evidence;
    TimeConfirmation confirmation = TimeConfirmation::None;
    std::string confirmedAt;
    bool operator==(const TaskTime &) const = default;
};
struct TaskReminder {
    bool enabled = false;
    int minutesBefore = 1440;
    int daysBefore = 1;
    std::string dateOnlyAt = "09:00"; // Reminder clock, not deadline precision.
    bool operator==(const TaskReminder &) const = default;
};
struct PersonalTask {
    std::string id, schoolId, noticeId;
    std::int64_t noticeRevisionId = 0;
    std::string title, notes;
    std::string action = "other";
    TaskStatus status = TaskStatus::NotStarted;
    TaskTime time;
    TaskReminder reminder;
    std::string calendarUid, createdAt, updatedAt;
    int revision = 0;
    bool operator==(const PersonalTask &) const = default;
};
struct TaskView {
    PersonalTask task;
    std::string noticeTitle, noticeUrl;
    std::int64_t currentNoticeRevision = 0;
    bool needsReview() const {
        return currentNoticeRevision != task.noticeRevisionId;
    }
};
class TaskRepository {
  public:
    virtual ~TaskRepository() = default;
    virtual std::vector<PersonalTask> list(const std::string &schoolId) const = 0;
    virtual std::optional<PersonalTask> find(const std::string &schoolId,
                                             const std::string &id) const = 0;
    virtual PersonalTask save(const PersonalTask &task, int expectedRevision) = 0;
};
} // namespace campus
