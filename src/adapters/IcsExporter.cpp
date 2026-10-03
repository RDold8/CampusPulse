#include "adapters/IcsExporter.h"
#include "adapters/ArtifactWriter.h"
#include <QDateTime>
#include <QStringDecoder>
#include <QTimeZone>
#include <QUrl>
#include <ical.h>
#include <memory>
#include <set>
#include <stdexcept>

namespace campus {
namespace {
using Component = std::unique_ptr<icalcomponent, decltype(&icalcomponent_free)>;
void add(icalcomponent *component, icalproperty *property) {
    if (!property)
        throw std::runtime_error("无法构造日历字段");
    icalcomponent_add_property(component, property);
}
Component component(icalcomponent_kind kind) {
    Component value(icalcomponent_new(kind), icalcomponent_free);
    if (!value)
        throw std::runtime_error("无法构造日历记录");
    return value;
}
void text(const std::string &value) {
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString decoded = decoder(QByteArray::fromStdString(value));
    Q_UNUSED(decoded);
    if (decoder.hasError() || value.find('\0') != std::string::npos)
        throw std::invalid_argument("日历文本包含无效UTF-8或空字符");
    for (const auto c : value)
        if (static_cast<unsigned char>(c) < 32 && c != '\n' && c != '\r' && c != '\t')
            throw std::invalid_argument("日历文本包含无效控制字符");
}
std::string normalized(std::string value) {
    text(value);
    for (size_t pos = 0; (pos = value.find('\r', pos)) != std::string::npos;) {
        if (pos + 1 < value.size() && value[pos + 1] == '\n')
            value.erase(pos, 1);
        else
            value[pos++] = '\n';
    }
    return value;
}
icaltimetype utc(const std::string &value) {
    if (!TaskService::validUtc(value))
        throw std::invalid_argument("日历UTC时刻无效");
    auto data = QString::fromStdString(value).remove('-').remove(':').toLatin1();
    auto result = icaltime_from_string(data.constData());
    if (!icaltime_is_valid_time(result) || icaltime_is_null_time(result))
        throw std::invalid_argument("日历时刻无法序列化");
    return result;
}
icaltimetype date(const QDate &value) {
    if (!value.isValid() || value.year() < 1 || value.year() > 9999)
        throw std::invalid_argument("日历日期无效");
    return icaltime_from_string(value.toString("yyyyMMdd").toLatin1().constData());
}
void extra(icalcomponent *event, const char *name, const std::string &value) {
    text(value);
    auto *property = icalproperty_new_x(value.c_str());
    if (!property)
        throw std::runtime_error("无法构造日历任务字段");
    icalproperty_set_x_name(property, name);
    add(event, property);
}
std::string confirmationText(TimeConfirmation value) {
    return value == TimeConfirmation::OriginalText ? "核对原文后的时间" : "个人计划时间";
}
std::string statusText(TaskStatus value) {
    switch (value) {
    case TaskStatus::Completed:
        return "已完成";
    case TaskStatus::Cancelled:
        return "已取消";
    case TaskStatus::InProgress:
        return "进行中";
    case TaskStatus::NotStarted:
        return "未开始";
    }
    throw std::invalid_argument("日历任务状态无效");
}
void alarm(icalcomponent *event, const PersonalTask &task) {
    auto value = component(ICAL_VALARM_COMPONENT);
    add(value.get(), icalproperty_new_action(ICAL_ACTION_DISPLAY));
    add(value.get(), icalproperty_new_description(normalized(task.title).c_str()));
    if (task.time.precision == TimePrecision::DateTime) {
        if (task.reminder.minutesBefore < 0 || task.reminder.minutesBefore > 525600)
            throw std::invalid_argument("日历提前提醒分钟无效");
        add(value.get(),
            icalproperty_new_trigger(icaltriggertype_from_int(-task.reminder.minutesBefore * 60)));
    } else {
        const QTimeZone zone(QByteArray::fromStdString(task.time.timeZone));
        const auto d = QDate::fromString(QString::fromStdString(task.time.date), Qt::ISODate);
        const auto clock =
            QTime::fromString(QString::fromStdString(task.reminder.dateOnlyAt), "HH:mm");
        if (!zone.isValid() || !clock.isValid() || task.reminder.daysBefore < 0 ||
            task.reminder.daysBefore > 365)
            throw std::invalid_argument("全天任务提醒时刻或时区无效");
        const QDateTime local(d.addDays(-task.reminder.daysBefore), clock, zone,
                              QDateTime::TransitionResolution::Reject);
        if (!local.isValid())
            throw std::invalid_argument("提醒钟点落在学校时区不存在或不唯一的时刻，请修改偏好");
        icaltriggertype trigger{};
        trigger.time = utc(local.toUTC().toString("yyyy-MM-ddTHH:mm:ssZ").toStdString());
        trigger.duration = icaldurationtype_null_duration();
        add(value.get(), icalproperty_new_trigger(trigger));
    }
    icalcomponent_add_component(event, value.release());
}
} // namespace
QByteArray IcsExporter::render(const std::vector<CalendarItem> &items, IcsExportOptions options) {
    if (items.empty())
        throw std::invalid_argument("没有可导出的已确认时间待办");
    auto calendar = component(ICAL_VCALENDAR_COMPONENT);
    add(calendar.get(), icalproperty_new_version("2.0"));
    add(calendar.get(), icalproperty_new_prodid("-//CampusPulse//Personal Calendar 1.0//ZH-CN"));
    add(calendar.get(), icalproperty_new_calscale("GREGORIAN"));
    std::set<std::string> identities;
    for (const auto &item : items) {
        const auto &task = item.view.task;
        if (!CalendarService::hasConfirmedTime(task))
            throw std::invalid_argument("未知或未确认时间不能导出日历");
        text(task.calendarUid);
        if (task.calendarUid.empty() ||
            task.calendarUid.find_first_of("\r\n") != std::string::npos || task.revision < 1 ||
            !identities.insert(task.calendarUid).second)
            throw std::invalid_argument("日历UID或任务版本无效或重复");
        const QUrl url(QString::fromStdString(item.view.noticeUrl), QUrl::StrictMode);
        if (!url.isValid() || url.host().isEmpty() ||
            (url.scheme() != "https" && url.scheme() != "http") || !url.userInfo().isEmpty())
            throw std::invalid_argument("关联原文网址无效");
        auto event = component(ICAL_VEVENT_COMPONENT);
        add(event.get(), icalproperty_new_uid(task.calendarUid.c_str()));
        add(event.get(), icalproperty_new_sequence(task.revision));
        add(event.get(), icalproperty_new_dtstamp(utc(task.updatedAt)));
        add(event.get(), icalproperty_new_created(utc(task.createdAt)));
        add(event.get(), icalproperty_new_lastmodified(utc(task.updatedAt)));
        auto summary = normalized(task.title);
        if (!TaskService::active(task.status))
            summary = statusText(task.status) + " · " + summary;
        add(event.get(), icalproperty_new_summary(summary.c_str()));
        std::string description = "时间来源：" + confirmationText(task.time.confirmation) +
                                  "\n时间说明：" + task.time.evidence + "\n学校时区：" +
                                  task.time.timeZone + "\n个人状态：" + statusText(task.status) +
                                  "\n\n个人备注：\n" + task.notes + "\n\n关联通知：" +
                                  item.view.noticeTitle + "\n" + item.view.noticeUrl;
        add(event.get(), icalproperty_new_description(normalized(description).c_str()));
        add(event.get(), icalproperty_new_url(url.toEncoded().constData()));
        add(event.get(),
            icalproperty_new_status(task.status == TaskStatus::Cancelled ? ICAL_STATUS_CANCELLED
                                                                         : ICAL_STATUS_CONFIRMED));
        add(event.get(), icalproperty_new_transp(ICAL_TRANSP_TRANSPARENT));
        extra(event.get(), "X-CAMPUSPULSE-TASK-STATUS", taskStatusKey(task.status));
        extra(event.get(), "X-CAMPUSPULSE-TIME-SOURCE", confirmationKey(task.time.confirmation));
        if (task.time.precision == TimePrecision::DateOnly) {
            const auto start =
                QDate::fromString(QString::fromStdString(task.time.date), Qt::ISODate);
            add(event.get(), icalproperty_new_dtstart(date(start)));
            add(event.get(), icalproperty_new_dtend(date(start.addDays(1))));
        } else {
            add(event.get(), icalproperty_new_dtstart(utc(task.time.utcDateTime)));
        }
        if (options.includeAlarms && task.reminder.enabled && TaskService::active(task.status))
            alarm(event.get(), task);
        icalcomponent_add_component(calendar.get(), event.release());
    }
    if (!icalcomponent_check_restrictions(calendar.get()))
        throw std::runtime_error("日历内容未通过RFC结构校验");
    std::unique_ptr<char, decltype(&icalmemory_free_buffer)> data(
        icalcomponent_as_ical_string_r(calendar.get()), icalmemory_free_buffer);
    if (!data)
        throw std::runtime_error("日历序列化失败");
    return QByteArray(data.get());
}
void IcsExporter::write(const QString &path, const std::vector<CalendarItem> &items,
                        IcsExportOptions options) {
    writeArtifact(path, render(items, options));
}
} // namespace campus
