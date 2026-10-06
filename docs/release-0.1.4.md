# CampusPulse 0.1.4 · Windows 桌面预览版

2026-10-06，发布标签`v0.1.4`。我把近期本机0.1.3迭代中完成的待办、提醒声音、AI接入与来源补充整理为这次可下载版本；0.1.3没有公开Release，上一公开版本为0.1.2-r1。

## 这次改了什么

- **闹钟待办卡片。** 从通知加入待办，填写事项与一个日期时刻即可保存，默认到时间提醒。卡片显示实际提醒时刻，可修改时间或标记完成；原文依据、自定义时间与其他操作收进“更多设置”。个人计划与学校原文时间保持区分，旧任务不重设。
- **更明显的到点提醒。** 独立置顶窗口保留多条提醒，直到手动关闭；“查看待办”回到事项。每1秒检查，启动或短延迟时只补发最近5分钟内有效且未投递的提醒。Windows托盘可用时关闭主窗口继续运行，托盘菜单可彻底退出。
- **提示音。** 轻柔铃声、双响提示、闹钟提示与静音；音量可调，支持试听和停止。默认轻柔铃声、音量60、重复提示；每12秒重复一次，最多1分钟。停止声音不完成待办，关闭最后一条或按Esc关闭窗口会停音。
- **更简单的AI设置。** 基础配置填写接口地址和Key，自动读取模型并检查连接；手动模型保留为可选项。本地配置、加密凭据和启用状态使用SQLite事务保存，兼容旧配置；连接后保存失败可仅重试本地保存。
- **补充待接入来源并显示反馈。** 来源页可选择未接入入口进行定向AI补充；候选、官网校验结果、失败原因和进度显示在下方，并按学校持久保存。八类搜索范围使用各自提示词，验证通过才接入，不以模型候选替代官网核验。
- **学校接入与目录。** 接入时显示加载动画与阶段文字。随附社区配置仅保留东北电力大学；其他大学仍可输入官网尝试通用自动发现，本机学校草案保留。

## 下载与使用

- [Windows x64安装包](https://github.com/RDold8/CampusPulse/releases/download/v0.1.4/CampusPulse-0.1.4-windows-x64-setup.exe)
- [Windows x64便携包](https://github.com/RDold8/CampusPulse/releases/download/v0.1.4/CampusPulse-0.1.4-windows-x64-portable.zip)
- [文件哈希](https://github.com/RDold8/CampusPulse/releases/download/v0.1.4/SHA256SUMS.txt)

支持Windows 10 1809+与Windows 11 x64，运行不需要Python或Qt SDK。安装前从托盘退出正在运行的CampusPulse，再安装更新；个人通知、订阅、待办、收藏与凭据保存在安装目录外。需要浏览器验证的公开站点使用单独安装的WebView2 Runtime。

打开“我的待办 → 提醒声音 → 试听”检查本机效果，“测试提醒”不创建待办。声音通过Windows默认输出设备播放，软件音量不改系统主音量；程序彻底退出、关机和睡眠期间不检查或播放。

## 验证与范围

本版重新构建和回归的实际结果记录在[0.1.4发布验收](release-0.1.4-validation.md)。此前本机声音迭代的26组CTest、Windows音频探针和已安装界面检查见[声音验收](reminder-sounds.md)；文件哈希与公开附件应以本版下载为准。

此前北京化工大学真实DeepSeek搜索返回4个栏目候选，其中1个学生通知来源通过列表和正文验证，见[AI验收](ai-pending-validation.md)。本次发布没有新增AI调用，不把旧实网结果当作本版全校覆盖证明。常规爬虫不调用模型，用户主动发起AI补充会消耗API token。

来源覆盖仍有限，登录、人工验证码、动态页面和官网改版可能阻断采集。手机ICS导入、Windows系统横幅实际送达和扬声器听感依赖目标环境；移动App与双端同步尚未实现。独立弹窗、原生音频调用和用户实际看到或听到分别记录。程序和安装器版本均为0.1.4，沿用原升级AppId，保留旧版本标签。

原创代码和配置采用MIT；附件包含固定依赖源码，包内保留第三方许可。初版，有许多不足之处，请见谅。

## English

CampusPulse 0.1.4 publishes the recent local 0.1.3 improvements as a downloadable Windows preview. Version 0.1.3 had no public Release; the previous public build was 0.1.2-r1.

- Alarm-style task cards use a title and a date/time, with an at-time reminder by default. Cards show the actual trigger time and offer quick editing and completion. Original-notice evidence and custom settings remain optional.
- Persistent reminder popups retain multiple tasks until dismissed. The scheduler checks every second and catches up valid, undelivered reminders from the previous five minutes. Where Windows tray support is available, closing the main window keeps the application running; the tray menu provides an explicit exit.
- Three built-in tones, mute, adjustable volume, preview, and stop controls. The default is a gentle tone at volume60 with repetition every12seconds, for at most one minute. Stopping sound does not complete a task; dismissing the last reminder or using Esc stops playback.
- AI setup needs an endpoint and a key, discovers models, and checks a short response. SQLite transactions commit provider settings, encrypted credentials, and activation together, with legacy migration and local save retries.
- Targeted AI repair of pending sources shows candidate links, validation states, failures, and progress. Reports persist per university; eight scopes use separate prompts. Only independently validated official sections are added.
- Animated university-onboarding progress and one bundled community package, Northeast Electric Power University. Homepage-based discovery and locally discovered universities remain available.

[Installer](https://github.com/RDold8/CampusPulse/releases/download/v0.1.4/CampusPulse-0.1.4-windows-x64-setup.exe) · [Portable ZIP](https://github.com/RDold8/CampusPulse/releases/download/v0.1.4/CampusPulse-0.1.4-windows-x64-portable.zip) · [SHA-256 hashes](https://github.com/RDold8/CampusPulse/releases/download/v0.1.4/SHA256SUMS.txt).

Windows10 1809+ / Windows11 x64; no Python or Qt SDK at runtime. Exit the running application through its tray menu before upgrading. Personal data and credentials remain outside the installation directory. Public browser verification needs a separately installed WebView2 Runtime.

Open the task page's sound settings to preview your output device. Local reminders require the application to run; no checks run after explicit exit, shutdown, or during sleep. Sound uses the Windows default output device without changing the system master volume.

Current build and release checks are recorded in [0.1.4 validation](release-0.1.4-validation.md). Earlier live BUCT AI checks returned four candidates and added one validated source; publishing this version made no new model calls. Regular crawling does not use a model; user-initiated AI requests consume tokens. Coverage is partial, and authentication, CAPTCHAs, dynamic pages, and site changes can prevent collection. Phone ICS import, system-banner delivery, and physical audibility depend on the target environment. Mobile and cross-device sync remain unimplemented.

Original code and configurations use MIT. Dependency source archives and third-party notices are included. This is an initial version with many shortcomings. Thank you for your understanding.
