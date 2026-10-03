#include "application/CalendarService.h"
#include "application/NoticeMatcher.h"
#include <algorithm>
#include <format>
#include <stdexcept>

namespace campus {
CalendarService::CalendarService(TaskService &tasks, std::string displayZone,
                                 ExactDateProjection projection)
    : tasks_(tasks), displayZone_(std::move(displayZone)), projection_(std::move(projection)) {
    if (displayZone_.empty() || !projection_)
        throw std::invalid_argument("日历缺少学校时区或时间投影");
}
bool CalendarService::hasConfirmedTime(const PersonalTask &task) {
    const auto &time = task.time;
    if (time.confirmation == TimeConfirmation::None || !TaskService::validUtc(time.confirmedAt))
        return false;
    return (time.precision == TimePrecision::DateOnly &&
            NoticeMatcher::publishedYear(time.date) > 0) ||
           (time.precision == TimePrecision::DateTime && TaskService::validUtc(time.utcDateTime));
}
std::vector<CalendarItem> CalendarService::items(bool includeHistory) const {
    std::vector<CalendarItem> result;
    for (auto view : tasks_.views()) {
        const auto &task = view.task;
        if (!hasConfirmedTime(task) || (!includeHistory && !TaskService::active(task.status)))
            continue;
        auto date = task.time.precision == TimePrecision::DateOnly
                        ? task.time.date
                        : projection_(task.time.utcDateTime, displayZone_);
        if (NoticeMatcher::publishedYear(date) == 0)
            throw std::runtime_error("办理时刻无法转换到学校日历日期");
        result.push_back({std::move(view), std::move(date)});
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        if (a.localDate != b.localDate)
            return a.localDate < b.localDate;
        const auto &at = a.view.task.time;
        const auto &bt = b.view.task.time;
        if (at.precision != bt.precision)
            return at.precision == TimePrecision::DateOnly;
        if (at.utcDateTime != bt.utcDateTime)
            return at.utcDateTime < bt.utcDateTime;
        return a.view.task.id < b.view.task.id;
    });
    return result;
}
std::vector<CalendarItem> CalendarService::month(int year, unsigned month,
                                                 bool includeHistory) const {
    if (year < 1 || year > 9999 || month < 1 || month > 12)
        throw std::invalid_argument("日历月份无效");
    auto result = items(includeHistory);
    const auto prefix = std::format("{:04}-{:02}-", year, month);
    std::erase_if(result, [&](const auto &item) { return !item.localDate.starts_with(prefix); });
    return result;
}
std::vector<CalendarItem> CalendarService::day(const std::string &date, bool includeHistory) const {
    if (NoticeMatcher::publishedYear(date) == 0)
        throw std::invalid_argument("日历日期无效");
    auto result = items(includeHistory);
    std::erase_if(result, [&](const auto &item) { return item.localDate != date; });
    return result;
}
std::size_t CalendarService::undatedCount() const {
    const auto tasks = tasks_.list();
    return std::count_if(tasks.begin(), tasks.end(),
                         [](const auto &task) { return !hasConfirmedTime(task); });
}
} // namespace campus
