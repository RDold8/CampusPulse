#include "application/ResourceService.h"
#include "application/TaskService.h"
#include <algorithm>
#include <stdexcept>

namespace campus {
namespace {
std::string trim(const std::string &value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos
               ? std::string{}
               : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::string folded(std::string value) {
    for (auto &c : value)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    return value;
}
template <std::size_t N>
bool known(const std::array<std::string_view, N> &values, const std::string &value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}
void plain(const std::string &value, std::size_t limit) {
    if (value.size() > limit || value.find('\0') != std::string::npos)
        throw std::invalid_argument("学校资源字段过长或包含空字符");
}
bool webUrl(const std::string &value) {
    const auto prefix = value.starts_with("https://") ? 8 : value.starts_with("http://") ? 7 : 0;
    if (!prefix || value.find_first_of(" \t\r\n") != std::string::npos)
        return false;
    const auto end = value.find_first_of("/?#", prefix);
    const auto authority = value.substr(prefix, end == std::string::npos ? end : end - prefix);
    return !authority.empty() && authority.find('@') == std::string::npos;
}
bool hexadecimal(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}
bool ipv4(std::string_view host) {
    int groups = 0;
    while (!host.empty()) {
        const auto end = host.find('.');
        const auto part = host.substr(0, end);
        if (part.empty() || part.size() > 3)
            return false;
        int number = 0;
        for (const auto c : part) {
            if (c < '0' || c > '9')
                return false;
            number = number * 10 + c - '0';
        }
        if (number > 255)
            return false;
        ++groups;
        if (end == std::string_view::npos)
            return groups == 4;
        host.remove_prefix(end + 1);
    }
    return false;
}
int ipv6Groups(std::string_view host, bool allowIpv4) {
    int groups = 0;
    while (!host.empty()) {
        const auto end = host.find(':');
        const auto part = host.substr(0, end);
        if (part.find('.') != std::string_view::npos)
            return allowIpv4 && end == std::string_view::npos && ipv4(part) ? groups + 2 : -1;
        if (part.empty() || part.size() > 4 || !std::all_of(part.begin(), part.end(), hexadecimal))
            return -1;
        ++groups;
        if (end == std::string_view::npos)
            return groups;
        host.remove_prefix(end + 1);
        if (host.empty())
            return -1;
    }
    return groups;
}
bool httpsUrl(const std::string &value) {
    if (value.size() < 8 || folded(value.substr(0, 8)) != "https://")
        return false;
    constexpr std::string_view forbidden = "<>\"{}|\\^`";
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        if (c <= 0x20 || c == 0x7f || forbidden.find(c) != std::string_view::npos)
            return false;
        if (c == '%' && (i + 2 >= value.size() || !hexadecimal(value[i + 1]) ||
                         !hexadecimal(value[i + 2])))
            return false;
    }
    const auto rest = std::string_view(value).substr(8);
    const auto authority = rest.substr(0, rest.find_first_of("/?#"));
    if (authority.empty() || authority.find('@') != std::string_view::npos)
        return false;
    std::string_view host, port;
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos)
            return false;
        host = authority.substr(1, close - 1);
        const auto compressed = host.find("::");
        if (compressed == std::string_view::npos) {
            if (ipv6Groups(host, true) != 8)
                return false;
        } else {
            const auto left = ipv6Groups(host.substr(0, compressed), false);
            const auto right = ipv6Groups(host.substr(compressed + 2), true);
            if (left < 0 || right < 0 || left + right >= 8)
                return false;
        }
        port = authority.substr(close + 1);
    } else {
        const auto colon = authority.find(':');
        host = authority.substr(0, colon);
        port = colon == std::string_view::npos ? std::string_view{} : authority.substr(colon);
        if (host.ends_with('.'))
            host.remove_suffix(1);
        if (host.empty() || host.size() > 253)
            return false;
        while (!host.empty()) {
            const auto dot = host.find('.');
            const auto label = host.substr(0, dot);
            if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-')
                return false;
            for (const auto c : label)
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-'))
                    return false;
            if (dot == std::string_view::npos)
                break;
            host.remove_prefix(dot + 1);
            if (host.empty())
                return false;
        }
    }
    if (port.empty())
        return true;
    if (port.front() != ':' || port.size() < 2 || port.size() > 6)
        return false;
    int number = 0;
    for (const auto c : port.substr(1)) {
        if (c < '0' || c > '9')
            return false;
        number = number * 10 + c - '0';
    }
    return number <= 65535;
}
bool checkedAt(const std::string &value) {
    if (value.empty() || TaskService::validUtc(value))
        return true;
    // Network adapters may retain fractional seconds; they remain UTC checks, not deadlines.
    const auto dot = value.find('.');
    if (dot != 19 || value.back() != 'Z' || value.size() < 22 || value.size() > 30)
        return false;
    for (std::size_t i = 20; i + 1 < value.size(); ++i)
        if (value[i] < '0' || value[i] > '9')
            return false;
    return TaskService::validUtc(value.substr(0, 19) + "Z");
}
void validate(SchoolResource &resource) {
    resource.id = trim(resource.id);
    resource.title = trim(resource.title);
    resource.url = trim(resource.url);
    resource.description = trim(resource.description);
    resource.provider = trim(resource.provider);
    resource.discoveredFrom = trim(resource.discoveredFrom);
    resource.accessNote = trim(resource.accessNote);
    resource.accessEvidence = trim(resource.accessEvidence);
    if (resource.id.empty() || resource.schoolId.empty() || resource.title.empty() ||
        !webUrl(resource.url))
        throw std::invalid_argument("学校资源缺少标识、学校、标题或有效官网链接");
    for (const auto *value : {&resource.id, &resource.schoolId, &resource.category,
                              &resource.status, &resource.linkKind})
        plain(*value, 200);
    plain(resource.title, 1200);
    plain(resource.url, 8192);
    plain(resource.description, 24000);
    plain(resource.provider, 1200);
    plain(resource.discoveredFrom, 8192);
    plain(resource.error, 12000);
    plain(resource.accessNote, 12000);
    plain(resource.accessEvidence, 8192);
    if (resource.accessNote.empty() != resource.accessEvidence.empty() ||
        (!resource.accessEvidence.empty() && !httpsUrl(resource.accessEvidence)))
        throw std::invalid_argument("学校资源使用说明需要成对保留有效的官方 HTTPS 出处");
    if (!known(ResourceCategories, resource.category) ||
        !known(ResourceStatuses, resource.status) || !known(ResourceLinkKinds, resource.linkKind))
        throw std::invalid_argument("学校资源分类、访问状态或链接来源类型无效");
    if ((!resource.discoveredFrom.empty() && !webUrl(resource.discoveredFrom)) ||
        (resource.linkKind == "official_recommended" && resource.discoveredFrom.empty()))
        throw std::invalid_argument("学校推荐的外部资源需要保留官方发现来源");
    if (!checkedAt(resource.lastCheckedAt))
        throw std::invalid_argument("学校资源最近检查时刻无效");
    if (resource.audiences.size() > ResourceAudiences.size() || resource.tags.size() > 64)
        throw std::invalid_argument("学校资源受众或标签数量过多");
    std::vector<std::string> audiences;
    for (const auto &audience : resource.audiences) {
        if (!known(ResourceAudiences, audience))
            throw std::invalid_argument("资源适用阶段必须明确；未知受众应留空");
        if (std::find(audiences.begin(), audiences.end(), audience) == audiences.end())
            audiences.push_back(audience);
    }
    resource.audiences = std::move(audiences);
    std::vector<std::string> tags;
    for (auto tag : resource.tags) {
        tag = trim(tag);
        plain(tag, 600);
        if (!tag.empty() && std::find(tags.begin(), tags.end(), tag) == tags.end())
            tags.push_back(std::move(tag));
    }
    resource.tags = std::move(tags);
    resource.favorite = false; // Collection never sets a personal preference.
}
} // namespace
ResourceService::ResourceService(ResourceRepository &repository, std::string schoolId)
    : repository_(repository), schoolId_(std::move(schoolId)) {
    if (schoolId_.empty())
        throw std::invalid_argument("学校资源缺少学校范围");
}
std::vector<SchoolResource> ResourceService::list(const ResourceQuery &query) const {
    if ((!query.category.empty() && !known(ResourceCategories, query.category)) ||
        (!query.stage.empty() && query.stage != "unknown" &&
         !known(ResourceAudiences, query.stage)))
        throw std::invalid_argument("学校资源筛选类别或适用阶段无效");
    const auto keyword = folded(trim(query.keyword));
    auto resources = repository_.list(schoolId_);
    std::erase_if(resources, [&](const auto &resource) {
        if (resource.schoolId != schoolId_ ||
            (!query.category.empty() && resource.category != query.category) ||
            (query.onlyFavorites && !resource.favorite))
            return true;
        if (query.stage == "unknown" && !resource.audiences.empty())
            return true;
        if (!query.stage.empty() && query.stage != "unknown" &&
            std::none_of(resource.audiences.begin(), resource.audiences.end(),
                         [&](const auto &audience) {
                             return audience == query.stage || audience == "general";
                         }))
            return true;
        std::string search = resource.title + " " + resource.description + " " + resource.provider +
                             " " + resource.accessNote;
        for (const auto &tag : resource.tags)
            search += " " + tag;
        return !keyword.empty() && folded(std::move(search)).find(keyword) == std::string::npos;
    });
    return resources;
}
void ResourceService::ingest(std::vector<SchoolResource> resources) {
    for (auto &resource : resources) {
        if (resource.schoolId.empty())
            resource.schoolId = schoolId_;
        if (resource.schoolId != schoolId_)
            throw std::invalid_argument("学校资源不属于当前学校，未保存本批次");
        validate(resource);
    }
    repository_.upsert(schoolId_, resources);
}
void ResourceService::setFavorite(const std::string &id, bool favorite) {
    if (id.empty())
        throw std::invalid_argument("请选择要收藏的学校资源");
    repository_.setFavorite(schoolId_, id, favorite);
}
} // namespace campus
