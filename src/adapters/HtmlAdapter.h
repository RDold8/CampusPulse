#pragma once
#include "domain/Notice.h"
#include "adapters/SchoolPackage.h"
#include <QByteArray>

namespace campus {
struct PageLink {
    QString title;
    QUrl url;
};
class HtmlAdapter {
  public:
    std::vector<Notice> parseList(const QByteArray &html, const SourceConfig &source) const;
    QUrl nextPage(const QByteArray &html, const SourceConfig &source, const QUrl &current) const;
    Notice parseDetail(const QByteArray &html, const SourceConfig &source, Notice notice) const;
    std::vector<PageLink> links(const QByteArray &html, const QUrl &page) const;
    QString pageTitle(const QByteArray &html) const;
};
} // namespace campus
