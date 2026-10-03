#pragma once
#include "application/NoticeService.h"
#include "domain/Subscription.h"

namespace campus {
class SubscriptionService {
  public:
    SubscriptionService(std::string schoolId, SubscriptionRepository &repository,
                        NoticeService &notices);
    std::vector<Subscription> list() const;
    Subscription find(const std::string &id) const;
    Subscription save(Subscription subscription);
    void setPaused(const std::string &id, bool paused);
    void remove(const std::string &id);
    std::vector<Notice> preview(const NoticeQuery &query, int currentYear) const;
    std::vector<Notice> matches(const std::string &id, int currentYear) const;
    static void normalize(NoticeQuery &query);

  private:
    std::string schoolId_;
    SubscriptionRepository &repository_;
    NoticeService &notices_;
};
} // namespace campus
