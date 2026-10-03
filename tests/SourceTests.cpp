#include "adapters/RefreshCoordinator.h"
#include "application/NoticeService.h"
#include "application/SourceService.h"
#include "storage/Database.h"
#include "storage/SqliteMigrations.h"
#include "storage/SqliteRepository.h"
#include "storage/SqliteSourceRepository.h"
#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <stdexcept>

using namespace campus;

namespace {
// This server never contacts a university. Its request log proves source isolation and allows
// deterministic HTTP, parsing, pagination and timeout failures at the real network boundary.
class LocalHttpServer final : public QObject {
  public:
    struct Response {
        int status = 200;
        QByteArray body;
        bool hang = false;
    };
    QHash<QString, Response> routes;
    QStringList requests;

    LocalHttpServer() {
        if (!server_.listen(QHostAddress::LocalHost, 0))
            throw std::runtime_error(server_.errorString().toStdString());
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (server_.hasPendingConnections()) {
                auto *socket = server_.nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    if (socket->property("handled").toBool())
                        return;
                    auto bytes = socket->property("requestBytes").toByteArray();
                    bytes += socket->readAll();
                    socket->setProperty("requestBytes", bytes);
                    if (!bytes.contains("\r\n\r\n"))
                        return;
                    socket->setProperty("handled", true);
                    const auto firstLine = bytes.left(bytes.indexOf("\r\n")).split(' ');
                    const auto target = QString::fromUtf8(firstLine.value(1));
                    requests << target;
                    const auto response = routes.value(target, {404, "missing fixture", false});
                    if (response.hang)
                        return;
                    const auto statusText = response.status == 200 ? "OK" : "Fixture Failure";
                    QByteArray wire = "HTTP/1.1 " + QByteArray::number(response.status) + " " +
                                      statusText + "\r\nContent-Type: text/html; charset=utf-8\r\n";
                    wire += "Content-Length: " + QByteArray::number(response.body.size()) +
                            "\r\nConnection: close\r\n\r\n" + response.body;
                    socket->write(wire);
                    socket->disconnectFromHost();
                });
            }
        });
    }

    QUrl url(const QString &path) const {
        return QUrl(QString("http://127.0.0.1:%1%2").arg(server_.serverPort()).arg(path));
    }

  private:
    QTcpServer server_;
};

QByteArray listHtml(const QString &title, const QString &noticePath, const QString &nextPath = {}) {
    QString html = QString("<html><ul><li><a href='%1'>%2</a>"
                           "<span>2026-10-02</span></li></ul>")
                       .arg(noticePath, title);
    if (!nextPath.isEmpty())
        html += QString("<a class='Next' href='%1'>下一页</a>").arg(nextPath);
    return (html + "</html>").toUtf8();
}

SourceConfig testSource(const LocalHttpServer &server, const QString &id, const QString &entry,
                        int maxPages = 1) {
    SourceConfig source;
    source.schoolId = "cn-fixture";
    source.id = id;
    source.name = "测试来源 " + id;
    source.entry = server.url(entry);
    source.allowedHosts = {"127.0.0.1"};
    source.itemSelector = "li";
    source.titleSelector = "a[href]";
    source.dateSelector = "span";
    source.bodySelector = "div";
    source.nextPageSelector = "a.Next";
    source.maxPages = maxPages;
    return source;
}

SourceDescription description(const SourceConfig &source) {
    SourceDescription result;
    result.schoolId = source.schoolId.toStdString();
    result.id = source.id.toStdString();
    result.name = source.name.toStdString();
    result.entryUrl = source.entry.toString().toStdString();
    result.discoveryUrl = result.entryUrl;
    result.categories = {"academic_affairs"};
    result.maxPages = source.maxPages;
    result.configuredEnabled = true;
    result.ready = true;
    return result;
}

SchoolPackage testSchool(const std::vector<SourceConfig> &sources) {
    SchoolPackage school;
    school.id = "cn-fixture";
    school.name = "本地测试学校";
    school.sources = sources;
    for (const auto &source : sources)
        school.catalog.push_back(description(source));
    return school;
}

QJsonObject schoolJson() {
    QFile file(CONFIG_FILE);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Missing school configuration fixture");
    return QJsonDocument::fromJson(file.readAll()).object();
}

SchoolPackage loadCatalogFixture(const QTemporaryDir &folder, QJsonArray sources) {
    auto root = schoolJson();
    root["sources"] = sources;
    QFile file(folder.filePath("access.json"));
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error("Cannot write isolated access fixture");
    const auto bytes = QJsonDocument(root).toJson();
    if (file.write(bytes) != bytes.size())
        throw std::runtime_error("Cannot finish isolated access fixture");
    file.close();
    return SchoolPackage::load(file.fileName());
}

QJsonObject loginSourceFixture() {
    auto source = schoolJson().value("sources").toArray().first().toObject();
    source["key"] = "login-fixture";
    source["entry_url"] = "https://jwc.neepu.edu.cn/";
    source["discovery_url"] = "https://jwc.neepu.edu.cn/";
    source["allowed_hosts"] = QJsonArray{"jwc.neepu.edu.cn"};
    source["enabled"] = false;
    source["pending"] = QJsonArray{"此入口需要登录，未采集受限内容。"};
    return source;
}

