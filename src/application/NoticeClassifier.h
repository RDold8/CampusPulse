#pragma once
#include <string>
#include <vector>

namespace campus {
struct NoticeClassification {
    std::string primaryCategory;
    std::vector<std::string> tags;
    std::vector<std::string> stages;
};
class NoticeClassifier {
  public:
    static NoticeClassification classify(const std::string &title);
};
} // namespace campus
