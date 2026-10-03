#include "application/ReminderService.h"
#include "application/TaskService.h"
namespace campus {
ReminderDecision ReminderService::decide(const PersonalTask &task, const std::string &trigger,
                                         const std::string &now, const std::string &previous,
                                         const std::string &earliest) {
    if (!task.reminder.enabled || !TaskService::active(task.status) ||
        task.time.precision == TimePrecision::Unknown ||
        task.time.confirmation == TimeConfirmation::None || !TaskService::validUtc(trigger) ||
        trigger > now)
        return ReminderDecision::Ignore;
    if (trigger <= previous || trigger < earliest)
        return ReminderDecision::Expired;
    return ReminderDecision::Deliver;
}
} // namespace campus
