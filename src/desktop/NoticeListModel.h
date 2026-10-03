#pragma once
#include "domain/Notice.h"
#include <QAbstractTableModel>

namespace campus {
QString categoryLabel(const std::string &category);
class NoticeListModel final : public QAbstractTableModel {
  public:
    explicit NoticeListModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
    void setNotices(std::vector<Notice> notices);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    const Notice &notice(int row) const;

  private:
    std::vector<Notice> notices_;
};
} // namespace campus
