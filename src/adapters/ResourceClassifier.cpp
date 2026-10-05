#include "adapters/ResourceClassifier.h"
#include <QCryptographicHash>
#include <QDate>
#include <QHostAddress>
#include <QRegularExpression>
#include <lexbor/html/html.h>
#include <memory>

namespace campus {
namespace {
bool contains(const QString &text, const char *pattern) {
    return QRegularExpression(QString::fromUtf8(pattern), QRegularExpression::CaseInsensitiveOption)
        .match(text)
        .hasMatch();
}
struct DocumentDeleter {
    void operator()(lxb_html_document_t *document) const {
        lxb_html_document_destroy(document);
    }
};
} // namespace

bool ResourceClassifier::isSafeHttps(const QUrl &url) {
    QHostAddress address;
    const auto host = url.host().toLower();
    static const QRegularExpression hostname(
        "^(?=.{1,253}$)(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\\.)+"
        "[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$",
        QRegularExpression::CaseInsensitiveOption);
    return url.isValid() && url.scheme() == "https" && url.userInfo().isEmpty() &&
           !url.authority(QUrl::FullyEncoded).contains('@') && url.port() == -1 &&
           !url.authority(QUrl::FullyEncoded).contains(':') && hostname.match(host).hasMatch() &&
           !address.setAddress(host) && !host.endsWith(".local") && !host.endsWith(".localhost") &&
           !host.endsWith(".internal");
}
bool ResourceClassifier::isSafeHttps(const QString &value) {
    static const QRegularExpression authority("^https://([^/?#]+)",
                                              QRegularExpression::CaseInsensitiveOption);
    const auto raw = authority.match(value);
    return raw.hasMatch() && !raw.captured(1).contains('@') && !raw.captured(1).contains(':') &&
           !value.contains(QRegularExpression("[\\s\\\\]")) &&
           isSafeHttps(QUrl(value, QUrl::StrictMode));
}
QString ResourceClassifier::officialRoot(const QUrl &homepage) {
    auto root = homepage.host().toLower();
    if (root.startsWith("www."))
        root.remove(0, 4);
    return root;
}
bool ResourceClassifier::isOfficial(const QUrl &url, const QString &root) {
    const auto host = url.host().toLower();
    return !root.isEmpty() && isSafeHttps(url) && (host == root || host.endsWith("." + root));
}
QUrl ResourceClassifier::canonicalUrl(QUrl url) {
    url.setFragment({});
    if (url.path().isEmpty())
        url.setPath("/");
    return url;
}
bool ResourceClassifier::isDownload(const QUrl &url) {
    return contains(url.path(),
                    "\\.(?:pdf|docx?|xlsx?|pptx?|zip|rar|7z|exe|msi|mp4|mp3|png|jpe?g|gif)$") ||
           contains(url.path(), "(?:^|/)(?:download|filedownload)\\.(?:jsp|php|aspx?)$");
}
bool ResourceClassifier::isStudentServiceNavigation(const QString &label) {
    return contains(label.simplified(),
        "^(?:学生服务|学业服务|选课(?:和|与|及)?退课|课程(?:和|与|及)?培养|"
        "考试(?:和|与|及)?成绩|教学服务|办事指南|学籍管理|成绩管理|课程管理|培养过程|规章制度)$");
}
QString ResourceClassifier::labelInContext(const QString &label, const QString &parentCategory) {
    auto value = label.simplified();
    static const QRegularExpression dated("^(\\d{4}-\\d{2}-\\d{2})\\s*(.+)$");
    const auto date = dated.match(value);
    if (date.hasMatch() && QDate::fromString(date.captured(1), "yyyy-MM-dd").isValid())
        value = date.captured(2).trimmed();
    static const QRegularExpression version("^\\d{4}\\s*版$");
    if (parentCategory == "study_plan" && version.match(value).hasMatch())
        return "培养方案 · " + value;
    return value;
}
bool ResourceClassifier::isPractical(const QString &label, const QUrl &) {
    const auto text = label.simplified();
    if (text.size() < 2 || text.size() > 180)
        return false;
    if (isStudentServiceNavigation(text))
        return true;
    if (contains(text,
                 "举办|举行|召开|开展|颁奖|斩获|荣获|顺利完成|圆满完成|闭幕|开幕|新闻|动态|研讨会"))
        return false;
    const bool guide =
        contains(text, "指南|流程|手册|操作说明|培养方案|教学大纲|课程资料|学习资料");
    if (contains(
            text,
            "通知|公告|公示|新闻|动态|召开|举行|举办|开展|颁奖|风采|获奖|斩获|荣获|检查|研讨会") &&
        !guide)
        return false;
    // Plan/guide announcements remain announcements; concrete plans and guides
    // such as '2025本科培养方案.pdf' are retained as resources.
    if (contains(text, "通知|公告|公示") && !contains(text, "办理指南|操作指南|使用指南|服务指南"))
        return false;
    if (guide &&
        contains(text,
                 "实习|竞赛.*证书|考试.*查询|考务|成绩.*核对|教室.*申请|正考|证书|教务系统密码"))
        return true;
    return contains(
        text, "(?:教务处|本科生院|学生处|研究生院|财务处|团委)(?:概况|简介|职责)|部门(?:介绍|概况|职责)|"
              "培养方案|教学大纲|课程建设|课程资料|学习资料|课程平台|在线课程|精品课程|教材资源|"
              "图书馆|数据库|电子资源|电子图书|电子期刊|文献检索|馆藏检索|知网|CNKI|读秀|"
              "Science\\s*Direct|IEEE|Engineering\\s*Village|Web\\s*of\\s*Science|"
              "学术支持|学科服务|科研工具|论文检索|检索证明|学术写作|研究支持|读者服务|"
              "竞赛平台|竞赛指南|竞赛网站|竞赛官方|竞赛官网|数学建模平台|学科竞赛|创新创业平台|"
              "挑战杯平台|"
              "办事|服务指南|办理指南|办理流程|学生服务|服务大厅|教务系统|教务管理系统|校内门户|信息门户|"
              "学生证|成绩查询|成绩单|学籍|选课|^退课$|^缺考$|^缓考$|课程考核.*(?:细则|办法)|考试纪律|重修安排|重考安排|^重修$|^补考$|毕业证|"
              "资助|奖助|助学贷款|学费|缴费流程|缴费指南|就业服务|就业指导|求职指南|"
              "招聘平台|就业信息网|心理咨询|心理服务|校园卡|校车|校历|住宿指南|入馆|借阅|"
              "座位预约|VPN校外访问|常用阅读器|读者手册|离校流程");
}
SchoolResource ResourceClassifier::describe(const QString &schoolId, const QString &label,
                                            const QUrl &target, const QUrl &from) {
    const auto url = canonicalUrl(target);
    const auto encoded = url.toString(QUrl::FullyEncoded);
    SchoolResource resource;
    resource.id =
        QCryptographicHash::hash((schoolId + "|" + encoded).toUtf8(), QCryptographicHash::Sha256)
            .toHex()
            .toStdString();
    resource.schoolId = schoolId.toStdString();
    resource.title = labelInContext(label).toStdString();
    resource.url = encoded.toStdString();
    resource.provider = url.host().toStdString();
    resource.discoveredFrom = canonicalUrl(from).toString(QUrl::FullyEncoded).toStdString();
    if (isStudentServiceNavigation(label) || contains(label, "课程考核.*(?:细则|办法)|考试纪律"))
        resource.category = "student_services";
    else if (contains(label, "培养方案|教学大纲"))
        resource.category = "study_plan";
    else if (contains(label, "课程|教材|学习资料"))
        resource.category = "course_material";
    else if (contains(label, "实习.*指南|竞赛.*证书|考试.*查询|考务.*指南|成绩.*核对|教室.*申请|"
                             "正考.*指南|证书.*指南"))
        resource.category = "student_services";
    else if (contains(label, "竞赛|挑战杯|创新创业"))
        resource.category = "competition";
    else if (contains(label, "数据库|图书馆|电子资源|电子图书|电子期刊|读秀|知网|CNKI|"
                             "Science\\s*Direct|IEEE|Engineering\\s*Village|Web\\s*of\\s*Science|"
                             "借阅|馆藏|入馆|读者|VPN校外访问"))
        resource.category = "library";
    else if (contains(label, "学术|学科服务|科研|论文|检索证明|研究支持"))
        resource.category = "academic_support";
    else if (contains(label, "就业|招聘|求职"))
        resource.category = "career";
    else if (contains(label, "校园卡|校车|校历|住宿|心理"))
        resource.category = "campus_life";
    else if (contains(
                 label,
                 "办事|服务|门户|部门|概况|职责|学籍|学生证|成绩|选课|退课|补考|缺考|缓考|重修|重考|毕业证|资助|奖助|贷款|学费|缴费|教务"))
        resource.category = "student_services";
    if (contains(label, "本科|学士"))
        resource.audiences.push_back("undergraduate");
    if (contains(label, "研究生|硕士|博士"))
        resource.audiences.push_back("postgraduate");
    if (contains(label, "全体师生|全校师生|师生通用|通用资源"))
        resource.audiences.push_back("general");
    for (const auto &tag : {QString("重修"), QString("补考"), QString("缴费"), QString("资助"),
                            QString("奖学金"), QString("培养方案"), QString("数据库")})
        if (label.contains(tag))
            resource.tags.push_back(tag.toStdString());
    return resource;
}
bool ResourceClassifier::isLoginPage(const QByteArray &html, const QString &title,
                                     const QUrl &url) {
    const bool identity =
        contains(title, "统一身份|身份认证") && !contains(title, "通知|指南|说明");
    const bool form =
        contains(QString::fromUtf8(html),
                 "<input\\b[^>]*\\btype\\s*=\\s*(?:\"password\"|'password'|password(?:\\s|>))");
    return identity || (form && (contains(url.path(), "(?:^|/)login(?:[/.]|$)") ||
                                 contains(title, "用户登录|账号登录")));
}
QString ResourceClassifier::staticText(const QByteArray &html) {
    std::unique_ptr<lxb_html_document_t, DocumentDeleter> document(lxb_html_document_create());
    if (!document || lxb_html_document_parse(document.get(),
                                             reinterpret_cast<const lxb_char_t *>(html.constData()),
                                             size_t(html.size())) != LXB_STATUS_OK)
        return {};
    std::vector<lxb_dom_node_t *> nodes{lxb_dom_interface_node(document.get())};
    QStringList text;
    while (!nodes.empty()) {
        const auto node = nodes.back();
        nodes.pop_back();
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT &&
            (node->local_name == LXB_TAG_HEAD || node->local_name == LXB_TAG_SCRIPT ||
             node->local_name == LXB_TAG_STYLE || node->local_name == LXB_TAG_NOSCRIPT))
            continue;
        if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
            size_t length = 0;
            const auto value = lxb_dom_node_text_content(node, &length);
            if (value)
                text << QString::fromUtf8(reinterpret_cast<const char *>(value), qsizetype(length));
        }
        for (auto child = node->last_child; child; child = child->prev)
            nodes.push_back(child);
    }
    return text.join(" ").simplified();
}
QString ResourceClassifier::unverifiedReason(const QByteArray &html, const QString &text,
                                             bool hasUsefulLinks) {
    if (text.size() < 500 && contains(text, "没有访问.*权限|无权访问|访问权限不足|当前栏目.*权限|访问被拒绝"))
        return "官网返回访问权限提示，尚未验证公开资源";
    if (text.size() < 500 && contains(text, "仅.*(?:校内|校园网)|请.*(?:校园网|校内网络).*访问"))
        return "官网提示需要校园网，尚未验证公开资源";
    if (text.size() < 500 && contains(text, "访问地址无效|访问的地址.*无效|栏目不存在|页面不存在"))
        return "官网返回无效地址提示，尚未验证公开资源";
    if (contains(text, "暂无内容|暂无数据|栏目建设中|正在建设|内容为空|尚无内容"))
        return "栏目暂为空或建设中，尚未验证可用资源";
    if (!hasUsefulLinks && text.size() < 35 &&
        contains(QString::fromUtf8(html), "<iframe|<script|javascript|正在加载|loading"))
        return "页面只有浏览器或脚本入口，静态采集无法验证实际内容";
    if (text.size() < 15 && !hasUsefulLinks)
        return "静态内容过少，尚未验证可用资源";
    return {};
}
} // namespace campus
