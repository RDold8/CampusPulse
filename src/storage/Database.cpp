#include "storage/Database.h"
#include "storage/SqliteMigrations.h"
#include <QSqlError>
#include <QUuid>
#include <stdexcept>

namespace campus {

Database::Database(const QString &filename) : connectionName_(QUuid::createUuid().toString()) {
    db_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    db_.setDatabaseName(filename);
    try {
        if (!db_.open())
            throw std::runtime_error(db_.lastError().text().toStdString());
        SqliteMigrations::apply(db_);
    } catch (...) {
        db_.close();
        db_ = {};
        QSqlDatabase::removeDatabase(connectionName_);
        throw;
    }
}

Database::~Database() {
    db_.close();
    db_ = {};
    QSqlDatabase::removeDatabase(connectionName_);
}

QSqlDatabase &Database::connection() {
    return db_;
}

} // namespace campus
