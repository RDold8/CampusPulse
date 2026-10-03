#include "adapters/UniversityRegistry.h"
#include "adapters/SchoolPackage.h"
#include <QtTest>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <stdexcept>

using namespace campus;

namespace {

void writePackage(const QString &filename, const QString &id, const QString &officialHomepage,
                  const QString &timeZone = "Asia/Shanghai") {
    const QJsonObject package{
        {"schema_version", "0.1-draft"},
        {"school", QJsonObject{{"key", id},
                               {"name", "测试大学"},
                               {"official_homepage", officialHomepage},
                               {"timezone", timeZone}}},
        {"sources",
         QJsonArray{QJsonObject{{"key", "pending"}, {"name", "待接入来源"}, {"enabled", false}}}}};
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(package).toJson()) < 0)
        throw std::runtime_error("无法写入测试学校包");
}

} // namespace

class UniversityRegistryTests final : public QObject {
    Q_OBJECT

  private slots:
    void installedSchoolAcceptsExactHomepage();
    void rejectsUnsafeOrUnregisteredInput_data();
    void rejectsUnsafeOrUnregisteredInput();
    void onlyInstalledSchoolPackagesAreAccepted();
    void invalidCommunityPackagesFailClearly();
    void invalidSchoolTimeZonesAreRejected();
};

void UniversityRegistryTests::installedSchoolAcceptsExactHomepage() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto copied = directory.filePath("neepu.json");
    QVERIFY(QFile::copy(CONFIG_FILE, copied));
    UniversityRegistry registry(directory.path());
    QCOMPARE(registry.list().size(), std::size_t(1));
    for (const auto &input :
         {"www.neepu.edu.cn", "www.neepu.edu.cn/", "https://www.neepu.edu.cn/",
          "http://www.neepu.edu.cn", "HTTPS://WWW.NEEPU.EDU.CN/", "  www.neepu.edu.cn  "}) {
        const auto selected = registry.resolve(input);
        QCOMPARE(selected.id, QString("cn-neepu"));
        QCOMPARE(selected.name, QString("东北电力大学"));
        QCOMPARE(selected.homepage, QUrl("https://www.neepu.edu.cn/"));
        QCOMPARE(selected.configFile, QFileInfo(copied).canonicalFilePath());
    }
}

void UniversityRegistryTests::rejectsUnsafeOrUnregisteredInput_data() {
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("wellFormedUnknown");
    const QStringList invalid{"",
                              "https://www.neepu.edu.cn/path",
                              "www.neepu.edu.cn/cs1.htm",
                              "https://www.neepu.edu.cn?",
                              "https://www.neepu.edu.cn?redirect=evil",
                              "https://www.neepu.edu.cn#",
                              "https://www.neepu.edu.cn#fragment",
                              "https://user:password@www.neepu.edu.cn/",
                              "https://@www.neepu.edu.cn/",
                              "https://www.neepu.edu.cn:443/",
                              "http://www.neepu.edu.cn:80/",
                              "https://www.neepu.edu.cn:8080/",
                              "http://127.0.0.1/",
                              "http://[::1]/",
                              "http://localhost/",
                              "http://school.localhost/",
                              "http://campus.local/",
                              "http://2130706433/",
                              "http://0x7f000001/",
                              "file:///C:/Windows/system.ini",
                              "ftp://www.neepu.edu.cn/",
                              "javascript:alert(1)",
                              "https://%77ww.neepu.edu.cn/",
                              "https://www.neepu.edu.cn%2eevil.org/",
                              "https://ｗｗｗ.neepu.edu.cn/",
                              "https://www.neepu.edu.cn\\@evil.org/",
                              "https://www.neepu.edu.cn /",
                              "https://www.neepu.edu.cn./"};
    for (int i = 0; i < invalid.size(); ++i)
        QTest::newRow(qPrintable(QString("invalid-%1").arg(i))) << invalid.at(i) << false;
    const QStringList unknown{"https://www.neepu.edu.cn.evil.org/",
                              "https://jwc.neepu.edu.cn/",
                              "https://neepu.edu.cn/",
                              "https://www.other-university.edu.cn/",
                              "https://neepu-edu.cn/",
                              "https://www.xn--p1ai.edu.cn/",
                              "https://www.neepu.edu.com/",
                              "https://example.com/"};
    for (int i = 0; i < unknown.size(); ++i)
        QTest::newRow(qPrintable(QString("unknown-%1").arg(i))) << unknown.at(i) << true;
}

void UniversityRegistryTests::rejectsUnsafeOrUnregisteredInput() {
    QFETCH(QString, input);
    QFETCH(bool, wellFormedUnknown);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QFile::copy(CONFIG_FILE, directory.filePath("neepu.json")));
    const UniversityRegistry registry(directory.path());
    bool rejected = false;
    try {
        registry.resolve(input);
    } catch (const std::runtime_error &error) {
        rejected = true;
        const auto reason = QString::fromUtf8(error.what());
        QVERIFY(!reason.isEmpty());
        if (wellFormedUnknown)
            QVERIFY(reason.contains("尚未收录核验"));
        else
            QVERIFY(reason.contains("请输入高校官网"));
    }
    QVERIFY2(rejected, qPrintable(input));
}

void UniversityRegistryTests::onlyInstalledSchoolPackagesAreAccepted() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    writePackage(directory.filePath("alpha.json"), "cn-alpha", "https://www.alpha.edu.cn/");
    UniversityRegistry first(directory.path());
    QCOMPARE(first.list().size(), std::size_t(1));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, first.resolve("https://www.beta.edu.cn/"));
    writePackage(directory.filePath("beta.json"), "cn-beta", "https://www.beta.edu.cn/");
    // Registry additions come from a new validated package load, never from resolving a URL.
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, first.resolve("https://www.beta.edu.cn/"));
    UniversityRegistry updated(directory.path());
    QCOMPARE(updated.list().size(), std::size_t(2));
    QCOMPARE(updated.resolve("www.beta.edu.cn").id, QString("cn-beta"));
}

void UniversityRegistryTests::invalidCommunityPackagesFailClearly() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto filename = directory.filePath("broken.json");
    for (const auto &official : {"http://127.0.0.1/", "https://www.alpha.edu.cn/path",
                                 "https://user@www.alpha.edu.cn/", "www.alpha.edu.cn"}) {
        writePackage(filename, "cn-alpha", official);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, UniversityRegistry(directory.path()));
    }
    writePackage(filename, "cn-alpha", "https://www.alpha.edu.cn/");
    writePackage(directory.filePath("duplicate.json"), "cn-alpha", "https://www.beta.edu.cn/");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, UniversityRegistry(directory.path()));
    writePackage(directory.filePath("duplicate.json"), "cn-beta", "https://www.alpha.edu.cn/");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, UniversityRegistry(directory.path()));
}

void UniversityRegistryTests::invalidSchoolTimeZonesAreRejected() {
    QTemporaryDir directory;
    const auto filename = directory.filePath("zone.json");
    for (const auto &timeZone : {QString{}, QString("Mars/Orbit")}) {
        writePackage(filename, "cn-alpha", "https://www.alpha.edu.cn/", timeZone);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, UniversityRegistry(directory.path()));
    }
    writePackage(filename, "cn-alpha", "https://www.alpha.edu.cn/", "Asia/Shanghai");
    const UniversityRegistry registry(directory.path());
    QCOMPARE(SchoolPackage::load(registry.resolve("www.alpha.edu.cn").configFile).timeZone,
             QString("Asia/Shanghai"));
}

QTEST_GUILESS_MAIN(UniversityRegistryTests)
#include "UniversityRegistryTests.moc"
