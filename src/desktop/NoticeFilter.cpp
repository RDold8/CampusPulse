#include "desktop/NoticeFilter.h"
#include "desktop/NoticeListModel.h"
#include "application/NoticeMatcher.h"
#include "domain/Theme.h"
#include <QRegularExpression>
namespace campus {
void NoticeFilter::setSource(QString sourceId) {
    query_.sourceIds.clear();
    if (!sourceId.isEmpty())
        query_.sourceIds.push_back(sourceId.toStdString());
    invalidateFilter();
}
void NoticeFilter::setQuery(QString text) {
    query_.keywordAll.clear();
    for (const auto &token : text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts))
        query_.keywordAll.push_back(token.toStdString());
    invalidateFilter();
}
void NoticeFilter::setCategory(QString label) {
    for (const auto &theme : Themes)
        if (QString::fromUtf8(theme.label.data(), static_cast<int>(theme.label.size())) == label) {
            setThemeKey(QString::fromUtf8(theme.key.data(), static_cast<int>(theme.key.size())));
            return;
        }
    setThemeKey(label);
}
void NoticeFilter::setThemeKey(QString key) {
    query_.themeKeys.clear();
    if (!key.isEmpty())
        query_.themeKeys.push_back(key.toStdString());
    invalidateFilter();
}
void NoticeFilter::setCurrentYear() {
    query_.yearPolicy = YearPolicy::CurrentYear;
    query_.fixedYear = 0;
    invalidateFilter();
}
void NoticeFilter::setRule(NoticeQuery query) {
    query_ = std::move(query);
    invalidateFilter();
}
bool NoticeFilter::filterAcceptsRow(int row, const QModelIndex &) const {
    const auto *model = dynamic_cast<const NoticeListModel *>(sourceModel());
    return model && NoticeMatcher::matches(model->notice(row), query_, QDate::currentDate().year());
}
} // namespace campus
