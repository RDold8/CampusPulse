#include "adapters/RefreshCoordinator.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QDateTime>
#include <algorithm>

namespace campus {
RefreshCoordinator::RefreshCoordinator(SchoolPackage school, NoticeService &service,
                                       SourceService &sources, QObject *parent,
                                       RefreshOptions options)
    : QObject(parent), school_(std::move(school)), service_(service), sources_(sources),
      options_(options) {
    if (options_.requestIntervalMs < 0 || options_.transferTimeoutMs < 1)
        throw std::runtime_error("网络时间配置无效");
    network_.setTransferTimeout(options_.transferTimeoutMs);
}
void RefreshCoordinator::enqueue(Request request) {
    pending_.push_back(std::move(request));
    pump();
}
void RefreshCoordinator::pump() {
    if (requestActive_ || pending_.empty() || pumpScheduled_)
        return;
    if (lastRequest_.isValid() && lastRequest_.elapsed() < options_.requestIntervalMs) {
        pumpScheduled_ = true;
        QTimer::singleShot(static_cast<int>(options_.requestIntervalMs - lastRequest_.elapsed()),
                           this, [this] {
                               pumpScheduled_ = false;
                               pump();
                           });
        return;
    }
    auto request = std::move(pending_.front());
    pending_.pop_front();
    if (!isAllowedUrl(request.url, request.source)) {
        request.complete({}, "请求地址不在允许域名内");
        pump();
        return;
    }
    requestActive_ = true;
    lastRequest_.restart();
    QNetworkRequest wire(request.url);
    wire.setRawHeader("User-Agent", "CampusPulse/0.1 (public-notice desktop prototype)");
    wire.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                      QNetworkRequest::ManualRedirectPolicy);
    auto *reply = network_.get(wire);
    connect(reply, &QIODevice::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 5 * 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, request = std::move(request)]() mutable {
                const auto redirect =
                    reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
                const auto status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QString error;
                QByteArray bytes;
                if (!redirect.isEmpty() && status >= 300 && status < 400) {
                    const auto target = request.url.resolved(redirect);
                    if (request.redirects >= 3 || !isAllowedUrl(target, request.source) ||
                        (request.url.scheme() == "https" && target.scheme() != "https"))
                        error = "重定向不被允许";
                    else {
                        request.url = target;
                        ++request.redirects;
                        pending_.push_front(std::move(request));
                        reply->deleteLater();
                        requestActive_ = false;
                        pump();
                        return;
                    }
                } else if (reply->error() != QNetworkReply::NoError)
                    error = reply->errorString();
                else if (status != 200)
                    error = QString("HTTP %1").arg(status);
                else {
                    bytes = reply->readAll();
                    if (bytes.size() > 5 * 1024 * 1024) {
                        error = "响应超过5MB限制";
                        bytes.clear();
                    }
                }
                reply->deleteLater();
                requestActive_ = false;
                request.complete(bytes, error);
                pump();
            });
}
void RefreshCoordinator::refresh() {
    if (refreshing_)
        return;
    std::vector<SourceConfig> selected;
    for (const auto &source : school_.sources)
        if (sources_.isRunnable(source.id.toStdString()))
            selected.push_back(source);
    startSources(selected);
}
void RefreshCoordinator::refreshSource(const QString &sourceId) {
    if (refreshing_)
        return;
    const auto found = std::find_if(school_.sources.begin(), school_.sources.end(),
                                    [&](const auto &s) { return s.id == sourceId; });
    if (found == school_.sources.end() || !sources_.isRunnable(sourceId.toStdString())) {
        emit message("该来源已暂停或尚未接入，未发出请求");
        return;
    }
    startSources({*found});
}
void RefreshCoordinator::startSources(const std::vector<SourceConfig> &selected) {
    refreshing_ = true;
    successes_ = 0;
    failures_ = 0;
    remaining_ = static_cast<int>(selected.size());
    emit started();
    if (selected.empty()) {
        refreshing_ = false;
        emit message("没有可更新的来源；可在来源页恢复，历史缓存仍可查看");
        emit finished(0, 0);
        return;
    }
    for (const auto &source : selected) {
        try {
            const auto runId = sources_.begin(
                source.id.toStdString(),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString());
            emit sourcesChanged();
            refreshPage(source, runId, source.entry, 1, {});
        } catch (const std::exception &e) {
            emit message(source.name + "：无法开始更新，" + QString::fromUtf8(e.what()));
            countCompleted(false);
        }
    }
}
void RefreshCoordinator::countCompleted(bool success) {
    if (success)
        ++successes_;
    else
        ++failures_;
    if (--remaining_ == 0) {
        refreshing_ = false;
        emit finished(successes_, failures_);
    }
}
void RefreshCoordinator::completeSource(const SourceConfig &source, std::int64_t runId,
                                        const QString &error) {
    bool success = error.isEmpty();
    try {
        sources_.finish(runId, error.toStdString(),
                        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString());
        emit sourcesChanged();
        emit message(source.name + (success ? "：本轮更新完整完成" : "：更新未完整完成，" + error));
    } catch (const std::exception &e) {
        success = false;
        emit message(source.name + "：更新状态保存失败，" + QString::fromUtf8(e.what()));
    }
    countCompleted(success);
}
void RefreshCoordinator::refreshPage(SourceConfig source, std::int64_t runId, QUrl url, int page,
                                     QStringList visited) {
    const auto canonical = url.toString(QUrl::FullyEncoded);
    if (visited.contains(canonical)) {
        completeSource(source, runId, "分页循环，停止此来源");
        return;
    }
    visited << canonical;
    enqueue(
        {source, url, [this, source, runId, url, page, visited](QByteArray bytes, QString error) {
             try {
                 if (!error.isEmpty())
                     throw std::runtime_error(error.toStdString());
                 const auto notices = parser_.parseList(bytes, source);
                 service_.ingest(notices);
                 std::string latest;
                 for (const auto &notice : notices)
                     latest = std::max(latest, notice.publishedDate);
                 sources_.page(runId, static_cast<int>(notices.size()), latest);
                 emit sourcesChanged();
                 emit changed();
                 emit message(QString("%1：第%2页读取 %3 条通知")
                                  .arg(source.name)
                                  .arg(page)
                                  .arg(notices.size()));
                 if (page < source.maxPages) {
                     const auto next = parser_.nextPage(bytes, source, url);
                     if (!next.isEmpty()) {
                         refreshPage(source, runId, next, page + 1, visited);
                         return;
                     }
                 }
                 completeSource(source, runId, {});
             } catch (const std::exception &e) {
                 completeSource(source, runId, QString::fromUtf8(e.what()));
             }
         }});
}
void RefreshCoordinator::loadDetail(const Notice &notice) {
    const auto id = QString::fromStdString(notice.id);
    if (pendingDetails_.contains(id)) {
        emit message("该通知正文正在读取");
        return;
    }
    const auto found =
        std::find_if(school_.sources.begin(), school_.sources.end(),
                     [&](const auto &s) { return s.id.toStdString() == notice.sourceId; });
    if (notice.schoolId != school_.id.toStdString() || found == school_.sources.end() ||
        !sources_.isRunnable(notice.sourceId)) {
        const QString error = "通知来源已暂停或尚未接入；可查看缓存或打开官方原文";
        emit detailFailed(id, error);
        emit message(error);
        return;
    }
    const auto source = *found;
    pendingDetails_.insert(id);
    emit detailStarted(id);
    enqueue({source, QUrl(QString::fromStdString(notice.url)),
             [this, source, notice, id](QByteArray bytes, QString error) {
                 pendingDetails_.remove(id);
                 try {
                     if (!error.isEmpty())
                         throw std::runtime_error(error.toStdString());
                     service_.saveDetail(parser_.parseDetail(bytes, source, notice));
                     emit detailFinished(QString::fromStdString(notice.id));
                     emit message("正文已读取并保存；日期仍以原文为准");
                 } catch (const std::exception &e) {
                     emit detailFailed(id, QString::fromUtf8(e.what()));
                     emit message("正文读取失败：" + QString::fromUtf8(e.what()));
                 }
             }});
}
} // namespace campus
