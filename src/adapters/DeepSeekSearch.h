#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QSet>
namespace campus {
class DeepSeekSearch final : public QObject {
    Q_OBJECT
  public:
    explicit DeepSeekSearch(QObject *parent = nullptr);
    void search(const QString &key, const QString &model, const QString &school,
                const QString &root, const QSet<QString> &existing);
    static QJsonObject requestBody(const QString &model, const QString &school,
                                   const QString &root);
    static QJsonArray candidates(const QJsonObject &response, const QString &root,
                                 const QSet<QString> &existing);
    static QString sessionKey();
  signals:
    void finished(QJsonArray candidates, QJsonObject usage);
    void failed(QString reason);

  private:
    QNetworkAccessManager network_;
    bool busy_ = false;
};
} // namespace campus
