#include "adapters/ResourceDiscovery.h"
#include "adapters/ResourceClassifier.h"
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <algorithm>
#include <map>
#include <stdexcept>
using namespace campus;
namespace {
class MemoryResources final : public ResourceRepository {
  public:
    std::map<std::string, SchoolResource> rows;
    std::vector<SchoolResource> list(const std::string &schoolId) const override {
        std::vector<SchoolResource> result;
        for (const auto &[id, resource] : rows)
            if (resource.schoolId == schoolId)
                result.push_back(resource);
        return result;
    }
    void upsert(const std::string &, const std::vector<SchoolResource> &resources) override {
        for (auto resource : resources) {
            if (rows.contains(resource.id))
                resource.favorite = rows.at(resource.id).favorite;
            rows[resource.id] = std::move(resource);
        }
    }
    void setFavorite(const std::string &, const std::string &id, bool favorite) override {
        rows.at(id).favorite = favorite;
    }
};
SchoolPackage school() {
    return SchoolPackage::load(QString(SCHOOL_CONFIG_DIR) + "/neepu.example.json");
}
QByteArray fixture(const QString &campus, const QString &name) {
    QFile file(QString(RESOURCE_FIXTURE_DIR) + "/" + campus + "/" + name + ".html");
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Missing official resource fixture");
    return file.readAll();
}
ResourceDiscoveryOptions options(const QTemporaryDir &directory) {
    ResourceDiscoveryOptions result;
    result.requestIntervalMs = 0;
    result.evidenceDirectory = directory.path();
    return result;
}
const SchoolResource &find(const MemoryResources &repository, const QString &url) {
    const auto found =
        std::find_if(repository.rows.begin(), repository.rows.end(), [&](const auto &item) {
            return QString::fromStdString(item.second.url) == url;
        });
    if (found == repository.rows.end())
        throw std::runtime_error("Expected resource URL was not collected");
    return found->second;
}
} // namespace
class ResourceDiscoveryTests : public QObject {
    Q_OBJECT
  private slots:
    void safeUrlsAndOfficialBoundariesNeverRelaxForFixtures() {
        const QString root = "neepu.edu.cn";
        QVERIFY(ResourceClassifier::isOfficial(QUrl("https://lib.neepu.edu.cn/"), root));
        QVERIFY(!ResourceClassifier::isOfficial(QUrl("https://lib.neepu.edu.cn.evil.org/"), root));
        for (const auto *value : {"http://lib.neepu.edu.cn/", "https://user@lib.neepu.edu.cn/",
                                  "https://@lib.neepu.edu.cn/", "https://lib.neepu.edu.cn:443/",
                                  "https://lib.neepu.edu.cn:/", "https://127.0.0.1/",
                                  "https://localhost/", "https://school.local/", "/resources"})
            QVERIFY2(!ResourceClassifier::isSafeHttps(QString::fromUtf8(value)), value);
        QCOMPARE(ResourceClassifier::officialRoot(QUrl("https://www.neepu.edu.cn/")), root);
        const auto first = ResourceClassifier::describe("cn-neepu", "数据库导航",
                                                        QUrl("https://lib.neepu.edu.cn/#one"),
                                                        QUrl("https://www.neepu.edu.cn/"));
        const auto second =
            ResourceClassifier::describe("cn-neepu", "不同标题", QUrl("https://lib.neepu.edu.cn/"),
                                         QUrl("https://www.neepu.edu.cn/"));
        QCOMPARE(first.id, second.id);
        auto another = ResourceClassifier::describe("cn-other", "数据库导航",
                                                    QUrl("https://lib.neepu.edu.cn/"),
                                                    QUrl("https://www.neepu.edu.cn/"));
        QVERIFY(first.id != another.id);
        QTemporaryDir directory;
        MemoryResources repository;
        ResourceService service(repository, "cn-neepu");
        auto invalid = school();
        invalid.officialHomepage = QUrl("https://127.0.0.1/");
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 ResourceDiscovery(invalid, service, nullptr, options(directory)));
    }
    void practicalCategoriesAndExplicitAudiences_data() {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("category");
        QTest::addColumn<QString>("audience");
        QTest::newRow("undergraduate-plan")
            << QString("本科专业培养方案") << QString("study_plan") << QString("undergraduate");
        QTest::newRow("postgraduate-plan")
            << QString("博士研究生培养方案") << QString("study_plan") << QString("postgraduate");
        QTest::newRow("materials")
            << QString("课程学习资料") << QString("course_material") << QString();
        QTest::newRow("database") << QString("CNKI数据库") << QString("library") << QString();
        QTest::newRow("academic") << QString("学术写作与论文检索证明")
                                  << QString("academic_support") << QString();
        QTest::newRow("competition")
            << QString("学科竞赛平台") << QString("competition") << QString();
        QTest::newRow("retake-guide")
            << QString("重修安排查询指南") << QString("student_services") << QString();
        QTest::newRow("internship-guide")
            << QString("学生实习相关事宜办理指南") << QString("student_services") << QString();
        QTest::newRow("competition-certificate")
            << QString("学生竞赛获奖证书相关事宜指南") << QString("student_services") << QString();
        QTest::newRow("exam-query")
            << QString("大学外语等级考试查询指南") << QString("student_services") << QString();
        QTest::newRow("grade-review")
            << QString("异议成绩核对指南") << QString("student_services") << QString();
        QTest::newRow("classroom-request")
            << QString("非教学活动使用教室申请指南") << QString("student_services") << QString();
        QTest::newRow("regular-exam-query")
            << QString("正考安排查询指南") << QString("student_services") << QString();
        QTest::newRow("funding") << QString("学生资助办事指南") << QString("student_services")
                                 << QString();
        QTest::newRow("career") << QString("就业服务与求职指南") << QString("career") << QString();
        QTest::newRow("campus") << QString("校园卡办理指南") << QString("campus_life") << QString();
        QTest::newRow("explicit-general")
            << QString("全校师生通用资源数据库") << QString("library") << QString("general");
    }
    void practicalCategoriesAndExplicitAudiences() {
        QFETCH(QString, title);
        QFETCH(QString, category);
        QFETCH(QString, audience);
        const QUrl url("https://lib.neepu.edu.cn/resource.htm");
        QVERIFY(ResourceClassifier::isPractical(title, url));
        const auto resource =
            ResourceClassifier::describe("cn-neepu", title, url, QUrl("https://www.neepu.edu.cn/"));
        QCOMPARE(QString::fromStdString(resource.category), category);
        if (audience.isEmpty())
            QVERIFY(resource.audiences.empty());
        else
            QCOMPARE(resource.audiences, std::vector<std::string>{audience.toStdString()});
        QCOMPARE(resource.status, std::string("discovered"));
        QVERIFY(resource.lastCheckedAt.empty());
        QVERIFY(!resource.favorite);
    }
    void noticesAreNotResourcesAndStaticShellsAreNotVerified() {
        for (const auto *title :
             {"关于培养方案修订的通知", "学生奖学金名单公示", "图书馆开展安全检查",
              "学校召开数据库建设会议", "竞赛获奖新闻", "图书馆举办电子资源使用指南培训"})
            QVERIFY2(!ResourceClassifier::isPractical(QString::fromUtf8(title), {}), title);
        const QByteArray shell =
            "<html><title>图书馆</title><body><div "
            "id='app'></div><script>document.write('数据库')</script></body></html>";
        QVERIFY(ResourceClassifier::staticText(shell).isEmpty());
        QVERIFY(!ResourceClassifier::unverifiedReason(shell, ResourceClassifier::staticText(shell),
                                                      false)
                     .isEmpty());
        QVERIFY(!ResourceClassifier::unverifiedReason("<html></html>", "栏目建设中，暂无内容", true)
                     .isEmpty());
        QVERIFY(ResourceClassifier::isLoginPage(
            "<title>统一身份认证</title><input type='password'>", "统一身份认证",
            QUrl("https://sso.neepu.edu.cn/cas/login")));
        QVERIFY(!ResourceClassifier::isLoginPage("<title>统一身份认证使用指南</title>",
                                                 "统一身份认证使用指南",
                                                 QUrl("https://lib.neepu.edu.cn/help")));
    }
    void versionsRequireStudyPlanContextAndDatesRemainSeparate() {
        QCOMPARE(ResourceClassifier::labelInContext("2025版", "study_plan"),
                 QString("培养方案 · 2025版"));
        QCOMPARE(ResourceClassifier::labelInContext("2025版", "library"), QString("2025版"));
        QCOMPARE(ResourceClassifier::labelInContext("2025", "study_plan"), QString("2025"));
        QCOMPARE(ResourceClassifier::labelInContext("2025年", "study_plan"), QString("2025年"));
        QCOMPARE(ResourceClassifier::labelInContext("2025-10-092025年博士研究生培养方案（25级适用）"),
                 QString("2025年博士研究生培养方案（25级适用）"));
        QCOMPARE(ResourceClassifier::labelInContext("2025-02-302025年培养方案"),
                 QString("2025-02-302025年培养方案"));
    }
    void officialHtmlFindsExternalAndDownloadLinksWithoutFetchingThem() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MemoryResources repository;
        ResourceService service(repository, "cn-neepu");
        ResourceDiscovery discovery(school(), service, nullptr, options(directory));
        const QUrl origin("https://lib.neepu.edu.cn/");
        ResourceDiscovery::Page page{origin,   origin, QUrl("https://www.neepu.edu.cn/"),
                                     "图书馆", 0,      0};
        discovery.consume(
            page,
            "<html><title>图书馆</title><body><a href='https://www.cnki.net/'>CNKI数据库</a>"
            "<a href='/plans.pdf'>2025本科培养方案.pdf</a><a href='/guide.htm#one'>办事指南</a>"
            "<a href='/guide.htm#two'>办事指南</a><a "
            "href='https://@lib.neepu.edu.cn/evil'>数据库非法账号</a>"
            "<a href='https://lib.neepu.edu.cn:/evil-port'>数据库非法端口</a>"
            "<a href='https://127.0.0.1/'>数据库IP</a><a "
            "href='http://other.org/'>数据库HTTP</a></body></html>",
            {});
        const auto &external = find(repository, "https://www.cnki.net/");
        QCOMPARE(external.linkKind, std::string("official_recommended"));
        QCOMPARE(external.status, std::string("discovered"));
        QCOMPARE(external.discoveredFrom, origin.toString().toStdString());
        QVERIFY(external.lastCheckedAt.empty());
        const auto &download = find(repository, "https://lib.neepu.edu.cn/plans.pdf");
        QCOMPARE(download.status, std::string("discovered"));
        QVERIFY(download.lastCheckedAt.empty());
        QCOMPARE(download.audiences, std::vector<std::string>{"undergraduate"});
        QCOMPARE(discovery.queue_.size(), size_t(1));
        QCOMPARE(discovery.queue_.front().url, QUrl("https://lib.neepu.edu.cn/guide.htm"));
        QCOMPARE(repository.rows.size(), size_t(4));
        QCOMPARE(find(repository, origin.toString()).status, std::string("verified"));
        QVERIFY(!discovery.reply_);
    }
    void rediscoveredExternalLinksPreserveReviewedEntranceAndUsageEvidence() {
        QTemporaryDir directory;
        MemoryResources repository;
        ResourceService service(repository, "cn-neepu");
        const QUrl origin("https://lib.neepu.edu.cn/");
        auto checked = ResourceClassifier::describe("cn-neepu", "CNKI数据库",
                           QUrl("https://www.cnki.net/"), origin);
        checked.linkKind = "official_recommended";
        checked.status = "unreachable";
        checked.lastCheckedAt = "2026-10-03T00:00:00Z";
        checked.error = "HTTP 403; 自动检查受限";
        checked.accessNote = "学校历史说明列出部分采购；当前授权待核实。";
        checked.accessEvidence = "https://lib.neepu.edu.cn/info/1381/1602.htm";
        service.ingest({checked});
        service.setFavorite(checked.id, true);
        ResourceDiscovery discovery(school(), service, nullptr, options(directory));
        discovery.start();
        discovery.cancel();
        ResourceDiscovery::Page page{origin, origin, school().officialHomepage, "图书馆", 0, 0};
        discovery.consume(page, "<title>图书馆</title><a href='https://www.cnki.net/'>CNKI数据库</a>", {});
        const auto &kept = find(repository, "https://www.cnki.net/");
        QCOMPARE(kept.status, checked.status);
        QCOMPARE(kept.lastCheckedAt, checked.lastCheckedAt);
        QCOMPARE(kept.error, checked.error);
        QCOMPARE(kept.accessNote, checked.accessNote);
        QCOMPARE(kept.accessEvidence, checked.accessEvidence);
        QVERIFY(kept.favorite);
        QVERIFY(discovery.queue_.empty());
    }
    void latestFailureLoginAndShellReplaceCachedVerificationWithoutLosingFavorites() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MemoryResources repository;
        ResourceService service(repository, "cn-neepu");
        const QUrl target("https://jwc.neepu.edu.cn/services/");
        auto existing = ResourceClassifier::describe("cn-neepu", "学生服务大厅", target,
                                                     QUrl("https://www.neepu.edu.cn/"));
        existing.status = "verified";
        existing.lastCheckedAt = "2026-10-01T00:00:00Z";
        service.ingest({existing});
        service.setFavorite(existing.id, true);
        ResourceDiscovery discovery(school(), service, nullptr, options(directory));
        discovery.start();
        discovery.cancel();
        ResourceDiscovery::Page page{target,         target, QUrl("https://www.neepu.edu.cn/"),
                                     "学生服务大厅", 0,      0};
        discovery.consume(
            page, "<title>服务大厅</title><iframe src='browser-app'></iframe><script></script>",
            {});
        QCOMPARE(find(repository, target.toString()).status, std::string("discovered"));
        QVERIFY(find(repository, target.toString()).favorite);
        QVERIFY(!find(repository, target.toString()).lastCheckedAt.empty());
        discovery.consume(page, {}, "HTTP 403 Forbidden");
        QCOMPARE(find(repository, target.toString()).status, std::string("unreachable"));
        QVERIFY(find(repository, target.toString()).error.find("403") != std::string::npos);
        page.url = QUrl("https://sso.neepu.edu.cn/cas/login");
        discovery.consume(page, "<html><title>统一身份认证</title><input type='password'></html>",
                          {});
        const auto &login = find(repository, target.toString());
        QCOMPARE(login.status, std::string("login_required"));
        QCOMPARE(login.id, existing.id);
        QVERIFY(login.favorite);
        QVERIFY(!discovery.resources_.contains("https://sso.neepu.edu.cn/cas/login"));
        const auto tooLarge = QByteArray(2 * 1024 * 1024 + 1, 'x');
        discovery.consume(page, tooLarge, {});
        QCOMPARE(find(repository, target.toString()).status, std::string("unreachable"));
    }
    void maximumDepthAndImmediateCancelStayBounded() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MemoryResources repository;
        ResourceService service(repository, "cn-neepu");
        auto opt = options(directory);
        opt.maxPages = 1;
        opt.maxDepth = 2;
        ResourceDiscovery discovery(school(), service, nullptr, opt);
        ResourceDiscovery::Page page{QUrl("https://lib.neepu.edu.cn/deep/"),
                                     QUrl("https://lib.neepu.edu.cn/deep/"),
                                     QUrl("https://www.neepu.edu.cn/"),
                                     "图书馆",
                                     2,
                                     0};
        discovery.consume(page, "<title>图书馆</title><a href='/deeper/'>数据库导航</a>", {});
        QVERIFY(discovery.queue_.empty());
        QCOMPARE(find(repository, "https://lib.neepu.edu.cn/deeper/").status,
                 std::string("discovered"));
        QSignalSpy started(&discovery, &ResourceDiscovery::started),
            finished(&discovery, &ResourceDiscovery::finished);
        discovery.start();
        QVERIFY(discovery.busy());
        QCOMPARE(started.count(), 1);
        discovery.cancel();
        QVERIFY(!discovery.busy());
        QCOMPARE(finished.count(), 1);
        QTest::qWait(20);
        QCOMPARE(discovery.fetched_, 0);
        QVERIFY(!discovery.reply_);
        QVERIFY(!discovery.timer_.isActive());
    }
    void failedSeedsRemainVisibleAndDefaultNamesCannotVerifyResources() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MemoryResources repository;
        ResourceService service(repository, "cn-neepu");
        ResourceDiscovery discovery(school(), service, nullptr, options(directory));
        discovery.start();
        discovery.cancel();
        const QUrl url("https://lib.neepu.edu.cn/");
        const auto originalId = find(repository, url.toString()).id;
        QCOMPARE(find(repository, url.toString()).status, std::string("discovered"));
        QVERIFY(find(repository, url.toString()).lastCheckedAt.empty());
        service.setFavorite(originalId, true);
        ResourceDiscovery::Page page{url, url, school().officialHomepage, {}, 0, 0};
        discovery.consume(page, {}, "HTTP 502 Bad Gateway");
        QCOMPARE(find(repository, url.toString()).status, std::string("unreachable"));
        QVERIFY(find(repository, url.toString()).error.find("502") != std::string::npos);
        QVERIFY(!find(repository, url.toString()).lastCheckedAt.empty());
        QVERIFY(find(repository, url.toString()).favorite);
        discovery.consume(
            page,
            "<body>这是一个没有页面标题的静态HTML页面，不能通过默认资源入口名称来声称已验证可用。</"
            "body>",
            {});
        QCOMPARE(find(repository, url.toString()).status, std::string("discovered"));
        QVERIFY(!find(repository, url.toString()).error.empty());
        discovery.consume(page, fixture("neepu", "library"), {});
        const auto &verified = find(repository, url.toString());
        QCOMPARE(verified.id, originalId);
        QCOMPARE(verified.title, std::string("东北电力大学图书馆"));
        QCOMPARE(verified.category, std::string("library"));
        QCOMPARE(verified.status, std::string("verified"));
        QVERIFY(verified.favorite);
    }
    void optionalSchoolResourceSeedsUseOnlyOfficialHttps() {
        for (const auto *name :
             {"neepu.example.json", "cust.auto.json", "jlu.auto.json", "beihua.auto.json"}) {
            const auto pack = SchoolPackage::load(QString(SCHOOL_CONFIG_DIR) + "/" + name);
            QVERIFY(!pack.officialHomepage.isEmpty());
            QVERIFY(!pack.resourceDiscoveryEntries.empty());
            QCOMPARE(pack.resourceDiscoveryLimit, 32);
            for (const auto &entry : pack.resourceDiscoveryEntries)
                QVERIFY(ResourceClassifier::isOfficial(
                    QUrl(entry), ResourceClassifier::officialRoot(pack.officialHomepage)));
        }
        QFile original(QString(SCHOOL_CONFIG_DIR) + "/neepu.example.json");
        QVERIFY(original.open(QIODevice::ReadOnly));
        auto json = QJsonDocument::fromJson(original.readAll()).object();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("school.json");
        const auto write = [&] {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly))
                throw std::runtime_error("isolated configuration write failed");
            file.write(QJsonDocument(json).toJson());
        };
        json.remove("resource_discovery");
        write();
        const auto compatible = SchoolPackage::load(path);
        QVERIFY(compatible.resourceDiscoveryEntries.empty());
        QCOMPARE(compatible.resourceDiscoveryLimit, 32);
        for (const auto *url : {"https://evil.org/", "https://www.neepu.edu.cn.evil.org/",
                                "https://@lib.neepu.edu.cn/", "https://lib.neepu.edu.cn:/",
                                "http://lib.neepu.edu.cn/"}) {
            json["resource_discovery"] = QJsonObject{{"department_urls", QJsonArray{url}}};
            write();
            QVERIFY_THROWS_EXCEPTION(std::runtime_error, SchoolPackage::load(path));
        }
    }
    void realNeepuCorpusContainsPlansRetakeGuidesAndOfficialRecommendations() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MemoryResources repository;
        ResourceService service(repository, "cn-neepu");
        ResourceDiscovery discovery(school(), service, nullptr, options(directory));
        const QList<QPair<QString, QString>> pages{
            {"library", "https://lib.neepu.edu.cn/"},
            {"library-electronic", "https://lib.neepu.edu.cn/dzzy.htm"},
            {"study-plans", "https://jwc.neepu.edu.cn/rcpy/pyfa.htm"},
            {"student-guides", "https://jwc.neepu.edu.cn/fwzn/xsfw.htm"},
            {"postgraduate-plans", "https://ee.neepu.edu.cn/yjsjy/pyfa.htm"}};
        for (const auto &[name, url] : pages) {
            ResourceDiscovery::Page page{QUrl(url), QUrl(url), school().officialHomepage, {}, 0, 0};
            discovery.consume(page, fixture("neepu", name), {});
            QCOMPARE(find(repository, url).status, std::string("verified"));
        }
        QVERIFY(repository.rows.size() >= 25);
        const auto &version = find(repository, "https://jwc.neepu.edu.cn/rcpy/pyfa/py2025b.htm");
        QCOMPARE(version.title, std::string("培养方案 · 2025版"));
        QCOMPARE(version.category, std::string("study_plan"));
        QCOMPARE(version.status, std::string("discovered"));
        QVERIFY(version.lastCheckedAt.empty());
        const auto &retake = find(repository, "https://jwc.neepu.edu.cn/info/1054/1655.htm");
        QVERIFY(retake.title.find("重修安排查询指南") != std::string::npos);
        QCOMPARE(retake.category, std::string("student_services"));
        QCOMPARE(retake.status, std::string("discovered"));
        QVERIFY(retake.lastCheckedAt.empty());
        const auto &cnki = find(repository, "https://www.cnki.net/");
        QCOMPARE(cnki.linkKind, std::string("official_recommended"));
        QCOMPARE(cnki.status, std::string("discovered"));
        QVERIFY(std::any_of(repository.rows.begin(), repository.rows.end(), [](const auto &row) {
            return row.second.category == "study_plan" &&
                   std::find(row.second.audiences.begin(), row.second.audiences.end(),
                             "postgraduate") != row.second.audiences.end();
        }));
        QVERIFY(std::none_of(repository.rows.begin(), repository.rows.end(), [](const auto &row) {
            return row.second.url.find("202.198.14.5") != std::string::npos;
        }));
    }
    void jluJavascriptLibraryRemainsUnverified() {
        const auto pack = SchoolPackage::load(QString(SCHOOL_CONFIG_DIR) + "/jlu.auto.json");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MemoryResources repository;
        ResourceService service(repository, "cn-jlu");
        ResourceDiscovery discovery(pack, service, nullptr, options(directory));
        const QUrl url("https://lib.jlu.edu.cn/");
        ResourceDiscovery::Page page{url, url, pack.officialHomepage, "吉林大学图书馆", 0, 0};
        discovery.consume(page, fixture("jlu", "library"), {});
        QCOMPARE(find(repository, url.toString()).status, std::string("discovered"));
        QVERIFY(!find(repository, url.toString()).error.empty());
    }
};
QTEST_GUILESS_MAIN(ResourceDiscoveryTests)
#include "ResourceDiscoveryTests.moc"
