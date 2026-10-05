#include "adapters/HtmlAdapter.h"
#include <QFile>
#include <QUrlQuery>
#include <QtTest>
#include <stdexcept>
#include <algorithm>

using namespace campus;

namespace {
QByteArray fixture(const char *name) {
    QFile file(QString::fromUtf8(GENERAL_TEMPLATE_FIXTURES) + "/" + QString::fromUtf8(name));
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("通用HTML样本未找到");
    return file.readAll();
}
SourceConfig automatic() {
    SourceConfig source;
    source.schoolId = "cn-template-test";
    source.id = "public-notices";
    source.name = "公开通知";
    source.entry = QUrl("https://www.example.edu.cn/notices/");
    source.allowedHosts = {"www.example.edu.cn"};
    source.autoDetect = true;
    source.allowUnknownDates = true;
    return source;
}
Notice emptyNotice(const char *path = "/info/1001/1002.htm") {
    Notice notice;
    notice.url = QUrl("https://www.example.edu.cn/").resolved(QUrl(QString::fromUtf8(path)))
                     .toString(QUrl::FullyEncoded).toStdString();
    notice.title = "原始公开通知标题";
    return notice;
}
} // namespace

class HtmlTemplateTests final : public QObject {
    Q_OBJECT
  private slots:
    void brokenCmsMenuTargetsAreIgnoredWithoutLosingStudentServices() {
        const auto links = HtmlAdapter{}.links(fixture("vsb-student-menu.html"),
                                              QUrl("https://jwc.example.edu.cn/"));
        QVERIFY(std::any_of(links.begin(), links.end(), [](const auto &link) {
            return link.title == "选课和退课" && link.url.path() == "/xsfw1/xkhtk.htm";
        }));
        QVERIFY(std::any_of(links.begin(), links.end(), [](const auto &link) {
            return link.title == "考试和成绩";
        }));
        for (const auto &link : links)
            QVERIFY(!link.url.toString().contains("转换链接错误"));
        const auto ordinary = HtmlAdapter{}.links(
            "<a href='/search?q=exam&amp;page=2'>选课安排</a>", QUrl("https://jwc.example.edu.cn/"));
        QCOMPARE(ordinary.size(), size_t(1));
        QCOMPARE(QUrlQuery(ordinary.front().url).queryItemValue("page"), QString("2"));
    }
    void historicalServiceGuideUsesItsOwnPublicationDate() {
        auto source = automatic();
        const auto detail = HtmlAdapter{}.parseDetail(fixture("vsb-retake-guide.html"), source,
                                                     emptyNotice("/info/1254/2768.htm"));
        QCOMPARE(detail.publishedDate, std::string("2021-04-01"));
        QVERIFY(QString::fromStdString(detail.body).contains("每学期信息门户"));
        QVERIFY(!QString::fromStdString(detail.body).contains("首页"));
    }
    void realHomepageBlocksDoNotValidateTheirMenuArchives() {
        auto source = automatic();
        source.entry = QUrl("https://jwc.njupt.edu.cn/");
        source.allowedHosts = {source.entry.host()};
        const auto notices = HtmlAdapter{}.parseList(fixture("webplus-homepage-navigation.html"), source);
        QVERIFY(notices.size() >= 5);
        QVERIFY(std::any_of(notices.begin(), notices.end(), [](const auto &notice) {
            return QString::fromStdString(notice.title).contains("重修报名通知");
        }));
        QVERIFY(std::none_of(notices.begin(), notices.end(), [](const auto &notice) {
            return QString::fromStdString(notice.url).contains("c18897") ||
                   QString::fromStdString(notice.title).contains("在线开放课程中心");
        }));
        source.entry = QUrl("https://teach.dlut.edu.cn/");
        source.allowedHosts = {source.entry.host()};
        const auto cards = HtmlAdapter{}.parseList(fixture("vsb-div-cards.html"), source);
        QCOMPARE(cards.size(), size_t(3));
        QVERIFY(QString::fromStdString(cards.front().url).contains("wbnewsid=18008"));
        for (const auto &card : cards) QVERIFY(card.publishedDate.empty());
    }
    void navigationArchivesCannotBecomeTheFirstNotice() {
        const QByteArray html(R"(<nav><ul><li><a href='/2025/1223/c1a10/page.htm'>部门历史介绍</a></li></ul></nav>
          <ul class='wp_nav'><li><a href='/2025/0526/c1a11/page.htm'>旧菜单入口</a></li></ul>
          <div class='post-21'><div class='tt'><h3 class='tit'>通知公告</h3>
          <div><a href='/1594/list.htm'>更多 +</a></div></div><div class='con'><ul>
          <li><a href='/2026/0829/c1594a307692/page.htm'><div class='news_title'>本学期补改选及重修报名通知</div><div class='news_time'>08-29</div></a></li>
          <li><a href='/2026/0920/c1594a308838/page.htm'>在线课程考试报名通知</a><span class='date'>2026-09-20</span></li>
          <li><a href='/2026/0911/c1594a308839/page.htm'>课程学习安排通知</a></li>
          </ul></div></div><footer><li><a href='/2016/1125/c1768a54057/page.htm'>旧课程平台</a></li></footer>)");
        const auto rows = HtmlAdapter{}.parseList(html, automatic());
        QCOMPARE(rows.size(), size_t(3));
        QVERIFY(QString::fromStdString(rows.front().title).contains("重修报名"));
        QVERIFY(rows.front().publishedDate.empty()); // month/day alone cannot establish year
        const auto links = HtmlAdapter{}.links(html, QUrl("https://www.example.edu.cn/"));
        const auto more = std::find_if(links.begin(), links.end(), [](const auto &link) { return link.url.path() == "/1594/list.htm"; });
        QVERIFY(more != links.end());
        QCOMPARE(more->title, QString("通知公告 · 更多"));
    }
    void divNoticeCardsAndTheirOwnSiblingDatesAreRead() {
        const QByteArray html(R"(<div class='cards'>
          <div class='list'><div class='date'>2026-09-24</div><div class='txt'><a href='/ny.jsp?urltype=news.NewsContentUrl&amp;wbnewsid=18008' title='本届毕业生图像采集报名通知'><h2>报名通知</h2>摘要</a></div></div>
          <div class='list'><div class='date'>2026-09-17</div><div class='txt'><a href='/ny.jsp?urltype=news.NewsContentUrl&amp;wbnewsid=17998'>综合素质提升学习通知</a></div></div>
          <div class='list'><div class='date'>2026-09-16</div><div class='txt'><a href='/ny.jsp?urltype=news.NewsContentUrl&amp;wbnewsid=17988'>新生应用设计大赛通知</a></div></div>
          </div><a href='/list.jsp?urltype=tree.TreeTempUrl&amp;wbtreeid=1016'><img alt='' src='more.png'></a>)");
        const auto rows = HtmlAdapter{}.parseList(html, automatic());
        QCOMPARE(rows.size(), size_t(3));
        QCOMPARE(rows.front().publishedDate, std::string("2026-09-24"));
        QCOMPARE(rows.front().title, std::string("本届毕业生图像采集报名通知"));
        const auto links = HtmlAdapter{}.links(html, QUrl("https://www.example.edu.cn/"));
        QVERIFY(std::any_of(links.begin(), links.end(), [](const auto &link) { return link.title == "公开栏目"; }));
    }
    void articleUrlRecognition_data() {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("expected");
        for (const auto *path : {
                 "/2026/0928/c17860a402175/page.htm", "/c10a123/page.html",
                 "/article/2026/09/30/132915", "/event/2026/09/22/132738", "/article/132915",
                 "/content.jsp?urltype=news.NewsContentUrl&wbtreeid=1013&wbnewsid=38202",
                 "/content2.jsp?urltype=news.NewsContentUrl&wbnewsid=20828",
                 "/content3.jsp?wbnewsid=21072", "/context.jsp?wbnewsid=2024",
                 "/nry.jsp?urltype=news.NewsContentUrl&wbnewsid=33990",
                 "/info/1010/1020.htm", "/detail/news/123", "/detail/career?id=123",
                 "/mportal/recruit/details?id=123", "/notice/0123456789abcdef01234567.htm",
                 "/notices/123.jhtml"})
            QTest::newRow(path) << QString("https://www.example.edu.cn") + path << true;
        for (const auto *path : {
                 "/17860/list.htm", "/article", "/articles?tags=notice", "/events",
                 "/department/70", "/taxonomy/term/10/34333", "/c10a0/page.htm",
                 "/content.jsp?urltype=tree.TreeTempUrl&wbtreeid=1013",
                 "/content.jsp?urltype=tree.TreeTempUrl&wbnewsid=38202",
                 "/content.jsp?wbnewsid=0", "/context.jsp?wbnewsid=abc", "/read.jsp?wbnewsid=12",
                 "/login.jsp?next=/info/1010/1020.htm", "/download.jsp?wbnewsid=12"})
            QTest::newRow(path) << QString("https://www.example.edu.cn") + path << false;
        QTest::newRow("credential_url") << QString("https://student@www.example.edu.cn/info/1/2.htm") << false;
        QTest::newRow("port_url") << QString("https://www.example.edu.cn:8443/info/1/2.htm") << false;
        QTest::newRow("file_url") << QString("file:///info/1/2.htm") << false;
    }
    void articleUrlRecognition() {
        QFETCH(QString, url);
        QFETCH(bool, expected);
        QCOMPARE(HtmlAdapter::isArticleUrl(QUrl(url)), expected);
    }
    void realWebPlusPublicationCardsKeepTitlesAndDatesSeparate() {
        const auto rows = HtmlAdapter{}.parseList(fixture("webplus-list.html"), automatic());
        QCOMPARE(rows.size(), size_t(3));
        QCOMPARE(rows.at(0).publishedDate, std::string("2026-09-28"));
        QCOMPARE(rows.at(1).publishedDate, std::string("2026-09-21"));
        QCOMPARE(rows.at(2).publishedDate, std::string("2026-09-18"));
        QCOMPARE(QString::fromStdString(rows.at(0).title),
                 QString("关于开展2026年秋季学期本科生评教与评学工作的通知"));
        QVERIFY(QString::fromStdString(rows.at(2).title).endsWith("考核结果公示"));
        QVERIFY(!QString::fromStdString(rows.at(2).title).contains("公示时间"));
        QVERIFY(!QString::fromStdString(rows.at(0).title).contains("Sep"));
    }
    void realVsbCardsUseTheirTitleAndSplitPublicationDay() {
        auto source = automatic();
        source.entry = QUrl("https://www.bkjx.sdu.edu.cn/");
        source.allowedHosts = {"www.bkjx.sdu.edu.cn", "www.ygb.sdu.edu.cn"};
        const auto rows = HtmlAdapter{}.parseList(fixture("vsb-list.html"), source);
        QCOMPARE(rows.size(), size_t(3));
        QCOMPARE(rows.at(0).publishedDate, std::string("2026-09-30"));
        QCOMPARE(rows.at(1).publishedDate, std::string("2026-09-28"));
        QCOMPARE(rows.at(2).publishedDate, std::string("2026-09-24"));
        QVERIFY(QString::fromStdString(rows.at(0).title).startsWith("关于举办第四届"));
        QVERIFY(!QString::fromStdString(rows.at(0).title).startsWith("实践教学"));
        QVERIFY(!QString::fromStdString(rows.at(2).title).contains("星期四"));
    }
    void realCmsBodiesAndPublicationHeaders_data() {
        QTest::addColumn<QByteArray>("html");
        QTest::addColumn<QString>("date");
        QTest::addColumn<QString>("bodyExcerpt");
        QTest::newRow("webplus") << fixture("webplus-detail.html") << QString("2026-09-10") << QString("考试报名");
        QTest::newRow("drupal") << fixture("drupal-detail.html") << QString("2026-09-30") << QString("数字媒体科技");
        QTest::newRow("vsb") << fixture("vsb-detail.html") << QString("2026-08-05") << QString("学费和住宿费");
    }
    void realCmsBodiesAndPublicationHeaders() {
        QFETCH(QByteArray, html);
        QFETCH(QString, date);
        QFETCH(QString, bodyExcerpt);
        const auto notice = HtmlAdapter{}.parseDetail(html, automatic(), emptyNotice());
        QCOMPARE(QString::fromStdString(notice.publishedDate), date);
        QVERIFY(QString::fromStdString(notice.body).contains(bodyExcerpt));
    }
    void drupalEventTimeIsNotItsPublicationDate() {
        const auto notice = HtmlAdapter{}.parseDetail(
            fixture("drupal-event.html"), automatic(), emptyNotice("/event/2026/09/22/132738"));
        QVERIFY(notice.publishedDate.empty());
        QVERIFY(QString::fromStdString(notice.body).contains("讲座主题"));
        const QByteArray list(R"(<ul>
            <li><span class='date'>2026-09-23</span><a href='/event/2026/09/22/1'>讲座活动报名通知之一</a></li>
            <li><time datetime='2026-09-24'>2026-09-24</time><a href='/event/2026/09/22/2'>讲座活动报名通知之二</a></li>
            <li><span class='publish-date'>2026-09-22</span><a href='/event/2026/09/22/3'>讲座活动报名通知之三</a></li>
        </ul>)");
        const auto rows = HtmlAdapter{}.parseList(list, automatic());
        QCOMPARE(rows.size(), size_t(3));
        QVERIFY(rows.at(0).publishedDate.empty());
        QVERIFY(rows.at(1).publishedDate.empty());
        QCOMPARE(rows.at(2).publishedDate, std::string("2026-09-22"));
    }
    void bodyAndUrlDatesDoNotInventPublicationDate() {
        const auto notice = HtmlAdapter{}.parseDetail(
            "<h1>2027年竞赛通知</h1><div class='article-content'><p>报名截止2026-10-31。</p>"
            "<p>活动时间2027-01-03。</p></div>", automatic(), emptyNotice("/article/2026/09/20/123"));
        QVERIFY(notice.publishedDate.empty());
        const auto conflict = HtmlAdapter{}.parseDetail(
            "<meta property='article:published_time' content='2026-09-20T10:00:00+08:00'>"
            "<span class='arti_update'>发布时间：2026-09-21</span><div class='wp_articlecontent'>公开通知正文。</div>",
            automatic(), emptyNotice());
        QVERIFY(conflict.publishedDate.empty());
    }
    void explicitArticleBodyWinsOverSidebarAndIgnoresScripts() {
        const auto notice = HtmlAdapter{}.parseDetail(
            "<div class='view-content'>侧栏其他通知，2026-10-04。</div>"
            "<div class='wp_articlecontent'><p>真实通知正文。</p><script>secretFunction()</script></div>",
            automatic(), emptyNotice());
        QCOMPARE(QString::fromStdString(notice.body), QString("真实通知正文。"));
        QVERIFY(notice.publishedDate.empty());
    }
    void vsbVariantsAreArticlesAndDoNotBorrowWrapperDates() {
        const QByteArray html(R"(<table><tr><td>
          <span class='date'>2026-10-04</span>
          <ul><li><a href='/content2.jsp?wbnewsid=111'>公开通知第一项</a></li>
          <li><a href='/context.jsp?wbnewsid=112'>公开通知第二项</a></li>
          <li><a href='/content3.jsp?wbnewsid=113'>公开通知第三项</a></li></ul>
        </td></tr></table>)");
        const auto rows = HtmlAdapter{}.parseList(html, automatic());
        QCOMPARE(rows.size(), size_t(3));
        for (const auto &row : rows) QVERIFY(row.publishedDate.empty());
    }
    void navigationLabelInheritanceIsLocalAndUnambiguous() {
        const QByteArray html(R"(<ul>
            <li><a>本科生院</a><a href='https://academic.example.edu.cn/'><img src='icon.png'></a></li>
            <li><a href='#'>图书馆</a><a href='/library/'></a></li>
            <li><a href='/actual-notices/'>通知公告</a><a href='/unrelated/'></a></li>
            <li><a>学生服务</a><a>就业服务</a><a href='/ambiguous/'></a></li>
            <li><a>兄弟学校</a><ul><li><a href='/nested/'></a></li></ul></li>
            <li><a href='javascript:evil()'>恶意脚本</a></li>
        </ul>)");
        const auto links = HtmlAdapter{}.links(html, QUrl("https://www.example.edu.cn/"));
        QCOMPARE(links.size(), size_t(3));
        QCOMPARE(links.at(0).title, QString("本科生院"));
        QCOMPARE(links.at(1).title, QString("图书馆"));
        QCOMPARE(links.at(2).title, QString("通知公告"));
        for (const auto &link : links) {
            QVERIFY(!link.url.path().contains("unrelated"));
            QVERIFY(!link.url.path().contains("ambiguous"));
            QVERIFY(!link.url.path().contains("nested"));
        }
    }
};

QTEST_GUILESS_MAIN(HtmlTemplateTests)
#include "HtmlTemplateTests.moc"
