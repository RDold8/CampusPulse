# CampusPulse 0.1.3 本地桌面预览版

这次更新重做 AI 接入：填写接口地址与 API Key 后，程序自动处理官方或常见兼容地址、读取模型列表、选择模型并验证连接。名称和手动模型保持为可选高级设置。AI 搜索页保留综合、重修补考与缴费、奖助学金申请、竞赛、活动、学习资源、教务和就业八种范围；默认页面不展示路由或密钥。

官方 DeepSeek 按原生搜索响应处理；其他兼容接口先由软件收集真实官网链接，再让模型从这些证据中筛选。候选仍由学校域名、列表与正文检查决定能否接入。接入学校时显示加载动画与阶段文字，普通采集不调用模型。

修复 `providers.json` 本地保存失败。新 AI 存储使用 SQLite 事务，将配置、DPAPI 加密凭据和启用状态一起提交；兼容读取旧 JSON，第一次成功保存时迁移。连接前检查本地能否保存，保存失败保留填写内容；连接已成功时可仅重试保存，避免再次调用模型。详见 [存储修复验收](ai-storage-fix-validation.md)与 [AI 配置说明](ai-provider-management.md)。

版本号统一为 0.1.3，包括程序信息、Windows 可执行文件属性和安装包。安装仍为当前 Windows 用户，沿用原 AppId，运行数据位于安装目录外。升级不修改校园通知、订阅、待办和收藏。

2026-10-06 增补：来源页可以选择待接入入口并定向进行 AI 补充；搜索页默认优先待接入目标。候选、目标、官网校验结果、失败原因和进度显示在下方，并按学校保存在 SQLite 中，页面重建后仍可查看。只有实际通过列表和正文校验的来源才合并；相同入口保留原来源 ID。加强 WebPlus 移动文章与 PDF 查看器过滤。

本版先交付本地安装包和便携包，公开 GitHub Release 尚未更新。北京化工大学已完成一次真实 DeepSeek 原生搜索与生产界面流程验收：返回4个栏目候选，新增1个通过列表和正文校验的“学生通知”来源；其他入口显示未通过状态，不代表学校全量覆盖。详见 [AI 待接入补充验收](ai-pending-validation.md)。已发布的 0.1.2-r1 与本版本是不同构建。

验收：最终 CTest 23/23 组通过；在原失败目录执行的生产存储探针通过，保存、重新加载、DPAPI 密钥恢复与无明文检查均成功，未调用真实 API。

## English

CampusPulse 0.1.3 simplifies AI setup to an endpoint address and an API key, with automatic model discovery and a short connection check. Optional advanced settings retain names and manual model IDs. Search supports eight scopes, while endpoint and key fields stay out of the default search page.

Official DeepSeek search requires typed native search results. Compatible providers select links from real university pages fetched by CampusPulse; candidates still pass independent domain, list and article checks. University onboarding shows an animated progress indicator. Regular crawling does not call a model.

AI configuration, DPAPI-encrypted credentials and activation now commit together in a SQLite transaction, fixing local JSON replacement failures. Existing JSON profiles remain readable and migrate on the first successful save. Storage is checked before API requests; after a successful connection, a failed save can be retried locally without another model call.

The October 6 update adds targeted repair of pending public sources, visible candidate links, verification states, failure reasons and progress. Per-university SQLite reports survive page recreation. A live DeepSeek search for Beijing University of Chemical Technology returned four section candidates; one student-notice source passed list and article verification. Other entries retain their unresolved states. This is a local preview delivery; GitHub Release has not been updated, and the live sample does not establish complete university coverage.
