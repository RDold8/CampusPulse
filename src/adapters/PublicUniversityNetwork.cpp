#include "adapters/PublicUniversityNetwork.h"
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>
#include <memory>

namespace campus {
namespace {
bool subnet(const QHostAddress &address, const char *network, int bits) {
    return address.isInSubnet(QHostAddress(QString::fromLatin1(network)), bits);
}
} // namespace

bool PublicUniversityNetwork::isPublicAddress(const QHostAddress &address) {
    if (address.isNull() || !address.scopeId().isEmpty())
        return false;
    bool ipv4 = false;
    const auto numeric = address.toIPv4Address(&ipv4);
    if (ipv4) {
        const QHostAddress v4(numeric);
        return !subnet(v4, "0.0.0.0", 8) && !subnet(v4, "10.0.0.0", 8) &&
               !subnet(v4, "100.64.0.0", 10) && !subnet(v4, "127.0.0.0", 8) &&
               !subnet(v4, "169.254.0.0", 16) && !subnet(v4, "172.16.0.0", 12) &&
               !subnet(v4, "192.0.0.0", 24) && !subnet(v4, "192.0.2.0", 24) &&
               !subnet(v4, "192.88.99.0", 24) && !subnet(v4, "192.168.0.0", 16) &&
               !subnet(v4, "198.18.0.0", 15) && !subnet(v4, "198.51.100.0", 24) &&
               !subnet(v4, "203.0.113.0", 24) && !subnet(v4, "224.0.0.0", 3);
    }
    // Accept native global unicast only, excluding documentation, transition
    // and special-purpose blocks. IPv4-mapped values were checked above.
    return address.protocol() == QHostAddress::IPv6Protocol &&
           subnet(address, "2000::", 3) && !subnet(address, "2001::", 23) &&
           !subnet(address, "2001:db8::", 32) && !subnet(address, "2002::", 16) &&
           !subnet(address, "3fff::", 20);
}

bool PublicUniversityNetwork::withinUniversity(const QUrl &url, const QString &officialRoot) {
    static const QRegularExpression rootPattern(
        "^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\\.)+"
        "[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$");
    const auto host = url.host().toLower();
    const auto authority = url.authority(QUrl::FullyEncoded);
    return rootPattern.match(officialRoot).hasMatch() && url.isValid() &&
           url.scheme() == "https" && !host.isEmpty() && url.userInfo().isEmpty() &&
           url.port() == -1 && !authority.contains('@') && !authority.contains(':') &&
           (host == officialRoot || host.endsWith("." + officialRoot));
}

QObject *PublicUniversityNetwork::get(const QUrl &url, const QString &officialRoot, QObject *owner,
                                     Callback callback, int maxBytes, int timeoutMs) {
    auto *operation = new QObject(owner);
    auto *timer = new QTimer(operation);
    timer->setSingleShot(true);
    auto done = std::make_shared<bool>(false);
    auto lookupId = std::make_shared<int>(-1);
    auto finish = [operation, timer, done, callback = std::move(callback)](
                      UniversityPageResponse response) {
        if (*done)
            return;
        *done = true;
        timer->stop();
        operation->deleteLater();
        callback(std::move(response));
    };
    if (!owner || maxBytes < 1 || timeoutMs < 1 || !withinUniversity(url, officialRoot)) {
        QTimer::singleShot(0, operation, [finish] {
            finish({{}, {}, "仅允许高校官方域内、无账号及端口的HTTPS网页。", 0});
        });
        return operation;
    }
    QObject::connect(timer, &QTimer::timeout, operation, [finish, lookupId] {
        if (*lookupId >= 0)
            QHostInfo::abortHostLookup(*lookupId);
        finish({{}, {}, "官网DNS或HTTPS请求超时。", 0});
    });
    timer->start(timeoutMs);
    *lookupId = QHostInfo::lookupHost(url.host(), operation, [=](const QHostInfo &info) {
        *lookupId = -1;
        if (*done)
            return;
        const auto addresses = info.addresses();
        if (info.error() != QHostInfo::NoError || addresses.isEmpty()) {
            finish({{}, {}, "无法解析高校官网公网地址。", 0});
            return;
        }
        if (std::any_of(addresses.begin(), addresses.end(), [](const QHostAddress &address) {
                return !PublicUniversityNetwork::isPublicAddress(address);
            })) {
            finish({{}, {}, "官网DNS包含内网、回环或保留地址，已停止访问。", 0});
            return;
        }
        auto address = addresses.first();
        // Many public campuses advertise IPv6 without providing a usable local
        // route; prefer IPv4 when available, while keeping native IPv6 support.
        for (const auto &candidate : addresses)
            if (candidate.protocol() == QHostAddress::IPv4Protocol) {
                address = candidate;
                break;
            }
        auto *network = new QNetworkAccessManager(operation);
        network->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        QUrl pinned = url;
        pinned.setHost(address.toString());
        QNetworkRequest request(pinned);
        request.setPeerVerifyName(url.host());
        request.setRawHeader("Host", url.host().toLatin1());
        request.setRawHeader("User-Agent", "CampusPulse/0.1 (public university discovery)");
        request.setRawHeader("Accept", "text/html, application/xhtml+xml");
        request.setRawHeader("Connection", "close");
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
        request.setTransferTimeout(timeoutMs);
        auto *reply = network->get(request);
        auto bytes = std::make_shared<QByteArray>();
        auto rejected = std::make_shared<QString>();
        QObject::connect(reply, &QNetworkReply::metaDataChanged, operation, [=] {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            const auto type = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
            if (status >= 300 && status < 400)
                return;
            if (length > maxBytes || (!type.isEmpty() && !type.contains("text/html") &&
                                      !type.contains("application/xhtml+xml"))) {
                *rejected = "仅接收大小受限的公开HTML，不下载附件或其他文件。";
                reply->abort();
            }
        });
        QObject::connect(reply, &QIODevice::readyRead, operation, [=] {
            bytes->append(reply->readAll());
            if (bytes->size() > maxBytes) {
                *rejected = "官网响应超过HTML大小上限。";
                reply->abort();
            }
        });
        QObject::connect(reply, &QNetworkReply::finished, operation, [=] {
            if (*done)
                return;
            bytes->append(reply->readAll());
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
            if (!rejected->isEmpty() || bytes->size() > maxBytes)
                finish({{}, {}, rejected->isEmpty() ? "官网响应超过HTML大小上限。" : *rejected,
                        status});
            else if (!redirect.isEmpty())
                finish({{}, url.resolved(redirect), {}, status});
            else if (reply->error() != QNetworkReply::NoError)
                finish({{}, {}, reply->errorString(), status});
            else if (status != 200)
                finish({{}, {}, QString("官网HTTP状态：%1").arg(status), status});
            else
                finish({*bytes, {}, {}, status});
        });
    });
    return operation;
}
} // namespace campus
