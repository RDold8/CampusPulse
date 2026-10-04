#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QUrl>
#include <functional>

namespace campus {

struct UniversityPageResponse {
    QByteArray bytes;
    QUrl redirect;
    QString error;
    int status = 0;
};

// One request, no implicit redirects. Resolves and pins a public address while
// retaining the original HTTPS certificate name and Host header.
class PublicUniversityNetwork final {
  public:
    using Callback = std::function<void(UniversityPageResponse)>;
    static bool isPublicAddress(const QHostAddress &address);
    static bool withinUniversity(const QUrl &url, const QString &officialRoot);
    static QObject *get(const QUrl &url, const QString &officialRoot, QObject *owner,
                        Callback callback, int maxBytes = 2 * 1024 * 1024,
                        int timeoutMs = 12000);
};

} // namespace campus
