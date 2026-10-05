#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QSet>
#include "adapters/AiProviderConfig.h"
#include "adapters/PublicUniversityNetwork.h"
#include <functional>
namespace campus {
class DeepSeekSearch final : public QObject {
    Q_OBJECT
  public:
    using PageFetcher = std::function<void(const QUrl &, const QString &, QObject *,
                                          PublicUniversityNetwork::Callback)>;
    explicit DeepSeekSearch(QObject *parent = nullptr, PageFetcher pageFetcher = {});
    void search(const QString &key, const QString &model, const QString &school,
                const QString &root, const QSet<QString> &existing,
                const QString &templateId = "general", const QUrl &homepage = {});
    void search(const AiProviderConfig &provider, const QString &key, const QString &school,
                const QString &root, const QSet<QString> &existing,
                const QString &templateId = "general", const QUrl &homepage = {},
                const QJsonArray &repairTargets = {});
    static QJsonObject withRepairTargets(QJsonObject body, const QJsonArray &targets,
                                         const QString &root);
    static QJsonObject requestBody(const QString &model, const QString &school,
                                   const QString &root, const QString &templateId = "general");
    static QJsonArray candidates(const QJsonObject &response, const QString &root,
                                 const QSet<QString> &existing);
    static QString sessionKey();
    static QJsonObject suggestionRequestBody(const QString &model, const QString &school,
                                            const QString &root, const QSet<QString> &existing,
                                            const QString &templateId = "general");
    static QJsonArray suggestionCandidates(const QJsonObject &response, const QString &root,
                                          const QSet<QString> &existing);
    static QJsonArray discoveredLinks(const QByteArray &html, const QUrl &page,
                                     const QString &root, const QString &templateId = "general");
    static QJsonObject groundedRequestBody(const QString &model, const QString &school,
                                          const QString &root, const QSet<QString> &existing,
                                          const QJsonArray &observed,
                                          const QString &templateId = "general");
    static QJsonArray groundedCandidates(const QJsonObject &response, const QString &root,
                                        const QSet<QString> &existing, const QJsonArray &observed);
  signals:
    void progress(QString message);
    void finished(QJsonArray candidates, QJsonObject usage);
    void failed(QString reason);
    void diagnostic(QJsonObject metadata);

  private:
    PageFetcher pageFetcher_;
    bool busy_ = false;
    QJsonArray repairTargets_;
    void send(const AiProviderConfig &provider, const QString &key, const QString &school,
              const QString &root, const QSet<QString> &existing, const QUrl &pinned,
              const QString &templateId, const QJsonArray &observed = {},
              const QJsonObject &crawl = {});
    void resolveAndSend(const AiProviderConfig &provider, const QString &key, const QString &school,
                        const QString &root, const QSet<QString> &existing,
                        const QString &templateId, const QJsonArray &observed = {},
                        const QJsonObject &crawl = {});
};
} // namespace campus
