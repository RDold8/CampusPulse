#include "application/NoticeMatcher.h"
#include "application/NoticeClassifier.h"
#include "domain/Theme.h"
#include "desktop/NoticeListModel.h"
#include <QtTest>
#include <algorithm>
using namespace campus;
class ThemeCoverageTests final : public QObject {
    Q_OBJECT
  private slots:
    void cachedPaymentAndMultipleThemes() {
        Notice notice;
        notice.title = "华为杯数学建模竞赛报名缴费通知";
        notice.publishedDate = "2026-05-25";
        notice.tags = {"competition"}; // An existing cache created before payment became a theme.
        NoticeQuery query;
        query.yearPolicy = YearPolicy::AllYears;
        query.themeKeys = {"payment"};
        QVERIFY(knownTheme("payment"));
        QVERIFY(NoticeMatcher::matches(notice, query, 2026));
        query.themeKeys = {"competition"};
        QVERIFY(NoticeMatcher::matches(notice, query, 2026));
        query.themeKeys = {"retake_payment"};
        QVERIFY(!NoticeMatcher::matches(notice, query, 2026));
        notice.title = "补考报名缴费通知";
        notice.tags.clear();
        query.themeKeys = {"exam"};
        QVERIFY(NoticeMatcher::matches(notice, query, 2026));
        query.themeKeys = {"payment"};
        QVERIFY(NoticeMatcher::matches(notice, query, 2026));
        notice.title = "华为杯数学建模竞赛通知";
        notice.body = "报名后请按原文说明缴费。";
        QVERIFY(NoticeMatcher::matches(notice, query, 2026));
    }
    void activitiesAndOutcomes() {
        Notice notice;
        NoticeQuery query;
        query.yearPolicy = YearPolicy::AllYears;
        query.themeKeys = {"campus_activity"};
        notice.title = "校园文化节及志愿服务活动";
        QVERIFY(NoticeMatcher::matches(notice, query, 2026));
        const auto classified = NoticeClassifier::classify("挑战杯竞赛获奖名单公示");
        QVERIFY(std::find(classified.tags.begin(), classified.tags.end(), "competition") !=
                classified.tags.end());
        QVERIFY(std::find(classified.stages.begin(), classified.stages.end(), "registration") ==
                classified.stages.end());
        notice.title = "2024年缴费流程";
        notice.publishedDate = "2024-10-16";
        query.themeKeys = {"payment"};
        query.yearPolicy = YearPolicy::CurrentYear;
        QVERIFY(!NoticeMatcher::matches(notice, query, 2026));
    }
    void practiceAndTuitionAidAreDiscoverable() {
        Notice notice;
        NoticeQuery query;
        query.yearPolicy = YearPolicy::AllYears;
        query.themeKeys = {"campus_activity"};
        notice.title = "吉林大学社会实践项目报名通知";
        QVERIFY(NoticeMatcher::matches(notice, query, 2026));
        query.themeKeys = {"scholarship"};
        notice.title = "关于做好本科生学费全额减免工作的通知";
        QVERIFY(NoticeMatcher::matches(notice, query, 2026));
    }
    void cachedLabelsFollowCurrentTitleRules() {
        Notice notice;
        notice.category = "academic_affairs";
        notice.title = "关于本科生学费全额减免的通知";
        NoticeListModel model;
        model.setNotices({notice});
        QCOMPARE(model.index(0, 2).data().toString(), QString("奖助学金"));
        notice.title = "社会实践项目报名通知";
        model.setNotices({notice});
        QCOMPARE(model.index(0, 2).data().toString(), QString("校园活动"));
        notice.title = "重修补考缴费通知";
        model.setNotices({notice});
        QCOMPARE(model.index(0, 2).data().toString(), QString("重修 / 补考 / 缴费"));
    }
};
QTEST_GUILESS_MAIN(ThemeCoverageTests)
#include "ThemeCoverageTests.moc"
