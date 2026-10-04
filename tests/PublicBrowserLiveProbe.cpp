#include "adapters/PublicBrowserSession.h"
#include "adapters/PublicUniversityNetwork.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("url"), QStringLiteral("Official HTTPS page to verify"), QStringLiteral("url")});
    parser.addOption({QStringLiteral("evidence"), QStringLiteral("JSON result path (contains no cookie values)"), QStringLiteral("file")});
    parser.addOption({QStringLiteral("html"), QStringLiteral("Optional public HTML result file"), QStringLiteral("file")});
    parser.process(app);
    const QUrl url(parser.value(QStringLiteral("url")), QUrl::StrictMode);
    const QString evidence = parser.value(QStringLiteral("evidence"));
    const QString htmlPath = parser.value(QStringLiteral("html"));
    if (!url.isValid() || url.scheme() != QStringLiteral("https") || evidence.isEmpty())
        return 2;

    int resultCode = 1;
    bool finished = false;
    const auto writeResult = [&](campus::PublicBrowserSession::Result result) {
        if (finished)
            return;
        finished = true;
        const bool passed = result.error.isEmpty() && result.status == 200 && !result.html.isEmpty();
        QJsonObject record{{QStringLiteral("url"), url.toString()},
                           {QStringLiteral("final_url"), result.finalUrl.toString()},
                           {QStringLiteral("passed"), passed},
                           {QStringLiteral("status"), result.status},
                           {QStringLiteral("error"), result.error},
                           {QStringLiteral("html_bytes"), result.html.size()},
                           {QStringLiteral("cookie_count"), result.cookies.size()},
                           {QStringLiteral("model_calls"), 0}};
        if (passed && !htmlPath.isEmpty()) {
            QDir().mkpath(QFileInfo(htmlPath).absolutePath());
            QSaveFile html(htmlPath);
            if (html.open(QIODevice::WriteOnly)) {
                html.write(result.html);
                html.commit();
            }
        }
        QDir().mkpath(QFileInfo(evidence).absolutePath());
        QSaveFile output(evidence);
        if (output.open(QIODevice::WriteOnly)) {
            output.write(QJsonDocument(record).toJson(QJsonDocument::Indented));
            output.commit();
        }
        const QByteArray brief = QJsonDocument(record).toJson(QJsonDocument::Compact);
        std::fwrite(brief.constData(), 1, brief.size(), stdout);
        std::fwrite("\n", 1, 1, stdout);
        resultCode = passed ? 0 : 1;
        // Let the isolated runtime release its temporary profile before exiting.
        QTimer::singleShot(2500, &app, &QCoreApplication::quit);
    };
    QHostInfo::lookupHost(url.host(), &app, [&](const QHostInfo &lookup) {
        QHostAddress pinned;
        if (lookup.error() != QHostInfo::NoError || lookup.addresses().isEmpty()) {
            campus::PublicBrowserSession::Result error;
            error.error = QStringLiteral("DNS lookup failed");
            writeResult(std::move(error));
            return;
        }
        for (const auto &address : lookup.addresses()) {
            if (!campus::PublicUniversityNetwork::isPublicAddress(address)) {
                campus::PublicBrowserSession::Result error;
                error.error = QStringLiteral("Nonpublic DNS address rejected");
                writeResult(std::move(error));
                return;
            }
            if (pinned.isNull() || address.protocol() == QAbstractSocket::IPv4Protocol)
                pinned = address;
        }
        campus::PublicBrowserSession::verify(url, pinned, &app, writeResult);
    });
    QTimer::singleShot(35000, &app, [&] {
        campus::PublicBrowserSession::Result error;
        error.error = QStringLiteral("Overall browser probe deadline exceeded");
        writeResult(std::move(error));
    });
    app.exec();
    return resultCode;
}