struct FrozenDatabase {
    int version = -1;
    QList<QVariantList> notices;
    QList<QVariantList> revisions;
};

class RawConnection final {
  public:
    explicit RawConnection(const QString &filename)
        : name_("legacy-fixture-" + QUuid::createUuid().toString()),
          database_(QSqlDatabase::addDatabase("QSQLITE", name_)) {
        database_.setDatabaseName(filename);
        if (!database_.open()) {
            const auto error = database_.lastError().text();
            database_ = {};
            QSqlDatabase::removeDatabase(name_);
            throw std::runtime_error(error.toStdString());
        }
    }
    ~RawConnection() {
        database_.close();
        database_ = {};
        QSqlDatabase::removeDatabase(name_);
    }
    QSqlDatabase &connection() {
        return database_;
    }

  private:
    QString name_;
    QSqlDatabase database_;
};

void executeRaw(const QString &filename, const QStringList &statements) {
    RawConnection connection(filename);
    QSqlQuery query(connection.connection());
    for (const auto &sql : statements)
        if (!query.exec(sql))
            throw std::runtime_error(query.lastError().text().toStdString());
}

int rawScalar(const QString &filename, const QString &sql) {
    RawConnection connection(filename);
    QSqlQuery query(connection.connection());
    if (!query.exec(sql) || !query.next())
        throw std::runtime_error(query.lastError().text().toStdString());
    return query.value(0).toInt();
}

// Keep the shipped v1 schema frozen here; building it with current migrations would miss
// backwards-compatibility regressions when contributors run tests without private evidence.
void createFrozenVersionOne(const QString &filename) {
    executeRaw(filename,
               {"CREATE TABLE notices(id TEXT PRIMARY KEY,school_id TEXT NOT NULL,source_id "
                "TEXT NOT NULL,source_name TEXT NOT NULL,title TEXT NOT NULL,url TEXT NOT NULL,"
                "published_date TEXT NOT NULL,category TEXT NOT NULL,body TEXT NOT NULL "
                "DEFAULT '',attachments TEXT NOT NULL DEFAULT '[]')",
                "CREATE TABLE notice_revisions(id INTEGER PRIMARY KEY,notice_id TEXT NOT NULL "
                "REFERENCES notices(id),title TEXT NOT NULL,published_date TEXT NOT NULL,"
                "body TEXT NOT NULL,attachments TEXT NOT NULL,created_at TEXT NOT NULL "
                "DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')))",
                "CREATE INDEX notice_dates ON notices(published_date DESC)",
                "INSERT INTO notices VALUES('frozen-notice','cn-frozen','academic-notices',"
                "'冻结教务来源','重修缴费通知','https://fixture.invalid/payment','2026-09-16',"
                "'exam','请在规定时间缴费。','[{\"name\":\"指南\",\"url\":"
                "\"https://fixture.invalid/guide.pdf\"}]')",
                "INSERT INTO notice_revisions VALUES(1,'frozen-notice','重修缴费通知',"
                "'2026-09-16','请在规定时间缴费。','[{\"name\":\"指南\",\"url\":"
                "\"https://fixture.invalid/guide.pdf\"}]','2026-10-01T01:02:03.000Z')",
                "PRAGMA user_version=1"});
}

FrozenDatabase readFrozenDatabase(const QString &filename) {
    FrozenDatabase snapshot;
    const auto name = "snapshot-" + QUuid::createUuid().toString();
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
        db.setDatabaseName(filename);
        if (!db.open())
            throw std::runtime_error(db.lastError().text().toStdString());
        {
            QSqlQuery query(db);
            if (!query.exec("PRAGMA user_version") || !query.next())
                throw std::runtime_error("Cannot read fixture database version");
            snapshot.version = query.value(0).toInt();
            auto readRows = [&](const QString &sql, int columns, QList<QVariantList> &rows) {
                if (!query.exec(sql))
                    throw std::runtime_error(query.lastError().text().toStdString());
                while (query.next()) {
                    QVariantList row;
                    for (int column = 0; column < columns; ++column)
                        row << query.value(column);
                    rows << row;
                }
            };
            readRows("SELECT id,school_id,source_id,source_name,title,url,published_date,"
                     "category,body,attachments FROM notices ORDER BY id",
                     10, snapshot.notices);
            readRows("SELECT id,notice_id,title,published_date,body,attachments,created_at "
                     "FROM notice_revisions ORDER BY id",
                     7, snapshot.revisions);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return snapshot;
}
} // namespace

class SourceTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    }

    void catalogKeepsPendingAndAllDisabled() {
        const auto school = SchoolPackage::load(CONFIG_FILE);
        QCOMPARE(school.sources.size(), size_t(6));
        QCOMPARE(school.catalog.size(), size_t(7));
        const auto career =
            std::find_if(school.catalog.begin(), school.catalog.end(),
                         [](const auto &s) { return s.id == "career-public-listings"; });
        QVERIFY(career != school.catalog.end());
        QVERIFY(!career->configuredEnabled);
        QVERIFY(!career->ready);
        QVERIFY(career->entryUrl.empty());
        QCOMPARE(career->discoveryUrl, std::string("https://jy.neepu.edu.cn/"));
        QVERIFY(!career->pendingReason.empty());
        QVERIFY(std::find(career->categories.begin(), career->categories.end(), "career") !=
                career->categories.end());

        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QFile original(CONFIG_FILE);
        QVERIFY(original.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(original.readAll()).object();
        QJsonArray disabled;
        for (const auto &value : root.value("sources").toArray()) {
            auto item = value.toObject();
            item["enabled"] = false;
            disabled.append(item);
        }
        root["sources"] = disabled;
        QFile replacement(folder.path() + "/disabled.json");
        QVERIFY(replacement.open(QIODevice::WriteOnly));
        replacement.write(QJsonDocument(root).toJson());
        replacement.close();
        const auto allDisabled = SchoolPackage::load(replacement.fileName());
        QVERIFY(allDisabled.sources.empty());
        QCOMPARE(allDisabled.catalog.size(), size_t(7));
        QVERIFY(std::none_of(allDisabled.catalog.begin(), allDisabled.catalog.end(),
                             [](const auto &s) { return s.configuredEnabled; }));
        Database db(folder.path() + "/data.sqlite");
        SqliteSourceRepository repository(db);
        SourceService service(allDisabled.id.toStdString(), allDisabled.catalog, repository);
        for (const auto &view : service.list()) {
            QCOMPARE(view.effectiveStatus(), std::string("not_ready"));
            QVERIFY(view.state.lastSuccessAt.empty());
        }
    }

    void preferencesAreSchoolScopedAndPersist() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto path = folder.path() + "/data.sqlite";
        SourceDescription first;
        first.schoolId = "cn-first";
        first.id = "shared-source-key";
        first.name = "第一所大学";
        first.configuredEnabled = first.ready = true;
        auto second = first;
        second.schoolId = "cn-second";
        {
            Database db(path);
            SqliteSourceRepository repository(db);
            SourceService a(first.schoolId, {first}, repository);
            SourceService b(second.schoolId, {second}, repository);
            QVERIFY(a.isRunnable(first.id));
            QVERIFY(b.isRunnable(second.id));
            a.setPaused(first.id, true);
            QCOMPARE(a.find(first.id)->effectiveStatus(), std::string("paused"));
            QVERIFY(!a.isRunnable(first.id));
            QVERIFY(b.isRunnable(second.id));
            QVERIFY(b.find(second.id)->state.lastAttemptAt.empty());
            QVERIFY_THROWS_EXCEPTION(std::runtime_error, a.begin(first.id, "2026-10-02T00:00:00Z"));
            QVERIFY(!a.find("missing-source"));
        }
        {
            Database db(path);
            SqliteSourceRepository repository(db);
            SourceService a(first.schoolId, {first}, repository);
            SourceService b(second.schoolId, {second}, repository);
            QVERIFY(a.find(first.id)->state.paused);
            QVERIFY(!b.find(second.id)->state.paused);
            a.setPaused(first.id, false);
            QVERIFY(a.isRunnable(first.id));
            QCOMPARE(a.find(first.id)->state.status, std::string("never_checked"));
            QVERIFY(a.find(first.id)->state.lastSuccessAt.empty());
        }
    }

    void structuredLoginAccessIsValidatedBeforeDisabledSources() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        auto source = loginSourceFixture();
        source["access"] =
            QJsonObject{{"mode", "login_required"}, {"login_url", "https://jwc.neepu.edu.cn/"}};
        for (const bool enabled : {false, true}) {
            source["enabled"] = enabled;
            const auto school = loadCatalogFixture(folder, QJsonArray{source});
            QCOMPARE(school.catalog.size(), size_t(1));
            QVERIFY(school.sources.empty());
            const auto &description = school.catalog.front();
            QVERIFY(description.requiresLogin);
            QVERIFY(!description.ready);
            QCOMPARE(description.id, std::string("login-fixture"));
            QCOMPARE(description.loginUrl, std::string("https://jwc.neepu.edu.cn/"));
            SourceView view{description, {}};
            view.state.paused = true;
            view.state.status = "success";
            QCOMPARE(view.effectiveStatus(), std::string("login_required"));
        }
        source["enabled"] = false;
        // Even a host explicitly whitelisted by a malformed package must remain
        // within the school's official domain. Disabled entries are not exempt.
        source["allowed_hosts"] =
            QJsonArray{"jwc.neepu.edu.cn", "evil.org", "jwc.neepu.edu.cn.evil.org"};
        for (const auto *url :
             {"http://jwc.neepu.edu.cn/", "https://evil.org/", "https://jwc.neepu.edu.cn.evil.org/",
              "https://user@jwc.neepu.edu.cn/", "https://@jwc.neepu.edu.cn/",
              "https://jwc.neepu.edu.cn:443/", "https://jwc.neepu.edu.cn:/",
              "https://xsc.neepu.edu.cn/", "https://127.0.0.1/", "/login"}) {
            source["access"] = QJsonObject{{"mode", "login_required"}, {"login_url", url}};
            bool rejected = false;
            try {
                loadCatalogFixture(folder, QJsonArray{source});
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            QVERIFY2(rejected, url);
        }
        for (const auto &access :
             {QJsonObject{{"mode", "login_required"}}, QJsonObject{{"mode", "unknown"}},
              QJsonObject{{"mode", "public"}, {"login_url", "https://evil.org/"}},
              QJsonObject{{"mode", "public"}, {"cookies", "unsupported"}}}) {
            source["access"] = access;
            QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                     loadCatalogFixture(folder, QJsonArray{source}));
        }
        source["access"] = QJsonObject{{"mode", "public"}};
        const auto explicitlyPublic = loadCatalogFixture(folder, QJsonArray{source});
        QVERIFY(!explicitlyPublic.catalog.front().requiresLogin);
        QVERIFY(explicitlyPublic.catalog.front().loginUrl.empty());
    }

    void legacyLoginMetadataDoesNotMisclassifyHttpFailures() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        auto source = loginSourceFixture();
        const auto legacy = loadCatalogFixture(folder, QJsonArray{source});
        QVERIFY(legacy.catalog.front().requiresLogin);
        QCOMPARE(legacy.catalog.front().loginUrl, std::string("https://jwc.neepu.edu.cn/"));
        source["entry_url"] = "https://evil.org/";
        const auto fallback = loadCatalogFixture(folder, QJsonArray{source});
        QCOMPARE(fallback.catalog.front().loginUrl, std::string("https://jwc.neepu.edu.cn/"));
        source["discovery_url"] = "http://jwc.neepu.edu.cn/";
        const auto unsafe = loadCatalogFixture(folder, QJsonArray{source});
        QVERIFY(unsafe.catalog.front().requiresLogin);
        QVERIFY(unsafe.catalog.front().loginUrl.empty());
        source["entry_url"] = source["discovery_url"] = "https://jwc.neepu.edu.cn/";
        for (const auto *reason : {"HTTP 403 Forbidden", "HTTP 502 Bad Gateway", "此入口不需要登录",
                                   "此入口可能需要登录", "此入口疑似需要登录"}) {
            source["pending"] = QJsonArray{reason};
            const auto otherFailure = loadCatalogFixture(folder, QJsonArray{source});
            QVERIFY(!otherFailure.catalog.front().requiresLogin);
            QVERIFY(otherFailure.catalog.front().loginUrl.empty());
        }
    }

    void loginRequiredSourcesCannotRunResumeOrIssueRequests() {
        LocalHttpServer server;
        server.routes["/login"] = {200, listHtml("公开通知样本", "/detail"), false};
        auto school = testSchool({testSource(server, "login", "/login")});
        school.catalog.front().requiresLogin = true;
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        Database db(folder.filePath("login-gate.sqlite"));
        SqliteRepository noticeRepository(db);
        SqliteSourceRepository sourceRepository(db);
        NoticeService notices(noticeRepository);
        SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
        QVERIFY(!sources.isRunnable("login"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 sources.begin("login", "2026-10-03T00:00:00Z"));
        sources.setPaused("login", true);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, sources.setPaused("login", false));
        QVERIFY(sources.find("login")->state.paused);
        QCOMPARE(sources.find("login")->effectiveStatus(), std::string("login_required"));
        RefreshCoordinator coordinator(school, notices, sources, nullptr, {0, 1000});
        QSignalSpy finished(&coordinator, &RefreshCoordinator::finished);
        coordinator.refreshSource("login");
        coordinator.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
        QTest::qWait(20);
        QVERIFY(server.requests.isEmpty());
        QVERIFY(notices.list().empty());
        QVERIFY(sources.find("login")->state.lastAttemptAt.empty());
        QVERIFY(sources.find("login")->state.lastSuccessAt.empty());
    }

    void noticesRemainSchoolScopedWithSharedDatabase() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        Database db(folder.path() + "/data.sqlite");
        SqliteRepository repository(db);
        NoticeService a(repository, "cn-first");
        NoticeService b(repository, "cn-second");
        Notice first;
        first.id = "first-notice";
        first.schoolId = "cn-first";
        first.sourceId = "same-source-key";
        first.sourceName = "第一所大学教务处";
        first.title = "重修缴费";
        first.url = "https://first.invalid/notice";
        first.publishedDate = "2026-10-02";
        auto second = first;
        second.id = "second-notice";
        second.schoolId = "cn-second";
        second.sourceName = "第二所大学教务处";
        second.title = "补考申请";
        second.url = "https://second.invalid/notice";
        a.ingest({first});
        b.ingest({second});
        QCOMPARE(repository.list().size(), size_t(2));
        QCOMPARE(a.list().size(), size_t(1));
        QCOMPARE(b.list().size(), size_t(1));
        QCOMPARE(a.list().front().id, first.id);
        QCOMPARE(b.list().front().id, second.id);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, a.ingest({second}));
        second.body = "学校二正文";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, a.saveDetail(second));
        QCOMPARE(repository.revisionCount(second.id), 1);
        QVERIFY(b.list().front().body.empty());
        b.saveDetail(second);
        QCOMPARE(b.list().front().body, second.body);
        QCOMPARE(repository.revisionCount(second.id), 2);
    }

    void progressPreservesLastFullSuccess() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        Database db(folder.path() + "/data.sqlite");
        SqliteSourceRepository repository(db);
        SourceDescription source;
        source.schoolId = "cn-first";
        source.id = "source";
        source.name = "测试来源";
        source.configuredEnabled = source.ready = true;
        SourceService service(source.schoolId, {source}, repository);
        const auto success = service.begin(source.id, "2026-10-02T01:00:00Z");
        QCOMPARE(service.find(source.id)->state.status, std::string("updating"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 service.begin(source.id, "2026-10-02T01:00:01Z"));
        service.page(success, 3, "2026-09-27");
        service.finish(success, {}, "2026-10-02T01:01:00Z");
        auto state = service.find(source.id)->state;
        QCOMPARE(state.status, std::string("success"));
        QCOMPARE(state.successfulPages, 1);
        QCOMPARE(state.rowCount, 3);
        QCOMPARE(state.lastAttemptAt, std::string("2026-10-02T01:00:00Z"));
        QCOMPARE(state.lastSuccessAt, std::string("2026-10-02T01:01:00Z"));
        const auto partial = service.begin(source.id, "2026-10-02T02:00:00Z");
        service.page(partial, 7, "2026-09-01");
        service.page(partial, 4, "2026-10-02");
        service.finish(partial, "HTTP 503, page 3", "2026-10-02T02:01:00Z");
        state = service.find(source.id)->state;
        QCOMPARE(state.status, std::string("partial_success"));
        QCOMPARE(state.successfulPages, 2);
        QCOMPARE(state.rowCount, 11);
        QCOMPARE(state.latestPublishedDate, std::string("2026-10-02"));
        QCOMPARE(state.lastSuccessAt, std::string("2026-10-02T01:01:00Z"));
        QVERIFY(state.error.find("503") != std::string::npos);
        const auto failure = service.begin(source.id, "2026-10-02T03:00:00Z");
        service.finish(failure, "network timeout", "2026-10-02T03:01:00Z");
        state = service.find(source.id)->state;
        QCOMPARE(state.status, std::string("failure"));
        QCOMPARE(state.successfulPages, 0);
        QCOMPARE(state.rowCount, 0);
        QCOMPARE(state.lastAttemptAt, std::string("2026-10-02T03:00:00Z"));
        QCOMPARE(state.lastSuccessAt, std::string("2026-10-02T01:01:00Z"));
        QCOMPARE(state.latestPublishedDate, std::string("2026-10-02"));
    }

    void restartRecoversOnlyInterruptedSchool() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto path = folder.path() + "/data.sqlite";
        SourceDescription first;
        first.schoolId = "cn-first";
        first.id = "shared-source-key";
        first.configuredEnabled = first.ready = true;
        auto second = first;
        second.schoolId = "cn-second";
        {
            Database db(path);
            SqliteSourceRepository repository(db);
            SourceService a(first.schoolId, {first}, repository);
            SourceService b(second.schoolId, {second}, repository);
            const auto run = a.begin(first.id, "2026-10-02T01:00:00Z");
            a.page(run, 2, "2026-09-30");
            b.begin(second.id, "2026-10-02T01:00:00Z");
        }
        {
            Database db(path);
            SqliteSourceRepository repository(db);
            SourceService a(first.schoolId, {first}, repository);
            SourceService b(second.schoolId, {second}, repository);
            a.recover("2026-10-02T02:00:00Z");
            const auto recovered = a.find(first.id)->state;
            QCOMPARE(recovered.status, std::string("interrupted"));
            QCOMPARE(recovered.successfulPages, 1);
            QCOMPARE(recovered.rowCount, 2);
            QVERIFY(!recovered.error.empty());
            QVERIFY(recovered.lastSuccessAt.empty());
            QCOMPARE(b.find(second.id)->state.status, std::string("updating"));
            a.recover("2026-10-02T03:00:00Z");
            QCOMPARE(a.find(first.id)->state.status, std::string("interrupted"));
            QCOMPARE(a.find(first.id)->state.lastAttemptAt, std::string("2026-10-02T01:00:00Z"));
            const auto retry = a.begin(first.id, "2026-10-02T04:00:00Z");
            a.page(retry, 1, "2026-10-02");
            a.finish(retry, {}, "2026-10-02T04:01:00Z");
            QCOMPARE(a.find(first.id)->state.status, std::string("success"));
        }
    }

    void singleSourceRefreshUsesOnlyItsEntry() {
        LocalHttpServer server;
        server.routes["/a"] = {200, listHtml("补考申请", "/detail-a"), false};
        server.routes["/b"] = {200, listHtml("竞赛报名", "/detail-b"), false};
        const auto school =
            testSchool({testSource(server, "a", "/a"), testSource(server, "b", "/b")});
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        Database db(folder.path() + "/data.sqlite");
        SqliteRepository noticeRepository(db);
        SqliteSourceRepository sourceRepository(db);
        NoticeService notices(noticeRepository);
        SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
        RefreshCoordinator coordinator(school, notices, sources, nullptr, {0, 1000});
        QSignalSpy finished(&coordinator, &RefreshCoordinator::finished);
        coordinator.refreshSource("a");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QCOMPARE(server.requests, QStringList{"/a"});
        QCOMPARE(finished.front().at(0).toInt(), 1);
        QCOMPARE(finished.front().at(1).toInt(), 0);
        QVERIFY(!coordinator.busy());
        QCOMPARE(notices.list().size(), size_t(1));
        QCOMPARE(sources.find("a")->state.status, std::string("success"));
        QCOMPARE(sources.find("b")->state.status, std::string("never_checked"));
        QVERIFY(sources.find("b")->state.lastAttemptAt.empty());
    }

    void pageTwoFailureKeepsPageOneAndOldCache() {
        LocalHttpServer server;
        server.routes["/a"] = {200, listHtml("重修缴费", "/detail-a", "/a2"), false};
        server.routes["/a2"] = {503, "later page unavailable", false};
        const auto school = testSchool({testSource(server, "a", "/a", 2)});
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        Database db(folder.path() + "/data.sqlite");
        SqliteRepository noticeRepository(db);
        SqliteSourceRepository sourceRepository(db);
        NoticeService notices(noticeRepository);
        SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
        Notice old;
        old.id = "old-cached-notice";
        old.schoolId = "cn-fixture";
        old.sourceId = "a";
        old.sourceName = "测试来源 a";
        old.title = "历史奖学金申请";
        old.url = server.url("/older").toString().toStdString();
        old.publishedDate = "2025-10-01";
        notices.ingest({old});
        old.body = "已保存的原文正文";
        old.attachments = {{"办理指南", server.url("/guide.pdf").toString().toStdString()}};
        notices.saveDetail(old);
        RefreshCoordinator coordinator(school, notices, sources, nullptr, {0, 1000});
        QSignalSpy finished(&coordinator, &RefreshCoordinator::finished);
        coordinator.refreshSource("a");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QCOMPARE(server.requests, QStringList({"/a", "/a2"}));
        QCOMPARE(finished.front().at(0).toInt(), 0);
        QCOMPARE(finished.front().at(1).toInt(), 1);
        auto state = sources.find("a")->state;
        QCOMPARE(state.status, std::string("partial_success"));
        QCOMPARE(state.successfulPages, 1);
        QCOMPARE(state.rowCount, 1);
        QVERIFY(!state.error.empty());
        QVERIFY(state.lastSuccessAt.empty());
        auto cached = notices.list();
        QCOMPARE(cached.size(), size_t(2));
        auto retained = std::find_if(cached.begin(), cached.end(),
                                     [&](const auto &notice) { return notice.id == old.id; });
        QVERIFY(retained != cached.end());
        QCOMPARE(retained->body, old.body);
        QCOMPARE(retained->attachments.size(), size_t(1));
        QCOMPARE(noticeRepository.revisionCount(old.id), 2);
        server.routes["/a2"] = {200, listHtml("奖学金申请", "/detail-scholarship"), false};
        coordinator.refreshSource("a");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 3000);
        state = sources.find("a")->state;
        QCOMPARE(state.status, std::string("success"));
        QCOMPARE(state.successfulPages, 2);
        QCOMPARE(state.rowCount, 2);
        QVERIFY(!state.lastSuccessAt.empty());
        const auto fullSuccessAt = state.lastSuccessAt;
        QCOMPARE(notices.list().size(), size_t(3));
        server.routes["/a"] = {500, "entry failed", false};
        coordinator.refreshSource("a");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 3000);
        state = sources.find("a")->state;
        QCOMPARE(state.status, std::string("failure"));
        QCOMPARE(state.successfulPages, 0);
        QCOMPARE(state.lastSuccessAt, fullSuccessAt);
        QCOMPARE(notices.list().size(), size_t(3));
    }

    void parsingAndTimeoutBecomeVisibleFailures_data() {
        QTest::addColumn<QByteArray>("response");
        QTest::addColumn<bool>("hang");
        QTest::newRow("changed HTML structure") << QByteArray("<html>新页面结构</html>") << false;
        QTest::newRow("network timeout") << QByteArray() << true;
    }

    void parsingAndTimeoutBecomeVisibleFailures() {
        QFETCH(QByteArray, response);
        QFETCH(bool, hang);
        LocalHttpServer server;
        server.routes["/a"] = {200, response, hang};
        const auto school = testSchool({testSource(server, "a", "/a")});
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        Database db(folder.path() + "/data.sqlite");
        SqliteRepository noticeRepository(db);
        SqliteSourceRepository sourceRepository(db);
        NoticeService notices(noticeRepository);
        SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
        RefreshCoordinator coordinator(school, notices, sources, nullptr, {0, 100});
        QSignalSpy finished(&coordinator, &RefreshCoordinator::finished);
        coordinator.refreshSource("a");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(server.requests, QStringList{"/a"});
        const auto state = sources.find("a")->state;
        QCOMPARE(state.status, std::string("failure"));
        QCOMPARE(state.successfulPages, 0);
        QVERIFY(!state.lastAttemptAt.empty());
        QVERIFY(!state.error.empty());
        QVERIFY(state.lastSuccessAt.empty());
        QVERIFY(!coordinator.busy());
    }

    void pausedUnavailableAndUnknownSourcesMakeNoRequests() {
        LocalHttpServer server;
        server.routes["/a"] = {200, listHtml("补考申请", "/detail-a"), false};
        auto school = testSchool({testSource(server, "a", "/a")});
        SourceDescription pending;
        pending.schoolId = "cn-fixture";
        pending.id = "pending";
        pending.name = "尚待社区适配";
        pending.discoveryUrl = server.url("/pending").toString().toStdString();
        pending.pendingReason = "公开列表尚未确认";
        school.catalog.push_back(pending);
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        Database db(folder.path() + "/data.sqlite");
        SqliteRepository noticeRepository(db);
        SqliteSourceRepository sourceRepository(db);
        NoticeService notices(noticeRepository);
        SourceService sources(school.id.toStdString(), school.catalog, sourceRepository);
        sources.setPaused("a", true);
        RefreshCoordinator coordinator(school, notices, sources, nullptr, {0, 1000});
        QSignalSpy finished(&coordinator, &RefreshCoordinator::finished);
        coordinator.refreshSource("a");
        coordinator.refreshSource("pending");
        coordinator.refreshSource("unknown");
        QTest::qWait(20);
        QVERIFY(server.requests.isEmpty());
        QVERIFY(!coordinator.busy());
        finished.clear();
        coordinator.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
        QCOMPARE(finished.front().at(0).toInt(), 0);
        QCOMPARE(finished.front().at(1).toInt(), 0);
        QVERIFY(server.requests.isEmpty());
        QCOMPARE(sources.find("a")->effectiveStatus(), std::string("paused"));
        QCOMPARE(sources.find("pending")->effectiveStatus(), std::string("not_ready"));
        QVERIFY(sources.find("pending")->state.lastAttemptAt.empty());
        sources.setPaused("a", false);
        finished.clear();
        coordinator.refreshSource("a");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QCOMPARE(server.requests, QStringList{"/a"});
        QCOMPARE(sources.find("a")->state.status, std::string("success"));
        QVERIFY(sources.find("pending")->state.lastSuccessAt.empty());
    }

    void frozenVersionOneMigratesWithoutExternalEvidence() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto filename = folder.path() + "/frozen-v1.sqlite";
        createFrozenVersionOne(filename);
        const auto before = readFrozenDatabase(filename);
        QCOMPARE(before.version, 1);
        QCOMPARE(before.notices.size(), 1);
        QCOMPARE(before.revisions.size(), 1);
        {
            Database database(filename);
            SqliteRepository notices(database);
            SqliteSourceRepository sources(database);
            QCOMPARE(notices.list().size(), size_t(1));
            QCOMPARE(notices.list().front().body, std::string("请在规定时间缴费。"));
            QCOMPARE(notices.list().front().attachments.size(), size_t(1));
            const auto state = sources.state("cn-frozen", "academic-notices");
            QCOMPARE(state.status, std::string("never_checked"));
            QCOMPARE(state.latestPublishedDate, std::string("2026-09-16"));
            QVERIFY(state.lastAttemptAt.empty());
            QVERIFY(state.lastSuccessAt.empty());
        }
        const auto after = readFrozenDatabase(filename);
        QCOMPARE(after.version, SqliteMigrations::CurrentVersion);
        QCOMPARE(after.notices, before.notices);
        QCOMPARE(after.revisions, before.revisions);
        {
            Database reopened(filename);
        }
        QCOMPARE(readFrozenDatabase(filename).notices, before.notices);
        QCOMPARE(readFrozenDatabase(filename).revisions, before.revisions);
    }

    void conflictingMigrationRollsBackWithoutChangingVersionOne() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto filename = folder.path() + "/conflicting-v1.sqlite";
        createFrozenVersionOne(filename);
        // Version 2 creates source_preferences before source_state. A conflict after the
        // first CREATE verifies that the entire migration, including DDL, rolls back.
        executeRaw(filename, {"CREATE TABLE source_state(unexpected_column TEXT)"});
        const auto before = readFrozenDatabase(filename);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Database{filename});
        const auto after = readFrozenDatabase(filename);
        QCOMPARE(after.version, 1);
        QCOMPARE(after.notices, before.notices);
        QCOMPARE(after.revisions, before.revisions);
        QCOMPARE(rawScalar(filename, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' "
                                     "AND name='source_preferences'"),
                 0);
        QCOMPARE(rawScalar(filename, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' "
                                     "AND name='fetch_run'"),
                 0);
        QCOMPARE(rawScalar(filename, "SELECT COUNT(*) FROM pragma_table_info('source_state') "
                                     "WHERE name='unexpected_column'"),
                 1);
    }

    void newerSchemaIsRejectedWithoutChanges() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto filename = folder.path() + "/future.sqlite";
        createFrozenVersionOne(filename);
        executeRaw(filename,
                   {QString("PRAGMA user_version=%1").arg(SqliteMigrations::CurrentVersion + 1)});
        const auto before = readFrozenDatabase(filename);
        QCOMPARE(before.version, SqliteMigrations::CurrentVersion + 1);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Database{filename});
        const auto after = readFrozenDatabase(filename);
        QCOMPARE(after.version, SqliteMigrations::CurrentVersion + 1);
        QCOMPARE(after.notices, before.notices);
        QCOMPARE(after.revisions, before.revisions);
        QCOMPARE(rawScalar(filename, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' "
                                     "AND name='source_preferences'"),
                 0);
    }

    void fetchHistoryIsBoundedPerSource() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto filename = folder.path() + "/history.sqlite";
        Database database(filename);
        SqliteSourceRepository repository(database);
        SourceDescription first;
        first.schoolId = "cn-first";
        first.id = "source-a";
        first.configuredEnabled = first.ready = true;
        auto second = first;
        second.id = "source-b";
        SourceService service(first.schoolId, {first, second}, repository);
        for (int index = 0; index < 3; ++index) {
            const auto run = service.begin(second.id, "2026-10-02T01:00:00Z");
            service.page(run, 1, "2026-10-01");
            service.finish(run, {}, "2026-10-02T01:01:00Z");
        }
        const auto otherState = service.find(second.id)->state;
        for (int index = 0; index < 35; ++index) {
            const auto run = service.begin(first.id, "2026-10-02T02:00:00Z");
            service.page(run, index + 1, "2026-10-02");
            service.finish(run, {}, "2026-10-02T02:01:00Z");
        }
        QSqlQuery query(database.connection());
        QVERIFY(query.exec("SELECT COUNT(*) FROM fetch_run WHERE school_id='cn-first' "
                           "AND source_id='source-a'"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 30);
        QVERIFY(query.exec("SELECT MIN(row_count),MAX(row_count) FROM fetch_run "
                           "WHERE school_id='cn-first' AND source_id='source-a'"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 6);
        QCOMPARE(query.value(1).toInt(), 35);
        QVERIFY(query.exec("SELECT COUNT(*) FROM fetch_run WHERE school_id='cn-first' "
                           "AND source_id='source-b'"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 3);
        QCOMPARE(service.find(second.id)->state.status, otherState.status);
        QCOMPARE(service.find(second.id)->state.lastSuccessAt, otherState.lastSuccessAt);
        QCOMPARE(service.find(second.id)->state.rowCount, otherState.rowCount);
        QCOMPARE(service.find(first.id)->state.rowCount, 35);
    }

    void realVersionOneMigrationCopiesAndRestarts() {
        if (!QFile::exists(REAL_V1_DB))
            QSKIP("Optional local 95-notice v1 evidence is absent; the frozen v1 migration "
                  "contract above always runs in community CI.");
        const auto original = readFrozenDatabase(REAL_V1_DB);
        QCOMPARE(original.version, 1);
        QCOMPARE(original.notices.size(), 95);
        QVERIFY(!original.revisions.isEmpty());
        QVERIFY(std::any_of(original.notices.begin(), original.notices.end(), [](const auto &row) {
            return !row.at(8).toString().isEmpty() && row.at(9).toString() != "[]";
        }));
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto copy = folder.path() + "/migrated.sqlite";
        QVERIFY(QFile::copy(REAL_V1_DB, copy));
        const auto school = SchoolPackage::load(CONFIG_FILE);
        {
            Database db(copy);
            SqliteRepository notices(db);
            QCOMPARE(notices.list().size(), size_t(95));
            SqliteSourceRepository repository(db);
            SourceService service(school.id.toStdString(), school.catalog, repository);
            for (const auto &view : service.list()) {
                QVERIFY(view.state.lastAttemptAt.empty());
                QVERIFY(view.state.lastSuccessAt.empty());
                QCOMPARE(view.state.status, std::string("never_checked"));
            }
        }
        const auto migrated = readFrozenDatabase(copy);
        QCOMPARE(migrated.version, SqliteMigrations::CurrentVersion);
        QCOMPARE(migrated.notices, original.notices);
        QCOMPARE(migrated.revisions, original.revisions);
        {
            Database reopened(copy);
            SqliteRepository notices(reopened);
            QCOMPARE(notices.list().size(), size_t(95));
        }
        const auto afterRestart = readFrozenDatabase(copy);
        QCOMPARE(afterRestart.notices, original.notices);
        QCOMPARE(afterRestart.revisions, original.revisions);
        const auto untouched = readFrozenDatabase(REAL_V1_DB);
        QCOMPARE(untouched.version, 1);
        QCOMPARE(untouched.notices, original.notices);
        QCOMPARE(untouched.revisions, original.revisions);
    }
};

QTEST_GUILESS_MAIN(SourceTests)
#include "SourceTests.moc"
