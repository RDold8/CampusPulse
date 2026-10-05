#include "adapters/HtmlAdapter.h"
#include <QRegularExpression>
#include <QUrlQuery>
#include <QDate>
#include <QCryptographicHash>
#include <QSet>
#include <QHash>
#include <QLocale>
#include <lexbor/html/html.h>
#include <lexbor/css/css.h>
#include <lexbor/selectors/selectors.h>
#include <memory>
#include <stdexcept>
#include <algorithm>

namespace campus {
namespace {
struct DocDeleter {
    void operator()(lxb_html_document_t *p) const {
        lxb_html_document_destroy(p);
    }
};
struct ParserDeleter {
    void operator()(lxb_css_parser_t *p) const {
        lxb_css_parser_destroy(p, true);
    }
};
struct SelectorDeleter {
    void operator()(lxb_selectors_t *p) const {
        lxb_selectors_destroy(p, true);
    }
};
struct ListDeleter {
    void operator()(lxb_css_selector_list_t *p) const {
        lxb_css_selector_list_destroy_memory(p);
    }
};

class HtmlDocument {
  public:
    explicit HtmlDocument(const QByteArray &html) : doc_(lxb_html_document_create()) {
        if (!doc_ || lxb_html_document_parse(doc_.get(),
                                             reinterpret_cast<const lxb_char_t *>(html.constData()),
                                             static_cast<size_t>(html.size())) != LXB_STATUS_OK)
            throw std::runtime_error("HTML解析失败");
    }
    lxb_dom_node_t *root() const {
        return lxb_dom_interface_node(doc_.get());
    }
    std::vector<lxb_dom_node_t *> select(lxb_dom_node_t *root, const QString &css) const {
        std::unique_ptr<lxb_css_parser_t, ParserDeleter> parser(lxb_css_parser_create());
        std::unique_ptr<lxb_selectors_t, SelectorDeleter> selectors(lxb_selectors_create());
        if (!parser || !selectors || lxb_css_parser_init(parser.get(), nullptr) != LXB_STATUS_OK ||
            lxb_selectors_init(selectors.get()) != LXB_STATUS_OK)
            throw std::runtime_error("CSS解析器初始化失败");
        const auto encoded = css.toUtf8();
        std::unique_ptr<lxb_css_selector_list_t, ListDeleter> list(lxb_css_selectors_parse(
            parser.get(), reinterpret_cast<const lxb_char_t *>(encoded.constData()),
            static_cast<size_t>(encoded.size())));
        if (!list || parser->status != LXB_STATUS_OK)
            throw std::runtime_error("CSS选择器无效");
        std::vector<lxb_dom_node_t *> result;
        auto callback = [](lxb_dom_node_t *node, lxb_css_selector_specificity_t,
                           void *ctx) -> lxb_status_t {
            static_cast<std::vector<lxb_dom_node_t *> *>(ctx)->push_back(node);
            return LXB_STATUS_OK;
        };
        if (lxb_selectors_find(selectors.get(), root, list.get(), callback, &result) !=
            LXB_STATUS_OK)
            throw std::runtime_error("CSS查询失败");
        return result;
    }

