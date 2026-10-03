#pragma once
#include "adapters/HtmlAdapter.h"
#include "application/ResourceService.h"
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <deque>

class ResourceDiscoveryTests;
class QNetworkReply;

namespace campus {
struct ResourceDiscoveryOptions {
    int maxPages = 32;
    int requestIntervalMs = 3000;
    int transferTimeoutMs = 8000;
    int maxResponseBytes = 2 * 1024 * 1024;
    int maxDepth = 3;
    // Empty uses the application's local resource-evidence directory.
    QString evidenceDirectory;
};

class ResourceDiscovery final : public QObject {
    Q_OBJECT
  public:
    ResourceDiscovery(const SchoolPackage &school, ResourceService &service,
                      QObject *parent = nullptr, ResourceDiscoveryOptions options = {});
    ~ResourceDiscovery() override;
    bool busy() const;
  public slots:
    void start();
    void cancel();
  signals:
    void started();
    void progress(QString message);
    void changed();
    void finished(int resources, int verified, int failed);
    void failed(QString reason);

  private:
    friend class ::ResourceDiscoveryTests;
    struct Page {
        QUrl url, origin, discoveredFrom;
        QString label;
        int depth = 0;
        int redirects = 0;
    };
    SchoolPackage school_;
    ResourceService &service_;
    ResourceDiscoveryOptions options_;
    QString officialRoot_, evidenceDirectory_;
    QNetworkAccessManager network_;
    QNetworkReply *reply_ = nullptr;
    QTimer timer_;
    HtmlAdapter parser_;
    std::deque<Page> queue_;
    QSet<QString> queued_, observed_, verifiedUrls_;
    QHash<QString, SchoolResource> resources_;
    int fetched_ = 0, failures_ = 0;
    bool busy_ = false;
    void enqueue(const Page &page);
    void next();
    void consume(const Page &page, const QByteArray &bytes, const QString &error);
    void saveResource(SchoolResource resource);
    void complete();
};
} // namespace campus
