#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QNetworkCookie>
#include <QObject>
#include <QUrl>
#include <functional>

namespace campus {

// An isolated, short-lived browser for a public site's JavaScript verification.
// The caller must resolve and validate the pinned address before calling verify.
// This never opens the user's browser profile or authenticates a school account.
class PublicBrowserSession final {
  public:
    struct Result {
        QByteArray html;
        QList<QNetworkCookie> cookies;
        QByteArray userAgent;
        QUrl finalUrl;
        QString error;
        int status = 0;
    };
    using Callback = std::function<void(Result)>;

    static QObject *verify(const QUrl &url, const QHostAddress &pinnedAddress,
                           QObject *owner, Callback callback,
                           int maxBytes = 2 * 1024 * 1024,
                           int timeoutMs = 25000);
};

} // namespace campus
