# AI 接口：地址与 Key 即可连接

在 AI 补充页点击“配置接口”，填写接口地址和 API Key，再点击“保存并连接”。DeepSeek 官方地址填 `https://api.deepseek.com` 即可。程序读取模型目录，自动选择文本模型，用一次短请求检查连接；成功后保存并启用。未通过连接不会显示已连接。首次点击“一键搜索”缺少配置时进入同一表单，连接成功后继续这次搜索。

基础表单只显示地址、隐藏的密钥和可选的 Windows 加密记住开关。名称与手动模型在折叠的高级设置中；提供方列表、模型调整与自动补充也默认折叠。八种搜索范围保留，默认综合：重修补考与缴费、奖助学金申请、竞赛、校园活动、学习资源、教务、就业招聘。软件不会读取其他 AI 客户端的配置。

## 自动处理地址与模型

DeepSeek 官方的根地址、`/v1`、`/anthropic`、`/anthropic/v1` 和两种完整请求地址会统一为官方根地址。模型列表请求 `/models`；校园原生搜索内部调用 `/anthropic/v1/messages`。协议、认证头与请求路径由程序配套选择，避免向 Anthropic 地址拼接 Chat Completions。完整 `/chat/completions` 与 `/messages` 地址自动识别；自定义根地址使用常见的 `/v1` 兼容接口，明确提供的中转路径保留。高级配置的既有认证方式与备注保留，跨地址或协议的旧密钥不会被静默复用。

自动选择优先保留目录中存在的所选文本模型，其次选 `deepseek-flash` 等轻量模型，排除嵌入、重排、语音和图片模型。目录不提供模型时，已有准确模型名可用真实短请求验证；无法推断时明确失败，允许在高级设置中填写。未知路由不能保证仅凭地址就推断出供应商的特殊协议。

“保存并连接”先在本地进行写入事务检查，检查后回滚；本地不能保存就停止，不发送 API 请求。通过后包含一次模型目录 GET 和一次最多 128 输出 token 的短模型请求，后者可能计费。鉴权、限流、错误 JSON、空目录、无文本响应等分别显示失败，程序不自动重试。列表成功和模型响应成功不代表联网搜索已经完成。连接成功但最终本地保存失败时，填写内容与已选择的模型保留，点击“重试保存”仅重试本地写入；修改地址、Key 或模型后需要重新验证连接。

## 一键搜索实际做什么

DeepSeek 官方调用真实网页搜索，最多两次服务端搜索和八条本校候选。其他兼容服务先由软件读取学校官网与重点部门页面，再请模型从已发现链接中筛选：最多六次页面获取、80 条证据链接、20 KiB 提示词证据及八条候选。这个路径不要求模型自带联网能力。没有获得官网证据就停止，尚未发出付费模型请求；不采纳未出现在真实页面中的模型 URL。

每条入口还必须经独立爬虫校验学校域名、栏目列表和正文，通过后才合并到来源。页面读取、AI 筛选和校验过程分别显示进度。已有真实原生工具结果但搜索预算用尽或响应截断时，可以保留明确标注的部分结果；没有真实结果的工具错误仍失败。普通模型文本和连接成功不能充当网页搜索证据。

## 存储与隔离

提供方配置使用用户应用目录 `ai-providers/ai-providers.sqlite`。配置、加密凭据和启用状态在同一个 SQLite 事务内提交；失败时保持之前的状态。密钥默认仅保留当前进程，选择记住时才存入绑定当前 Windows 用户、提供方和请求配置的 DPAPI 密文。明文 Key 不写入数据库、学校包、通知库、导出或源码仓库。

旧 `providers.json` 和 `credentials.dpapi.json` 仍可读取；第一次成功保存时迁入 SQLite，保留旧文件。迁移后 SQLite 是唯一生效的存储，旧文件不再更新；旧程序无法读取新保存的配置。构造表单不迁移文件、不调用 API，仍按原凭据绑定读取 Key，再显示规范化地址。

API 请求仅发送学校名称、官方域和公开链接证据，不发送待办、订阅或学校账号。只支持公共 HTTPS API 地址；DNS、证书、大小、超时、重定向与同校域限制由程序执行。实际用量以接口返回为准，未知用量显示未返回。

## 参考依据

2026-10-05 核对 [DeepSeek 首次调用](https://api-docs.deepseek.com/)、[模型列表](https://api-docs.deepseek.com/api/list-models/)、[Anthropic 兼容接口](https://api-docs.deepseek.com/guides/anthropic_api/)与[官方搜索说明](https://api-docs.deepseek.com/quick_start/agent_integrations/claude_code/)。[Responses 文档](https://api-docs.deepseek.com/guides/responses_api/)说明内置 `web_search` 被忽略，所以本项目没有以 Responses 普通响应冒充搜索。

借鉴 [LobeHub 的 DeepSeek 路由与模型目录分工](https://github.com/lobehub/lobehub/blob/026e7afc519eb568850474a1060f33be7f83e5ba/packages/model-runtime/src/providers/deepseek/index.ts)以及 [Cherry Studio 的独立提供方配置](https://github.com/CherryHQ/cherry-studio/blob/50d69b685697a8c3a634bc8b4f19ce3978768423/packages/provider-registry/src/providers/deepseek.ts)。只参考机制，Qt 实现为本项目原创；上游声明的工具能力仍须与官方说明和实际响应核验。

代码入口：`AiProviderConfig` 规范地址并管理凭据；`AiProviderProbe` 自动连接与选择模型；`AiProviderDialog` 是简化表单；`AiSourcesPage` 管理一键流程和记录；`DeepSeekSearch` 区分官方原生搜索与真实官网证据筛选；`SchoolOnboarding` 验证来源。
