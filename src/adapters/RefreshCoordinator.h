#pragma once
#include "adapters/SchoolPackage.h"
#include "adapters/HtmlAdapter.h"
#include "application/NoticeService.h"
#include "application/SourceService.h"
#include <QNetworkAccessManager>
#include <QObject>
#include <QElapsedTimer>
#include <QSet>
#include <deque>
#include <functional>

namespace campus {
struct RefreshOptions {
    int requestIntervalMs = 3000;
    int transferTimeoutMs = 25000;
};
// Network and database work for this small prototype stay on their owning event-loop thread.
class RefreshCoordinator final : public QObject {
    Q_OBJECT
  public:
    RefreshCoordinator(SchoolPackage school, NoticeService &service, SourceService &sources,
                       QObject *parent = nullptr, RefreshOptions options = {});
    void refresh();
    void refreshSource(const QString &sourceId);
    void loadDetail(const Notice &notice);
    bool busy() const {
        return refreshing_;
    }
  signals:
    void changed();
    void sourcesChanged();
    void started();
    void message(QString text);
    void finished(int successfulSources, int failedSources);
    void detailFinished(QString id);
    void detailStarted(QString id);
    void detailFailed(QString id, QString error);

  private:
    struct Request {
        SourceConfig source;
        QUrl url;
        std::function<void(QByteArray, QString)> complete;
        int redirects = 0;
    };
    SchoolPackage school_;
    NoticeService &service_;
    SourceService &sources_;
    RefreshOptions options_;
    HtmlAdapter parser_;
    QNetworkAccessManager network_;
    std::deque<Request> pending_;
    QElapsedTimer lastRequest_;
    bool requestActive_ = false;
    bool pumpScheduled_ = false;
    bool refreshing_ = false;
    QSet<QString> pendingDetails_;
    int remaining_ = 0, successes_ = 0, failures_ = 0;
    void startSources(const std::vector<SourceConfig> &selected);
    void refreshPage(SourceConfig source, std::int64_t runId, QUrl url, int page,
                     QStringList visited);
    void completeSource(const SourceConfig &source, std::int64_t runId, const QString &error);
    void countCompleted(bool success);
    void pump();
    void enqueue(Request request);
};
} // namespace campus
