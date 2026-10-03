#pragma once
#include "domain/NoticeQuery.h"
#include <optional>

namespace campus {
struct Subscription {
    std::string id;
    std::string schoolId;
    std::string name;
    bool paused = false;
    NoticeQuery query;
    std::string createdAt;
    std::string updatedAt;
};
class SubscriptionRepository {
  public:
    virtual ~SubscriptionRepository() = default;
    virtual std::vector<Subscription> list(const std::string &schoolId) const = 0;
    virtual std::optional<Subscription> find(const std::string &schoolId,
                                             const std::string &id) const = 0;
    virtual Subscription save(const Subscription &subscription) = 0;
    virtual void remove(const std::string &schoolId, const std::string &id) = 0;
};
} // namespace campus
