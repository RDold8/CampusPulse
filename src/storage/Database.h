#pragma once

#include <QSqlDatabase>

namespace campus {

// Repositories borrow this connection; the Database owner must outlive them.
class Database {
  public:
    explicit Database(const QString &filename);
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    QSqlDatabase &connection();

  private:
    QString connectionName_;
    QSqlDatabase db_;
};

} // namespace campus
