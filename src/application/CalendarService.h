#pragma once
#include "application/TaskService.h"
#include <functional>

namespace campus {
// A projection of an existing task. No independent calendar dates are stored.
struct CalendarItem {
    TaskView view;
    std::string localDate; // YYYY-MM-DD in the current school's display zone.
};
class CalendarService {
  public:
    using ExactDateProjection =
        std::function<std::string(const std::string &utc, const std::string &displayZone)>;
    CalendarService(TaskService &tasks, std::string displayZone, ExactDateProjection projection);
    std::vector<CalendarItem> items(bool includeHistory = false) const;
    std::vector<CalendarItem> month(int year, unsigned month, bool includeHistory = false) const;
    std::vector<CalendarItem> day(const std::string &date, bool includeHistory = false) const;
    std::size_t undatedCount() const;
    static bool hasConfirmedTime(const PersonalTask &task);

  private:
    TaskService &tasks_;
    std::string displayZone_;
    ExactDateProjection projection_;
};
} // namespace campus
