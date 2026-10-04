#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QSet>
#include "adapters/AiProviderConfig.h"
namespace campus {
class DeepSeekSearch final : public QObject {
    Q_OBJECT
  public:
    explicit DeepSeekSearch(QObject *parent = nullptr);
    void search(const QString &key, const QString &model, const QString &school,
                const QString &root, const QSet<QString> &existing);
    void search(const AiProviderConfig &provider, const QString &key, const QString &school,
                const QString &root, const QSet<QString> &existing);
    static QJsonObject requestBody(const QString &model, const QString &school,
                                   const QString &root);
    static QJsonArray candidates(const QJsonObject &response, const QString &root,
                                 const QSet<QString> &existing);
    static QString sessionKey();
    static QJsonObject suggestionRequestBody(const QString &model, const QString &school,
                                            const QString &root, const QSet<QString> &existing);
    static QJsonArray suggestionCandidates(const QJsonObject &response, const QString &root,
                                          const QSet<QString> &existing);
  signals:
    void finished(QJsonArray candidates, QJsonObject usage);
    void failed(QString reason);
    void diagnostic(QJsonObject metadata);

  private:
    QNetworkAccessManager network_;
    bool busy_ = false;
    void send(const AiProviderConfig &provider, const QString &key, const QString &school,
              const QString &root, const QSet<QString> &existing, const QUrl &pinned);
};
} // namespace campus
