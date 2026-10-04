#include "adapters/DeepSeekSearch.h"
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>
#include <stdexcept>
using namespace campus;
namespace {
QJsonObject reply(const QJsonArray &hits, const QString &finish = "stop") {
    return {{"choices", QJsonArray{QJsonObject{{"finish_reason", finish},
        {"message", QJsonObject{{"content", QString::fromUtf8(QJsonDocument(
            QJsonObject{{"candidates", hits}}).toJson(QJsonDocument::Compact))}}}}}}};
}
}
class AiSearchTests final : public QObject {
    Q_OBJECT
private slots:
    void budgetLimitCanRetainRealResultsButNeverCreateSearchEvidence() {
        const auto error = QJsonObject{{"type", "web_search_tool_result_error"},
                                      {"error_code", "max_uses_exceeded"}};
        const auto actual = QJsonObject{{"type", "web_search_result"},
            {"url", "https://jwc.hit.edu.cn/tzgg/"}, {"title", "教务通知"}};
        auto make = [](QJsonArray items) {
            return QJsonObject{{"stop_reason", "end_turn"}, {"content", QJsonArray{
                QJsonObject{{"type", "web_search_tool_result"}, {"content", items}}}}};
        };
        QCOMPARE(DeepSeekSearch::candidates(make({actual, error}), "hit.edu.cn", {}).size(), 1);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
            DeepSeekSearch::candidates(make({error}), "hit.edu.cn", {}));
        auto different = error; different["error_code"] = "unavailable";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
            DeepSeekSearch::candidates(make({actual, different}), "hit.edu.cn", {}));
    }
    void suggestionsCannotPretendToUseSearch() {
        auto body = DeepSeekSearch::suggestionRequestBody("model", "测试大学", "hit.edu.cn", {});
        QVERIFY(!body.contains("tools"));
        QVERIFY(body.value("messages").toArray().first().toObject().value("content").toString()
            .contains("没有联网检索工具"));
        auto native = DeepSeekSearch::requestBody("model", "测试大学", "hit.edu.cn");
        QVERIFY(native.value("tools").isArray());
    }
    void suggestionsStillRequireOfficialDomainAndRemainCandidates() {
        QJsonArray hits{
            QJsonObject{{"url", "https://jwc.hit.edu.cn/tzgg/"}, {"title", "教务通知"}},
            QJsonObject{{"url", "https://jwc.hit.edu.cn.evil.org/"}, {"title", "假冒"}},
            QJsonObject{{"url", "https://user@hit.edu.cn/"}, {"title", "账号"}},
            QJsonObject{{"url", "https://hit.edu.cn/login"}, {"title", "登录"}},
            QJsonObject{{"url", "https://hit.edu.cn/info/1/2.htm"}, {"title", "新闻"}},
            QJsonObject{{"url", "https://lib.hit.edu.cn/"}, {"title", "已有栏目"}}};
        auto selected = DeepSeekSearch::suggestionCandidates(reply(hits), "hit.edu.cn",
                                                             {"https://lib.hit.edu.cn/"});
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected.first().toObject().value("status").toString(), QString("candidate"));
        QCOMPARE(selected.first().toObject().value("provenance").toString(), QString("model_suggestion"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
            DeepSeekSearch::candidates(reply(hits), "hit.edu.cn", {}));
    }
    void incompleteOrFreeTextAnswersAreRejected() {
        auto empty = reply({});
        QCOMPARE(DeepSeekSearch::suggestionCandidates(empty, "hit.edu.cn", {}).size(), 0);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
            DeepSeekSearch::suggestionCandidates(reply({}, "length"), "hit.edu.cn", {}));
        auto freeText = QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"},
            {"message", QJsonObject{{"content", "我已查到 https://hit.edu.cn/"}}}}}}};
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
            DeepSeekSearch::suggestionCandidates(freeText, "hit.edu.cn", {}));
        QJsonArray tooMany;
        for (int i = 0; i < 9; ++i)
            tooMany.append(QJsonObject{{"url", QString("https://hit.edu.cn/%1/").arg(i)}, {"title", "标题"}});
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
            DeepSeekSearch::suggestionCandidates(reply(tooMany), "hit.edu.cn", {}));
    }
    void invalidCredentialsAndEndpointNeverIssueRequest() {
        DeepSeekSearch service;
        QSignalSpy failures(&service, &DeepSeekSearch::failed);
        auto profile = AiProviderConfig::deepSeekPreset();
        service.search(profile, "invalid\r\nheader", "学校", "hit.edu.cn", {});
        QCOMPARE(failures.size(), 1);
        profile.baseUrl = "https://127.0.0.1/";
        service.search(profile, "fake-test-key", "学校", "hit.edu.cn", {});
        QCOMPARE(failures.size(), 2);
    }
};
QTEST_GUILESS_MAIN(AiSearchTests)
#include "AiSearchTests.moc"
