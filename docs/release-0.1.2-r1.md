# CampusPulse 0.1.2 更新预览版 / Updated preview

2026-10-05 · 发布标签 / Release tag: `v0.1.2-r1` · Windows x64

## 中文

这次更新让目录外高校的公开通知和实用资源更容易被发现，并改善教务处学生服务指南的读取。软件内版本号仍为 **0.1.2**；使用新的 `v0.1.2-r1` 标签保存本次源码与程序包，原 `v0.1.2` 及旧附件保留。

### 下载与安装

- [Windows 安装包](https://github.com/RDold8/CampusPulse/releases/download/v0.1.2-r1/CampusPulse-0.1.2-windows-x64-setup.exe)：双击安装，按当前用户安装，无需管理员权限。
- [Windows 便携包](https://github.com/RDold8/CampusPulse/releases/download/v0.1.2-r1/CampusPulse-0.1.2-windows-x64-portable.zip)：完整解压后运行 `CampusPulse.exe`。
- [SHA-256 校验](https://github.com/RDold8/CampusPulse/releases/download/v0.1.2-r1/SHA256SUMS.txt)与 [发布清单](https://github.com/RDold8/CampusPulse/releases/download/v0.1.2-r1/release-manifest.json)。

支持 Windows 10 1809+ / Windows 11 x64，Qt 与 VC 运行库随包提供。公开浏览器验证需本机单独安装 Microsoft Edge WebView2 Runtime。卸载保留个人数据；安装说明见 [Windows 下载与安装](windows-release.md)。

### 这次改进

- 通用解析支持更多 WebPlus、VSB 和 Drupal 列表、卡片及正文结构，改善拆分日期读取，减少菜单、页脚和宣传轮播对通知的干扰。
- 扩大官网部门、院系、资助、财经和迎新入口发现；按主机分配请求机会，避免大菜单挤掉其他部门，来源标识重复时合并。
- 自动接入已核实的正文与发布日期直接保存，后续无日期列表不会抹掉明确日期。学年、URL 日期和办理日期不猜成发布日期。
- 只输入学校官网完成接入后，通知更新会接续学校资源发现。
- 选课、考试、学籍等学生服务索引进入资源发现，不当作当期通知来源；优先检查已发现的重修、补考、缴费指南正文，过滤官网坏链接，保留登录和访问失败状态。

这些是共享采集器改进，没有为测试大学增加专用学校包或运行时域名特判；常规采集和资源发现的模型调用数为 **0**。

### 实际验证与限制

最终 CTest **22/22 组通过**。自动接入、HTML、资源发现日志分别为 **35、50、36 passed**，这些 QtTest 数量包含初始化和收尾。最终便携运行程序在 Windows-only PATH、无 QT 环境变量条件下输出 `CampusPulse 0.1.2`，退出码 0。

东北大学、大连理工大学、哈尔滨工业大学、山东大学、南京邮电大学仅输入官网首页进行了前后对比。五校实网数字冻结于较早的本地 v4 构建，最终代码通过共享 DOM 回归，没有将五校重新实网采集一遍。逐项收录、遗漏与请求边界见 [五校报告](five-school-universal-validation.md)。

北邮最终资源实网复测保存 **57 项入口，13 项完成公开页面核实**，其余为待核实、需要登录或访问失败。选课和退课、考试和成绩、重修选课、补考均核实了公开页面；生产资源页也在桌面2完成原生界面检查。北邮栏目报告的 238 行是列表候选，**不是 238 条去重通知**。详见 [北邮教务指南验收](bupt-generic-crawling-validation.md)。

公开重修与补考指南分别发布于 2021-04-01、2021-04-02，不能证明今年的报名、缴费或考试期限。账号门户内容没有采集；来源发现与资源发现仍有 48、32 次请求上限及未扫描入口。本次不代表全校全量覆盖。AI 完整补充链路、手机 ICS 导入、Windows 系统通知送达仍需各自环境验证。

初版，有许多不足之处，请见谅。

## English

This update improves public notice and resource discovery for universities outside the registry, including academic-affairs student-service guides. The application version remains **0.1.2**. The immutable release tag **`v0.1.2-r1`** records this update; the original `v0.1.2` and its assets remain available.

Use the Windows installer or fully extract the portable ZIP linked above. Windows 10 1809+ / Windows 11 x64 is supported, with Qt and VC runtime files included. Public browser verification requires a separately installed Microsoft Edge WebView2 Runtime. Installation is per user; uninstall preserves personal data.

### Changes

- Shared WebPlus, VSB, and Drupal parsing handles more list, card, article, and split-date structures, while reducing navigation/footer and carousel pollution.
- Department, faculty, student-aid, finance, and new-student navigation discovery receives broader coverage and fairer request scheduling. Duplicate source identities are merged.
- Verified article text and publication dates are retained during onboarding and subsequent list refreshes. Academic years, URL dates, and action dates are not guessed as publication dates.
- Notice refresh after homepage-only onboarding continues with university resource discovery.
- Student-service indexes are explored as resources. Retake, resit, and payment guide text receives priority; invalid links, login restrictions, and failures retain explicit handling.

No school-specific runtime domain branches or dedicated packages were added for these test universities. Standard crawling and resource discovery made **zero model calls**.

### Validation and scope

All **22 final CTest groups passed**. The onboarding, HTML, and resource logs report **35, 50, and 36 passed**, including QtTest initialization and cleanup. The final deployed executable returned `CampusPulse 0.1.2` and exit code 0 with Windows-only PATH and no QT environment variables.

Five universities were compared using homepage-only input: Northeastern University, Dalian University of Technology, Harbin Institute of Technology, Shandong University, and Nanjing University of Posts and Telecommunications. Their live numbers are frozen at an earlier local v4 build; shared DOM regressions passed on the final code, but all five were not crawled live again. See the [five-university comparison](five-school-universal-validation.md).

The final BUPT live resource check saved **57 entries, with 13 public pages verified**. Course selection, exams/results, retake, and resit guides were verified, and the production resource page was checked on Windows desktop2. The onboarding report's 238 rows are list candidates, **not a deduplicated notice total**. See [BUPT validation](bupt-generic-crawling-validation.md).

The retake/resit guides were published in 2021 and do not establish current deadlines. Authenticated portal content was not collected. Discovery remains bounded at 48 source requests and 32 resource requests, with pending entries; this is partial coverage. The complete AI supplementation pipeline, phone ICS import, and Windows notification delivery retain separate verification requirements.

This is an initial version with many shortcomings. Thank you for your understanding.
