#include "adapters/IcsExporter.h"
#include "application/CalendarService.h"
#include "storage/Database.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteTaskRepository.h"
#include <QDateTime>
#include <QFile>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>
#include <ical.h>
#include <memory>

using namespace campus;
namespace {
std::string project(const std::string &value, const std::string &zone) {
    return QDateTime::fromString(QString::fromStdString(value), Qt::ISODate)
        .toTimeZone(QTimeZone(QByteArray::fromStdString(zone)))
        .date()
        .toString(Qt::ISODate)
        .toStdString();
}
struct Session {
    QTemporaryDir folder;
    Database db;
    SqliteRepository noticeRepository;
    SqliteTaskRepository taskRepository;
    NoticeService notices;
    TaskService tasks;
    CalendarService calendar;
    Session()
        : db(folder.filePath("calendar.sqlite")), noticeRepository(db), taskRepository(db),
          notices(noticeRepository, "test-a"),
          tasks("test-a", "Asia/Shanghai", taskRepository, notices,
                [] { return "2026-10-03T02:00:00Z"; }),
          calendar(tasks, "Asia/Shanghai", project) {
        Notice n;
        n.id = "notice-a";
        n.schoolId = "test-a";
        n.title = "【演示】缴费、竞赛报名与活动通知";
        n.url = "https://school.example.edu.cn/info/123/456.htm?a=1&b=2";
        n.sourceId = "academic";
        n.sourceName = "演示教务";
        n.publishedDate = "2026-10-01";
        notices.ingest({n});
        auto other = n;
        other.schoolId = "test-b";
        other.id = "notice-b";
        noticeRepository.upsertBatch({other});
    }
    PersonalTask saveDate(const std::string &title, const std::string &date = "2026-10-06") {
        auto task = tasks.draft("notice-a");
        task.title = title;
        task.time.precision = TimePrecision::DateOnly;
        task.time.date = date;
        task.time.confirmation = TimeConfirmation::Personal;
        task.time.evidence = "【演示】用户已核对；不代表真实学校截止时间";
        return tasks.save(task, true);
    }
    PersonalTask saveExact(const std::string &time = "2026-10-05T16:30:00Z") {
        auto task = tasks.draft("notice-a");
        task.title = "【演示】竞赛报名准确时刻";
        task.time.precision = TimePrecision::DateTime;
        task.time.utcDateTime = time;
        task.time.confirmation = TimeConfirmation::OriginalText;
        task.time.evidence = "【演示摘录】已核对准确时刻";
        return tasks.save(task, true);
    }
};
using Parsed = std::unique_ptr<icalcomponent, decltype(&icalcomponent_free)>;
Parsed parse(const QByteArray &data) {
    return Parsed(icalparser_parse_string(data.constData()), icalcomponent_free);
}
icalproperty *calendarProperty(icalcomponent *component, icalproperty_kind kind) {
    return icalcomponent_get_first_property(component, kind);
}
} // namespace
class CalendarTests final : public QObject {
    Q_OBJECT
  private slots:
    void projectionUsesTaskFactsAndSchoolTimeZone() {
        Session s;
        const auto first = s.saveDate("【演示】重修缴费");
        const auto precise = s.saveExact();
        auto unknown = s.tasks.draft("notice-a");
        unknown.title = "【演示】奖学金申请日期待定";
        s.tasks.save(unknown);
        auto done = s.saveDate("【演示】已完成活动");
        s.tasks.setStatus(done.id, TaskStatus::Completed);
        auto cancelled = s.saveDate("【演示】已取消活动");
        s.tasks.setStatus(cancelled.id, TaskStatus::Cancelled);
        QCOMPARE(s.calendar.month(2026, 10).size(), size_t(2));
        QCOMPARE(s.calendar.day("2026-10-06").size(), size_t(2));
        QCOMPARE(s.calendar.month(2026, 10, true).size(), size_t(4));
        QCOMPARE(s.calendar.undatedCount(), size_t(1));
        auto moved = s.tasks.find(first.id);
        moved.time.date = "2026-11-02";
        s.tasks.save(moved, true);
        QCOMPARE(s.calendar.month(2026, 10).size(), size_t(1));
        QCOMPARE(s.calendar.month(2026, 11).at(0).view.task.id, first.id);
        CalendarService utc(s.tasks, "UTC", project);
        QCOMPARE(utc.day("2026-10-05").at(0).view.task.id, precise.id);
        QVERIFY_EXCEPTION_THROWN(s.calendar.month(2026, 13), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(s.calendar.day("2026-02-30"), std::invalid_argument);
        NoticeService otherNotices(s.noticeRepository, "test-b");
        TaskService otherTasks("test-b", "Asia/Shanghai", s.taskRepository, otherNotices);
        CalendarService otherCalendar(otherTasks, "Asia/Shanghai", project);
        QVERIFY(otherCalendar.items(true).empty());
        const auto nextMonth = s.saveExact("2026-10-31T17:30:00Z");
        QCOMPARE(s.calendar.day("2026-11-01").at(0).view.task.id, nextMonth.id);
        QCOMPARE(utc.day("2026-10-31").at(0).view.task.id, nextMonth.id);
    }
    void allDayAndUtcPointEventsRoundTrip() {
        Session s;
        const auto allDay = s.saveDate("【演示】申请材料");
        const auto exact = s.saveExact();
        const auto data = IcsExporter::render(s.calendar.month(2026, 10));
        QVERIFY(data.startsWith("BEGIN:VCALENDAR\r\n"));
        QVERIFY(data.endsWith("END:VCALENDAR\r\n"));
        QVERIFY(data.contains("DTSTART;VALUE=DATE:20261006\r\n"));
        QVERIFY(data.contains("DTEND;VALUE=DATE:20261007\r\n"));
        QVERIFY(data.contains("DTSTART:20261005T163000Z\r\n"));
        QCOMPARE(data.count("DTEND"), 1);
        QVERIFY(!data.contains("235959"));
        auto parsed = parse(data);
        QVERIFY(parsed);
        QVERIFY(icalcomponent_check_restrictions(parsed.get()));
        QCOMPARE(icalcomponent_count_components(parsed.get(), ICAL_VEVENT_COMPONENT), 2);
        auto *event = icalcomponent_get_first_component(parsed.get(), ICAL_VEVENT_COMPONENT);
        QCOMPARE(std::string(icalproperty_get_uid(calendarProperty(event, ICAL_UID_PROPERTY))),
                 allDay.calendarUid);
        QCOMPARE(icalproperty_get_sequence(calendarProperty(event, ICAL_SEQUENCE_PROPERTY)),
                 allDay.revision);
        QVERIFY(icalproperty_get_dtstart(calendarProperty(event, ICAL_DTSTART_PROPERTY)).is_date);
        QCOMPARE(std::string(icalproperty_get_url(calendarProperty(event, ICAL_URL_PROPERTY))),
                 s.notices.list().at(0).url);
        event = icalcomponent_get_next_component(parsed.get(), ICAL_VEVENT_COMPONENT);
        QVERIFY(!calendarProperty(event, ICAL_DTEND_PROPERTY));
        QCOMPARE(std::string(icalproperty_get_uid(calendarProperty(event, ICAL_UID_PROPERTY))),
                 exact.calendarUid);
        const auto description = QString::fromUtf8(
            icalproperty_get_description(calendarProperty(event, ICAL_DESCRIPTION_PROPERTY)));
        QVERIFY(description.contains("核对原文后的时间"));
        QVERIFY(description.contains("学校时区：Asia/Shanghai"));
#ifdef EVIDENCE_DIR
        IcsExporter::write(QString(EVIDENCE_DIR) + "/stage4-calendar-demo.ics",
                           s.calendar.month(2026, 10));
#endif
    }
    void utf8EscapingStableUidAndVersion() {
        Session s;
        const auto title = (QString("【演示】中文重修缴费竞赛申请材料").repeated(8) +
                            ",分隔;反斜线\\\nSTATUS:CANCELLED")
                               .toStdString();
        auto task = s.saveDate(title);
        task.notes = "备注,含逗号;分号\\换行\r\nBEGIN:VEVENT";
        task = s.tasks.save(task);
        const auto first = IcsExporter::render(s.calendar.items());
        QCOMPARE(first, IcsExporter::render(s.calendar.items()));
        QCOMPARE(s.tasks.find(task.id).revision, task.revision);
        for (auto line : first.split('\n')) {
            if (line.endsWith('\r'))
                line.chop(1);
            QVERIFY2(line.size() <= 75, line.constData());
            QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
            const QString decoded = decoder(line);
            Q_UNUSED(decoded);
            QVERIFY(!decoder.hasError());
        }
        auto parsed = parse(first);
        QVERIFY(parsed);
        QCOMPARE(icalcomponent_count_components(parsed.get(), ICAL_VEVENT_COMPONENT), 1);
        auto *event = icalcomponent_get_first_component(parsed.get(), ICAL_VEVENT_COMPONENT);
        QCOMPARE(
            std::string(icalproperty_get_summary(calendarProperty(event, ICAL_SUMMARY_PROPERTY))),
            title);
        QCOMPARE(icalproperty_get_status(calendarProperty(event, ICAL_STATUS_PROPERTY)),
                 ICAL_STATUS_CONFIRMED);
        task.time.date = "2026-10-08";
        task = s.tasks.save(task, true);
        const auto second = IcsExporter::render(s.calendar.items());
        QVERIFY(second.contains(QByteArray::fromStdString("UID:" + task.calendarUid)));
        QVERIFY(second.contains(
            QByteArray::fromStdString("SEQUENCE:" + std::to_string(task.revision))));
        QVERIFY(second.contains("DTSTART;VALUE=DATE:20261008"));
        auto changed = parse(second);
        event = icalcomponent_get_first_component(changed.get(), ICAL_VEVENT_COMPONENT);
        QCOMPARE(std::string(icalproperty_get_uid(calendarProperty(event, ICAL_UID_PROPERTY))),
                 task.calendarUid);
    }
    void optionalAlarmsAndHistoricalStatuses() {
        Session s;
        auto allDay = s.saveDate("【演示】缴费提醒");
        allDay.reminder.enabled = true;
        allDay.reminder.daysBefore = 1;
        allDay.reminder.dateOnlyAt = "09:00";
        s.tasks.save(allDay);
        auto exact = s.saveExact("2026-10-06T00:30:00Z");
        exact.reminder.enabled = true;
        exact.reminder.minutesBefore = 60;
        s.tasks.save(exact);
        QVERIFY(!IcsExporter::render(s.calendar.items()).contains("BEGIN:VALARM"));
        const auto alarmed = IcsExporter::render(s.calendar.items(), {true});
        QCOMPARE(alarmed.count("BEGIN:VALARM"), 2);
        QVERIFY(alarmed.contains("TRIGGER;VALUE=DATE-TIME:20261005T010000Z"));
        QVERIFY(alarmed.contains("TRIGGER:-PT1H"));
        auto ambiguous = s.calendar.items();
        ambiguous.resize(1);
        ambiguous.at(0).view.task.time.timeZone = "America/New_York";
        ambiguous.at(0).view.task.time.date = "2026-11-01";
        ambiguous.at(0).view.task.reminder.daysBefore = 0;
        ambiguous.at(0).view.task.reminder.dateOnlyAt = "01:30";
        QVERIFY_EXCEPTION_THROWN(IcsExporter::render(ambiguous, {true}), std::invalid_argument);
        s.tasks.setStatus(allDay.id, TaskStatus::Completed);
        s.tasks.setStatus(exact.id, TaskStatus::Cancelled);
        const auto history = IcsExporter::render(s.calendar.items(true), {true});
        QVERIFY(!history.contains("BEGIN:VALARM"));
        QVERIFY(history.contains("STATUS:CANCELLED"));
        QVERIFY(history.contains("X-CAMPUSPULSE-TASK-STATUS:completed"));
        QVERIFY(history.contains("X-CAMPUSPULSE-TASK-STATUS:cancelled"));
    }
    void unsafeDatesAndFileFailureRemainVisible() {
        Session s;
        s.saveDate("【演示】导出检查");
        auto values = s.calendar.items();
        values.at(0).view.task.time.confirmation = TimeConfirmation::None;
        QVERIFY_EXCEPTION_THROWN(IcsExporter::render(values), std::invalid_argument);
        values = s.calendar.items();
        values.at(0).view.task.time.confirmedAt.clear();
        QVERIFY_EXCEPTION_THROWN(IcsExporter::render(values), std::invalid_argument);
        values.at(0).view.task.time.precision = TimePrecision::Unknown;
        QVERIFY_EXCEPTION_THROWN(IcsExporter::render(values), std::invalid_argument);
        values = s.calendar.items();
        values.at(0).view.task.title = std::string(1, static_cast<char>(0xc3));
        QVERIFY_EXCEPTION_THROWN(IcsExporter::render(values), std::invalid_argument);
        values = s.calendar.items();
        values.at(0).view.task.title = std::string("invalid\0title", 13);
        QVERIFY_EXCEPTION_THROWN(IcsExporter::render(values), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(IcsExporter::render({}), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(IcsExporter::write(s.folder.path(), s.calendar.items()),
                                 std::runtime_error);
        const auto path = s.folder.filePath("calendar.ics");
        IcsExporter::write(path, s.calendar.items());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), IcsExporter::render(s.calendar.items()));
    }
};
QTEST_GUILESS_MAIN(CalendarTests)
#include "CalendarTests.moc"
