#include "application/NoticeService.h"
#include "application/NoticeClassifier.h"

namespace campus {

std::string NoticeService::categoryHint(const std::string &title) {
    return NoticeClassifier::classify(title).primaryCategory;
}

void NoticeService::ingest(std::vector<Notice> notices) {
    for (auto &notice : notices) {
        requireSchool(notice);
        auto classified = NoticeClassifier::classify(notice.title);
        notice.category = std::move(classified.primaryCategory);
        notice.tags = std::move(classified.tags);
        notice.stages = std::move(classified.stages);
    }
    repository_.upsertBatch(notices);
}
} // namespace campus
