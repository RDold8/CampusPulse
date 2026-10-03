#include "application/NoticeMatcher.h"
#include "application/NoticeClassifier.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace campus {
std::string yearPolicyKey(YearPolicy policy) {
    switch (policy) {
    case YearPolicy::CurrentYear:
        return "current_year";
    case YearPolicy::AllYears:
        return "all_years";
    case YearPolicy::FixedYear:
        return "fixed_year";
    case YearPolicy::UnknownDate:
        return "unknown_date";
    }
    throw std::invalid_argument("年份策略无效");
}
YearPolicy yearPolicyFromKey(const std::string &key) {
    if (key == "current_year")
        return YearPolicy::CurrentYear;
    if (key == "all_years")
        return YearPolicy::AllYears;
    if (key == "fixed_year")
        return YearPolicy::FixedYear;
    if (key == "unknown_date")
        return YearPolicy::UnknownDate;
    throw std::invalid_argument("保存的年份策略无效");
}
namespace {
bool contains(const std::vector<std::string> &keys, const std::string &key) {
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}
std::string folded(std::string value) {
    // UTF-8 Chinese stays unchanged; English keywords are insensitive to ASCII letter case.
    for (auto &c : value)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    return value;
}
bool intersects(const std::vector<std::string> &wanted, const std::vector<std::string> &actual) {
    return wanted.empty() || std::any_of(wanted.begin(), wanted.end(),
                                         [&](const auto &key) { return contains(actual, key); });
}
} // namespace
int NoticeMatcher::publishedYear(const std::string &date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-')
        return 0;
    for (size_t i = 0; i < date.size(); ++i)
        if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9'))
            return 0;
    const int year = std::stoi(date.substr(0, 4));
    const unsigned month = static_cast<unsigned>(std::stoi(date.substr(5, 2)));
    const unsigned day = static_cast<unsigned>(std::stoi(date.substr(8, 2)));
    const std::chrono::year_month_day parsed{std::chrono::year(year), std::chrono::month(month),
                                             std::chrono::day(day)};
    return year > 0 && parsed.ok() ? year : 0;
}
bool NoticeMatcher::matches(const Notice &notice, const NoticeQuery &query, int currentYear) {
    if (!query.schoolId.empty() && notice.schoolId != query.schoolId)
        return false;
    const int year = publishedYear(notice.publishedDate);
    switch (query.yearPolicy) {
    case YearPolicy::CurrentYear:
        if (year != currentYear || year == 0)
            return false;
        break;
    case YearPolicy::FixedYear:
        if (year != query.fixedYear || year == 0)
            return false;
        break;
    case YearPolicy::UnknownDate:
        if (year != 0)
            return false;
        break;
    case YearPolicy::AllYears:
        break;
    default:
        return false;
    }
    auto sourceIds = notice.sourceIds;
    sourceIds.push_back(notice.sourceId);
    if (!intersects(query.sourceIds, sourceIds))
        return false;
    const auto classification = NoticeClassifier::classify(notice.title);
    auto tags = notice.tags;
    tags.insert(tags.end(), classification.tags.begin(), classification.tags.end());
    if (notice.body.find("缴费") != std::string::npos || notice.body.find("交费") != std::string::npos)
        tags.push_back("payment");
    if (!notice.category.empty())
        tags.push_back(notice.category);
    // The focused theme deliberately preserves its original title-based retake scope.
    if (!query.themeKeys.empty()) {
        const bool themeMatches =
            std::any_of(query.themeKeys.begin(), query.themeKeys.end(), [&](const auto &key) {
                return key == "retake_payment" ? notice.title.find("重修") != std::string::npos
                                               : contains(tags, key);
            });
        if (!themeMatches)
            return false;
    }
    if (!intersects(query.stageKeys, notice.stages.empty() ? classification.stages : notice.stages))
        return false;
    const auto haystack = folded(notice.title + " " + notice.sourceName);
    const auto found = [&](const std::string &word) {
        return !word.empty() && haystack.find(folded(word)) != std::string::npos;
    };
    if (std::any_of(query.keywordExclude.begin(), query.keywordExclude.end(), found))
        return false;
    if (!std::all_of(query.keywordAll.begin(), query.keywordAll.end(), found))
        return false;
    return query.keywordAny.empty() ||
           std::any_of(query.keywordAny.begin(), query.keywordAny.end(), found);
}
} // namespace campus
