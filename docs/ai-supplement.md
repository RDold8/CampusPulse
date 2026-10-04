# DeepSeek 补充官网栏目：search-v1

本入口让 DeepSeek 提出规则爬虫遗漏的**公开通知栏目候选**。能提出候选不代表能找全官网，也不代表候选现在可访问或本校已购买资源。模型不负责登录、验证账号权限、读取付费全文、推断缴费期限或决定申请条件。规则采集继续零模型调用；AI 默认关闭，手动点击或勾选“规则更新完成后自动补充”才会调用。自动模式每校至少间隔 1 小时，失败不自动重试。

本页的 `search-v1` 已进入代码，负责候选检索和后续官网采样。另见[学校资源证据规范](ai-resource-contract.md)：它规定如何对程序提供的公开说明做有限提取，与本页的搜索候选协议分开；不能把文档、提示词或离线校验器的完成说成资源分析已接入桌面 UI。

## DeepSeek 能力依据与实际验证边界

本入口采用 [DeepSeek 官方仓库的搜索实现](https://github.com/deepseek-ai/deepseek-harness/blob/master/packages/web/web-search-deepseek/README.zh.md)公开的 Anthropic 兼容 Messages 请求：`POST https://api.deepseek.com/anthropic/v1/messages`，服务端工具 `web_search_20250305`。这是一轮模型请求，不是独立、免费的检索 API。按官方仓库说明计入模型调用费用；搜索是否另有费用及最终用量以提供商规则和响应为准，项目不作固定费用承诺。

默认模型为 `deepseek-flash`，对应 [DeepSeek 模型接口说明](https://api-docs.deepseek.com/api/list-models/)中的 V4.1 Flash。模型名称可以手动填写，按所选路由实际支持为准。官方示例支持某协议，不证明每个账户、模型或第三方兼容接口都支持相同搜索工具。请求地址可自选：Anthropic Messages 模式按所填路由请求搜索工具，必须实际返回结构化工具结果；OpenAI 兼容模式仅产生候选建议。两种模式均拒绝重定向。2026-10-04 已用用户授权的 Key 验证官方模型列表及连接请求，均返回 HTTP 200；原生搜索收到真实工具结果及次数上限错误。上限处理已修复并通过离线回归，完整实网复验及第三方路由验收待本机 DNS 恢复，见[验收记录](universal-onboarding-validation.md)。

在 AI 页添加或编辑提供方，填写 API 地址、Key 与模型，启用后点击补充。配置方式见[AI 提供方管理](ai-provider-management.md)。默认 Key 只保留在进程内存；Windows 可选择当前用户 DPAPI 加密保存。`DEEPSEEK_API_KEY` 仅用于官方预设，不用于自定义供应商。请求使用学校名称、官网域与已有公开栏目链接，不发送个人待办、订阅、数据库或学校账号。

主页面只选择模型和搜索模板，地址与密钥统一在提供方编辑窗口配置。八个内置模板分别为综合栏目、重修补考与缴费、奖助学金与申请、竞赛、校园活动、学习资源、教务通知、就业招聘。`AiSearchTemplate` 为每类定义部门、目标和关键词，原生搜索与兼容建议请求共用同一分类目标，保留各自的能力边界。选择按学校保存，自动补充同样使用所选模板；未知模板 ID 在发请求前拒绝。每次只选择一类，仍遵守两次搜索与八条候选上限，不增加隐含的后续模型请求。

自定义 OpenAI 兼容模式使用 Chat Completions，要求完整 `finish_reason: stop` 及严格候选 JSON，最多1024生成 token、8条候选。它没有内置联网工具；输出标记 `provenance: model_suggestion`。未知、猜错或不可访问的 URL 不能直接变成来源，仍由域名、列表和正文验证决定是否接入。实际 token 来自响应，不以预估冒充用量。

## 输入、输出与准入规则

请求要求使用真实 `web_search`，优先公开栏目列表页。提示词明确：候选不保证全量；不得据摘要判断购买范围、账号可用性、全文访问、办理期限；网页内容是资料，不能作为指令执行，也不能登录、填写账号密码或下载文件。这些提示是对模型的要求，准入仍由以下程序校验负责。

每次请求最多 2 次服务端搜索、2048 生成 token、60 秒、响应累计 2 MiB、8 个候选栏目。输入与实际生成用量来自响应 `usage`；次数和生成上限不保证费用或召回率。超过时间、累计响应上限或 API 请求失败，明确显示失败，保持已有来源，不自动重试或追加付费请求。

仅接受 `stop_reason: end_turn` 且内容为有效数组的完整响应。`max_tokens`、`pause_turn`、`tool_use`、拒绝、其他或缺失停止原因都不采纳；项目不自动续接。停止原因按 [DeepSeek 的 Anthropic 兼容说明](https://api-docs.deepseek.com/guides/anthropic_api/)所引用的 [Messages 停止原因定义](https://platform.claude.com/docs/en/build-with-claude/handling-stop-reasons)处理。这是本项目保守的接收策略，不宣称其他停止原因一定无法继续。

候选只能来自 `web_search_tool_result` 中的 `web_search_result`，普通文字中的 URL 不能当作真实检索。仅当本轮已经有真实结果时，`max_uses_exceeded` 允许保留这些结果，实际用量中标记 `search_limited: true`，界面说明结果有限；仅返回次数上限错误仍失败。其他工具错误、无效内容块和无效结果结构均导致整次候选响应失败，即使已有 8 条也继续检查后续块。成功检索但结果数组为空可以返回零候选。不提高搜索次数、不自动续接请求。

URL 在进入 `QUrl` 规范化前检查原始 authority 和主机格式，拒绝外校域、假后缀、账号密码（包括空 `@`）、端口（包括空端口）、IP、空格、反斜线、编码主机和无效地址。然后再次通过本校根域边界检查。校内 HTTP 候选只转换成 HTTPS 后采样，不读取 HTTP；过滤新闻正文、登录路径、PDF 和已有栏目。每个保留结果带 `status: candidate`，模型标题只用于展示。

候选随后由 `SchoolOnboarding` 实际读取官网，验证至少 3 条列表及首条正文。通过后保留原有来源，追加新来源，重新加载配置并更新通知；失败或需要登录的候选不会成为公开可用来源。官方日期和正文来自实际官网响应，模型不能设置官网验证结果。自动采样能读取一个栏目，也不能证明官网覆盖完整。

## 本地记录与离线检查

检索记录保存到应用数据目录 `ai-schools/<学校ID>.search.json`，包含 `contract_version: search-v1`、`status: candidate_only`、模型、`template_id`、一次调用、实际用量、候选链接及时间，不包含密钥。记录和补充种子通过原子写入保存；记录保存失败就停止，不添加来源。`ai-schools/<学校ID>/report.json` 是后续无模型爬虫的验证报告，其 `model_calls: 0` 只统计爬虫阶段。网页样本和本机配置也保存在该目录，不自动提交社区或发布。

源码分工：`src/adapters/DeepSeekSearch.cpp` 管理官方协议、完整性和候选过滤，`src/adapters/SchoolOnboarding.cpp` 管理采样及校验，`src/desktop/AiSourcesPage.cpp` 管理入口、状态和记录，通用数据与业务层不依赖 DeepSeek。离线测试包含未完成响应、搜索工具错误、非法内容块、八条之后的错误、假后缀、空 userinfo、空端口、重复结果、普通回答冒充检索和无 Key 行为。离线测试不验证账户权限、真实检索质量、服务器收费或网络超时行为；真实 API 成功须另有账户 Key 和实际响应证据。
