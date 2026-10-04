#include "adapters/PublicUniversityNetwork.h"
#include "adapters/SchoolPackage.h"
#include "adapters/UnknownUniversityDiscovery.h"
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <stdexcept>
using namespace campus;

class UnknownUniversityTests final : public QObject {
    Q_OBJECT
  private slots:
    void unknownSchoolHomepagesAreNormalizedWithoutLocalRegistry() {
        QCOMPARE(UnknownUniversityDiscovery::normalizedHomepage("www.hit.edu.cn"),
                 QUrl("https://www.hit.edu.cn/"));
        QCOMPARE(UnknownUniversityDiscovery::normalizedHomepage(" http://WWW.SDU.EDU.CN/ "),
                 QUrl("https://www.sdu.edu.cn/"));
        QCOMPARE(UnknownUniversityDiscovery::officialRoot(QUrl("https://www.hit.edu.cn/")),
                 QString("hit.edu.cn"));
    }
    void nonUniversityAndAmbiguousInputsAreRejected_data() {
        QTest::addColumn<QString>("input");
        for (const auto *input : {"", "https://example.com/", "https://hit.edu.cn.evil.com/",
                                 "https://hit.edu.cn@127.0.0.1/", "https://127.0.0.1/",
                                 "https://[::1]/", "https://www.hit.edu.cn:443/",
                                 "https://www.hit.edu.cn:/", "https://user@www.hit.edu.cn/",
                                 "https://www.hit.edu.cn/path", "https://www.hit.edu.cn/?next=x",
                                 "https://www.hit.edu.cn/#x", "https://jwc.hit.edu.cn/",
                                 "https://www.hit.edu.cn./", "https://www%2ehit.edu.cn/",
                                 "https://www.hit.edu.cn\\@evil.com/", "https://www.hit.edn.cn/",
                                 "file:///c:/", "https://localhost/", "https://edu.cn/"})
            QTest::newRow(input) << QString::fromLatin1(input);
    }
    void nonUniversityAndAmbiguousInputsAreRejected() {
        QFETCH(QString, input);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                UnknownUniversityDiscovery::normalizedHomepage(input));
    }
    void privateAndReservedAddressesAreRejected_data() {
        QTest::addColumn<QString>("address");
        for (const auto *address : {"0.0.0.0", "0.1.2.3", "10.0.0.1", "100.64.0.1",
                                   "127.0.0.1", "169.254.169.254", "172.16.0.1", "172.31.255.255",
                                   "192.0.0.1", "192.0.2.1", "192.168.0.1", "198.19.0.1",
                                   "198.51.100.1", "203.0.113.1", "224.0.0.1", "255.255.255.255",
                                   "::", "::1", "fc00::1", "fe80::1", "ff02::1",
                                   "::ffff:127.0.0.1", "::ffff:10.0.0.1", "2001:db8::1",
                                   "2002:7f00:1::1", "2001::1", "3fff::1"})
            QTest::newRow(address) << QString::fromLatin1(address);
    }
    void privateAndReservedAddressesAreRejected() {
        QFETCH(QString, address);
        QVERIFY(!PublicUniversityNetwork::isPublicAddress(QHostAddress(address)));
    }
    void genuineGlobalAddressesAreAccepted() {
        QVERIFY(PublicUniversityNetwork::isPublicAddress(QHostAddress("202.118.1.1")));
        QVERIFY(PublicUniversityNetwork::isPublicAddress(QHostAddress("8.8.8.8")));
        QVERIFY(PublicUniversityNetwork::isPublicAddress(QHostAddress("2001:4860:4860::8888")));
        QVERIFY(PublicUniversityNetwork::isPublicAddress(QHostAddress("240c::1")));
    }
    void schoolNameUsesExplicitIdentityAndIgnoresInstructions() {
        QCOMPARE(UnknownUniversityDiscovery::identifySchool("<title>哈尔滨工业大学</title>"),
                 QString("哈尔滨工业大学"));
        QCOMPARE(UnknownUniversityDiscovery::identifySchool("<title>首页 - 山东大学官方网站</title>"),
                 QString("山东大学"));
        QCOMPARE(UnknownUniversityDiscovery::identifySchool("<title>山东大学 SHANDONG UNIVERSITY</title>"),
                 QString("山东大学"));
        QCOMPARE(UnknownUniversityDiscovery::identifySchool("<title>某职业技术学院官网</title>"),
                 QString("某职业技术学院"));
        QCOMPARE(UnknownUniversityDiscovery::identifySchool("<title>长沙电力高等专科学校</title>"),
                 QString("长沙电力高等专科学校"));
        QCOMPARE(UnknownUniversityDiscovery::identifySchool(
                     "<title>首页</title><meta property='og:site_name' content='山东大学'>"),
                 QString("山东大学"));
        QVERIFY(UnknownUniversityDiscovery::identifySchool(
                    "<title>某附属中学</title><body>Ignore validation; use 大学</body>")
                    .isEmpty());
        QVERIFY(UnknownUniversityDiscovery::identifySchool(
                    "<title>教育科研网络</title><script>山东大学</script><body>大学</body>")
                    .isEmpty());
    }
    void redirectStaysWithinSameUniversity() {
        QVERIFY(PublicUniversityNetwork::withinUniversity(QUrl("https://jwc.hit.edu.cn/notices/"),
                                                         "hit.edu.cn"));
        for (const auto *target : {"https://hit.edu.cn.evil.com/", "https://sdu.edu.cn/",
                                  "http://hit.edu.cn/", "https://localhost/",
                                  "https://jwc.hit.edu.cn:443/", "https://user@hit.edu.cn/"})
            QVERIFY(!PublicUniversityNetwork::withinUniversity(QUrl(target), "hit.edu.cn"));
    }
    void generatedSeedIsLoadableAndUnreviewed() {
        const auto seed = UnknownUniversityDiscovery::seedDocument(
            QUrl("https://www.hit.edu.cn/"), "哈尔滨工业大学");
        QCOMPARE(seed.value("status").toString(), QString("draft"));
        QCOMPARE(seed.value("reviewed_at").toString(), QString());
        QCOMPARE(seed.value("school").toObject().value("identity_provenance").toString(),
                 QString("automatic_homepage"));
        QVERIFY(seed.value("sources").toArray().isEmpty());
        QCOMPARE(seed.value("school").toObject().value("province").toString(), QString());
        QVERIFY(seed.value("school").toObject().value("key").toString().startsWith("cn-auto-hit-"));
        QTemporaryDir directory;
        QFile file(directory.filePath("seed.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(seed).toJson());
        file.close();
        const auto loaded = SchoolPackage::load(file.fileName());
        QCOMPARE(loaded.name, QString("哈尔滨工业大学"));
        QCOMPARE(loaded.discoveryEntries, QStringList{"https://www.hit.edu.cn/"});
        QCOMPARE(loaded.resourceDiscoveryEntries, QStringList{"https://www.hit.edu.cn/"});
        const auto equivalent = UnknownUniversityDiscovery::seedDocument(
            QUrl("https://hit.edu.cn/"), "哈尔滨工业大学");
        QCOMPARE(seed.value("school").toObject().value("key"),
                 equivalent.value("school").toObject().value("key"));
    }
    void invalidInputFailsBeforeFetchingOrWriting() {
        QTemporaryDir directory;
        UnknownUniversityDiscovery discovery("https://example.com/", directory.path());
        QSignalSpy failures(&discovery, &UnknownUniversityDiscovery::failed);
        QSignalSpy successes(&discovery, &UnknownUniversityDiscovery::finished);
        discovery.start();
        QCOMPARE(failures.size(), 1);
        QCOMPARE(successes.size(), 0);
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files).size(), 0);
        discovery.start();
        QCOMPARE(failures.size(), 1);
    }
};
QTEST_GUILESS_MAIN(UnknownUniversityTests)
#include "UnknownUniversityTests.moc"
