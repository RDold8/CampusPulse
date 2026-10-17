# CampusPulse 0.2.0 · 清爽首页与独立提醒

2026-10-14完成本机构建，2026-10-17公开发布。Windows 10 1809+ / Windows 11 x64 桌面预发布版，提供安装程序和便携 ZIP。沿用 C++20、Qt 6.8.3 和 SQLite，无需安装 Python 或 Qt SDK。移动 App 与同步仍在后续计划中。

## 这次可以直接使用什么

- **四个常用入口：概览、通知、学校资源、提醒。** 首页提供看通知、找资源、新建提醒。大学、来源、订阅、AI 补充和日历收进“更多”，原有功能仍可访问。
- **不关联通知也能新建提醒。** 点击“新建提醒”，填写“提醒什么”和“什么时候”，保存即可。默认到点提醒；提前量、日期精度、备注等放进更多设置。通知建立的事项继续保留原文链接与变更复核。
- **独立提醒也进入日历和 ICS。** 导出稳定 UID 和已确认的时间，不编造学校原文地址。完成、取消和修改时间沿用原有规则。
- **通知阅读更顺畅。** 窄窗口纵向布局、正文滚动、关键词快捷键、无结果恢复操作，以及来源更新进度与原因反馈。
- **通用正文解析改进。** 按 DOM 顺序保留段落、表格、列表和直接文本，过滤脚本与导航噪声；正文中提到的活动日期不再误替代发布日期。修复共享适配器，不用新增某所学校的专用配置掩盖问题。
- **数据库升级到版本 8。** 新建提醒可以不关联通知；升级保留原有事项、日历 UID 和提醒投递记录，并检查外键完整性。首次打开旧库会迁移；迁移后的库不能交给旧版应用使用。

## 下载与运行

[安装包](https://github.com/RDold8/CampusPulse/releases/download/v0.2.0/CampusPulse-0.2.0-windows-x64-setup.exe) · [便携 ZIP](https://github.com/RDold8/CampusPulse/releases/download/v0.2.0/CampusPulse-0.2.0-windows-x64-portable.zip) · [SHA-256](https://github.com/RDold8/CampusPulse/releases/download/v0.2.0/SHA256SUMS.txt)

升级前在托盘退出 CampusPulse，安装后重新打开。便携版须解压完整目录；默认数据仍保存在当前用户的 LocalAppData，并非 ZIP 内。安装器按当前用户安装，不修改开机启动，不自动启动程序；卸载保留个人数据。

第一次使用可直接看通知和学校资源，无需配置 AI。随附社区学校仍只有东北电力大学；其他 `.edu.cn` 学校可输入官网尝试发现。AI 可选，使用时才调用接口并产生费用。

## 验证与当前限制

本轮 Release 编译与 26 组 CTest 通过，覆盖独立提醒的实际编辑保存、无通知数据库、到点投递一次、取消不投递、ICS 无伪造 URL、冻结版本 7 数据迁移、导航与焦点、窄窗口及共享 HTML 回归。详细分层记录见[发布验收](release-0.2.0-validation.md)。

本地提醒需要程序运行。托盘可用时关闭主窗口继续后台提醒；彻底退出、睡眠、关机期间不检查，重启只补发最近 5 分钟内有效且未投递的提醒。周期重复提醒尚未实现。系统横幅和实际听感依赖目标电脑；手机 ICS 是手动导入，不持续同步。

本轮未新增付费 AI 或多校实网全覆盖测试，不把历史样例当作当前全校覆盖证明。登录、验证码、动态页面和网页改版仍可能造成部分采集失败。安装包未做代码签名；发布哈希用于核对完整性。

## English

CampusPulse 0.2.0 is a deployable Windows x64 desktop release with a cleaner overview and four primary destinations: Overview, Notices, University Resources, and Reminders. Secondary tools remain available under More.

You can now create a reminder without an official notice. Enter a title and date/time; optional timing and notes stay under advanced settings. Manual reminders participate in scheduling, the built-in calendar, and ICS export without inventing an official URL. Notice-based tasks retain their original links and review flags. Database version 8 preserves existing tasks, calendar UIDs, and delivery records during migration.

The shared HTML parser preserves mixed paragraphs, tables, lists, and direct text in document order and avoids confusing article-body dates with publication dates. Release compilation and all 26 CTest suites passed. See the validation report for the distinction between automated, packaged-runtime, and unverified checks.

Install per user or extract the full portable ZIP. No Python or Qt SDK is needed. Reminders require the app to remain running; explicit exit, shutdown, and sleep stop checks. Mobile synchronization, recurring reminders, exhaustive university coverage, and guaranteed system-banner delivery are not included. AI is optional; this release made no new paid model calls. This is still an early version with limitations.
