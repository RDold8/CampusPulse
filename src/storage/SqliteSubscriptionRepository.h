#pragma once
#include "domain/Subscription.h"
#include "storage/Database.h"
namespace campus {
class SqliteSubscriptionRepository final : public SubscriptionRepository {
  public:
    explicit SqliteSubscriptionRepository(Database &database);
    std::vector<Subscription> list(const std::string &schoolId) const override;
    std::optional<Subscription> find(const std::string &schoolId,
                                     const std::string &id) const override;
    Subscription save(const Subscription &subscription) override;
    void remove(const std::string &schoolId, const std::string &id) override;

  private:
    QSqlDatabase db_;
};
} // namespace campus
