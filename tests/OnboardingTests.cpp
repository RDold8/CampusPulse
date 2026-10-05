#include "adapters/HtmlAdapter.h"
#include "adapters/SchoolOnboarding.h"
#include "adapters/DeepSeekSearch.h"
#include "adapters/UniversityRegistry.h"
#include "application/NoticeService.h"
#include "application/SubscriptionService.h"
#include "application/TaskService.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteSubscriptionRepository.h"
#include "storage/SqliteTaskRepository.h"
#include <QtTest>
#include <QFile>
#include <QDate>
#include <QTemporaryDir>
#include <QDir>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <algorithm>
using namespace campus;
namespace {
QByteArray fixture(const QString &name) {
    QFile file(QString(JLU_FIXTURES) + "/" + name + ".html");
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Missing JLU fixture");
    return file.readAll();
}
SourceConfig automatic(const QString &name, const QString &url) {
    SourceConfig s;
    s.schoolId = "cn-jlu";
    s.id = name;
    s.name = name;
    s.entry = QUrl(url);
    s.allowedHosts = {s.entry.host()};
    s.autoDetect = true;
    return s;
}
QString unknownSeed(const QString &folder) {
    const QJsonObject seed{
        {"schema_version", "0.1-draft"},
        {"school", QJsonObject{{"key", "cn-auto-test"}, {"name", "测试大学"},
            {"timezone", "Asia/Shanghai"}, {"official_homepage", "https://www.example.edu.cn/"},
            {"identity_provenance", "automatic_homepage"}}},
        {"sources", QJsonArray{}},
        {"auto_discovery", QJsonObject{{"department_urls", QJsonArray{}}, {"max_pages", 24}}}};
    const auto path = QDir(folder).filePath("unknown-seed.json");
    QFile file(path);
    const auto bytes = QJsonDocument(seed).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("Cannot write synthetic school seed");
    return path;
}
QByteArray departmentList(int count = 3, const QString &absoluteHost = {}) {
    QByteArray html = "<html><title>测试大学教务处</title><h2>通知公告</h2><ul>";
    for (int i = 1; i <= count; ++i) {
        const auto url = (absoluteHost.isEmpty() ? QString{} : "https://" + absoluteHost) +
                         QString("/info/1001/%1.htm").arg(i);
        html += QString("<li><a href='%1'>正式教务通知第%2条</a></li>").arg(url).arg(i).toUtf8();
    }
    return html + "</ul><a href='gztz.htm'>更多</a></html>";
}
QByteArray validBody() {
    return "<html><title>正式教务通知</title><div class='v_news_content'><p>"
           "请同学们依据学校教务处公布的正式要求办理有关事项，并分别核对报名、缴费和结果。"
           "</p></div></html>";
}
QJsonObject objectFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Missing synthetic onboarding result");
    return QJsonDocument::fromJson(file.readAll()).object();
}
} // namespace
class OnboardingTests final : public QObject {
    Q_OBJECT
  private slots:
    void studentServiceGuidesAreTraversedAsResourcesRatherThanNoticeSources() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        QVERIFY(QDir().mkpath(scan.directory_ + "/samples"));
        QFile file(QString(GENERAL_TEMPLATE_FIXTURES) + "/vsb-student-menu.html");
        QVERIFY(file.open(QIODevice::ReadOnly));
        scan.consume({QUrl("https://jwc.example.edu.cn/"), "教务处", 1, true}, file.readAll(), {});
        for (const auto &path : {QString("/xsfw1/xkhtk.htm"), QString("/xsfw1/kshcj.htm")}) {
            const auto page = std::find_if(scan.queue_.begin(), scan.queue_.end(), [&](const auto &entry) {
                return entry.url.path() == path;
            });
            QVERIFY(page != scan.queue_.end());
            QVERIFY(!page->candidate);
        }
        QFile list(QString(GENERAL_TEMPLATE_FIXTURES) + "/vsb-service-exams.html");
        QVERIFY(list.open(QIODevice::ReadOnly));
        const int sourceCount = scan.sources_.size();
        scan.consume({QUrl("https://jwc.example.edu.cn/xsfw1/kshcj.htm"), "考试和成绩", 2, false},
                     list.readAll(), {});
        QCOMPARE(scan.sources_.size(), sourceCount);
        for (const auto &page : scan.queue_)
            QVERIFY(!page.url.toString().contains("转换链接错误"));
    }
    void collegeDirectoryAndWelcomePortalAreGeneralDiscoveryEntries() {
        for (const auto &label : {QString("学部学院"), QString("学部与学院"), QString("院系设置"),
                                  QString("教学单位"), QString("大连凌水主校区"), QString("迎新网")})
            QVERIFY2(SchoolOnboarding::isDiscoveryLabel(label), qPrintable(label));
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        scan.enqueue(QUrl("https://www.example.edu.cn/colleges"), "学部学院", 1, false);
        scan.enqueue(QUrl("https://www.example.edu.cn/welcome"), "迎新网", 1, true);
        QCOMPARE(scan.takeNext().url.path(), QString("/colleges"));
    }
    void fullQueueKeepsAnUnvisitedDepartmentAheadOfRepeatedHostMenus() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        for (int i = 0; i < 48; ++i)
            scan.enqueue(QUrl(QString("https://jwc.example.edu.cn/section/%1").arg(i)), "通知公告", 2, true);
        scan.hostRequests_["jwc.example.edu.cn"] = 5;
        scan.enqueue(QUrl("https://welcome.example.edu.cn/"), "迎新网", 1, true);
        QCOMPARE(scan.queue_.size(), size_t(48));
        QCOMPARE(scan.takeNext().url.host(), QString("welcome.example.edu.cn"));
    }
    void realHomepageCarouselStillEnablesPublicRetakeNotices() {
        QFile file(QString(GENERAL_TEMPLATE_FIXTURES) + "/webplus-carousel-and-notices.html");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        QVERIFY(QDir().mkpath(scan.directory_ + "/samples"));
        SchoolOnboarding::Page home{QUrl("https://jwc.example.edu.cn/"), "本科生院", 1, true};
        scan.consume(home, file.readAll(), {});
        QVERIFY(scan.queue_.front().detail.has_value());
        QVERIFY(QString::fromStdString(scan.queue_.front().detail->title).contains("重修报名"));
        const auto check = scan.queue_.front(); scan.queue_.pop_front();
        scan.consume(check, validBody(), {});
        QCOMPARE(scan.ready_, 1);
    }
    void publicityCarouselCannotExhaustAllBodyChecksBeforeNotices() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        QVERIFY(QDir().mkpath(scan.directory_ + "/samples"));
        SchoolOnboarding::Page home{QUrl("https://jwc.example.edu.cn/"), "本科生院", 1, true};
        const QByteArray html(R"(<title>测试大学本科生院</title><div class='mbanner'><ul>
          <li><a href='/2025/1223/c18897a295175/page.htm'>大学宣传图片一</a></li>
          <li><a href='/2025/0526/c18897a284379/page.htm'>大学宣传图片二</a></li>
          <li><a href='/2025/0526/c18897a284377/page.htm'>大学宣传图片三</a></li>
          </ul></div><h2>通知公告</h2><ul>
          <li><a href='/2026/0829/c1594a307692/page.htm'>本学期重修报名通知</a></li>
          <li><a href='/2026/0901/c1594a307693/page.htm'>课程考试报名通知</a></li>
          <li><a href='/2026/0902/c1594a307694/page.htm'>本年度奖学金申请通知</a></li>
          </ul></html>)");
        scan.consume(home, html, {});
        QCOMPARE(scan.counts_.value(scan.sources_.first().toObject().value("key").toString()), 6);
        QVERIFY(scan.queue_.front().detail.has_value());
        QVERIFY(QString::fromStdString(scan.queue_.front().detail->title).contains("重修报名"));
        const auto check = scan.queue_.front(); scan.queue_.pop_front();
        scan.consume(check, validBody(), {});
        QCOMPARE(scan.ready_, 1);
        QCOMPARE(scan.verifiedNotices().size(), size_t(1));
        QVERIFY(!scan.verifiedNotices().front().body.empty());
    }
    void departmentNoticeColumnPrecedesGenericAdministrativeMenus() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        scan.enqueue(QUrl("https://jwc.example.edu.cn/forms"), "教学管理事务", 2, true);
        scan.enqueue(QUrl("https://jwc.example.edu.cn/notices"), "通知公告 · 更多", 2, true);
        QCOMPARE(scan.takeNext().url.path(), QString("/notices"));
    }
    void departmentMenuCannotStarveFinanceAndStudentSupport() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        scan.enqueue(QUrl("https://jwc.example.edu.cn/courses"), "教学管理", 2, true);
        scan.enqueue(QUrl("https://finance.example.edu.cn/"), "财务处", 2, true);
        scan.enqueue(QUrl("https://support.example.edu.cn/"), "学生指导服务中心", 2, true);
        scan.hostRequests_["jwc.example.edu.cn"] = 5;
        QVERIFY(scan.takeNext().url.host() != "jwc.example.edu.cn");
        QVERIFY(scan.takeNext().url.host() != "jwc.example.edu.cn");
        QCOMPARE(scan.takeNext().url.host(), QString("jwc.example.edu.cn"));
    }
    void duplicateRedirectTargetsDoNotCreateDuplicateSourceKeys() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        QVERIFY(QDir().mkpath(scan.directory_ + "/samples"));
        SchoolOnboarding::Page home{QUrl("https://jwc.example.edu.cn/"), "本科生院", 1, true};
        scan.consume(home, departmentList(), {});
        scan.consume(home, departmentList(), {});
        QCOMPARE(scan.sources_.size(), 1);
        QCOMPARE(scan.queue_.size(), size_t(1));
        const auto body = scan.queue_.front(); scan.queue_.pop_front();
        scan.consume(body, validBody(), {});
        scan.consume(home, departmentList(), {});
        QCOMPARE(scan.sources_.size(), 1);
        QCOMPARE(scan.ready_, 1);
        QVERIFY(scan.sources_.first().toObject().value("enabled").toBool());
        scan.finish();
        QCOMPARE(SchoolPackage::load(scan.directory_ + "/school.json").sources.size(), size_t(1));
    }
    void generalDepartmentsAndArticlesDoNotConsumeNavigationAsLists() {
        for (const auto &label : {QString("内设机构"), QString("党政群部门"), QString("本科"),
                                  QString("研究生"), QString("学生指导服务中心"),
                                  QString("计划财经处"), QString("通知通告")})
            QVERIFY2(SchoolOnboarding::isDiscoveryLabel(label), qPrintable(label));
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        for (const auto &url : {QString("https://jwc.example.edu.cn/2026/0928/c10a20/page.htm"),
                                QString("https://today.example.edu.cn/article/2026/09/28/20"),
                                QString("https://jwc.example.edu.cn/context.jsp?wbnewsid=20&urltype=news.NewsContentUrl"),
                                QString("https://jwc.example.edu.cn/contest.xlsx")})
            scan.enqueue(QUrl(url), "学科竞赛", 1, true);
        QVERIFY(scan.queue_.empty());
        scan.enqueue(QUrl("https://jwc.example.edu.cn"), "本科", 1, true);
        scan.enqueue(QUrl("https://jwc.example.edu.cn/"), "本科", 1, true);
        QCOMPARE(scan.queue_.size(), size_t(1));
    }
    void mixedPublicListTriesAnotherBodyWithoutMarkingWholeListPrivate() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        QVERIFY(QDir().mkpath(scan.directory_ + "/samples"));
        SchoolOnboarding::Page home{QUrl("https://jwc.example.edu.cn/"), "本科生院", 1, true};
        scan.consume(home, departmentList(), {});
        const auto first = scan.queue_.front(); scan.queue_.pop_front();
        scan.consume(first, "<title>统一身份认证</title><input type='password'>", {});
        QCOMPARE(scan.ready_, 0);
        QCOMPARE(scan.queue_.size(), size_t(1));
        const auto second = scan.queue_.front(); scan.queue_.pop_front();
        QVERIFY(second.url != first.url);
        scan.consume(second, validBody(), {});
        QCOMPARE(scan.ready_, 1);
        QVERIFY(scan.sources_.first().toObject().value("enabled").toBool());
        QVERIFY(!scan.sources_.first().toObject().contains("access"));
    }
    void permissionStubCannotVerifyPublicSource() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        QVERIFY(QDir().mkpath(scan.directory_ + "/samples"));
        SchoolOnboarding::Page home{QUrl("https://jwc.example.edu.cn/"), "本科生院", 1, true};
        scan.consume(home, "<title>系统提示</title><p>您没有访问当前栏目的权限</p>", {});
        QCOMPARE(scan.ready_, 0);
        QVERIFY(!scan.sources_.first().toObject().value("enabled").toBool());
        QCOMPARE(scan.sources_.first().toObject().value("access").toObject().value("mode").toString(),
                 QString("login_required"));
    }
    void splitPublicationDateDoesNotBorrowTitleOrSummaryDates() {
        auto source = automatic("cards", "https://jwc.jlu.edu.cn/");
        source.allowUnknownDates = true;
        const QByteArray html(
            "<li><a href='/info/1/1.htm'><h5>08</h5><span>2026.07</span>"
            "<h2>正式通知第一条</h2><p>活动时间2027-01-01</p></a></li>"
            "<li><a href='/info/1/2.htm'><h5>09</h5><h5>10</h5><span>2026.07</span>"
            "<h2>正式通知第二条</h2></a></li>"
            "<li><a href='/info/1/3.htm'><h2>2026学年正式通知第三条</h2>"
            "<p>活动时间2026-07-11</p></a></li>");
        const auto notices = HtmlAdapter{}.parseList(html, source);
        QCOMPARE(notices.size(), size_t(3));
        QCOMPARE(notices[0].title, std::string("正式通知第一条"));
        QCOMPARE(notices[0].publishedDate, std::string("2026-07-08"));
        QVERIFY(notices[1].publishedDate.empty());
        QVERIFY(notices[2].publishedDate.empty());
    }
    void historyPageNumbersDoNotBecomeNewSections() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        QVERIFY(QDir().mkpath(scan.directory_ + "/samples"));
        SchoolOnboarding::Page page{QUrl("https://grs.example.edu.cn/"), "研究生教育", 1, false};
        page.origin = page.url;
        scan.consume(page, departmentList() +
            "<a href='/tzgg/118.htm'>2</a><a href='/tzgg/117.htm'>下一页</a>"
            "<a href='/tzgg.htm'>通知公告</a><a href='https://job.example.edu.cn/'>就业</a>", {});
        bool notices = false, careers = false;
        for (const auto &entry : scan.queue_) {
            QVERIFY(entry.url.path() != "/tzgg/118.htm");
            QVERIFY(entry.url.path() != "/tzgg/117.htm");
            notices |= entry.url.path() == "/tzgg.htm";
            careers |= entry.url.host() == "job.example.edu.cn";
        }
        QVERIFY(notices);
        QVERIFY(careers);
    }
    void unknownDepartmentHomepageStillRequiresThreeRowsAndFirstBody() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        QVERIFY(QDir().mkpath(scan.directory_ + "/samples"));
        SchoolOnboarding::Page home{QUrl("https://jwc.example.edu.cn/"), "教务处", 1, false};
        home.origin = home.url;
        scan.consume(home, departmentList(), {});
        QCOMPARE(scan.sources_.size(), 1);
        QCOMPARE(scan.counts_.value(scan.sources_.first().toObject().value("key").toString()), 3);
        QVERIFY(!scan.sources_.first().toObject().value("enabled").toBool());
        QCOMPARE(scan.ready_, 0);
        QCOMPARE(scan.queue_.size(), size_t(1));
        QVERIFY(scan.queue_.front().detail.has_value());
        const auto body = scan.queue_.front();
        scan.queue_.pop_front();
        scan.consume(body, validBody(), {});
        QCOMPARE(scan.ready_, 1);
        QVERIFY(scan.sources_.first().toObject().value("enabled").toBool());
        QSignalSpy completed(&scan, &SchoolOnboarding::finished);
        scan.finish();
        QCOMPARE(completed.size(), 1);
        const auto config = objectFile(scan.directory_ + "/school.json");
        QCOMPARE(config.value("onboarding_version").toInt(), SchoolOnboarding::AlgorithmVersion);
        QVERIFY(SchoolOnboarding::AlgorithmVersion > 3);
        const auto loaded = SchoolPackage::load(scan.directory_ + "/school.json");
        QCOMPARE(loaded.sources.size(), size_t(1));
        QVERIFY(loaded.automaticallyIdentified);
    }
    void tooFewRowsMissingOrShortBodyCannotEnableUnknownSource() {
        QTemporaryDir folder;
        const auto seed = unknownSeed(folder.path());
        const SchoolOnboarding::Page home{QUrl("https://jwc.example.edu.cn/"), "本科生院", 1, false};
        SchoolOnboarding tooFew(seed, folder.path(), nullptr, 0);
        QVERIFY(QDir().mkpath(tooFew.directory_ + "/samples"));
        tooFew.consume(home, departmentList(2), {});
        QCOMPARE(tooFew.ready_, 0);
        QVERIFY(tooFew.queue_.empty());
        QVERIFY(!tooFew.sources_.first().toObject().value("enabled").toBool());
        for (const auto &html : {QByteArray("<html><main>正文选择器不存在</main></html>"),
                                QByteArray("<div class='v_news_content'>short</div>")}) {
            SchoolOnboarding rejected(seed, folder.path(), nullptr, 0);
            rejected.consume(home, departmentList(), {});
            const auto body = rejected.queue_.front();
            rejected.queue_.pop_front();
            rejected.consume(body, html, {});
            QCOMPARE(rejected.ready_, 0);
            QVERIFY(!rejected.sources_.first().toObject().value("enabled").toBool());
        }
        SchoolOnboarding noListGate(seed, folder.path(), nullptr, 0);
        noListGate.consume(home, departmentList(), {});
        const auto body = noListGate.queue_.front();
        noListGate.queue_.pop_front();
        noListGate.counts_[body.sourceKey] = 2;
        noListGate.consume(body, validBody(), {});
        QCOMPARE(noListGate.ready_, 0);
        QVERIFY(!noListGate.sources_.first().toObject().value("enabled").toBool());
    }
    void iconNavigationUsesReadableAttributesWithoutOverridingVisibleText() {
        const auto links = HtmlAdapter{}.links(
            "<a href='/visible' title='不覆盖文本'>教务处</a>"
            "<a href='/title' title=' 本科生院 '><img src='x.png'></a>"
            "<a href='/aria' aria-label='学生工作处'><img alt='不覆盖ARIA'></a>"
            "<a href='/image'><img alt=''><img alt=' 图书馆 '></a>"
            "<a href='/blank'><img alt=''></a>", QUrl("https://www.example.edu.cn/"));
        QCOMPARE(links.size(), size_t(4));
        QCOMPARE(links[0].title, QString("教务处"));
        QCOMPARE(links[1].title, QString("本科生院"));
        QCOMPARE(links[2].title, QString("学生工作处"));
        QCOMPARE(links[3].title, QString("图书馆"));
        QCOMPARE(links[3].url, QUrl("https://www.example.edu.cn/image"));
        const auto tooLong = QByteArray("<a href='/long' title='") + QByteArray(301, 'a') +
                             "'><img></a>";
        QVERIFY(HtmlAdapter{}.links(tooLong, QUrl("https://www.example.edu.cn/")).empty());
    }
    void fullQueueRetainsTeachingInstitutionsAndFirstBodyBeforeGenericNavigation() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        for (int i = 0; i < 48; ++i)
            scan.enqueue(QUrl(QString("https://www.example.edu.cn/bridge/%1").arg(i)), "一般入口", 1, false);
        scan.enqueue(QUrl("https://jwc.example.edu.cn/"), "教务处", 1, false);
        QCOMPARE(scan.queue_.size(), size_t(48));
        QCOMPARE(scan.queue_.front().url, QUrl("https://jwc.example.edu.cn/"));
        QVERIFY(scan.queue_.front().candidate);
        scan.enqueue(QUrl("https://www.example.edu.cn/zzjg.htm"), "组织机构", 1, false);
        QCOMPARE(scan.queue_.size(), size_t(48));
        QCOMPARE(scan.queue_.front().url, QUrl("https://www.example.edu.cn/zzjg.htm"));
        SchoolOnboarding::Page detail{QUrl("https://jwc.example.edu.cn/info/1001/1.htm"), "正文校验", 1, false};
        detail.detail = Notice{};
        detail.sourceKey = "synthetic-source";
        detail.priority = 4;
        scan.pushPage(detail);
        QCOMPARE(scan.queue_.size(), size_t(48));
        QVERIFY(scan.queue_.front().detail.has_value());
        QCOMPARE(scan.deferredFrontier_.size(), 3);
        scan.enqueue(QUrl("https://evil.org/"), "教务处", 1, false);
        QCOMPARE(scan.queue_.size(), size_t(48));
        QCOMPARE(scan.deferredFrontier_.size(), 3);
    }
    void strongerDepartmentAliasPromotesAnExistingQueuedNavigation() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        const QUrl url("https://jwc.example.edu.cn/");
        scan.enqueue(url, "一般入口", 1, false);
        scan.enqueue(QUrl("https://www.example.edu.cn/other"), "学生服务", 1, true);
        scan.enqueue(url, "教务在线", 2, false);
        QCOMPARE(scan.queue_.size(), size_t(2));
        QCOMPARE(scan.queue_.front().url, url);
        QCOMPARE(scan.queue_.front().depth, 1);
        QVERIFY(scan.queue_.front().candidate);
    }
    void requestBudgetAndBoundedFrontierReportExplainIncompleteDiscovery() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        QVERIFY(QDir().mkpath(scan.directory_ + "/samples"));
        scan.enqueue(QUrl("https://jwc.example.edu.cn/"), "教务处", 1, false);
        scan.enqueue(QUrl("https://www.example.edu.cn/deep"), "通知公告", 4, true);
        scan.fetched_ = scan.school_.discoveryLimit;
        QSignalSpy rejected(&scan, &SchoolOnboarding::failed);
        scan.finish();
        QCOMPARE(rejected.size(), 1);
        QVERIFY(!QFile::exists(scan.directory_ + "/school.json"));
        const auto report = objectFile(scan.directory_ + "/report.json");
        QVERIFY(report.value("budget_exhausted").toBool());
        QVERIFY(!report.value("discovery_complete").toBool());
        QVERIFY(!report.value("passed").toBool());
        const auto pending = report.value("pending_frontier").toArray();
        QCOMPARE(pending.size(), 2);
        QCOMPARE(pending[0].toObject().value("reason").toString(), QString("request_budget"));
        QCOMPARE(pending[1].toObject().value("reason").toString(), QString("depth_limit"));
        QCOMPARE(report.value("model_calls").toInt(), 0);
        for (int i = 0; i < 300; ++i)
            scan.enqueue(QUrl(QString("https://www.example.edu.cn/deep/%1").arg(i)), "通知公告", 4, true);
        QCOMPARE(scan.deferredFrontier_.size(), 256);
        QVERIFY(scan.frontierTruncated_);
    }
    void verifiedDetailRedirectChainIsPreservedWithoutTrustingUnvisitedHosts() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        QVERIFY(QDir().mkpath(scan.directory_ + "/samples"));
        const SchoolOnboarding::Page home{QUrl("https://jwc.example.edu.cn/"), "教务处", 1, false};
        scan.consume(home, departmentList(), {});
        auto body = scan.queue_.front();
        scan.queue_.pop_front();
        body.redirectHosts = {"jwc.example.edu.cn", "relay.example.edu.cn"};
        body.url = QUrl("https://content.example.edu.cn/info/1001/1.htm");
        scan.consume(body, validBody(), {});
        QCOMPARE(scan.ready_, 1);
        const auto hosts = scan.sources_.first().toObject().value("allowed_hosts").toArray();
        QCOMPARE(hosts.size(), 3);
        QVERIFY(hosts.contains("jwc.example.edu.cn"));
        QVERIFY(hosts.contains("relay.example.edu.cn"));
        QVERIFY(hosts.contains("content.example.edu.cn"));
        QVERIFY(!hosts.contains("unvisited.example.edu.cn"));
        scan.finish();
        const auto loaded = SchoolPackage::load(scan.directory_ + "/school.json");
        QVERIFY(isAllowedUrl(QUrl("https://relay.example.edu.cn/redirect"), loaded.sources.front()));
        QVERIFY(isAllowedUrl(body.url, loaded.sources.front()));
        QVERIFY(!isAllowedUrl(QUrl("https://unvisited.example.edu.cn/"), loaded.sources.front()));
    }
    void failedLoginOrForeignDetailNeverWidensConfiguredHosts() {
        QTemporaryDir folder;
        const auto seed = unknownSeed(folder.path());
        for (const auto &kind : {QString("login"), QString("foreign"), QString("failed")}) {
            SchoolOnboarding scan(seed, folder.path(), nullptr, 0);
            QVERIFY(QDir().mkpath(scan.directory_ + "/samples"));
            const SchoolOnboarding::Page home{QUrl("https://jwc.example.edu.cn/"), "教务处", 1, false};
            scan.consume(home, departmentList(), {});
            auto body = scan.queue_.front();
            scan.queue_.pop_front();
            body.redirectHosts = {"jwc.example.edu.cn", "relay.example.edu.cn"};
            body.url = QUrl(kind == "foreign" ? "https://evil.org/info/1/1.htm"
                                               : "https://sso.example.edu.cn/login");
            scan.consume(body, kind == "login" ? QByteArray("<title>统一身份认证</title><input type='password'>")
                                               : validBody(), kind == "failed" ? "HTTP 403" : QString{});
            QCOMPARE(scan.ready_, 0);
            const auto source = scan.sources_.first().toObject();
            QVERIFY(!source.value("enabled").toBool());
            QCOMPARE(source.value("allowed_hosts").toArray(), QJsonArray{"jwc.example.edu.cn"});
        }
    }
    void sharedDepartmentNavigationLabelsReachTeachingEntries() {
        for (const auto *label : {"教务在线", "组织机构", "人才培养", "本科生院"})
            QVERIFY(SchoolOnboarding::isDiscoveryLabel(QString::fromUtf8(label)));
        QVERIFY(!SchoolOnboarding::isDiscoveryLabel("关于2027年重修缴费的通知"));
        QVERIFY(!SchoolOnboarding::isDiscoveryLabel("校友捐赠"));
    }
    void subjectSectionsAreListCandidatesAndInnovationLinksAreFollowed() {
        QTemporaryDir folder;
        SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
        QVERIFY(QDir().mkpath(scan.directory_ + "/samples"));
        const SchoolOnboarding::Page home{QUrl("https://jwc.example.edu.cn/"), "教务处", 1, false};
        scan.consume(home, departmentList() +
            "<a href='https://innovation.example.edu.cn/'>双创教育</a>"
            "<a href='/competition.htm'>学科竞赛</a>"
            "<a href='/retake.htm'>重修补考</a><a href='/fees.htm'>缴费</a>"
            "<a href='/services.htm'>教学服务</a>", {});
        for (const auto &path : {QString("/competition.htm"), QString("/retake.htm"), QString("/fees.htm")}) {
            const auto candidate = std::find_if(scan.queue_.begin(), scan.queue_.end(),
                [&](const auto &page) { return !page.detail && page.url.path() == path; });
            QVERIFY(candidate != scan.queue_.end());
            QVERIFY(candidate->candidate);
        }
        QVERIFY(std::any_of(scan.queue_.begin(), scan.queue_.end(), [](const auto &page) {
            return page.url.host() == "innovation.example.edu.cn" && page.candidate;
        }));
        QVERIFY(std::any_of(scan.queue_.begin(), scan.queue_.end(), [](const auto &page) {
            return page.url.path() == "/services.htm";
        }));
    }
    void verifiedListGetsBoundedPaginationButForeignNextNeverWidensTrust() {
        for (const auto &target : {QString("/list/2.htm"), QString("https://foreign.example.org/list/2.htm")}) {
            QTemporaryDir folder;
            SchoolOnboarding scan(unknownSeed(folder.path()), folder.path(), nullptr, 0);
            QVERIFY(QDir().mkpath(scan.directory_ + "/samples"));
            const SchoolOnboarding::Page home{QUrl("https://jwc.example.edu.cn/list.htm"), "通知公告", 1, true};
            scan.consume(home, departmentList() + QString("<span class='p_next'><a href='%1'>下页</a></span>")
                .arg(target).toUtf8(), {});
            const auto body = scan.queue_.front();
            scan.queue_.pop_front();
            scan.consume(body, validBody(), {});
            QCOMPARE(scan.ready_, 1);
            scan.finish();
            const auto loaded = SchoolPackage::load(scan.directory_ + "/school.json");
            QCOMPARE(loaded.sources.front().maxPages, target.startsWith('/') ? 3 : 1);
            QCOMPARE(loaded.sources.front().allowedHosts, QStringList{"jwc.example.edu.cn"});
            if (target.startsWith('/'))
                QCOMPARE(HtmlAdapter{}.nextPage(departmentList() +
                    "<span class='p_next'><a href='/list/2.htm'>下页</a></span>",
                    loaded.sources.front(), home.url), QUrl("https://jwc.example.edu.cn/list/2.htm"));
            else
                QVERIFY(!scan.failures_.empty());
        }
    }
    void sharedJspCmsLinksReadPublishedDatesWithoutTitleInference() {
        auto source = automatic("jsp", "https://www.bkjx.example.edu.cn/index/tzgg.htm");
        source.allowUnknownDates = true;
        const QByteArray page = "<ul>"
            "<li><a href='../content.jsp?urltype=news.NewsContentUrl&amp;wbnewsid=38111'>"
            "<div class='time'><span>09.30</span><i>2026</i></div><h5>关于2027学年竞赛报名的通知</h5></a></li>"
            "<li><a href='../content.jsp?urltype=news.NewsContentUrl&amp;wbnewsid=38112'>"
            "<div class='time'><span>09.28</span><i>2026</i></div><h5>奖学金申请通知</h5></a></li>"
            "<li><a href='../content.jsp?urltype=news.NewsContentUrl&amp;wbnewsid=38113'>"
            "<div class='time'><span>02.30</span><i>2026</i></div><h5>2027年重修缴费通知</h5></a></li>"
            "<li><a href='../content.jsp?urltype=tree.TreeTempUrl&amp;wbnewsid=38114'>导航不是通知</a></li>"
            "<li><a href='https://example.com/content.jsp?urltype=news.NewsContentUrl&amp;wbnewsid=38115'>外站不能加入</a></li>"
            "</ul>";
        const auto rows = HtmlAdapter{}.parseList(page, source);
        QCOMPARE(rows.size(), std::size_t(3));
        QCOMPARE(rows[0].publishedDate, std::string("2026-09-30"));
        QCOMPARE(rows[0].title, std::string("关于2027学年竞赛报名的通知"));
        QVERIFY(rows[0].url.find("content.jsp?") != std::string::npos);
        QCOMPARE(rows[1].publishedDate, std::string("2026-09-28"));
        QVERIFY(rows[2].publishedDate.empty());
    }
    void loginDetectionKeepsOfficialOriginAndSourceId() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        SchoolOnboarding onboarding(registry.resolve("www.jlu.edu.cn").configFile, folder.path(),
                                    nullptr, 0);
        QVERIFY(QDir().mkpath(onboarding.directory_ + "/samples"));
        const QUrl original("https://jwc.jlu.edu.cn/");
        SchoolOnboarding::Page page{QUrl("https://sso.jlu.edu.cn/cas/login?service=outside"),
                                    "官方入口", 0, true};
        page.origin = original;
        const QByteArray html = "<html><title>吉林大学统一身份认证</title><form>"
                                "<input type='password' name='password'></form></html>";
        onboarding.consume(page, html, {});
        QCOMPARE(onboarding.sources_.size(), 1);
        auto source = onboarding.sources_.first().toObject();
        const auto expectedKey =
            "auto-" + QCryptographicHash::hash(original.toString(QUrl::FullyEncoded).toUtf8(),
                                               QCryptographicHash::Sha256)
                          .toHex()
                          .left(12);
        QCOMPARE(source.value("key").toString(), QString::fromLatin1(expectedKey));
        QCOMPARE(source.value("entry_url").toString(), original.toString());
        QVERIFY(!source.value("enabled").toBool());
        QCOMPARE(source.value("access").toObject().value("mode").toString(),
                 QString("login_required"));
        QCOMPARE(source.value("access").toObject().value("login_url").toString(),
                 original.toString());
        QVERIFY(source.value("pending").toArray().first().toString().contains("需要登录"));
        QCOMPARE(onboarding.ready_, 0);
        QVERIFY(onboarding.queue_.empty());
        onboarding.consume(page, html, {});
        QCOMPARE(onboarding.sources_.size(), 1);
        QCOMPARE(onboarding.sources_.first().toObject().value("key").toString(),
                 source.value("key").toString());

        // A first-body check that redirects to authentication retains its
        // original source ID and entry, rather than exposing the redirect URL.
        source["key"] = "existing-public-column";
        source["enabled"] = true;
        source.remove("access");
        onboarding.sources_ = QJsonArray{source};
        page.candidate = false;
        page.detail = Notice{};
        page.sourceKey = "existing-public-column";
        onboarding.consume(page, html, {});
        const auto detailSource = onboarding.sources_.first().toObject();
        QCOMPARE(detailSource.value("key").toString(), QString("existing-public-column"));
        QVERIFY(!detailSource.value("enabled").toBool());
        QCOMPARE(detailSource.value("access").toObject().value("login_url").toString(),
                 original.toString());
    }

    void httpFailuresAndPublicLoginHelpStayDistinct() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        const auto seed = registry.resolve("www.jlu.edu.cn").configFile;
        SchoolOnboarding failed(seed, folder.path(), nullptr, 0);
        for (const auto *error : {"HTTP 403 Forbidden", "HTTP 502 Bad Gateway"}) {
            const QUrl original(QString("https://jwc.jlu.edu.cn/%1").arg(failed.sources_.size()));
            SchoolOnboarding::Page page{original, "官方入口", 0, true};
            page.origin = original;
            failed.consume(page, {}, error);
            const auto source = failed.sources_.last().toObject();
            QVERIFY(!source.contains("access"));
            QVERIFY(!source.value("enabled").toBool());
            QVERIFY(source.value("pending").toArray().first().toString().contains(error));
        }
        SchoolOnboarding publicHelp(seed, folder.path(), nullptr, 0);
        QVERIFY(QDir().mkpath(publicHelp.directory_ + "/samples"));
        SchoolOnboarding::Page page{QUrl("https://jwc.jlu.edu.cn/login-help/index.htm"), "官方入口",
                                    0, true};
        page.origin = page.url;
        publicHelp.consume(page,
                           "<html><title>统一身份认证平台升级通知</title>"
                           "<ul><li><a href='/info/1/1.htm'>通知公告样本之一</a></li>"
                           "<li><a href='/info/1/2.htm'>通知公告样本之二</a></li>"
                           "<li><a href='/info/1/3.htm'>通知公告样本之三</a></li></ul></html>",
                           {});
        QCOMPARE(publicHelp.sources_.size(), 1);
        QVERIFY(!publicHelp.sources_.first().toObject().contains("access"));
        QCOMPARE(publicHelp.sources_.first().toObject().value("validation_state").toString(),
                 QString("detail_discovered"));
        QCOMPARE(publicHelp.queue_.size(), size_t(1));
    }

    void deepseekRequiresRealSearchAndUniversityBoundaries() {
        const auto request = DeepSeekSearch::requestBody("test-model", "测试大学", "jlu.edu.cn");
        QCOMPARE(request.value("max_tokens").toInt(), 2048);
        QCOMPARE(request.value("tools").toArray().first().toObject().value("max_uses").toInt(), 2);
        QVERIFY_THROWS_EXCEPTION(
            std::runtime_error,
            DeepSeekSearch::candidates(
                QJsonObject{
                    {"stop_reason", "end_turn"},
                    {"content", QJsonArray{QJsonObject{{"type", "text"},
                                                       {"text", "https://jwc.jlu.edu.cn/"}}}}},
                "jlu.edu.cn", {}));
        const QJsonObject response{
            {"stop_reason", "end_turn"},
            {"content",
             QJsonArray{QJsonObject{
                 {"type", "web_search_tool_result"},
                 {"content",
                  QJsonArray{QJsonObject{{"type", "web_search_result"},
                                         {"url", "https://jwc.jlu.edu.cn/fwzn/xs1/ksap.htm"}},
                             QJsonObject{{"type", "web_search_result"},
                                         {"url", "http://jwc.jlu.edu.cn/fwzn/xs1/ksap.htm"}},
                             QJsonObject{{"type", "web_search_result"},
                                         {"url", "https://jwc.jlu.edu.cn.evil.org/"}},
                             QJsonObject{{"type", "web_search_result"},
                                         {"url", "https://user@jwc.jlu.edu.cn/"}},
                             QJsonObject{{"type", "web_search_result"},
                                         {"url", "https://jwc.jlu.edu.cn/info/1/2.htm"}},
                             QJsonObject{{"type", "web_search_result"},
                                         {"url", "https://xsc.jlu.edu.cn/index/tzgg.htm"}}}}}}}};
        const auto hits = DeepSeekSearch::candidates(response, "jlu.edu.cn",
                                                     {"https://xsc.jlu.edu.cn/index/tzgg.htm"});
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().toObject().value("url").toString(),
                 QString("https://jwc.jlu.edu.cn/fwzn/xs1/ksap.htm"));
        QCOMPARE(hits.first().toObject().value("status").toString(), QString("candidate"));
        DeepSeekSearch provider;
        QSignalSpy failure(&provider, &DeepSeekSearch::failed);
        provider.search(QString{}, "test", "test", "jlu.edu.cn", {});
        QCOMPARE(failure.count(), 1);
        QVERIFY(failure.first().first().toString().contains("尚未发起请求"));
    }
    void deepseekIncompleteAndToolErrorsCannotAddCandidates() {
        const QJsonObject goodBlock{
            {"type", "web_search_tool_result"},
            {"content", QJsonArray{QJsonObject{{"type", "web_search_result"},
                                               {"url", "https://jwc.jlu.edu.cn/tzgg.htm"}}}}};
        for (const auto &reason : {"max_tokens", "pause_turn", "tool_use", "stop_sequence",
                                  "refusal", "model_context_window_exceeded", ""}) {
            const QJsonObject incomplete{{"stop_reason", reason},
                                         {"content", QJsonArray{goodBlock}}};
            QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                     DeepSeekSearch::candidates(incomplete, "jlu.edu.cn", {}));
        }
        auto malformed = QJsonObject{{"stop_reason", "end_turn"}, {"content", "not an array"}};
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 DeepSeekSearch::candidates(malformed, "jlu.edu.cn", {}));
        malformed["content"] = QJsonArray{goodBlock, "not a block"};
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 DeepSeekSearch::candidates(malformed, "jlu.edu.cn", {}));
        malformed["content"] = QJsonArray{QJsonObject{
            {"type", "web_search_tool_result"},
            {"content", QJsonArray{QJsonObject{{"type", "web_search_result"}, {"url", 123}}}}}};
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 DeepSeekSearch::candidates(malformed, "jlu.edu.cn", {}));
        malformed["content"] = QJsonArray{QJsonObject{
            {"type", "web_search_tool_result"},
            {"content", QJsonArray{QJsonObject{{"type", "web_search_tool_result_error"},
                                               {"error_code", "unavailable"}}}}}};
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 DeepSeekSearch::candidates(malformed, "jlu.edu.cn", {}));

        // Reaching the eight-candidate cap must not hide a later tool failure.
        QJsonArray hits;
        for (int i = 0; i < 10; ++i)
            hits.append(QJsonObject{{"type", "web_search_result"},
                                     {"url", QString("https://jwc.jlu.edu.cn/list%1.htm").arg(i)}});
        const QJsonObject manyBlock{{"type", "web_search_tool_result"}, {"content", hits}};
        const QJsonObject complete{{"stop_reason", "end_turn"},
                                  {"content", QJsonArray{manyBlock}}};
        QCOMPARE(DeepSeekSearch::candidates(complete, "jlu.edu.cn", {}).size(), 8);
        const QJsonObject failedTool{
            {"type", "web_search_tool_result"},
            {"content", QJsonObject{{"type", "web_search_tool_result_error"},
                                     {"error_code", "max_uses_exceeded"}}}};
        malformed["content"] = QJsonArray{manyBlock, failedTool};
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 DeepSeekSearch::candidates(malformed, "jlu.edu.cn", {}));
        const QJsonObject emptySearch{
            {"stop_reason", "end_turn"},
            {"content", QJsonArray{QJsonObject{{"type", "web_search_tool_result"},
                                                {"content", QJsonArray{}}}}}};
        QVERIFY(DeepSeekSearch::candidates(emptySearch, "jlu.edu.cn", {}).empty());
    }
    void deepseekRejectsUnsafeRawUrlsBeforeNormalization() {
        QJsonArray hits;
        for (const auto &url : {"https://@jwc.jlu.edu.cn/", "https://jwc.jlu.edu.cn:/",
                               "https://jwc.jlu.edu.cn:443/", "https://jwc.jlu.edu.cn./",
                               "https://jwc..jlu.edu.cn/", "https://%6awc.jlu.edu.cn/",
                               "https://jwc.jlu.edu.cn\\@evil.org/",
                               "https:// jwc.jlu.edu.cn/", "https://evil.org/jlu.edu.cn/",
                               "https://jwc.jlu.edu.cn.evil.org/", "https://127.0.0.1/",
                               "https://[::1]/", "file:///jwc.jlu.edu.cn/",
                               "//jwc.jlu.edu.cn/", "https://jwc.jlu.edu.cn/tzgg.htm#top"})
            hits.append(QJsonObject{{"type", "web_search_result"}, {"url", url}});
        const QJsonObject response{
            {"stop_reason", "end_turn"},
            {"content", QJsonArray{QJsonObject{{"type", "web_search_tool_result"},
                                                {"content", hits}}}}};
        const auto candidates = DeepSeekSearch::candidates(response, "jlu.edu.cn", {});
        QCOMPARE(candidates.size(), 1);
        QCOMPARE(candidates.first().toObject().value("url").toString(),
                 QString("https://jwc.jlu.edu.cn/tzgg.htm"));
    }
    void trustedDirectoryAndNavigationBoundaries() {
        const UniversityRegistry registry(SCHOOL_CONFIG_DIR);
        QCOMPARE(registry.resolve("www.jlu.edu.cn").id, QString("cn-jlu"));
        const auto seed = SchoolPackage::load(registry.resolve("www.jlu.edu.cn").configFile);
        QCOMPARE(seed.discoveryEntries.size(), 5);
        QVERIFY(seed.sources.empty());
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 registry.resolve("https://www.jlu.edu.cn.evil.org/"));
        for (const auto &url : {"https://evil.org/", "https://www.jlu.edu.cn.evil.org/",
                                "https://user@jwc.jlu.edu.cn/", "http://jwc.jlu.edu.cn/",
                                "https://jwc.jlu.edu.cn:443/", "https://127.0.0.1/"})
            QVERIFY(!SchoolOnboarding::withinUniversity(QUrl(url), "jlu.edu.cn"));
        QVERIFY(SchoolOnboarding::withinUniversity(QUrl("https://xsc.jlu.edu.cn/index/tzgg.htm"),
                                                   "jlu.edu.cn"));
        QVERIFY(SchoolOnboarding::isDiscoveryLabel("通知公告"));
        QVERIFY(SchoolOnboarding::isDiscoveryLabel("考试安排"));
        QVERIFY(!SchoolOnboarding::isDiscoveryLabel("关于2026年奖学金评选的通知"));
    }
    void realSamplesNeedNoHandwrittenSelectors() {
        HtmlAdapter parser;
        struct Sample {
            const char *name;
            const char *url;
            size_t count;
        };
        for (const auto &sample :
             {Sample{"academic", "https://jwc.jlu.edu.cn/zxzx/tzgg.htm", 15},
              Sample{"academic-exams", "https://jwc.jlu.edu.cn/fwzn/xs1/ksap.htm", 15},
              Sample{"student", "https://xsc.jlu.edu.cn/index/tzgg.htm", 10},
              Sample{"youth", "https://youth.jlu.edu.cn/index/tzgg.htm", 5},
              Sample{"career-list", "https://jdjywpt.jlu.edu.cn/mportal/recruit/list?type=2",
                     15}}) {
            const auto source = automatic(sample.name, sample.url);
            const auto rows = parser.parseList(fixture(sample.name), source);
            QCOMPARE(rows.size(), sample.count);
            for (const auto &n : rows) {
                QCOMPARE(n.schoolId, std::string("cn-jlu"));
                QVERIFY(QDate::fromString(QString::fromStdString(n.publishedDate), Qt::ISODate)
                            .isValid());
                QVERIFY(isAllowedUrl(QUrl(QString::fromStdString(n.url)), source));
                QVERIFY(!n.title.empty());
            }
            if (QString(sample.name) == "student") {
                QCOMPARE(rows.front().publishedDate, std::string("2026-09-30"));
                QVERIFY(std::any_of(rows.begin(), rows.end(), [](const auto &n) {
                    return n.title.find("助学金") != std::string::npos;
                }));
            }
            if (QString(sample.name) == "career-list")
                QCOMPARE(rows.front().publishedDate, std::string("2026-09-20"));
        }
        auto source = automatic("youth", "https://youth.jlu.edu.cn/index/tzgg.htm");
        const auto rows = parser.parseList(fixture("youth"), source);
        QVERIFY(rows[2].title.find("公示") != std::string::npos);
        QVERIFY_THROWS_EXCEPTION(
            std::runtime_error,
            parser.parseList("<li><a href='/info/1/2.htm'>2026-09-30未知发布日期</a></li>",
                             source));
        source.allowUnknownDates = true;
        const auto unknown =
            parser.parseList("<ul><li><a href='/info/1/1.htm'>2027学年通知之一</a></li><li><a "
                             "href='/info/1/2.htm'>2027学年通知之二</a></li><li><a "
                             "href='/info/1/3.htm'>2027学年通知之三</a></li></ul>",
                             source);
        for (const auto &n : unknown)
            QVERIFY(n.publishedDate.empty());
    }
    void realBodySamplesAndSchoolIsolation() {
        HtmlAdapter parser;
        const auto source = automatic("academic", "https://jwc.jlu.edu.cn/zxzx/tzgg.htm");
        auto rows = parser.parseList(fixture("academic"), source);
        const auto detail = parser.parseDetail(fixture("academic-detail"), source, rows.front());
        QVERIFY(detail.body.find("停开") != std::string::npos);
        QVERIFY(!detail.attachments.empty());
        const auto exams = automatic("academic-exams", "https://jwc.jlu.edu.cn/fwzn/xs1/ksap.htm");
        const auto examRows = parser.parseList(fixture("academic-exams"), exams);
        QCOMPARE(examRows.front().publishedDate, std::string("2026-06-03"));
        const auto examDetail =
            parser.parseDetail(fixture("academic-exam-detail"), exams, examRows.front());
        QVERIFY(examDetail.body.find("重修") != std::string::npos);
        auto student = automatic("student", "https://xsc.jlu.edu.cn/index/tzgg.htm");
        auto n = parser.parseList(fixture("student"), student).at(2);
        QVERIFY(parser.parseDetail(fixture("student-detail"), student, n).body.find("资助") !=
                std::string::npos);
        auto youth = automatic("youth", "https://youth.jlu.edu.cn/index/tzgg.htm");
        QVERIFY(!parser
                     .parseDetail(fixture("youth-detail"), youth,
                                  parser.parseList(fixture("youth"), youth).front())
                     .body.empty());
        const auto career =
            automatic("career", "https://jdjywpt.jlu.edu.cn/mportal/recruit/list?type=2");
        auto job = parser.parseList(fixture("career-list"), career).front();
        job.publishedDate.clear();
        const auto jobDetail = parser.parseDetail(fixture("career-detail"), career, job);
        QVERIFY(jobDetail.body.find("双选会") != std::string::npos);
        QCOMPARE(jobDetail.publishedDate, std::string("2026-09-20"));
        QTemporaryDir dir;
        Database db(dir.filePath("two-schools.sqlite"));
        SqliteRepository notices(db);
        SqliteSubscriptionRepository subscriptions(db);
        SqliteTaskRepository taskRepository(db);
        NoticeService jlu(notices, "cn-jlu"), neepu(notices, "cn-neepu");
        jlu.ingest(rows);
        jlu.saveDetail(detail);
        auto another = rows.front();
        another.id = "different-school";
        another.schoolId = "cn-neepu";
        neepu.ingest({another});
        QCOMPARE(jlu.list().size(), size_t(15));
        QCOMPARE(neepu.list().size(), size_t(1));
        TaskService jluTasks("cn-jlu", "Asia/Shanghai", taskRepository, jlu),
            neepuTasks("cn-neepu", "Asia/Shanghai", taskRepository, neepu);
        const auto task = jluTasks.save(jluTasks.draft(detail.id));
        QVERIFY(neepuTasks.list().empty());
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, neepuTasks.find(task.id));
        SubscriptionService jluSubscriptions("cn-jlu", subscriptions, jlu),
            neepuSubscriptions("cn-neepu", subscriptions, neepu);
        Subscription sub;
        sub.name = "吉大通知";
        jluSubscriptions.save(sub);
        QVERIFY(neepuSubscriptions.list().empty());
    }
};
QTEST_GUILESS_MAIN(OnboardingTests)
#include "OnboardingTests.moc"
