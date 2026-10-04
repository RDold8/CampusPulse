#include "adapters/PublicUniversityNetwork.h"
#ifdef Q_OS_WIN
#include "adapters/PublicBrowserSession.h"
#endif
#include <QDateTime>
#include <QHash>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkCookie>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>
#include <memory>

namespace campus {
namespace {
struct PublicSession {
    QList<QNetworkCookie> cookies;
    QByteArray userAgent;
    QDateTime expires;
};
// Host-scoped, in-memory public verification only. Never persisted or logged.
QHash<QString, PublicSession> sessions;
QByteArray sessionCookies(const PublicSession &session, const QUrl &url) {
    QByteArray header;
    for (const auto &cookie : session.cookies) {
        QString domain = cookie.domain().toLower();
        if (domain.startsWith('.')) domain.remove(0, 1);
        const auto path = cookie.path().isEmpty() ? QString("/") : cookie.path();
        const auto requestedPath = url.path().isEmpty() ? QString("/") : url.path();
        if ((!domain.isEmpty() && domain != url.host().toLower()) ||
            (!cookie.isSessionCookie() && cookie.expirationDate() <= QDateTime::currentDateTimeUtc()) ||
            !(requestedPath == path || (requestedPath.startsWith(path) &&
               (path.endsWith('/') || requestedPath.mid(path.size()).startsWith('/')))))
            continue;
        if (!header.isEmpty()) header += "; ";
        header += cookie.toRawForm(QNetworkCookie::NameAndValueOnly);
    }
    return header;
}
bool subnet(const QHostAddress &address, const char *network, int bits) {
    return address.isInSubnet(QHostAddress(QString::fromLatin1(network)), bits);
}
} // namespace

bool PublicUniversityNetwork::isBrowserVerification(int status, const QByteArray &html) {
    if (status != 403 && status != 412 && status != 503)
        return false;
    const auto lower = html.left(65536).toLower();
    if (!lower.contains("<script"))
        return false;
    return (lower.contains("$_ts") && lower.contains(".nsd=")) ||
           lower.contains("/cdn-cgi/challenge-platform/") ||
           (lower.contains("document.cookie") && lower.contains("location.reload"));
}
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
                                     Callback callback, int maxBytes, int timeoutMs,
                                     std::function<void(const QString &)> progress) {
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
        const auto session = sessions.value(url.host().toLower());
        const bool currentSession = session.expires > QDateTime::currentDateTimeUtc();
        request.setRawHeader("User-Agent", currentSession ? session.userAgent :
                             QByteArray("CampusPulse/0.1.2 (public university discovery)"));
        if (currentSession) {
            const auto cookies = sessionCookies(session, url);
            if (!cookies.isEmpty()) request.setRawHeader("Cookie", cookies);
        }
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
            else if (isBrowserVerification(status, *bytes)) {
#ifdef Q_OS_WIN
                // The server supplied a public browser verification page, not
                // a university homepage. Execute it in an isolated browser;
                // subsequent pages return to the bounded HTTP path.
                timer->stop();
                if (progress) progress("官网返回浏览器验证页，正在使用独立公开会话验证；不读取个人浏览器或教务账号。");
                PublicBrowserSession::verify(url, address, operation,
                    [finish, url, maxBytes](PublicBrowserSession::Result result) {
                        if (!result.error.isEmpty() || result.status != 200 ||
                            result.html.isEmpty() || result.html.size() > maxBytes) {
                            finish({{}, {}, result.error.isEmpty() ?
                                QString("官网浏览器验证未完成，保留当前学校。") : result.error,
                                result.status});
                            return;
                        }
                        sessions.insert(url.host().toLower(),
                            {result.cookies, result.userAgent, QDateTime::currentDateTimeUtc().addSecs(1200)});
                        if (!result.finalUrl.isEmpty() && result.finalUrl != url)
                            finish({{}, result.finalUrl, {}, 302});
                        else
                            finish({result.html, {}, {}, result.status});
                    }, maxBytes, 25000);
#else
                finish({{}, {}, "官网需要公开浏览器验证；当前平台尚未提供独立验证组件。", status});
#endif
            }
            else if (reply->error() != QNetworkReply::NoError)
                finish({{}, {}, status ? QString("官网 %1 返回 HTTP %2。").arg(url.host()).arg(status) :
                        QString("官网 %1 的 HTTPS 请求失败（网络错误 %2）。").arg(url.host()).arg(int(reply->error())), status});
            else if (status != 200)
                finish({{}, {}, QString("官网HTTP状态：%1").arg(status), status});
            else
                finish({*bytes, {}, {}, status});
        });
    });
    return operation;
}
} // namespace campus
