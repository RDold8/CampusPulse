#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

namespace campus {

// Deterministic onboarding of an unconfigured Chinese education-domain school.
// A detected name is a draft identity, never a claim of community review.
class UnknownUniversityDiscovery final : public QObject {
    Q_OBJECT
  public:
    UnknownUniversityDiscovery(QString homepageInput, QString seedDirectory,
                               QObject *parent = nullptr);
    void start();
    static QUrl normalizedHomepage(const QString &input);
    static QString officialRoot(const QUrl &homepage);
    static QString identifySchool(const QByteArray &html);
    static QJsonObject seedDocument(const QUrl &homepage, const QString &name);
  signals:
    void progress(QString message);
    void finished(QString seedFile, QString schoolName);
    void failed(QString reason);

  private:
    QString input_, directory_, root_;
    QUrl requested_;
    bool started_ = false;
    void fetch(const QUrl &url, int redirects);
};
} // namespace campus
