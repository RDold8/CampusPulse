#pragma once

#include <QSqlDatabase>

namespace campus {

class SqliteMigrations {
  public:
    static constexpr int CurrentVersion = 8;
    static void apply(QSqlDatabase &database);
};

} // namespace campus
