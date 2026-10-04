#pragma once
#include "adapters/AiProviderConfig.h"
#include <QJsonArray>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QNetworkReply;
class QTimer;
namespace campus {
enum class AiProbeOperation { Connection, Models };
struct AiProbeResult {
    AiProbeOperation operation = AiProbeOperation::Connection;
    bool success = false;
    int httpStatus = 0;
    qint64 elapsedMs = 0;
    QString message;
    QString responseModel;
    QStringList modelIds;
    QJsonObject usage;
};

// Explicit button operations only; construction never performs network requests.
class AiProviderProbe final : public QObject {
    Q_OBJECT
  public:
    explicit AiProviderProbe(QObject *parent = nullptr);
    void probe(const AiProviderConfig &provider, const QString &key);
    void fetchModels(const AiProviderConfig &provider, const QString &key);
    bool busy() const;
    void cancel();
    static QUrl endpoint(const AiProviderConfig &provider, AiProbeOperation operation);
    static QJsonObject connectionBody(const AiProviderConfig &provider);
    // Response parser is separately testable without credentials or chargeable API calls.
    static AiProbeResult evaluate(AiApiProtocol protocol, AiProbeOperation operation,
                                  int status, const QByteArray &bytes, qint64 elapsedMs,
                                  const QString &transportError = {});

  signals:
    void finished(campus::AiProbeResult result);

  private:
    QPointer<QNetworkReply> reply_;
    int lookupId_ = -1;
    quint64 generation_ = 0;
    bool busy_ = false;
    QTimer *deadline_ = nullptr;
    QElapsedTimer elapsed_;
    AiProbeOperation operation_ = AiProbeOperation::Connection;
    void start(const AiProviderConfig &provider, const QString &key, AiProbeOperation operation);
};
} // namespace campus
Q_DECLARE_METATYPE(campus::AiProbeResult)
