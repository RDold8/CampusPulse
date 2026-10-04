#pragma once
#include "adapters/HtmlAdapter.h"
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <deque>

class OnboardingTests;

namespace campus {
// Created from a community package or the constrained public-homepage discovery seed.
class SchoolOnboarding final : public QObject {
    Q_OBJECT
  public:
    static constexpr int AlgorithmVersion = 4;
    SchoolOnboarding(const QString &seedFile, const QString &outputDirectory,
                     QObject *parent = nullptr, int intervalMs = 3000,
                     QStringList supplementalEntries = {});
    void start();
    static bool withinUniversity(const QUrl &url, const QString &officialRoot);
    static bool isDiscoveryLabel(const QString &label);
  signals:
    void progress(QString message);
    void finished(QString configFile, int readySources, int cachedRows);
    void failed(QString reason);

  private:
    friend class ::OnboardingTests;
    struct Page {
        QUrl url;
        QString label;
        int depth = 0;
        bool candidate = false;
        int redirects = 0;
        std::optional<Notice> detail;
        QString sourceKey;
        QUrl origin;
        int priority = 0;
        QStringList redirectHosts;
    };
    QJsonObject seed_;
    SchoolPackage school_;
    QString directory_, root_;
    QStringList supplementalEntries_;
    HtmlAdapter parser_;
    std::deque<Page> queue_;
    QSet<QString> queued_;
    QJsonArray sources_, samples_, failures_;
    QHash<QString, int> counts_;
    QMap<QString, QJsonObject> deferredFrontier_;
    bool frontierTruncated_ = false;
    int interval_, fetched_ = 0, ready_ = 0, rows_ = 0;
    bool started_ = false;
    void enqueue(QUrl url, QString label, int depth, bool candidate);
    void pushPage(Page page);
    void defer(const Page &page, const QString &reason);
    static QString frontierId(const Page &page);
    static QJsonObject frontierEntry(const Page &page, const QString &reason);
    QStringList pageHosts(const Page &page) const;
    void next();
    void consume(const Page &page, const QByteArray &bytes, const QString &error);
    void finish();
};
} // namespace campus
