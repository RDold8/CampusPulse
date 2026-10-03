#include "application/TaskService.h"
#include "application/NoticeMatcher.h"
#include <algorithm>
#include <chrono>
#include <format>
#include <stdexcept>
#include <unordered_map>

namespace campus {
std::string taskStatusKey(TaskStatus v) {
    switch (v) {
    case TaskStatus::NotStarted:
        return "not_started";
    case TaskStatus::InProgress:
        return "in_progress";
    case TaskStatus::Completed:
        return "completed";
    case TaskStatus::Cancelled:
        return "cancelled";
    }
    throw std::invalid_argument("待办状态无效");
}
TaskStatus taskStatusFromKey(const std::string &v) {
    if (v == "not_started")
        return TaskStatus::NotStarted;
    if (v == "in_progress")
        return TaskStatus::InProgress;
    if (v == "completed")
        return TaskStatus::Completed;
    if (v == "cancelled")
        return TaskStatus::Cancelled;
    throw std::invalid_argument("保存的待办状态无效");
}
std::string timePrecisionKey(TimePrecision v) {
    switch (v) {
    case TimePrecision::Unknown:
        return "unknown";
    case TimePrecision::DateOnly:
        return "date";
    case TimePrecision::DateTime:
        return "datetime";
    }
    throw std::invalid_argument("时间精度无效");
}
TimePrecision timePrecisionFromKey(const std::string &v) {
    if (v == "unknown")
        return TimePrecision::Unknown;
    if (v == "date")
        return TimePrecision::DateOnly;
    if (v == "datetime")
        return TimePrecision::DateTime;
    throw std::invalid_argument("保存的时间精度无效");
}
std::string confirmationKey(TimeConfirmation v) {
    switch (v) {
    case TimeConfirmation::None:
        return "none";
    case TimeConfirmation::Personal:
        return "personal";
    case TimeConfirmation::OriginalText:
        return "original_text";
    }
    throw std::invalid_argument("时间确认来源无效");
}
TimeConfirmation confirmationFromKey(const std::string &v) {
    if (v == "none")
        return TimeConfirmation::None;
    if (v == "personal")
        return TimeConfirmation::Personal;
    if (v == "original_text")
        return TimeConfirmation::OriginalText;
    throw std::invalid_argument("保存的时间确认来源无效");
}
namespace {
std::string trim(const std::string &v) {
    const auto first = v.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{}
                                      : v.substr(first, v.find_last_not_of(" \t\r\n") - first + 1);
}
bool clockTime(const std::string &v, bool seconds) {
    if (v.size() != (seconds ? 8 : 5) || v[2] != ':' || (seconds && v[5] != ':'))
        return false;
    for (size_t i = 0; i < v.size(); ++i)
        if (i != 2 && i != 5 && (v[i] < '0' || v[i] > '9'))
            return false;
    return std::stoi(v.substr(0, 2)) < 24 && std::stoi(v.substr(3, 2)) < 60 &&
           (!seconds || std::stoi(v.substr(6, 2)) < 60);
}
bool sameTimeInput(TaskTime a, TaskTime b) {
    a.confirmedAt.clear();
    b.confirmedAt.clear();
    return a == b;
}
} // namespace
TaskService::TaskService(std::string schoolId, std::string timeZone, TaskRepository &repository,
                         NoticeService &notices, Clock clock)
    : schoolId_(std::move(schoolId)), timeZone_(std::move(timeZone)), repository_(repository),
      notices_(notices), clock_(clock ? std::move(clock) : Clock(nowUtc)) {
    if (schoolId_.empty() || timeZone_.empty())
        throw std::invalid_argument("待办缺少学校或时区");
}
std::string TaskService::nowUtc() {
    return std::format("{:%FT%TZ}",
                       std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}
bool TaskService::validUtc(const std::string &v) {
    return v.size() == 20 && v[10] == 'T' && v[19] == 'Z' &&
           NoticeMatcher::publishedYear(v.substr(0, 10)) > 0 && clockTime(v.substr(11, 8), true);
}
std::vector<PersonalTask> TaskService::list() const {
    return repository_.list(schoolId_);
}
PersonalTask TaskService::find(const std::string &id) const {
    const auto value = repository_.find(schoolId_, id);
    if (!value)
        throw std::invalid_argument("待办不存在或不属于当前学校");
    return *value;
}
Notice TaskService::notice(const std::string &id) const {
    for (const auto &n : notices_.list())
        if (n.id == id && n.schoolId == schoolId_)
            return n;
    throw std::invalid_argument("关联通知不存在或不属于当前学校");
}
PersonalTask TaskService::draft(const std::string &id) const {
    const auto n = notice(id);
    PersonalTask task;
    task.schoolId = schoolId_;
    task.noticeId = n.id;
    task.title = n.title;
    task.time.timeZone = timeZone_;
    return task;
}
std::vector<TaskView> TaskService::views() const {
    const auto notices = notices_.list();
    std::unordered_map<std::string, const Notice *> byId;
    for (const auto &n : notices)
        if (n.schoolId == schoolId_)
            byId.emplace(n.id, &n);
    std::vector<TaskView> result;
    for (auto task : list()) {
        const auto found = byId.find(task.noticeId);
        if (found == byId.end())
            throw std::runtime_error("待办关联通知缺失");
        const auto &n = *found->second;
        result.push_back({std::move(task), n.title, n.url, n.revisionId});
    }
    return result;
}
PersonalTask TaskService::save(PersonalTask task, bool confirmTime) {
    if (task.schoolId.empty())
        task.schoolId = schoolId_;
    if (task.schoolId != schoolId_)
        throw std::invalid_argument("待办不属于当前学校");
    std::optional<PersonalTask> previous;
    if (!task.id.empty()) {
        previous = find(task.id);
        if (task.revision != previous->revision)
            throw std::runtime_error("待办已被修改，请重新打开编辑器");
        if (task.noticeId != previous->noticeId)
            throw std::invalid_argument("编辑时不能更换关联通知");
        task.noticeRevisionId = previous->noticeRevisionId;
        task.calendarUid = previous->calendarUid;
        task.createdAt = previous->createdAt;
        task.updatedAt = previous->updatedAt;
    }
    const auto n = notice(task.noticeId);
    task.title = trim(task.title);
    task.notes = trim(task.notes);
    task.time.evidence = trim(task.time.evidence);
    if (task.title.empty() || task.title.size() > 600)
        throw std::invalid_argument("办理事项不能为空或过长");
    if (task.notes.size() > 12000 || task.time.evidence.size() > 6000)
        throw std::invalid_argument("备注或时间说明过长");
    if (std::none_of(TaskActions.begin(), TaskActions.end(),
                     [&](const auto &a) { return a.key == task.action; }))
        throw std::invalid_argument("办理操作类型无效");
    taskStatusKey(task.status);
    timePrecisionKey(task.time.precision);
    confirmationKey(task.time.confirmation);
    if (task.time.timeZone.empty())
        task.time.timeZone = timeZone_;
    if (task.time.timeZone != timeZone_ &&
        (!previous || task.time.timeZone != previous->time.timeZone))
        throw std::invalid_argument("办理时间必须使用学校时区");
    if (task.time.precision == TimePrecision::Unknown) {
        task.time.date.clear();
        task.time.utcDateTime.clear();
        task.time.confirmation = TimeConfirmation::None;
        task.time.confirmedAt.clear();
        task.reminder.enabled = false;
    } else {
        if (task.time.precision == TimePrecision::DateOnly) {
            if (NoticeMatcher::publishedYear(task.time.date) == 0)
                throw std::invalid_argument("办理日期无效");
            task.time.utcDateTime.clear();
        } else {
            if (!validUtc(task.time.utcDateTime))
                throw std::invalid_argument("办理时刻无效");
            task.time.date.clear();
        }
        if (task.time.confirmation == TimeConfirmation::None)
            throw std::invalid_argument("请选择时间来源并确认");
        if (task.time.confirmation == TimeConfirmation::OriginalText && task.time.evidence.empty())
            throw std::invalid_argument("摘自原文的时间需要填写原文时间说明");
        const bool changed = !previous || !sameTimeInput(task.time, previous->time);
        if (changed && !confirmTime)
            throw std::invalid_argument("请核对并确认办理时间");
        task.time.confirmedAt = changed ? clock_() : previous->time.confirmedAt;
        if (!validUtc(task.time.confirmedAt))
            throw std::runtime_error("确认时刻无效");
    }
    if (task.reminder.minutesBefore < 0 || task.reminder.minutesBefore > 525600 ||
        task.reminder.daysBefore < 0 || task.reminder.daysBefore > 365 ||
        !clockTime(task.reminder.dateOnlyAt, false))
        throw std::invalid_argument("提醒偏好无效");
    if (previous && task == *previous)
        return *previous;
    const auto now = clock_();
    if (!validUtc(now))
        throw std::runtime_error("当前时刻无效");
    if (!previous) {
        task.noticeRevisionId = n.revisionId;
        task.createdAt = now;
        task.revision = 0;
        task.calendarUid.clear();
    }
    task.updatedAt = now;
    return repository_.save(task, previous ? previous->revision : 0);
}
PersonalTask TaskService::setStatus(const std::string &id, TaskStatus status) {
    auto task = find(id);
    task.status = status;
    return save(std::move(task));
}
PersonalTask TaskService::acknowledgeNotice(const std::string &id) {
    auto task = find(id);
    const auto head = notice(task.noticeId).revisionId;
    if (head == task.noticeRevisionId)
        return task;
    const auto expected = task.revision;
    task.noticeRevisionId = head;
    task.updatedAt = clock_();
    if (!validUtc(task.updatedAt))
        throw std::runtime_error("当前时刻无效");
    return repository_.save(task, expected);
}
bool TaskService::active(TaskStatus state) {
    return state == TaskStatus::NotStarted || state == TaskStatus::InProgress;
}
bool TaskService::overdue(const PersonalTask &task, const std::string &schoolDate,
                          const std::string &now) {
    if (!active(task.status))
        return false;
    if (task.time.precision == TimePrecision::DateOnly)
        return NoticeMatcher::publishedYear(schoolDate) > 0 && task.time.date < schoolDate;
    return task.time.precision == TimePrecision::DateTime && validUtc(now) &&
           task.time.utcDateTime < now;
}
} // namespace campus
