#include "adapters/HtmlAdapter.h"
#include <QRegularExpression>
#include <QDate>
#include <QCryptographicHash>
#include <QSet>
#include <QLocale>
#include <lexbor/html/html.h>
#include <lexbor/css/css.h>
#include <lexbor/selectors/selectors.h>
#include <memory>
#include <stdexcept>

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
    // Some university templates render the day before the nested year-month span.
    static const QRegularExpression reversed("^(\\d{1,2})\\s*(\\d{4})[-./](\\d{1,2})$");
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
} // namespace

std::vector<Notice> HtmlAdapter::parseList(const QByteArray &html,
                                           const SourceConfig &source) const {
    HtmlDocument doc(html);
    if (source.autoDetect) {
        std::vector<Notice> result;
        QSet<QString> seen;
        const QRegularExpression detailPath(
            "(/info/\\d+/\\d+\\.(?:htm|html)$|/mportal/(?:article|recruit)/details$|/detail/"
            "(?:news|career|online)|/[a-fA-F0-9]{24,64}\\.htm$|/\\d+\\.jhtml$)");
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
        const QString dateSelector = "time, .date, .time, .more, .mark, .list_time, .n-time";
        for (auto row : doc.select(doc.root(), "li, tr, article, .list-li, .item-list > .item")) {
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
                    detailPath.match(resolved.path()).hasMatch()) {
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
            QString published;
            for (const auto *name : {"data-time", "datetime", "data-date"}) {
                published = publicationDate(attribute(anchor, name, qstrlen(name)));
                if (!published.isEmpty())
                    break;
            }
            for (auto node : doc.select(row, dateSelector)) {
                if (!published.isEmpty())
                    break;
                const auto raw = text(node);
                if (raw.size() <= 40 && !(published = publicationDate(raw)).isEmpty())
                    break;
            }
            if (published.isEmpty())
                for (auto node : doc.select(row, "span")) {
                    const auto raw = text(node);
                    if (raw.size() <= 40 && isDateField(raw) &&
                        !(published = publicationDate(raw)).isEmpty())
                        break;
                }
            // Some CMS templates place a full publication date directly after
            // the anchor. Read direct text only; never infer it from the title.
            if (published.isEmpty()) {
                const auto raw = directText(row);
                if (raw.size() <= 40)
                    published = publicationDate(raw);
            }
            if (published.isEmpty() && !source.allowUnknownDates)
                continue;
            auto title = attribute(anchor, "title", 5).simplified();
            if (title.isEmpty()) {
                const auto titles = doc.select(anchor, ".title, h3, h4");
                if (!titles.empty())
                    title = text(titles.front());
            }
            if (title.isEmpty()) {
                QSet<lxb_dom_node_t *> dateNodes;
                for (auto node : doc.select(anchor, "time, .date, .time, .list_time, .n-time"))
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
    const auto bodies = doc.select(
        doc.root(),
        source.autoDetect
            ? ".v_news_content, #vsb_content, #vsb_content_2, .view-content, .article-content, "
              ".article-body, .detail-content, .content-detail, .m-news-detail article, "
              ".page_article .article, .item_content, .content-txt"
            : source.bodySelector);
    if (bodies.empty())
        throw std::runtime_error("未找到通知正文，来源结构可能已变化");
    auto body = bodies.front();
    if (source.autoDetect) {
        // Malformed legacy markup can leave an empty #vsb_content paragraph
        // before the actual .v_news_content div after HTML recovery.
        for (auto candidate : bodies) {
            QSet<lxb_dom_node_t *> ignored;
            for (auto node : doc.select(candidate, "script, style"))
                ignored.insert(node);
            if (!textWithout(candidate, ignored).isEmpty()) {
                body = candidate;
                break;
            }
        }
    }
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
        for (auto node :
             doc.select(doc.root(), ".m-recruit-detail-tips li, .detail-tit, .xq, .ac, "
                                    ".article_author, .news_information, .content-assist, "
                                    ".ny_fbt, .information, h1")) {
            const auto raw = text(node);
            if (raw.size() <= 150 &&
                (raw.contains("发布时间") || raw.contains("发布日期") || raw.contains("发表于"))) {
                const auto date = publicationDate(raw);
                if (!date.isEmpty()) {
                    notice.publishedDate = date.toStdString();
                    break;
                }
            }
        }
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
        const auto label = text(node);
        const auto url = page.resolved(QUrl(attribute(node, "href", 4)));
        if (!label.isEmpty())
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
