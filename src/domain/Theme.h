#pragma once
#include <array>
#include <string_view>

namespace campus {
struct NamedKey {
    std::string_view key;
    std::string_view label;
};
inline constexpr std::array<NamedKey, 8> Themes{{{"academic_affairs", "教务通知"},
                                                 {"exam", "考试"},
                                                 {"retake_payment", "重修 / 缴费"},
                                                 {"payment", "缴费"},
                                                 {"competition", "竞赛"},
                                                 {"scholarship", "奖助学金"},
                                                 {"campus_activity", "校园活动"},
                                                 {"career", "就业招聘"}}};
inline constexpr std::array<NamedKey, 7> Stages{{{"application", "申请"},
                                                 {"registration", "报名"},
                                                 {"payment", "缴费"},
                                                 {"arrangement", "安排"},
                                                 {"result", "结果"},
                                                 {"publicity", "公示"},
                                                 {"unknown", "待核实"}}};
inline std::string_view themeLabel(std::string_view key) {
    for (const auto &theme : Themes)
        if (theme.key == key)
            return theme.label;
    return key;
}
inline std::string_view stageLabel(std::string_view key) {
    for (const auto &stage : Stages)
        if (stage.key == key)
            return stage.label;
    return key;
}
inline bool knownTheme(std::string_view key) {
    for (const auto &theme : Themes)
        if (theme.key == key)
            return true;
    return false;
}
inline bool knownStage(std::string_view key) {
    for (const auto &stage : Stages)
        if (stage.key == key)
            return true;
    return false;
}
} // namespace campus
