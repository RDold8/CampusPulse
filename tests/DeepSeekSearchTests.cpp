#include "adapters/DeepSeekSearch.h"
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>
#include <stdexcept>

using namespace campus;
namespace {
QJsonObject selection(const QJsonArray &items) {
    return {{"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"},
        {"message", QJsonObject{{"content", QString::fromUtf8(QJsonDocument(
            QJsonObject{{"candidates", items}}).toJson(QJsonDocument::Compact))}}}}}}};
}
QJsonObject toolResponse(QString reason, QJsonValue results) {
    return {{"stop_reason", reason}, {"content", QJsonArray{QJsonObject{
        {"type", "web_search_tool_result"}, {"content", results}}}}};
}
QJsonObject actualHit() {
    return {{"type", "web_search_result"}, {"url", "https://jwc.sample.edu.cn/tzgg/"},
            {"title", "教务通知"}};
}
AiProviderConfig customProvider(AiApiProtocol protocol = AiApiProtocol::OpenAiCompatible) {
    auto provider = AiProviderConfig::deepSeekPreset();
    provider.id = "synthetic-provider";
    provider.baseUrl = "https://gateway.example.com";
    provider.model = "synthetic-model";
    provider.protocol = protocol;
    return provider;
}
}

class DeepSeekSearchTests final : public QObject {
    Q_OBJECT
  private slots:
    void nativeSearchRejectsWebPlusMobileArticlesAndPdfViewers() {
        QJsonArray hits;
        for (const auto &path : {"/610/list1.htm", "/2026/0828/c610a222457/pagem.htm",
            "/2025/0726/c611a209533/page.htm?out=embedded_pdf", "/pdfjs22228/web/viewer.html?file=/_upload/a.pdf"})
            hits.append(QJsonObject{{"type", "web_search_result"}, {"title", "官方搜索返回"},
                {"url", "https://sample.edu.cn" + QString(path)}});
        const auto accepted = DeepSeekSearch::candidates(
            {{"stop_reason", "end_turn"}, {"content", QJsonArray{QJsonObject{
                {"type", "web_search_tool_result"}, {"content", hits}}}}}, "sample.edu.cn", {});
        QCOMPARE(accepted.size(), 1);
        QCOMPARE(accepted.first().toObject().value("url").toString(), QString("https://sample.edu.cn/610/list1.htm"));
    }
    void pendingTargetsAreIncludedWithoutCrossSchoolOrLoginUrls() {
        const QJsonArray targets{QJsonObject{{"title", "待接入教务"}, {"url", "https://jwc.sample.edu.cn/tzgg/"}},
            QJsonObject{{"url", "https://evil.org/"}}, QJsonObject{{"url", "https://sample.edu.cn/login"}}};
        const auto body = DeepSeekSearch::withRepairTargets(
            DeepSeekSearch::requestBody("model", "大学", "sample.edu.cn"), targets, "sample.edu.cn");
        const auto text = QJsonDocument(body).toJson(QJsonDocument::Compact);
        QVERIFY(text.contains("https://jwc.sample.edu.cn/tzgg/"));
        QVERIFY(!text.contains("evil.org"));
        QVERIFY(!text.contains("https://sample.edu.cn/login"));
        QVERIFY(body.contains("tools"));
    }
    void pendingEntryIsReadEvenWhenHomepageDoesNotLinkToIt() {
        QStringList visited;
        DeepSeekSearch search(nullptr, [&](const QUrl &url, const QString &, QObject *,
                                          PublicUniversityNetwork::Callback callback) {
            visited << url.toString();
            callback({"<html><title>公开学校页面</title><p>真实公开内容</p></html>", {}, {}, 200});
        });
        const QString target = "https://jwc.sample.edu.cn/tzgg/";
        // Secret echo filtering removes this synthetic URL from model evidence; no API dispatch.
        QSignalSpy failed(&search, &DeepSeekSearch::failed);
        QSignalSpy diagnostic(&search, &DeepSeekSearch::diagnostic);
        search.search(customProvider(), target, "大学", "sample.edu.cn", {}, "general",
            QUrl("https://www.sample.edu.cn/"), {QJsonObject{{"url", target}, {"title", "待接入教务"}}});
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QVERIFY(visited.contains(target));
        QVERIFY(visited.size() <= 6);
        QCOMPARE(diagnostic.last().first().toJsonObject().value("model_calls").toInt(), 0);
    }
    void crawlLinksHaveRealHtmlProvenanceAndRespectScope() {
        const QByteArray html =
            "<html><a href='https://jwc.sample.edu.cn/tzgg/'>教务通知</a>"
            "<a href='https://jwc.sample.edu.cn/tzgg/'>重复</a>"
            "<a href='https://sample.edu.cn.evil.org/'>假后缀</a>"
            "<a href='/info/12/1234.htm'>单篇文章</a>"
            "<a href='/login'>登录</a><a href='/a.pdf'>附件</a>"
            "<a href='/2026/0828/c610a222457/pagem.htm'>移动文章</a>"
            "<a href='/2025/0726/c611a209533/page.htm?out=embedded_pdf'>嵌入附件</a>"
            "<a href='/pdfjs22228/web/viewer.html'>PDF查看器</a>"
            "<a href='/account?token=synthetic'>敏感参数</a>"
            "<a href='javascript:alert(1)'>脚本</a></html>";
        const auto links = DeepSeekSearch::discoveredLinks(html, QUrl("https://sample.edu.cn/"),
                                                          "sample.edu.cn");
        QCOMPARE(links.size(), 1);
        const auto item = links.first().toObject();
        QCOMPARE(item.value("url").toString(), QString("https://jwc.sample.edu.cn/tzgg/"));
        QCOMPARE(item.value("observed_on").toString(), QString("https://sample.edu.cn/"));
        QCOMPARE(item.value("html_sha256").toString().size(), 64);
        QVERIFY(DeepSeekSearch::discoveredLinks(html, QUrl("https://evil.org/"), "sample.edu.cn").isEmpty());
    }
    void departmentPriorityChangesWithTemplateAndIsBounded() {
        QByteArray html = "<a href='https://finance.sample.edu.cn/'>财务处</a>"
                          "<a href='https://lib.sample.edu.cn/'>图书馆</a>";
        QCOMPARE(DeepSeekSearch::discoveredLinks(html, QUrl("https://sample.edu.cn/"),
                     "sample.edu.cn", "retake-payment").first().toObject().value("url").toString(),
                 QString("https://finance.sample.edu.cn/"));
        QCOMPARE(DeepSeekSearch::discoveredLinks(html, QUrl("https://sample.edu.cn/"),
                     "sample.edu.cn", "study-resources").first().toObject().value("url").toString(),
                 QString("https://lib.sample.edu.cn/"));
        for (int i = 0; i < 200; ++i)
            html += QString("<a href='/column-%1/'>栏目%1</a>").arg(i).toUtf8();
        const auto links = DeepSeekSearch::discoveredLinks(html, QUrl("https://sample.edu.cn/"), "sample.edu.cn");
        QCOMPARE(links.size(), 80);
        QVERIFY(DeepSeekSearch::discoveredLinks(QByteArray(1024 * 1024 + 1, '<'),
                     QUrl("https://sample.edu.cn/"), "sample.edu.cn").isEmpty());
        const auto body = DeepSeekSearch::groundedRequestBody("model", "示例大学", "sample.edu.cn", {}, links);
        QCOMPARE(body.value("max_tokens").toInt(), 1024);
        QVERIFY(!body.contains("tools"));
        QVERIFY(QJsonDocument(body).toJson(QJsonDocument::Compact).size() < 30000);
    }
    void modelCannotInventOrRewriteObservedUrls() {
        const auto links = DeepSeekSearch::discoveredLinks(
            "<a href='https://jwc.sample.edu.cn/tzgg/'>真实教务栏目</a>",
            QUrl("https://sample.edu.cn/"), "sample.edu.cn");
        const auto good = QJsonObject{{"url", "https://jwc.sample.edu.cn/tzgg/"}, {"title", "模型改写的标题"}};
        const auto accepted = DeepSeekSearch::groundedCandidates(selection({good}), "sample.edu.cn", {}, links);
        QCOMPARE(accepted.size(), 1);
        QCOMPARE(accepted.first().toObject().value("title").toString(), QString("真实教务栏目"));
        QCOMPARE(accepted.first().toObject().value("provenance").toString(), QString("official_site_crawl_ai_selection"));
        for (const auto &url : {"https://jwc.sample.edu.cn/invented/", "https://lib.sample.edu.cn/",
                               "http://jwc.sample.edu.cn/tzgg/", "https://jwc.sample.edu.cn/tzgg/#invented",
                               "https://evil.org/"}) {
            auto invented = good;
            invented["url"] = url;
            QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                DeepSeekSearch::groundedCandidates(selection({good, invented}), "sample.edu.cn", {}, links));
        }
        QVERIFY(DeepSeekSearch::groundedCandidates(selection({good}), "sample.edu.cn",
            {"https://jwc.sample.edu.cn/tzgg/"}, links).isEmpty());
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
            DeepSeekSearch::groundedRequestBody("model", "大学", "sample.edu.cn", {}, {}));
    }
    void customMessagesCanSelectEvidenceWithoutPretendingToSearch() {
        const auto links = DeepSeekSearch::discoveredLinks("<a href='/tzgg/'>通知公告</a>",
            QUrl("https://sample.edu.cn/"), "sample.edu.cn");
        const auto selected = selection({QJsonObject{{"url", "https://sample.edu.cn/tzgg/"}, {"title", "通知"}}});
        const auto text = selected.value("choices").toArray().first().toObject().value("message")
                              .toObject().value("content").toString();
        QJsonObject response{{"stop_reason", "end_turn"}, {"content", QJsonArray{
            QJsonObject{{"type", "text"}, {"text", text}}}}};
        QCOMPARE(DeepSeekSearch::groundedCandidates(response, "sample.edu.cn", {}, links).size(), 1);
        response["stop_reason"] = "max_tokens";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
            DeepSeekSearch::groundedCandidates(response, "sample.edu.cn", {}, links));
    }
    void malformedSelectionsFailWithoutContainerAssertions() {
        const auto links = DeepSeekSearch::discoveredLinks("<a href='/tzgg/'>通知公告</a>",
            QUrl("https://sample.edu.cn/"), "sample.edu.cn");
        for (const auto &response : {QJsonObject{}, QJsonObject{{"choices", QJsonArray{}}},
                QJsonObject{{"choices", "invalid"}}, QJsonObject{{"choices", QJsonArray{QJsonObject{}}}},
                QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"},
                    {"message", QJsonObject{{"content", "not JSON"}}}}}}}})
            QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                DeepSeekSearch::groundedCandidates(response, "sample.edu.cn", {}, links));
    }
    void realNativeResultsSurviveKnownPartialStops_data() {
        QTest::addColumn<QString>("reason");
        for (const auto &reason : {"end_turn", "max_tokens", "pause_turn", "tool_use", "stop_sequence",
                                  "model_context_window_exceeded"})
            QTest::newRow(reason) << QString::fromLatin1(reason);
    }
    void realNativeResultsSurviveKnownPartialStops() {
        QFETCH(QString, reason);
        QCOMPARE(DeepSeekSearch::candidates(toolResponse(reason, QJsonArray{actualHit()}),
                                          "sample.edu.cn", {}).size(), 1);
        if (reason != "end_turn")
            QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                DeepSeekSearch::candidates(toolResponse(reason, QJsonArray{}), "sample.edu.cn", {}));
    }
    void nativeBudgetErrorObjectRetainsEarlierResultsButNotErrorsAlone() {
        const QJsonObject error{{"type", "web_search_tool_result_error"}, {"error_code", "max_uses_exceeded"}};
        auto response = toolResponse("max_tokens", QJsonArray{actualHit()});
        auto content = response.value("content").toArray();
        content.append(QJsonObject{{"type", "web_search_tool_result"}, {"content", error}});
        response["content"] = content;
        QCOMPARE(DeepSeekSearch::candidates(response, "sample.edu.cn", {}).size(), 1);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
            DeepSeekSearch::candidates(toolResponse("end_turn", error), "sample.edu.cn", {}));
        for (const auto &reason : {"refusal", "unknown", ""}) {
            response["stop_reason"] = reason;
            QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                DeepSeekSearch::candidates(response, "sample.edu.cn", {}));
        }
        auto broken = error;
        broken["error_code"] = "unavailable";
        response = toolResponse("max_tokens", QJsonArray{actualHit(), broken});
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
            DeepSeekSearch::candidates(response, "sample.edu.cn", {}));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
            DeepSeekSearch::candidates({{"stop_reason", "max_tokens"}, {"content", QJsonArray{
                QJsonObject{{"type", "text"}, {"text", "https://sample.edu.cn/"}}}}}, "sample.edu.cn", {}));
    }
    void failedCrawlNeverInvokesModel_data() {
        QTest::addColumn<bool>("messages");
        QTest::newRow("openai") << false;
        QTest::newRow("custom-messages") << true;
    }
    void failedCrawlNeverInvokesModel() {
        QFETCH(bool, messages);
        int requests = 0;
        DeepSeekSearch search(nullptr, [&](const QUrl &url, const QString &root, QObject *,
                                           PublicUniversityNetwork::Callback callback) {
            ++requests;
            QVERIFY(PublicUniversityNetwork::withinUniversity(url, root));
            callback({{}, {}, "synthetic DNS failure", 0});
        });
        QSignalSpy failed(&search, &DeepSeekSearch::failed);
        QSignalSpy complete(&search, &DeepSeekSearch::finished);
        QSignalSpy diagnostics(&search, &DeepSeekSearch::diagnostic);
        search.search(customProvider(messages ? AiApiProtocol::DeepSeekNative : AiApiProtocol::OpenAiCompatible),
                      "synthetic-test-key", "大学", "sample.edu.cn", {});
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QCOMPARE(complete.size(), 0);
        QCOMPARE(requests, 2);
        QCOMPARE(diagnostics.last().first().toJsonObject().value("model_calls").toInt(), 0);
        QVERIFY(failed.first().first().toString().contains("未调用模型"));
    }
    void sixPageBudgetAndSecretEchoFilteringPreventModelCall() {
        int requests = 0;
        DeepSeekSearch search(nullptr, [&](const QUrl &, const QString &, QObject *,
                                           PublicUniversityNetwork::Callback callback) {
            ++requests;
            const auto html = QString("<a href='/synthetic-test-key/column-%1/'>教务通知</a>"
                                      "<a href='/synthetic-test-key/column-%2/'>学生服务</a>")
                                  .arg(requests * 2).arg(requests * 2 + 1).toUtf8();
            callback({html, {}, {}, 200});
        });
        QSignalSpy failed(&search, &DeepSeekSearch::failed);
        QSignalSpy complete(&search, &DeepSeekSearch::finished);
        QSignalSpy diagnostics(&search, &DeepSeekSearch::diagnostic);
        search.search(customProvider(), "synthetic-test-key", "大学", "sample.edu.cn", {});
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QCOMPARE(requests, 6);
        QCOMPARE(complete.size(), 0);
        const auto stats = diagnostics.last().first().toJsonObject();
        QCOMPARE(stats.value("model_calls").toInt(), 0);
        QCOMPARE(stats.value("crawl_requests").toInt(), 6);
        QCOMPARE(stats.value("pages_read").toInt(), 6);
        QVERIFY(stats.value("crawl_limited").toBool());
        QCOMPARE(stats.value("observed_links").toInt(), 0);
    }
    void invalidHomepageCannotReachEitherTransport() {
        int requests = 0;
        DeepSeekSearch search(nullptr, [&](const QUrl &, const QString &, QObject *,
                                           PublicUniversityNetwork::Callback) { ++requests; });
        QSignalSpy failed(&search, &DeepSeekSearch::failed);
        for (const auto &url : {"https://127.0.0.1/", "https://sample.edu.cn.evil.org/", "https://user@sample.edu.cn/"})
            search.search(customProvider(), "synthetic-test-key", "大学", "sample.edu.cn", {}, "general", QUrl(url));
        QCOMPARE(failed.size(), 3);
        QCOMPARE(requests, 0);
    }
};
QTEST_GUILESS_MAIN(DeepSeekSearchTests)
#include "DeepSeekSearchTests.moc"
