#include "application/SubscriptionService.h"
#include "application/NoticeMatcher.h"
#include "domain/Theme.h"
#include <algorithm>
#include <regex>
#include <stdexcept>

namespace campus {
namespace {
std::string trim(const std::string &text) {
    const auto begin = text.find_first_not_of(" \r\n\t");
    if (begin == std::string::npos)
        return {};
    return text.substr(begin, text.find_last_not_of(" \r\n\t") - begin + 1);
}
void clean(std::vector<std::string> &items) {
    if (items.size() > 64)
        throw std::invalid_argument("每组筛选条件最多64项");
    std::vector<std::string> result;
    for (const auto &item : items) {
        const auto value = trim(item);
        if (value.empty())
            continue;
        if (value.size() > 600)
            throw std::invalid_argument("筛选条件过长");
        if (std::find(result.begin(), result.end(), value) == result.end())
            result.push_back(value);
    }
    items = std::move(result);
}
} // namespace
SubscriptionService::SubscriptionService(std::string schoolId, SubscriptionRepository &repository,
                                         NoticeService &notices)
    : schoolId_(std::move(schoolId)), repository_(repository), notices_(notices) {
    if (schoolId_.empty())
        throw std::invalid_argument("订阅缺少学校标识");
}
std::vector<Subscription> SubscriptionService::list() const {
    return repository_.list(schoolId_);
}
Subscription SubscriptionService::find(const std::string &id) const {
    const auto result = repository_.find(schoolId_, id);
    if (!result)
        throw std::invalid_argument("订阅不存在或不属于当前学校");
    return *result;
}
void SubscriptionService::normalize(NoticeQuery &query) {
    clean(query.sourceIds);
    clean(query.themeKeys);
    clean(query.stageKeys);
    clean(query.keywordAll);
    clean(query.keywordAny);
    clean(query.keywordExclude);
    static const std::regex key("^[a-z0-9]+(?:-[a-z0-9]+)*$");
    for (const auto &source : query.sourceIds)
        if (!std::regex_match(source, key))
            throw std::invalid_argument("来源标识无效");
    for (const auto &theme : query.themeKeys)
        if (!knownTheme(theme))
            throw std::invalid_argument("主题标识无效");
    for (const auto &stage : query.stageKeys)
        if (!knownStage(stage))
            throw std::invalid_argument("阶段标识无效");
    yearPolicyKey(query.yearPolicy);
    if (query.yearPolicy == YearPolicy::FixedYear) {
        if (query.fixedYear < 1 || query.fixedYear > 9999)
            throw std::invalid_argument("固定年份必须在1至9999之间");
    } else
        query.fixedYear = 0;
}
Subscription SubscriptionService::save(Subscription subscription) {
    subscription.name = trim(subscription.name);
    if (subscription.name.empty() || subscription.name.size() > 240)
        throw std::invalid_argument("订阅名称不能为空，且不能超过80个中文字符");
    if (subscription.schoolId.empty())
        subscription.schoolId = schoolId_;
    if (subscription.schoolId != schoolId_)
        throw std::invalid_argument("订阅不属于当前学校");
    if (!subscription.query.schoolId.empty() && subscription.query.schoolId != schoolId_)
        throw std::invalid_argument("订阅筛选不属于当前学校");
    subscription.query.schoolId = schoolId_;
    normalize(subscription.query);
    if (!subscription.id.empty())
        find(subscription.id);
    return repository_.save(subscription);
}
void SubscriptionService::setPaused(const std::string &id, bool paused) {
    auto subscription = find(id);
    subscription.paused = paused;
    save(std::move(subscription));
}
void SubscriptionService::remove(const std::string &id) {
    find(id);
    repository_.remove(schoolId_, id);
}
std::vector<Notice> SubscriptionService::preview(const NoticeQuery &query, int currentYear) const {
    auto checked = query;
    if (!checked.schoolId.empty() && checked.schoolId != schoolId_)
        throw std::invalid_argument("筛选不属于当前学校");
    checked.schoolId = schoolId_;
    normalize(checked);
    auto notices = notices_.list();
    std::erase_if(notices, [&](const auto &notice) {
        return !NoticeMatcher::matches(notice, checked, currentYear);
    });
    return notices;
}
std::vector<Notice> SubscriptionService::matches(const std::string &id, int currentYear) const {
    const auto subscription = find(id);
    return subscription.paused ? std::vector<Notice>{} : preview(subscription.query, currentYear);
}
} // namespace campus
