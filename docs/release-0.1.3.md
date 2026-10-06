# CampusPulse 0.1.3 本地桌面预览版

2026-10-06 声音增补：待办页和托盘新增“提醒声音”，内置轻柔铃声、双响提示、闹钟提示与静音，可调音量并试听。到点立即播放，重复提示每12秒一次、最多1分钟；弹窗可停止声音，关闭最后一条提醒时停止。26/26组CTest通过，Windows原生音频探针验证三种声音完成和中途停止；扬声器实际可听效果由本机“试听”确认。详见[声音说明与验收](reminder-sounds.md)。

2026-10-06 待办改进：默认闹钟卡片，填写事项并选择一个日期时刻即可保存，默认到时间提醒。原文确认、时间精度及自定义收进更多设置，旧任务保留原值。到点独立提醒窗口保留多条事项，等待手动关闭；页顶可以测试提醒。Windows托盘可用时关闭主窗口继续后台运行，托盘菜单可彻底退出。调度改为1秒检查，启动及短延迟只补发最近5分钟内尚未投递的有效提醒，修复当前分钟保存及稍晚启动直接跳过的情况。详见[闹钟待办验收](alarm-reminders-validation.md)。

这次更新重做 AI 接入：填写接口地址与 API Key 后，程序自动处理官方或常见兼容地址、读取模型列表、选择模型并验证连接。名称和手动模型保持为可选高级设置。AI 搜索页保留综合、重修补考与缴费、奖助学金申请、竞赛、活动、学习资源、教务和就业八种范围；默认页面不展示路由或密钥。

官方 DeepSeek 按原生搜索响应处理；其他兼容接口先由软件收集真实官网链接，再让模型从这些证据中筛选。候选仍由学校域名、列表与正文检查决定能否接入。接入学校时显示加载动画与阶段文字，普通采集不调用模型。

修复 `providers.json` 本地保存失败。新 AI 存储使用 SQLite 事务，将配置、DPAPI 加密凭据和启用状态一起提交；兼容读取旧 JSON，第一次成功保存时迁移。连接前检查本地能否保存，保存失败保留填写内容；连接已成功时可仅重试保存，避免再次调用模型。详见 [存储修复验收](ai-storage-fix-validation.md)与 [AI 配置说明](ai-provider-management.md)。

版本号统一为 0.1.3，包括程序信息、Windows 可执行文件属性和安装包。安装仍为当前 Windows 用户，沿用原 AppId，运行数据位于安装目录外。升级不修改校园通知、订阅、待办和收藏。

2026-10-06 增补：来源页可以选择待接入入口并定向进行 AI 补充；搜索页默认优先待接入目标。候选、目标、官网校验结果、失败原因和进度显示在下方，并按学校保存在 SQLite 中，页面重建后仍可查看。只有实际通过列表和正文校验的来源才合并；相同入口保留原来源 ID。加强 WebPlus 移动文章与 PDF 查看器过滤。

随附社区学校配置现只保留东北电力大学。吉林大学、北华大学和长春理工大学的种子仅用于源码回归测试，不进入安装包；升级时移除安装目录里的这三份旧社区配置。输入其他大学官网自动接入和本机已发现的学校继续保留。

本版先交付本地安装包和便携包，公开 GitHub Release 尚未更新。北京化工大学已完成一次真实 DeepSeek 原生搜索与生产界面流程验收：返回4个栏目候选，新增1个通过列表和正文校验的“学生通知”来源；其他入口显示未通过状态，不代表学校全量覆盖。详见 [AI 待接入补充验收](ai-pending-validation.md)。已发布的 0.1.2-r1 与本版本是不同构建。

AI接入与存储原验收：CTest 23/23组通过；在原失败目录执行的生产存储探针通过，保存、重新加载、DPAPI密钥恢复与无明文检查均成功，未调用真实API。新增闹钟卡片和可见提醒验收另见上述记录。

## English

The October 6 sound update adds three built-in tones, mute, adjustable volume, and a preview in the task page and tray menu. A due reminder plays immediately; optional repetition runs every 12 seconds for at most one minute. The popup can stop the current sound without completing a task. All 26 CTest groups pass, and a Windows audio probe confirms native playback completion and interruption. Physical speaker audibility remains a local preview check. See [sound behavior and validation](reminder-sounds.md).

The October 6 task update adds alarm-style cards and a simple title, date/time and reminder selector. Saving confirms a personal plan; original-notice evidence and custom settings remain optional advanced controls. A persistent in-app popup retains multiple due tasks until dismissed. Where Windows tray support is available, closing the main window keeps reminders running; the tray menu provides an explicit exit. The scheduler checks every second and catches up only valid, undelivered reminders from the previous five minutes. Existing task values and completion states are preserved.

CampusPulse 0.1.3 simplifies AI setup to an endpoint address and an API key, with automatic model discovery and a short connection check. Optional advanced settings retain names and manual model IDs. Search supports eight scopes, while endpoint and key fields stay out of the default search page.

Official DeepSeek search requires typed native search results. Compatible providers select links from real university pages fetched by CampusPulse; candidates still pass independent domain, list and article checks. University onboarding shows an animated progress indicator. Regular crawling does not call a model.

AI configuration, DPAPI-encrypted credentials and activation now commit together in a SQLite transaction, fixing local JSON replacement failures. Existing JSON profiles remain readable and migrate on the first successful save. Storage is checked before API requests; after a successful connection, a failed save can be retried locally without another model call.

The October 6 update adds targeted repair of pending public sources, visible candidate links, verification states, failure reasons and progress. Per-university SQLite reports survive page recreation. A live DeepSeek search for Beijing University of Chemical Technology returned four section candidates; one student-notice source passed list and article verification. Other entries retain their unresolved states. This is a local preview delivery; GitHub Release has not been updated, and the live sample does not establish complete university coverage.
