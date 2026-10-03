#include "desktop/NoticeListModel.h"
#include <QColor>
#include <QStringList>
#include "domain/Theme.h"
#include "application/NoticeClassifier.h"

namespace campus {
QString categoryLabel(const std::string &c) {
    const auto label = themeLabel(c);
    return QString::fromUtf8(label.data(), static_cast<int>(label.size()));
}
void NoticeListModel::setNotices(std::vector<Notice> notices) {
    beginResetModel();
    notices_ = std::move(notices);
    endResetModel();
}
int NoticeListModel::rowCount(const QModelIndex &p) const {
    return p.isValid() ? 0 : static_cast<int>(notices_.size());
}
int NoticeListModel::columnCount(const QModelIndex &p) const {
    return p.isValid() ? 0 : 5;
}
QVariant NoticeListModel::data(const QModelIndex &i, int role) const {
    if (!i.isValid() || i.row() >= rowCount())
        return {};
    const auto &n = notices_[static_cast<size_t>(i.row())];
    if (role == Qt::ToolTipRole)
        return QString::fromStdString(n.title) + "\n" + QString::fromStdString(n.url);
    if (role != Qt::DisplayRole)
        return {};
    switch (i.column()) {
    case 0:
        return QString::fromStdString(n.publishedDate);
    case 1:
        return QString::fromStdString(n.title);
    case 2: {
        // Show the words actually present in the title, independent of stored theme groups.
        QStringList topics;
        for (const auto *word : {"重修", "补考", "缴费"})
            if (n.title.find(word) != std::string::npos)
                topics << QString::fromUtf8(word);
        if (!topics.isEmpty())
            return topics.join(" / ");
        const auto classified = NoticeClassifier::classify(n.title);
        return categoryLabel(classified.primaryCategory != "academic_affairs" || n.category.empty()
                                 ? classified.primaryCategory
                                 : n.category);
    }
    case 3:
        return QString::fromStdString(n.sourceName);
    case 4: {
        QStringList stages;
        for (const auto &stage :
             n.stages.empty() ? NoticeClassifier::classify(n.title).stages : n.stages) {
            const auto label = stageLabel(stage);
            stages << QString::fromUtf8(label.data(), static_cast<int>(label.size()));
        }
        return stages.join(" / ");
    }
    }
    return {};
}
QVariant NoticeListModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    const QStringList names = {"发布日期", "通知标题", "主题提示", "主来源", "阶段提示"};
    return names.value(section);
}
const Notice &NoticeListModel::notice(int row) const {
    return notices_.at(static_cast<size_t>(row));
}
} // namespace campus
