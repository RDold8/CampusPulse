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
} // namespace
class OnboardingTests final : public QObject {
    Q_OBJECT
  private slots:
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
        provider.search({}, "test", "test", "jlu.edu.cn", {});
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
