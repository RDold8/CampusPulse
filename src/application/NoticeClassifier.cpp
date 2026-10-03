#include "application/NoticeClassifier.h"
#include <initializer_list>

namespace campus {
namespace {
bool any(const std::string &text, std::initializer_list<const char *> words) {
    for (const auto *word : words)
        if (text.find(word) != std::string::npos)
            return true;
    return false;
}
} // namespace
NoticeClassification NoticeClassifier::classify(const std::string &title) {
    NoticeClassification result;
    const auto add = [&](const char *key, bool found) {
        if (found) {
            result.tags.emplace_back(key);
            if (result.primaryCategory.empty())
                result.primaryCategory = key;
        }
    };
    const bool retake = title.find("重修") != std::string::npos;
    const bool retakePayment = retake && any(title, {"缴费", "交费", "付款"});
    // A retake payment is more specific than the general exam category. Keep both tags.
    add("competition", any(title, {"竞赛", "大赛", "选拔赛", "挑战杯"}));
    add("scholarship", any(title, {"奖学金", "助学金", "资助", "奖助", "助学贷款", "勤工助学",
                                    "学费减免", "学费全额减免", "学费补偿", "困难补助", "求职创业补贴"}));
    add("career", any(title, {"招聘", "就业", "宣讲会", "双选"}));
    add("retake_payment", retakePayment);
    add("exam", any(title, {"考试", "补考", "重考", "四六级", "四、六级", "重修"}));
    add("campus_activity", any(title, {"活动", "讲座", "展讯", "大讲堂", "文艺", "社团", "学生会",
                                       "开班", "开学典礼", "科技节", "文化节", "运动会", "志愿服务",
                                       "社会实践", "学术报告", "学术论坛", "音乐会"}));
    if (result.primaryCategory.empty()) {
        result.primaryCategory = "academic_affairs";
        result.tags.emplace_back("academic_affairs");
    }
    if (retake && !retakePayment)
        result.tags.emplace_back("retake_payment");
    if (any(title, {"缴费", "交费", "付款"}))
        result.tags.emplace_back("payment");

    const bool publicity = any(title, {"公示"});
    const bool outcome = any(title, {"结果", "获奖", "录取", "入选"});
    if (outcome)
        result.stages.emplace_back("result");
    if (publicity)
        result.stages.emplace_back("publicity");
    // Outcome/publicity headlines may mention an earlier application; do not imply it is open.
    if (!outcome && !publicity) {
        if (any(title, {"申请", "申报"}))
            result.stages.emplace_back("application");
        if (any(title, {"报名", "注册"}))
            result.stages.emplace_back("registration");
        if (any(title, {"缴费", "交费", "付款"}))
            result.stages.emplace_back("payment");
        if (any(title, {"安排", "日程", "时间表"}))
            result.stages.emplace_back("arrangement");
    }
    if (result.stages.empty())
        result.stages.emplace_back("unknown");
    return result;
}
} // namespace campus
