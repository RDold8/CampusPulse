#include "adapters/HtmlAdapter.h"
#include "adapters/SchoolPackage.h"
#include "adapters/UniversityRegistry.h"
#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

using namespace campus;
namespace {
QByteArray fixture(const QString &school, const QString &name) {
    QFile file(QString(SCHOOL_FIXTURES) + "/" + school + "/" + name + ".html");
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Missing primary-source school fixture");
    return file.readAll();
}
SourceConfig sourceFor(const QString &school, const QString &name, const QString &url) {
    SourceConfig source;
    source.schoolId = "cn-" + school;
    source.id = name;
    source.name = name;
    source.entry = QUrl(url);
    source.allowedHosts = {source.entry.host()};
    source.autoDetect = true;
    source.allowUnknownDates = true;
    return source;
}
const Notice &findNotice(const std::vector<Notice> &rows, const QString &urlSuffix) {
    const auto found = std::find_if(rows.begin(), rows.end(), [&](const Notice &notice) {
        return QString::fromStdString(notice.url).endsWith(urlSuffix);
    });
    if (found == rows.end())
        throw std::runtime_error("Known official notice was omitted");
    return *found;
}
} // namespace

class SchoolCoverageTests final : public QObject {
    Q_OBJECT
    QJsonArray observations_;
  private slots:
    void trustedSeedsAreMinimalAndScoped() {
        UniversityRegistry registry(TEST_SCHOOL_SEEDS_DIR);
        for (const auto &school : {QString("beihua"), QString("cust")}) {
            const auto homepage = "https://www." + school + ".edu.cn/";
            const auto entry = registry.resolve(homepage);
            QCOMPARE(entry.id, "cn-" + school);
            const auto config = SchoolPackage::load(entry.configFile);
            QCOMPARE(config.discoveryEntries.size(), 9);
            QVERIFY(config.sources.empty());
            QCOMPARE(config.discoveryLimit, 24);
            QVERIFY_THROWS_EXCEPTION(std::runtime_error, registry.resolve("https://www." + school +
                                                                          ".edu.cn.evil.org/"));
        }
    }
    void publicLists_data() {
        QTest::addColumn<QString>("school");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<QString>("date");
        QTest::addColumn<QString>("titleTerm");
        QTest::addColumn<int>("minimumRows");
        QTest::newRow("beihua-academic-old-publication")
            << QStringLiteral("beihua") << QStringLiteral("academic-notices")
            << QStringLiteral("https://dean.beihua.edu.cn/index/jwgg.htm")
            << QStringLiteral("/info/1025/1754.htm") << QStringLiteral("2025-09-10")
            << QStringLiteral("2026届") << 10;
        QTest::newRow("beihua-academic-publicity")
            << QStringLiteral("beihua") << QStringLiteral("academic-publicity")
            << QStringLiteral("https://dean.beihua.edu.cn/index/gszl.htm")
            << QStringLiteral("/info/1031/1795.htm") << QStringLiteral("2026-09-18")
            << QStringLiteral("2027年") << 10;
        QTest::newRow("beihua-student-notices")
            << QStringLiteral("beihua") << QStringLiteral("student-notices")
            << QStringLiteral("https://www.beihua.edu.cn/xgzx/index/tzgg.htm")
            << QStringLiteral("/info/1044/3362.htm") << QStringLiteral("2026-04-07")
            << QStringLiteral("活动") << 20;
        QTest::newRow("beihua-scholarship")
            << QStringLiteral("beihua") << QStringLiteral("student-scholarship")
            << QStringLiteral("https://www.beihua.edu.cn/xgzx/pypj.htm")
            << QStringLiteral("/info/1017/3398.htm") << QStringLiteral("2026-05-25")
            << QStringLiteral("奖学金") << 20;
        QTest::newRow("beihua-aid-old-publication")
            << QStringLiteral("beihua") << QStringLiteral("student-aid")
            << QStringLiteral("https://www.beihua.edu.cn/xgzx/zzdk.htm")
            << QStringLiteral("/info/1015/3322.htm") << QStringLiteral("2025-09-19")
            << QStringLiteral("助学金") << 10;
        QTest::newRow("beihua-competition-and-activity")
            << QStringLiteral("beihua") << QStringLiteral("youth-notices")
            << QStringLiteral("https://youth.beihua.edu.cn/index/tzgg.htm")
            << QStringLiteral("/info/1092/5875.htm") << QStringLiteral("2026-08-21")
            << QStringLiteral("挑战杯") << 10;
        QTest::newRow("beihua-payment-procedure")
            << QStringLiteral("beihua") << QStringLiteral("finance")
            << QStringLiteral("https://jcc.beihua.edu.cn/") << QStringLiteral("/info/1006/1274.htm")
            << QStringLiteral("2024-10-16") << QStringLiteral("缴费") << 3;
        QTest::newRow("cust-main-scholarship")
            << QStringLiteral("cust") << QStringLiteral("notices")
            << QStringLiteral("https://www.cust.edu.cn/tzgg2026/index.htm")
            << QStringLiteral("/3b99787765e8465a8c5f86fb3be49009.htm")
            << QStringLiteral("2026-09-23") << QStringLiteral("奖学金") << 20;
        QTest::newRow("cust-student-publicity")
            << QStringLiteral("cust") << QStringLiteral("student-notices")
            << QStringLiteral("https://xgb.cust.edu.cn/sylm/tzgg/index.htm")
            << QStringLiteral("/0327d7d68dfa481dbef2eaea15b5cbe6.htm")
            << QStringLiteral("2026-03-17") << QStringLiteral("辅导员") << 20;
        QTest::newRow("cust-graduate-retake-bare-text-date")
            << QStringLiteral("cust") << QStringLiteral("graduate-exams")
            << QStringLiteral("https://yjs.cust.edu.cn/yytz/py/index1.htm")
            << QStringLiteral("/ce42c427fcc3461d8435dc7e258d4917.htm")
            << QStringLiteral("2026-02-26") << QStringLiteral("重修") << 20;
        QTest::newRow("cust-graduate-competition-payment")
            << QStringLiteral("cust") << QStringLiteral("graduate-work")
            << QStringLiteral("https://yjs.cust.edu.cn/yytz/yg/index.htm")
            << QStringLiteral("/04c950e6434d4224b696f9870ef38e91.htm")
            << QStringLiteral("2026-05-25") << QStringLiteral("竞赛") << 12;
        QTest::newRow("cust-career")
            << QStringLiteral("cust") << QStringLiteral("career-notices")
            << QStringLiteral("https://job.cust.edu.cn/tzgg/index.jhtml")
            << QStringLiteral("/tzgg/45483.jhtml") << QStringLiteral("2026-09-16")
            << QStringLiteral("求职创业补贴") << 10;
    }
    void publicLists() {
        QFETCH(QString, school);
        QFETCH(QString, name);
        QFETCH(QString, url);
        QFETCH(QString, suffix);
        QFETCH(QString, date);
        QFETCH(QString, titleTerm);
        QFETCH(int, minimumRows);
        const auto source = sourceFor(school, name, url);
        const auto rows = HtmlAdapter{}.parseList(fixture(school, name), source);
        QVERIFY2(static_cast<int>(rows.size()) >= minimumRows, qPrintable(name));
        const auto &known = findNotice(rows, suffix);
        QCOMPARE(QString::fromStdString(known.publishedDate), date);
        QVERIFY(QString::fromStdString(known.title).contains(titleTerm));
        int currentYear = 0;
        int unknown = 0;
        for (const auto &notice : rows) {
            QCOMPARE(QString::fromStdString(notice.schoolId), "cn-" + school);
            const auto sourceUrl = QUrl(QString::fromStdString(notice.url));
            QVERIFY(isAllowedUrl(sourceUrl, source));
            QCOMPARE(sourceUrl.scheme(), QString("https"));
            QVERIFY(!notice.title.empty());
            currentYear += QString::fromStdString(notice.publishedDate).startsWith("2026-");
            unknown += notice.publishedDate.empty();
        }
        observations_.append(QJsonObject{{"school", school},
                                         {"sample", name},
                                         {"url", url},
                                         {"rows", static_cast<int>(rows.size())},
                                         {"published_2026", currentYear},
                                         {"unknown_dates", unknown},
                                         {"verified_notice", QString::fromStdString(known.title)},
                                         {"verified_date", date}});
        qInfo().noquote() << school << name << QStringLiteral("rows=") << rows.size()
                          << QStringLiteral("published_2026=") << currentYear
                          << QStringLiteral("unknown_dates=") << unknown;
        if (name == "notices") {
            QCOMPARE(QString::fromStdString(known.title),
                     QString("长春理工大学2025-2026学年第二学期本科优秀学生奖学金获奖学生公示"));
            QVERIFY(std::any_of(rows.begin(), rows.end(), [](const Notice &notice) {
                return notice.title.find("挑战杯") != std::string::npos;
            }));
        }
        if (name == "graduate-exams" || name == "graduate-work") {
            const auto path =
                name == "graduate-exams" ? QString("/yytz/py/") : QString("/yytz/yg/");
            for (const auto &notice : rows)
                QVERIFY(QUrl(QString::fromStdString(notice.url)).path().startsWith(path));
            if (name == "graduate-exams") {
                QCOMPARE(rows.size(), size_t(20));
                QVERIFY(QString::fromStdString(rows.front().url)
                            .endsWith("/ce42c427fcc3461d8435dc7e258d4917.htm"));
            } else {
                QCOMPARE(rows.size(), size_t(12));
                QVERIFY(QString::fromStdString(rows.front().url)
                            .endsWith("/7b84a4b16ac94fa28e9761c4ac106558.htm"));
            }
        }
    }
    void publicBodies_data() {
        QTest::addColumn<QString>("school");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("term");
        QTest::newRow("beihua-academic")
            << QStringLiteral("beihua") << QStringLiteral("academic-detail")
            << QStringLiteral("https://dean.beihua.edu.cn/info/1025/1754.htm")
            << QStringLiteral("2026");
        QTest::newRow("beihua-aid")
            << QStringLiteral("beihua") << QStringLiteral("student-aid-detail")
            << QStringLiteral("https://www.beihua.edu.cn/xgzx/info/1015/3322.htm")
            << QStringLiteral("5000");
        QTest::newRow("beihua-competition")
            << QStringLiteral("beihua") << QStringLiteral("youth-detail")
            << QStringLiteral("https://youth.beihua.edu.cn/info/1092/5875.htm")
            << QStringLiteral("挑战杯");
        QTest::newRow("beihua-payment")
            << QStringLiteral("beihua") << QStringLiteral("finance-detail")
            << QStringLiteral("https://jcc.beihua.edu.cn/info/1006/1274.htm")
            << QStringLiteral("缴费");
        QTest::newRow("cust-main")
            << QStringLiteral("cust") << QStringLiteral("home-detail")
            << QStringLiteral(
                   "https://www.cust.edu.cn/tzgg2026/e361d9deee904c7e9d76e9896e122ba3.htm")
            << QStringLiteral("税号");
        QTest::newRow("cust-student")
            << QStringLiteral("cust") << QStringLiteral("student-detail")
            << QStringLiteral(
                   "https://xgb.cust.edu.cn/sylm/tzgg/0327d7d68dfa481dbef2eaea15b5cbe6.htm")
            << QStringLiteral("辅导员");
        QTest::newRow("cust-graduate-retake")
            << QStringLiteral("cust") << QStringLiteral("graduate-detail")
            << QStringLiteral(
                   "https://yjs.cust.edu.cn/yytz/py/ce42c427fcc3461d8435dc7e258d4917.htm")
            << QStringLiteral("重修");
        QTest::newRow("cust-graduate-work-first")
            << QStringLiteral("cust") << QStringLiteral("graduate-work-first-detail")
            << QStringLiteral(
                   "https://yjs.cust.edu.cn/yytz/yg/7b84a4b16ac94fa28e9761c4ac106558.htm")
            << QStringLiteral("报名时间");
        QTest::newRow("cust-competition-payment")
            << QStringLiteral("cust") << QStringLiteral("competition-payment-detail")
            << QStringLiteral(
                   "https://yjs.cust.edu.cn/yytz/yg/04c950e6434d4224b696f9870ef38e91.htm")
            << QStringLiteral("缴费");
        QTest::newRow("cust-career")
            << QStringLiteral("cust") << QStringLiteral("career-detail")
            << QStringLiteral("https://job.cust.edu.cn/tzgg/45483.jhtml") << QStringLiteral("补贴");
    }
    void publicBodies() {
        QFETCH(QString, school);
        QFETCH(QString, name);
        QFETCH(QString, url);
        QFETCH(QString, term);
        auto source = sourceFor(school, name, url);
        Notice notice;
        notice.schoolId = source.schoolId.toStdString();
        notice.url = url.toStdString();
        notice.title = term.toStdString();
        Notice parsed;
        try {
            parsed = HtmlAdapter{}.parseDetail(fixture(school, name), source, notice);
        } catch (const std::exception &error) {
            QFAIL(error.what());
        }
        QVERIFY(QString::fromStdString(parsed.body).contains(term));
        QVERIFY(parsed.body.size() >= 30);
        if (name == "graduate-work-first-detail")
            QVERIFY(!parsed.attachments.empty());
        if (name == "competition-payment-detail") {
            const auto body = QString::fromStdString(parsed.body);
            // The source states the year at the range start and omits it at
            // the end: 2026年6月1日 ... 至9月21日 ... 17:00.
            QVERIFY(body.contains("2026年6月1日"));
            QVERIFY(body.contains("至9月21日"));
            QVERIFY(body.contains("17:00"));
            QVERIFY(body.contains("网上缴费起止时间"));
        }
    }
    void loginAndDynamicShellsAreNotNotices() {
        const auto locked = sourceFor("cust", "academic", "https://jwc.cust.edu.cn/");
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 HtmlAdapter{}.parseList(fixture("cust", "academic"), locked));
        const auto dynamic =
            sourceFor("cust", "career-brief", "https://job.cust.edu.cn/brief/index.jhtml");
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 HtmlAdapter{}.parseList(fixture("cust", "career-brief"), dynamic));
    }
    void titleYearsAndUnsafeUrlsDoNotBecomePublicationEvidence() {
        auto source = sourceFor("beihua", "boundaries", "https://dean.beihua.edu.cn/");
        source.allowedHosts << "youth.beihua.edu.cn";
        const QByteArray html =
            "<ul><li><a href='/info/1/1.htm'><span>2027年01月02日重修课程通知</span></a></li>"
            "<li><a href='/info/1/2.htm' data-time='2026-09-30 17:30'>2027届奖学金通知</a></li>"
            "<li><a href='http://dean.beihua.edu.cn/info/1/3.htm'>关于竞赛报名的通知</a></li>"
            "<li><a href='http://youth.beihua.edu.cn/info/1/4.htm'>HTTP其他host通知</a></li>"
            "<li><a href='https://user@dean.beihua.edu.cn/info/1/5.htm'>认证信息通知</a></li>"
            "<li><a href='https://dean.beihua.edu.cn:443/info/1/6.htm'>显式端口通知</a></li>"
            "<li><a "
            "href='https://dean.beihua.edu.cn.evil.org/info/1/7.htm'>伪造域名通知</a></li></ul>";
        const auto rows = HtmlAdapter{}.parseList(html, source);
        QCOMPARE(rows.size(), size_t(3));
        QVERIFY(findNotice(rows, "/info/1/1.htm").publishedDate.empty());
        QCOMPARE(findNotice(rows, "/info/1/2.htm").publishedDate, std::string("2026-09-30"));
        QCOMPARE(findNotice(rows, "/info/1/3.htm").url,
                 std::string("https://dean.beihua.edu.cn/info/1/3.htm"));
    }
    void unrelatedSidebarDoesNotBecomeFirstColumnNotice() {
        const auto source =
            sourceFor("cust", "graduate-exams", "https://yjs.cust.edu.cn/yytz/py/index1.htm");
        const QByteArray html =
            "<aside><ul><li><a href='/xkjs/pggz/111111111111111111111111.htm'>侧栏学科评估通知</a>"
            "<span>2026-09-30</span></li></ul></aside><main><ul>"
            "<li><a "
            "href='/yytz/py/aaaaaaaaaaaaaaaaaaaaaaaa.htm'>本栏目重修申请通知</a><span>2026-02-26</"
            "span></li>"
            "<li><a "
            "href='/yytz/py/bbbbbbbbbbbbbbbbbbbbbbbb.htm'>本栏目考试安排通知</a><span>2025-12-18</"
            "span></li>"
            "<li><a "
            "href='/yytz/py/cccccccccccccccccccccccc.htm'>本栏目课程安排通知</a><span>2025-12-17</"
            "span></li>"
            "</ul></main>";
        const auto rows = HtmlAdapter{}.parseList(html, source);
        QCOMPARE(rows.size(), size_t(3));
        QVERIFY(QString::fromStdString(rows.front().title).contains("重修"));
        QCOMPARE(rows.front().publishedDate, std::string("2026-02-26"));
        for (const auto &notice : rows)
            QVERIFY(QUrl(QString::fromStdString(notice.url)).path().startsWith("/yytz/py/"));
    }
    void cleanupTestCase() {
        if (observations_.isEmpty())
            return;
        QSaveFile report(QString(EVIDENCE_DIR) + "/new-schools-offline-observations.json");
        QVERIFY(report.open(QIODevice::WriteOnly));
        QVERIFY(report.write(QJsonDocument(observations_).toJson(QJsonDocument::Indented)) > 0);
        QVERIFY(report.commit());
    }
};
QTEST_GUILESS_MAIN(SchoolCoverageTests)
#include "SchoolCoverageTests.moc"
