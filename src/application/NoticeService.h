#pragma once
#include "domain/Notice.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace campus {

class NoticeService {
  public:
    explicit NoticeService(NoticeRepository &repository, std::string schoolId = {})
        : repository_(repository), schoolId_(std::move(schoolId)) {}
    std::vector<Notice> list() const {
        auto notices = repository_.list();
        if (!schoolId_.empty())
            std::erase_if(notices,
                          [this](const auto &notice) { return notice.schoolId != schoolId_; });
        return notices;
    }
    void ingest(std::vector<Notice> notices);
    void saveDetail(const Notice &notice) {
        requireSchool(notice);
        repository_.saveDetail(notice);
    }
    static std::string categoryHint(const std::string &title);

  private:
    NoticeRepository &repository_;
    std::string schoolId_;
    void requireSchool(const Notice &notice) const {
        if (!schoolId_.empty() && notice.schoolId != schoolId_)
            throw std::invalid_argument("通知不属于当前学校");
    }
};

} // namespace campus
