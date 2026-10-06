#pragma once
#include "domain/PersonalTask.h"
namespace campus {
enum class ReminderDecision { Ignore, Expired, Deliver };
class ReminderService {
  public:
    static ReminderDecision decide(const PersonalTask &, const std::string &triggerUtc,
                                   const std::string &nowUtc, const std::string &earliestUtc);
};
} // namespace campus
