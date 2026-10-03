#pragma once
#include "domain/Notice.h"
#include "storage/Database.h"
#include <QSqlDatabase>
#include <memory>

namespace campus {
class SqliteRepository final : public NoticeRepository {
  public:
    explicit SqliteRepository(const QString &filename);
    explicit SqliteRepository(Database &database);
    ~SqliteRepository() override;
    std::vector<Notice> list() const override;
    void upsertBatch(const std::vector<Notice> &notices) override;
    void saveDetail(const Notice &notice) override;
    int revisionCount(const std::string &id) const;

  private:
    std::unique_ptr<Database> ownedDatabase_;
    QSqlDatabase db_;
};
} // namespace campus
