#pragma once
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace campus {
inline constexpr std::array<std::string_view, 9> ResourceCategories{
    "study_plan",       "course_material", "library",     "competition", "academic_support",
    "student_services", "career",          "campus_life", "other"};
inline constexpr std::array<std::string_view, 3> ResourceAudiences{"undergraduate", "postgraduate",
                                                                   "general"};
inline constexpr std::array<std::string_view, 4> ResourceStatuses{"discovered", "verified",
                                                                  "login_required", "unreachable"};
inline constexpr std::array<std::string_view, 2> ResourceLinkKinds{"official",
                                                                   "official_recommended"};
// Practical school links are separate from announcements: no publication or deadline fields.
struct SchoolResource {
    std::string id, schoolId, title, url, description;
    std::string category = "other";
    std::string provider, discoveredFrom, lastCheckedAt;
    std::string status = "discovered";
    std::string linkKind = "official";
    std::string error;
    // Reviewed usage instructions and their official HTTPS source; discovery may leave both empty.
    std::string accessNote, accessEvidence;
    std::vector<std::string> audiences, tags;
    bool favorite = false;
    bool operator==(const SchoolResource &) const = default;
};
struct ResourceQuery {
    std::string category, stage, keyword;
    bool onlyFavorites = false;
};
class ResourceRepository {
  public:
    virtual ~ResourceRepository() = default;
    virtual std::vector<SchoolResource> list(const std::string &schoolId) const = 0;
    virtual void upsert(const std::string &schoolId,
                        const std::vector<SchoolResource> &resources) = 0;
    virtual void setFavorite(const std::string &schoolId, const std::string &id, bool favorite) = 0;
};
} // namespace campus
