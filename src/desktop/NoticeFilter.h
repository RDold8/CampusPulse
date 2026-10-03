#pragma once
#include <QSortFilterProxyModel>
#include <QDate>
#include "domain/NoticeQuery.h"
namespace campus {
class NoticeFilter final : public QSortFilterProxyModel {
  public:
    explicit NoticeFilter(QObject *parent = nullptr) : QSortFilterProxyModel(parent) {}
    void setYear(int year) {
        query_.yearPolicy = year == 0    ? YearPolicy::AllYears
                            : year == -1 ? YearPolicy::UnknownDate
                                         : YearPolicy::FixedYear;
        query_.fixedYear = year > 0 ? year : 0;
        invalidateFilter();
    }
    int year() const {
        return query_.yearPolicy == YearPolicy::CurrentYear   ? QDate::currentDate().year()
               : query_.yearPolicy == YearPolicy::AllYears    ? 0
               : query_.yearPolicy == YearPolicy::UnknownDate ? -1
                                                              : query_.fixedYear;
    }
    void setSource(QString sourceId);
    void setQuery(QString text);
    void setCategory(QString label);
    void setThemeKey(QString key);
    void setCurrentYear();
    void setRule(NoticeQuery query);
    const NoticeQuery &query() const {
        return query_;
    }

  protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

  private:
    NoticeQuery query_;
};
} // namespace campus