  private:
    std::unique_ptr<lxb_html_document_t, DocDeleter> doc_;
};
QString text(lxb_dom_node_t *node) {
    size_t length = 0;
    const auto content = lxb_dom_node_text_content(node, &length);
    return content ? QString::fromUtf8(reinterpret_cast<const char *>(content),
                                       static_cast<qsizetype>(length))
                         .simplified()
                   : QString{};
}
QString attribute(lxb_dom_node_t *node, const char *name, size_t nameLength) {
    size_t length = 0;
    const auto value = lxb_dom_element_get_attribute(lxb_dom_interface_element(node),
                                                     reinterpret_cast<const lxb_char_t *>(name),
                                                     nameLength, &length);
    return value ? QString::fromUtf8(reinterpret_cast<const char *>(value),
                                     static_cast<qsizetype>(length))
                 : QString{};
}
QString publicationDate(const QString &raw) {
    static const QRegularExpression englishDate("\\b([A-Za-z]{3}\\s+\\d{1,2},\\s+\\d{4})\\b");
    const auto englishMatch = englishDate.match(raw);
    const auto english = QLocale(QLocale::English, QLocale::UnitedStates)
                             .toDate(englishMatch.captured(1), "MMM d, yyyy");
    if (english.isValid())
        return english.toString(Qt::ISODate);
    static const QRegularExpression splitYear(
        "^(\\d{1,2})[-./](\\d{1,2})\\s*(\\d{4})(?:\\s*[-—–]+\\s*[A-Za-z]+)?$");
    const auto split = splitYear.match(raw.simplified());
    if (split.hasMatch())
        return QDate(split.captured(3).toInt(), split.captured(1).toInt(),
                     split.captured(2).toInt()).toString(Qt::ISODate);
    // Some university templates render the day before the nested year-month span.
    static const QRegularExpression reversed(
        "^(\\d{1,2})\\s*(\\d{4})[-./](\\d{1,2})(?:\\s*星期[一二三四五六日天])?$");
    const auto reversedMatch = reversed.match(raw.trimmed());
    if (reversedMatch.hasMatch())
        return QDate(reversedMatch.captured(2).toInt(), reversedMatch.captured(3).toInt(),
                     reversedMatch.captured(1).toInt())
            .toString(Qt::ISODate);
    static const QRegularExpression regex(
        "(\\d{4})(?:年|[-/.])(\\d{1,2})(?:月|[-/.])(\\d{1,2})(?:日)?");
    const auto match = regex.match(raw);
    if (!match.hasMatch())
        return {};
    return QDate(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt())
        .toString(Qt::ISODate);
}
QUrl automaticUrl(const QUrl &base, const QString &href) {
    auto resolved = base.resolved(QUrl(href));
    // Old official templates still contain HTTP self-links. Only the current
    // trusted HTTPS host may be upgraded; this never expands the allowlist.
    if (base.scheme() == "https" && resolved.scheme() == "http" &&
        resolved.host().compare(base.host(), Qt::CaseInsensitive) == 0 && resolved.port() == -1 &&
        resolved.userName().isEmpty() && resolved.password().isEmpty())
        resolved.setScheme("https");
    resolved.setFragment({});
    return resolved;
}
QString directText(lxb_dom_node_t *node) {
    QStringList pieces;
    for (auto child = node->first_child; child; child = child->next)
        if (child->type == LXB_DOM_NODE_TYPE_TEXT)
            pieces << text(child);
    return pieces.join(" ").simplified();
}
QString textWithout(lxb_dom_node_t *node, const QSet<lxb_dom_node_t *> &excluded) {
    if (excluded.contains(node))
        return {};
    if (node->type == LXB_DOM_NODE_TYPE_TEXT)
        return text(node);
    QStringList pieces;
    for (auto child = node->first_child; child; child = child->next)
        pieces << textWithout(child, excluded);
    return pieces.join(" ").simplified();
}
bool isDateField(const QString &raw) {
    static const QRegularExpression dateOnly(
        "^[\\[【(]?\\s*(?:\\d{4}(?:年|[-/.])\\d{1,2}(?:月|[-/.])\\d{1,2}日?"
        "(?:\\s+\\d{1,2}:\\d{2}(?::\\d{2})?)?|[A-Za-z]{3}\\s+\\d{1,2},\\s+\\d{4})"
        "\\s*[\\]】)]?$");
    return dateOnly.match(raw).hasMatch() || raw.startsWith("发布日期") ||
           raw.startsWith("发布时间") || raw.startsWith("发表于");
}
bool navigationRow(lxb_dom_node_t *node) {
    static const QRegularExpression menuClass(
        "(?:^|\\s)(?:wp_nav|wp-menu|nav|nav-list|nav_list|nav-item|nav_item|menu|menu-item|sub-menu|footer|foot-top|links)(?:\\s|$)");
    for (auto parent = node; parent; parent = parent->parent) {
        if (parent->type != LXB_DOM_NODE_TYPE_ELEMENT)
            continue;
        if (parent->local_name == LXB_TAG_NAV || parent->local_name == LXB_TAG_FOOTER ||
            attribute(parent, "role", 4) == "navigation" ||
            menuClass.match(attribute(parent, "class", 5)).hasMatch())
            return true;
    }
    return false;
}
} // namespace

bool HtmlAdapter::isArticleUrl(const QUrl &url) {
    if (!url.isValid() || url.host().isEmpty() ||
        (url.scheme() != "https" && url.scheme() != "http") ||
        !url.userInfo().isEmpty() || url.port() != -1)
        return false;
    static const QRegularExpression articlePath(
        "(?:/info/\\d+/\\d+\\.(?:htm|html)|"
        "/(?:20\\d{2}/\\d{4}/)?c[1-9]\\d*a[1-9]\\d*/page\\.(?:htm|html)|"
        "/(?:article|event)/(?:20\\d{2}/\\d{2}/\\d{2}/)?[1-9]\\d*/?|"
        "/mportal/(?:article|recruit)/details|/detail/(?:news|career|online)(?:/[^/]+)?|"
        "/[a-fA-F0-9]{24,64}\\.htm|/\\d+\\.jhtml)$");
    if (articlePath.match(url.path()).hasMatch())
        return true;
    const QUrlQuery query(url);
    static const QRegularExpression positiveId("^[1-9][0-9]*$");
    if (!positiveId.match(query.queryItemValue("wbnewsid")).hasMatch() ||
        !url.path().endsWith(".jsp"))
        return false;
    const auto type = query.queryItemValue("urltype");
    static const QRegularExpression vsbContentPath("/(?:content\\d*|context|nry)\\.jsp$");
    return type == "news.NewsContentUrl" ||
           (type.isEmpty() && vsbContentPath.match(url.path()).hasMatch());
}

std::vector<Notice> HtmlAdapter::parseList(const QByteArray &html,
                                           const SourceConfig &source) const {
    HtmlDocument doc(html);
    if (source.autoDetect) {
        std::vector<Notice> result;
        QSet<QString> seen;
        const QRegularExpression staticDetailPath("/(?:[a-fA-F0-9]{24,64}\\.htm|\\d+\\.jhtml)$");
        const auto entryPath = source.entry.path();
        const auto columnPath = entryPath.left(entryPath.lastIndexOf('/') + 1);
        bool scopedStaticColumn = false;
        if (columnPath != "/" && !columnPath.isEmpty())
            for (auto candidate : doc.select(doc.root(), "a[href]")) {
                const auto resolved = automaticUrl(source.entry, attribute(candidate, "href", 4));
                if (resolved.scheme() == "https" && resolved.port() == -1 &&
                    isAllowedUrl(resolved, source) && resolved.path().startsWith(columnPath) &&
                    staticDetailPath.match(resolved.path()).hasMatch()) {
                    scopedStaticColumn = true;
                    break;
                }
            }
        const QString dateSelector = "time, .date, .time, .more, .mark, .list_time, .n-time, "
                                     ".news_meta, .news_date, .news_time, .publish-date, .pubdate";
        const QRegularExpression eventPath("/event/(?:20\\d{2}/\\d{2}/\\d{2}/)?[1-9]\\d*/?$");
        auto rows = doc.select(doc.root(), "li, tr, article, .list-li, .item-list > .item, .views-row");
        QSet<lxb_dom_node_t *> knownRows(rows.begin(), rows.end());
        // Some CMS cards use div wrappers instead of list items. Derive only
        // the nearest small container with one unambiguous public article URL.
        // Multi-article wrappers never lend dates or headings to a child link.
        int inspected = 0;
        for (auto anchor : doc.select(doc.root(), "a[href]")) {
            const auto target = automaticUrl(source.entry, attribute(anchor, "href", 4));
            if (!isArticleUrl(target) || !isAllowedUrl(target, source) || navigationRow(anchor))
                continue;
            if (++inspected > 512)
                break;
            auto parent = anchor->parent;
            for (int depth = 0; parent && depth < 4; ++depth, parent = parent->parent) {
                if (knownRows.contains(parent) || navigationRow(parent))
                    break;
                if (parent->type != LXB_DOM_NODE_TYPE_ELEMENT)
                    continue;
                QSet<QString> urls;
                for (auto child : doc.select(parent, "a[href]")) {
                    const auto url = automaticUrl(source.entry, attribute(child, "href", 4));
                    if (isArticleUrl(url) && isAllowedUrl(url, source))
                        urls.insert(url.toString(QUrl::FullyEncoded));
                }
                if (urls.size() != 1 || text(parent).size() > 2000)
                    break;
                if (parent->local_name == LXB_TAG_DIV || parent->local_name == LXB_TAG_TD ||
                    parent->local_name == LXB_TAG_DD) {
                    rows.push_back(parent);
                    knownRows.insert(parent);
                }
            }
        }
        // Preserve DOM order so first-body validation checks the first actual
        // announcement rather than an arbitrary wrapper encountered earlier.
        QHash<lxb_dom_node_t *, int> domOrder;
        std::vector<lxb_dom_node_t *> pending{doc.root()};
        int order = 0;
        while (!pending.empty()) {
            auto node = pending.back(); pending.pop_back();
            domOrder[node] = order++;
            for (auto child = node->last_child; child; child = child->prev)
                pending.push_back(child);
        }
        std::sort(rows.begin(), rows.end(), [&](auto left, auto right) { return domOrder[left] < domOrder[right]; });
        for (auto row : rows) {
            if (navigationRow(row))
                continue;
            lxb_dom_node_t *anchor = nullptr;
            QUrl url;
            QSet<QString> rowUrls;
            for (auto candidate : doc.select(row, "a[href]")) {
                const auto resolved = automaticUrl(source.entry, attribute(candidate, "href", 4));
                // A static CMS column often shares navigation and sidebars
                // with other columns. Prefer this column's own article IDs;
                // leave /info/ and parameter-based adapters unchanged.
                if (scopedStaticColumn && staticDetailPath.match(resolved.path()).hasMatch() &&
                    !resolved.path().startsWith(columnPath))
                    continue;
                if (resolved.scheme() == "https" && resolved.port() == -1 &&
                    isAllowedUrl(resolved, source) &&
                    isArticleUrl(resolved)) {
                    rowUrls.insert(resolved.toString(QUrl::FullyEncoded));
                    if (!anchor ||
                        (!attribute(candidate, "title", 5).isEmpty() &&
                         attribute(anchor, "title", 5).isEmpty()) ||
                        (text(anchor).isEmpty() && !text(candidate).isEmpty())) {
                        anchor = candidate;
                        url = resolved;
                    }
                }
            }
            // A layout table or navigation wrapper must not lend another
            // article's date to its first link. Require one distinct detail URL.
            if (!anchor || rowUrls.size() != 1)
                continue;
            const bool event = eventPath.match(url.path()).hasMatch();
            QString published;
            const auto dateAttributes = event
                ? QStringList{"data-published", "data-pubdate"}
                : QStringList{"data-time", "datetime", "data-date", "data-published", "data-pubdate"};
            for (const auto &name : dateAttributes) {
                const auto encoded = name.toUtf8();
                published = publicationDate(attribute(anchor, encoded.constData(), static_cast<size_t>(encoded.size())));
                if (!published.isEmpty())
                    break;
            }
            for (auto node : doc.select(row, event ? ".publication-date, .publish-date, .pubdate, time[data-publication]" : dateSelector)) {
                if (!published.isEmpty())
                    break;
                const auto raw = text(node);
                if (raw.size() <= 40 && !(published = publicationDate(raw)).isEmpty())
                    break;
            }
            if (published.isEmpty() && !event)
                for (auto node : doc.select(row, "span")) {
                    const auto raw = text(node);
                    if (raw.size() <= 40 && isDateField(raw) &&
                        !(published = publicationDate(raw)).isEmpty())
                        break;
                }
            if (published.isEmpty() && !event) {
                // Some cards explicitly render the publication day and year-
                // month in separate short nodes. Require one unambiguous pair;
                // never search the title/summary for a year or an event date.
                const QRegularExpression yearMonth("^(\\d{4})[-/.](\\d{1,2})$");
                const QRegularExpression dayOnly("^\\d{1,2}$");
                QSet<QString> months, days;
                for (auto node : doc.select(row, "span, time, .date, .time, .month")) {
                    const auto raw = text(node);
                    if (yearMonth.match(raw).hasMatch()) months.insert(raw);
                }
                for (auto node : doc.select(row, "h2, h3, h4, h5, .day")) {
                    const auto raw = text(node);
                    if (dayOnly.match(raw).hasMatch()) days.insert(raw);
                }
                if (months.size() == 1 && days.size() == 1) {
                    const auto match = yearMonth.match(*months.begin());
                    published = QDate(match.captured(1).toInt(), match.captured(2).toInt(),
                                      days.begin()->toInt()).toString(Qt::ISODate);
                }
            }
            // Some CMS templates place a full publication date directly after
            // the anchor. Read direct text only; never infer it from the title.
            if (published.isEmpty() && !event) {
                const auto raw = directText(row);
                if (raw.size() <= 40)
                    published = publicationDate(raw);
            }
            if (published.isEmpty() && !source.allowUnknownDates)
                continue;
            auto title = attribute(anchor, "title", 5).simplified();
            if (title.isEmpty()) {
                for (auto node : doc.select(anchor, ".news_title, .news-title, .notice-title, .title, .nr, h2, h3, h4, h5")) {
                    const auto raw = text(node);
                    if (raw.size() >= 4 && !isDateField(raw)) {
                        title = raw;
                        break;
                    }
                }
            }
            if (title.isEmpty()) {
                QSet<lxb_dom_node_t *> dateNodes;
                for (auto node : doc.select(anchor, "time, .date, .time, .list_time, .n-time, .news_meta, .news_date, .news_time"))
                    dateNodes.insert(node);
                for (auto node : doc.select(anchor, "span")) {
                    const auto raw = text(node);
                    if (raw.size() <= 40 && isDateField(raw) && !publicationDate(raw).isEmpty())
                        dateNodes.insert(node);
                }
                title = textWithout(anchor, dateNodes);
            }
            if (title.size() < 4 || title.size() > 300)
                continue;
            url.setFragment({});
            const auto canonical = url.toString(QUrl::FullyEncoded);
            if (seen.contains(canonical)) {
                // Floating announcements can repeat a list link without its
                // date. Keep the URL identity and enrich it from the real row.
                if (!published.isEmpty())
                    for (auto &known : result)
                        if (known.url == canonical.toStdString() && known.publishedDate.empty()) {
                            known.publishedDate = published.toStdString();
                            break;
                        }
                continue;
            }
            seen.insert(canonical);
            Notice n;
            n.schoolId = source.schoolId.toStdString();
            n.sourceId = source.id.toStdString();
            n.sourceName = source.name.toStdString();
            n.url = canonical.toStdString();
            n.title = title.toStdString();
            n.publishedDate = published.toStdString();
            n.id = QCryptographicHash::hash((source.schoolId + "|" + canonical).toUtf8(),
                                            QCryptographicHash::Sha256)
                       .toHex()
                       .toStdString();
            result.push_back(std::move(n));
        }
        if (result.size() < 3)
            throw std::runtime_error("自动识别未找到至少3条有效公开通知，需其他适配器");
        return result;
    }
    const auto rows = doc.select(doc.root(), source.itemSelector);
    if (rows.empty())
        throw std::runtime_error("未找到通知列表，来源结构可能已变化");
    std::vector<Notice> result;
    QSet<QString> seen;
    for (auto row : rows) {
        const auto anchors = doc.select(row, source.titleSelector);
        const auto dates = doc.select(row, source.dateSelector);
        if (anchors.empty())
            throw std::runtime_error("通知行缺少标题链接");
        auto url = source.entry.resolved(QUrl(attribute(anchors.front(), "href", 4)));
        url.setFragment({});
        const auto published = dates.empty() ? QString{} : publicationDate(text(dates.front()));
        // Prefer the full official title when list text is truncated.
        auto title = attribute(anchors.front(), "title", 5).simplified();
        if (title.isEmpty()) {
            // Some list anchors embed the publication date; it is not part of the title.
            for (auto date : dates) {
                for (auto nested : doc.select(anchors.front(), source.dateSelector)) {
                    if (date == nested) {
                        lxb_dom_node_destroy_deep(date);
                        break;
                    }
                }
            }
            title = text(anchors.front());
        }
        if (title.isEmpty() || !isAllowedUrl(url, source))
            throw std::runtime_error("通知标题或链接无效");
        const auto canonical = url.toString(QUrl::FullyEncoded);
        if (seen.contains(canonical))
            continue;
        seen.insert(canonical);
        Notice notice;
        notice.schoolId = source.schoolId.toStdString();
        notice.sourceId = source.id.toStdString();
        notice.sourceName = source.name.toStdString();
        notice.title = title.toStdString();
        notice.url = canonical.toStdString();
        notice.id = QCryptographicHash::hash((source.schoolId + "|" + canonical).toUtf8(),
                                             QCryptographicHash::Sha256)
                        .toHex()
                        .toStdString();
        notice.publishedDate = published.toStdString();
        result.push_back(std::move(notice));
    }
    return result;
}

QUrl HtmlAdapter::nextPage(const QByteArray &html, const SourceConfig &source,
                           const QUrl &current) const {
    if (source.nextPageSelector.isEmpty())
        return {};
    HtmlDocument doc(html);
    const auto links = doc.select(doc.root(), source.nextPageSelector);
    if (links.empty())
        return {};
    const auto raw = attribute(links.front(), "href", 4);
    if (raw.isEmpty())
        throw std::runtime_error("下一页缺少链接");
    auto url = current.resolved(QUrl(raw));
    url.setFragment({});
    if (!isAllowedUrl(url, source))
        throw std::runtime_error("下一页超出允许域名");
    return url;
}

Notice HtmlAdapter::parseDetail(const QByteArray &html, const SourceConfig &source,
                                Notice notice) const {
    HtmlDocument doc(html);
    lxb_dom_node_t *body = nullptr;
    if (source.autoDetect) {
        // Prefer explicit CMS article bodies over layout .view-content blocks,
        // which may contain a sidebar or a list of unrelated announcements.
        for (const auto &selector : QStringList{
                 ".wp_articlecontent", ".v_news_content", "#vsb_content", "#vsb_content_2",
                 ".article-content", ".article-body", ".detail-content", ".content-detail",
                 ".m-news-detail article", ".page_article .article", ".item_content", ".content-txt",
                 ".node__content .field--name-body", ".field--name-body", ".view-content"}) {
            for (auto candidate : doc.select(doc.root(), selector)) {
                QSet<lxb_dom_node_t *> ignored;
                for (auto node : doc.select(candidate, "script, style"))
                    ignored.insert(node);
                if (!textWithout(candidate, ignored).isEmpty()) {
                    body = candidate;
                    break;
                }
            }
            if (body)
                break;
        }
    } else {
        const auto bodies = doc.select(doc.root(), source.bodySelector);
        if (!bodies.empty())
            body = bodies.front();
    }
    if (!body)
        throw std::runtime_error("未找到通知正文，来源结构可能已变化");
    for (auto node : doc.select(body, "script, style"))
        lxb_dom_node_destroy_deep(node);
    QStringList paragraphs;
    for (auto node : doc.select(body, "p")) {
        const auto line = text(node);
        if (!line.isEmpty())
            paragraphs << line;
    }
    notice.body = (paragraphs.isEmpty() ? text(body) : paragraphs.join("\n\n")).toStdString();
    if (source.autoDetect && notice.publishedDate.empty()) {
        QSet<QString> publicationDates;
        // Publication metadata and CMS submission headers describe publication.
        // Generic time/.date fields in an event body instead describe the event.
        for (auto node : doc.select(doc.root(),
                "meta[property='article:published_time'], meta[name='PubDate'], "
                "meta[name='publishdate'], meta[name='pubdate'], .arti_update, "
                ".Article_PublishDate, .article-date, .publication-date, .publish-date, .publish_time, "
                ".node__submitted time[datetime], .submitted time[datetime], "
                ".top_misc > .left-attr.first")) {
            auto raw = attribute(node, "content", 7);
            if (raw.isEmpty()) raw = attribute(node, "datetime", 8);
            if (raw.isEmpty()) raw = text(node);
            if (raw.size() <= 150) {
                const auto date = publicationDate(raw);
                if (!date.isEmpty()) publicationDates.insert(date);
            }
        }
        for (auto node :
             doc.select(doc.root(), ".m-recruit-detail-tips li, .detail-tit, .xq, .ac, "
                                    ".article_author, .news_information, .content-assist, "
                                    ".ny_fbt, .information, h1")) {
            const auto raw = text(node);
            if (raw.size() <= 150 &&
                (raw.contains("发布时间") || raw.contains("发布日期") || raw.contains("发表于"))) {
                const auto date = publicationDate(raw);
                if (!date.isEmpty()) publicationDates.insert(date);
            }
        }
        if (publicationDates.size() == 1)
            notice.publishedDate = publicationDates.begin()->toStdString();
    }
    if (notice.body.empty())
        throw std::runtime_error("通知正文为空");
    notice.attachments.clear();
    for (auto node : doc.select(
             doc.root(), source.autoDetect ? ".fujian a[href], a[href*='download.jsp'], "
                                             "a[href$='.pdf'], a[href$='.docx'], a[href$='.xlsx']"
                                           : source.attachmentSelector)) {
        const auto url =
            QUrl(QString::fromStdString(notice.url)).resolved(QUrl(attribute(node, "href", 4)));
        if (isAllowedUrl(url, source))
            notice.attachments.push_back(
                {text(node).toStdString(), url.toString(QUrl::FullyEncoded).toStdString()});
    }
    return notice;
}
std::vector<PageLink> HtmlAdapter::links(const QByteArray &html, const QUrl &page) const {
    HtmlDocument doc(html);
    std::vector<PageLink> result;
    for (auto node : doc.select(doc.root(), "a[href]")) {
        const auto href = attribute(node, "href", 4).trimmed();
        // Some CMS menus put literal error markup in href. QUrl otherwise
        // percent-encodes it into an apparent same-host URL and wastes requests.
        if (href.isEmpty() || href.startsWith('#') ||
            href.contains(QRegularExpression("[<>\\x00-\\x1f\\x7f]")))
            continue;
        auto label = text(node);
        // Icon-based navigation often stores its readable label only in attributes.
        if (label.isEmpty())
            label = attribute(node, "title", 5).simplified();
        if (label.isEmpty())
            label = attribute(node, "aria-label", 10).simplified();
        if (label.isEmpty())
            for (auto image : doc.select(node, "img[alt]")) {
                label = attribute(image, "alt", 3).simplified();
                if (!label.isEmpty())
                    break;
            }
        if (label.isEmpty() && node->parent && node->parent->local_name == LXB_TAG_LI) {
            // A two-link menu may separate an unlinked label from its icon.
            // Restrict inheritance to two direct sibling anchors, one with no
            // destination. Never borrow another real link's label or nested menu.
            std::vector<lxb_dom_node_t *> siblings;
            for (auto candidate : doc.select(node->parent, "a"))
                if (candidate->parent == node->parent) siblings.push_back(candidate);
            if (siblings.size() == 2) {
                auto other = siblings.front() == node ? siblings.back() : siblings.front();
                const auto otherHref = attribute(other, "href", 4).trimmed();
                const auto context = text(other);
                if ((otherHref.isEmpty() || otherHref == "#") &&
                    !context.isEmpty() && context.size() <= 80)
                    label = context;
            }
        }
        const auto url = page.resolved(QUrl(href));
        if (!isArticleUrl(url) && (label.isEmpty() ||
            QRegularExpression("^(?:更多|more)(?:\\s*[+>›»]+)?$", QRegularExpression::CaseInsensitiveOption)
                .match(label).hasMatch())) {
            for (auto parent = node->parent, depthNode = parent; parent; parent = parent->parent) {
                // At most three local block ancestors; never inherit a heading
                // from the page body, a whole sidebar, or multiple subcolumns.
                int depth = 0;
                for (auto check = depthNode; check && check != parent; check = check->parent) ++depth;
                if (depth > 2 || parent->local_name == LXB_TAG_BODY)
                    break;
                QSet<QString> headings;
                for (auto heading : doc.select(parent, ".tit, .title_text, .column-title, .post-title, h2, h3")) {
                    const auto value = text(heading);
                    if (value.size() >= 2 && value.size() <= 35 && !isDateField(value))
                        headings.insert(value);
                }
                if (headings.size() > 1)
                    break;
                if (headings.size() == 1) {
                    label = *headings.begin() + " · 更多";
                    break;
                }
            }
            if (label.isEmpty() && QUrlQuery(url).queryItemValue("urltype") == "tree.TreeTempUrl")
                label = "公开栏目";
        }
        if (!label.isEmpty() && label.size() <= 300 &&
            (url.scheme() == "https" || url.scheme() == "http"))
            result.push_back({label, url});
    }
    return result;
}
QString HtmlAdapter::pageTitle(const QByteArray &html) const {
    HtmlDocument doc(html);
    const auto titles = doc.select(doc.root(), "title");
    return titles.empty() ? QString{} : text(titles.front());
}
} // namespace campus
