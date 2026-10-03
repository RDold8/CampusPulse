# 学校资源 AI 说明契约 v1

本契约按 DeepSeek 普通 `chat/completions` 的 JSON Output 建设：程序先取得学校公开网页，AI 只对提供的片段提炼用途、学校公开使用说明和未知项。输出经本地校验后仍是待审核草稿。模型不承担网站抓取、账号登录、采购认证和个人授权认证。

截至 2026-10-03，本项目已有的“AI 补充”入口用于寻找遗漏官网栏目；它使用可选的 DeepSeek 原生搜索协议，实际候选仍由学校采集器检查。本契约中的“资源说明 AI”尚未接入桌面界面或业务库。本轮交付是可运行的离线契约校验器、提示词、Schema 和反例测试；本轮未提供或使用 API Key，未发真实模型请求，未做真实授权测试。原生搜索与资源片段分析是两个独立能力，不以搜索候选代替网页原文。

## 以实际 API 能力为边界

[DeepSeek JSON Output 文档](https://api-docs.deepseek.com/guides/json_mode/)要求设置 `response_format={"type":"json_object"}` 并在提示中明确 JSON。官方同时提示可能返回空内容，输出 token 设置不足可能截断。因此，“返回 JSON”不能替代本地字段、引用和业务边界检查。

[Chat Completions 文档](https://api-docs.deepseek.com/api/create-chat-completion/)当前列出的模型包含 `deepseek-flash`，支持关闭 thinking、非流式返回和 `max_tokens`。模型名以后可能变化，应读取受支持配置，不能把文档示例当作账户调用成功证据。下面的请求只是接入模板，不会读取提示词文件或发出网络请求：

```json
{
  "model": "deepseek-flash",
  "thinking": {"type": "disabled"},
  "messages": [
    {"role": "system", "content": "<configs/prompts/resource-evidence-v1.txt 的完整正文>"},
    {"role": "user", "content": "<通过输入契约校验后的 JSON 文本>"}
  ],
  "response_format": {"type": "json_object"},
  "max_tokens": 3072,
  "stream": false
}
```

不依赖 Beta strict tool schema、浏览器、文件上传、任意脚本执行或模型自主下载。这里的两个 JSON Schema 用于本地数据检查，不能直接作为 `json_object` 的服务器端严格 Schema 参数。[Tool Calls 文档](https://api-docs.deepseek.com/guides/tool_calls/)说明函数实现由客户端提供；本契约没有开放客户端工具执行。已有原生搜索协议的实现及状态见 `docs/ai-supplement.md`。

## 输入来自程序，网页文字是数据

输入固定遵守 `schemas/ai-resource-input.schema.json`。每次只处理一个资源：

| 字段 | 来源与约束 |
| --- | --- |
| `schema_version`、`request_id` | 程序生成；输出须完整对应此次请求。 |
| `school.school_id`、`root_host`、`trusted_source_hosts` | 已通过大学包验证的配置；不能接受模型或任意网页改写。可信来源 host 必须属于该校根域。 |
| `resource.resource_id`、`url` | 已存在的资源对象或待审核候选；URL 格式检查不意味着已做 HTTP 访问。 |
| `as_of` | 程序提供的当前业务日期，用于试用日期位置计算。 |
| `sources[].source_id`、`url` | 程序实际取得的官方来源；ID 和 URL 均不得重复。source URL host 必须精确命中可信来源列表。 |
| `retrieved_at`、`published_on` | 带时区的取得时间、可空的原文发布日期；日期含糊时不猜。 |
| `text`、`sha256` | 发送的精确公开文本片段及其 UTF-8 SHA-256；原始 HTML 哈希如有，应由采集证据另外保存。 |

一次最多 6 个来源，每个片段最多 6000 字符，总计最多 24000 字符；完整输入 JSON 不超过 256 KiB。摘录选取、去掉网页脚本与登录资料、日期识别以及主机白名单由程序负责。页面里“忽略此前规则”“调用某地址”“写入配置”等内容都是待分析文本，不能变成执行指令。不能发送学生账号、密码、Cookie、登录后的私有页面、个人待办或图书馆全文。片段不足就保留 unknown；不要通过增加模型能力假设填补材料缺口。

本地校验器会核对可信来源域和哈希，但无法证明这份输入真的来自网络。调用方必须使用已验证大学包并保留采集记录；不能把外部任意 JSON 自带的“可信域”当作学校认证。校验器不解析 DNS、不抓 URL，联网采集仍需现有请求层的 DNS、重定向、体积与访问政策检查。

as_of 与 retrieved_at 由调用程序按学校配置的业务时区生成，retrieved_at 的偏移量须使用该业务时区在取得时刻的实际偏移。校验器只按时间戳所带本地日期核对“取得日期不晚于 as_of”，不联网确认服务器时间、不自动推断源站时区；调用程序必须先完成这个转换。

## 输出把未知留出来

输出固定遵守 `schemas/ai-resource-output.schema.json`，只接受本版本字段，额外字段直接拒绝。`claims` 必须各有一项用途、学校使用性质、范围、校内条件、校外条件、登录条件；试用日期单列。每个确定断言必须引用至少一段输入原文，并携带 `source_id`、精确来源 URL 和连续原句 `quote`。

`school_status` 只允许 `unknown/purchased/trial/open`。其他未知 claim 的 value 必须为 null，evidence 必须为空；不能用空字符串伪装已填字段。学校情况缺乏明确材料时保持 unknown，并写在 unknowns。没有置信度字段，不用分数认证来源或授权。

平台列出一千种期刊，不能归纳成学校购买一千种；学校某年说明已购，不能归纳成当前仍已购；能打开入口，不能归纳成可以阅读全文；校园 IP、CARSI、VPN、个人手机号与学校统一身份认证各自按原文记录，不能统一改写成“输入学校账号即可”。

引文逐字符匹配只证明所给文本确有这句话。它不能机械识别引文是否足以支持归纳、是否偷换了学校与厂商、是否遗漏“仅试用”、是否仍在有效合同期。所有确定 claim 均带 `semantic_review_required` 标记；包括错误采购推论，只要引文形式合法，也可能通过机械检查并仍被标记为待语义审核。这不是采购认证。

引文或归纳若包含 HTML、Markdown 或指令样文字，也只作为字符串保存。未来审核界面须纯文本显示或转义 HTML，不能渲染可执行标签、执行文字里的命令或在验证时打开其中的网址。精确匹配不提供内容执行权限。

试用日期 v1 只接受来源引文中已经出现的字面 `YYYY-MM-DD`，拒绝起止颠倒；缺失或需要中文、相对日期换算时填 null，后续程序或人工先解析日期再重新出具材料。程序按 as_of 计算 `before_start/within_dates/after_end/unknown`，字段称“日期位置”，不称“服务有效”。即使在日期区间内，也没有验证账号、实际服务或机构权限。

候选链接只能来自给定学校原文，且 URL 必须完整出现在引文中并属于该校根域。v1 不让模型新造第三方推荐；现有第三方资源仍可作为 resource.url 被分析，但学校采购证据只能来自学校公开说明。候选不自动接入，后续需要现有采集器检查列表结构、正文与访问状态。

## 状态及接入位置

顺序是 `prepared_input → model_candidate → rejected 或 validated_draft → 人工核查`。校验器返回 `business_write_allowed=false`，不修改学校包、资源库、订阅、待办、日历和收藏。将来接入时另设审核页与明确的草稿转正式说明步骤；当前没有实现该业务步骤。

以下事实分别保存：

| 事实 | 由谁确认 | 不能据此推导 |
| --- | --- | --- |
| AI 候选/说明草稿 | 模型返回内容 | 网页存在或内容正确 |
| 机械校验通过 | 本地结构、哈希与引文检查 | 归纳语义、学校采购和当前合同 |
| HTTP 入口访问 | 程序实际 HTTP 响应与时间 | 全文可读或学校已订阅 |
| 学校购买/试用/访问说明 | 有日期的学校公开证据及审核 | 当前用户已经授权 |
| 用户实际授权 | 用户在提供方正式流程中的实际结果 | 可由通用爬虫或模型代办 |

普通更新和无 AI 的资源扫描保持零模型调用。本模块未来接入默认关闭，每次用户明确触发最多一个请求，30 秒超时，输出上限 3072 token；这些是产品初始限额，不是 API 吞吐、价格或输入 token 保证。保存实际 usage、模型名、契约/提示词版本、请求 ID、来源哈希、输入与输出缓存时间，不记录密钥及 Cookie；来源或契约变化后缓存失效。同一天相同材料可复用草稿，不自动重发。提示词/材料变更后的再次请求须可见地成为新尝试，而不是后台循环。

只有 `choices[0].finish_reason=stop`、非空 content 且完整 JSON 才可进入本校验器。`length`、过滤、工具调用、服务资源不足、aborted、401、模型不支持、超时、空内容、解析失败和字段错误均标记失败；不补括号、不截取 Markdown 中的半段 JSON、不吞异常、不自动重试。失败保留现有资源与人工说明。这里没有 API envelope 处理代码，这些响应条件是以后请求适配层必须实现的前置门槛。

## 复现当前离线验收

在项目根目录执行：

```powershell
py -X utf8 -m unittest discover -s tests -p test_ai_resource_contract.py -v
py -X utf8 tools/validate-ai-resource.py --input tests/fixtures/ai-resource/synthetic-input.json --output tests/fixtures/ai-resource/synthetic-output.json
```

校验器仅依赖 Python 标准库，不读取 API Key，不请求模型、不联网、不启动 GUI，也不写业务数据库。合成 fixture 的授权描述只用于测试。验收记录在 `evidence/ai-resource-contract-validation.json`；真实模型遵守率、成本、实际 API 返回、学校合同与用户授权均未验证。
