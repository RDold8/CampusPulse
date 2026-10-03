#pragma once
#include "domain/Notice.h"
#include "domain/NoticeQuery.h"

namespace campus {
class NoticeMatcher {
  public:
    static bool matches(const Notice &notice, const NoticeQuery &query, int currentYear);
    static int publishedYear(const std::string &date);
};
} // namespace campus
