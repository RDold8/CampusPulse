#pragma once
#include <string>
#include <vector>

namespace campus {
enum class YearPolicy { CurrentYear, AllYears, FixedYear, UnknownDate };
struct NoticeQuery {
    std::string schoolId;
    std::vector<std::string> sourceIds;
    std::vector<std::string> themeKeys;
    std::vector<std::string> stageKeys;
    std::vector<std::string> keywordAll;
    std::vector<std::string> keywordAny;
    std::vector<std::string> keywordExclude;
    YearPolicy yearPolicy = YearPolicy::CurrentYear;
    int fixedYear = 0;
};
std::string yearPolicyKey(YearPolicy policy);
YearPolicy yearPolicyFromKey(const std::string &key);
} // namespace campus
