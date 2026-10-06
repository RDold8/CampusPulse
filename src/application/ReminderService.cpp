#include "application/ReminderService.h"
#include "application/TaskService.h"
namespace campus {
ReminderDecision ReminderService::decide(const PersonalTask &task, const std::string &trigger,
                                         const std::string &now, const std::string &earliest) {
    if (!task.reminder.enabled || !TaskService::active(task.status) ||
        task.time.precision == TimePrecision::Unknown ||
        task.time.confirmation == TimeConfirmation::None || !TaskService::validUtc(trigger) ||
        !TaskService::validUtc(now) || !TaskService::validUtc(earliest) || trigger > now)
        return ReminderDecision::Ignore;
    // A recent due reminder may have been saved, enabled, or missed while the app
    // was restarting. The repository claim, rather than the previous poll, makes
    // delivery at most once for this task and trigger.
    if (trigger < earliest)
        return ReminderDecision::Expired;
    return ReminderDecision::Deliver;
}
} // namespace campus
