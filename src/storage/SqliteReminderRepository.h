#pragma once
#include "domain/Reminder.h"
#include <QSqlDatabase>
namespace campus {
class SqliteReminderRepository final : public ReminderRepository {
  public:
    explicit SqliteReminderRepository(QSqlDatabase &database) : database_(database) {}
    std::vector<std::string> schools() const override;
    bool claim(const ReminderRecord &) override;
    void finish(const ReminderRecord &) override;
    void recover(const std::string &) override;

  private:
    QSqlDatabase &database_;
};
} // namespace campus
