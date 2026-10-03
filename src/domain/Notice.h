#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace campus {

struct Attachment {
    std::string name;
    std::string url;
};

// All strings crossing the domain boundary use UTF-8.
// publishedDate is the publication date, never a deadline.
struct Notice {
    std::string id;
    std::string schoolId;
    std::string sourceId;
    std::string sourceName;
    std::string title;
    std::string url;
    std::string publishedDate; // YYYY-MM-DD, empty means unknown
    std::string category;
    std::string body;
    std::vector<Attachment> attachments;
    // One notice can appear in several columns. Its primary source remains stable.
    std::vector<std::string> sourceIds;
    std::vector<std::string> tags;
    std::vector<std::string> stages;
    std::int64_t revisionId = 0; // Latest saved official revision, not a deadline.
};

class NoticeRepository {
  public:
    virtual ~NoticeRepository() = default;
    virtual std::vector<Notice> list() const = 0;
    virtual void upsertBatch(const std::vector<Notice> &notices) = 0;
    virtual void saveDetail(const Notice &notice) = 0;
};

} // namespace campus
