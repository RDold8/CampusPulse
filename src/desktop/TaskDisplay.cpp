#include "desktop/TaskDisplay.h"
#include <QDateTime>
#include <QTimeZone>
namespace campus {
QString taskStatusText(TaskStatus value) {
    switch (value) {
    case TaskStatus::NotStarted:
        return "未开始";
    case TaskStatus::InProgress:
        return "进行中";
    case TaskStatus::Completed:
        return "已完成";
    case TaskStatus::Cancelled:
        return "已取消";
    }
    return "状态无效";
}
QString taskActionText(const std::string &value) {
    for (const auto &a : TaskActions)
        if (a.key == value)
            return QString::fromUtf8(a.label.data(), static_cast<int>(a.label.size()));
    return QString::fromStdString(value);
}
QString taskTimeText(const TaskTime &time) {
    if (time.precision == TimePrecision::Unknown)
        return "日期待定";
    if (time.precision == TimePrecision::DateOnly)
        return QString::fromStdString(time.date) + "（仅日期）";
    const auto utc = QDateTime::fromString(QString::fromStdString(time.utcDateTime), Qt::ISODate);
    const QTimeZone zone(QByteArray::fromStdString(time.timeZone));
    return utc.toTimeZone(zone).toString("yyyy-MM-dd HH:mm:ss") + " · " +
           QString::fromStdString(time.timeZone);
}
} // namespace campus
