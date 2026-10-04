# 大学配置与包契约 v0.1

这里描述当前可校验的学校 JSON 配置，与未来完整大学包的边界。当前配置版本保持 0.1-draft，结构 schema 在 [school.schema.json](../schemas/school.schema.json)。采用 [JSON Schema 2020-12](https://json-schema.org/draft/2020-12/json-schema-core)描述字段约束。

## 当前字段

| 字段 | 含义 |
|---|---|
| schema_version | 配置格式版本，不是软件版本 |
| school.key | 稳定学校标识；本仓库内唯一 |
| school.name/aliases | 校名与别名 |
| school.country/province/city | 地区，支持未来扩展 |
| school.timezone | IANA 时区名称，不固定为中国时区 |
| school.official_homepage | 已核验的官网首页；桌面输入页按完整主机精确匹配 |
| status/reviewed_at | 静态配置状态与复核日期，不代表实时健康 |
| categories | 当前配置声明的主题词表，不等于已验证覆盖 |
| fetch_defaults | 周期、抖动、同域并发、间隔、条件请求 |
| resource_discovery.department_urls | 可选学校资源发现起点，最多12个不重复的本校HTTPS入口 |
| resource_discovery.max_pages | 可选资源发现请求上限，整数1—32，默认32；重定向也计入上限 |
| sources[].key | 学校内稳定的来源标识 |
| sources[].discovery_url/entry_url | 官方发现入口与实际栏目入口，未知entry用null |
| sources[].allowed_hosts | 允许抓取的精确主机名，不含协议、路径或通配符 |
| sources[].adapter | 共享适配器ID，pending仅可用于未启用来源 |
| sources[].access | 可选访问说明：mode为public或login_required；login_url为核验后的官方HTTPS入口 |
| sources[].category_hints | 分类候选，不能覆盖正文及人群判断 |
| sources[].extraction | 适配器参数，未完成用null；由未来适配器自身schema校验 |
| sources[].enabled/validation_state/pending | 运行开关、已有证据状态、待核实清单 |

enabled=true要求entry_url、非pending adapter、非空extraction，并标记adapter_verified。当前HTML/CSS适配器已实现，东电六个栏目来源启用，就业动态列表保持关闭；新大学模板默认关闭。adapter_verified必须对应样本和运行证据，validator不替代审核。

资源发现字段不启用通知来源，也不代表入口已经验证可访问。旧包没有该字段仍有效；通用桌面会话可以从同校已安装社区包补充资源入口，保留原通知配置。资源起点、采集记录、访问状态和收藏的详细语义见[学校资源](school-resources.md)。

allowed_hosts 接纳URL主机不代表已授权访问，不代表网络地址安全。真正 fetcher 还需处理重定向、DNS、私有IP限制、来源策略、响应大小和限流。

## 稳定身份与兼容升级

学校或来源改名保留 key；新栏目新增来源 key；已关闭来源保留迁移说明。大学包发布需记录 pack_version、core compatibility、adapter requirements、资产哈希与审核证据，后续会落实 manifest schema。

导入学校包必须先预览变化，原子切换版本；保留上个生效版本以便回退。旧订阅引用不因包删除栏目而静默失效，展示来源已停用。声明新的schema_version而无迁移器的包拒绝激活。

## 当前与规划能力

可执行：开发期JSON/字段检查、唯一标识/时区/允许域检查；C++原型加载必要配置字段、解析HTML/CSS并运行固定样本回归。当前C++loader是实现所需契约子集，不替代开发期完整schema检查。

第3步起C++加载器也要求并验证`school.timezone`。个人待办按包的学校身份隔离，日期及准确时刻按学校时区输入和显示；准确时刻保存为UTC，仅日期保持纯日期。个人状态、确认时间、提醒偏好存本地数据库，不写回社区学校配置。包以后更改时区时，已有待办保留自己的时区记录；详见[个人待办与确认时间](tasks.md)。

当前东电固定样本包含URL/哈希记录，程序支持实网验证模式。规划：SDK发现、通用fixture_manifest和runner、包发布/锁定/升级/回退、社区CI。未实现工具不提供虚构命令。

## 有界分页补充 2026-10-02

HTML适配器可在extraction.pagination指定next_selector（下一页CSS选择器）和max_pages（1至10的页数上限）。未配置时只读入口页。下一页须符合来源允许域，重复下一页会停止并报错；页数上限代表当前采集范围，不代表完成历史全量。学工示例为两页。

## 官网输入与本机来源状态补充 2026-10-02

桌面“大学”页先匹配安装的社区配置（官网根域 / www 等价），再允许陌生 `.edu.cn` 学校首页通过独立 UnknownUniversityDiscovery 识别。UniversityRegistry 本身只解析并匹配输入，不联网；独立发现器检查公开 DNS、固定 HTTPS 地址与首页学校名称后才生成本机草案。`school.identity_provenance: automatic_homepage`、`school.status: draft` 和空 `reviewed_at` 明确区分自动身份与社区核验；格式与首页识别均不是官方身份证明。拒绝IP、本地地址、账号、显式或空端口、路径、查询、片段、编码或Unicode伪装。本机草案单独保存在用户目录，不能自动覆盖社区配置。

SchoolPackage将完整目录与可执行配置分开；禁用或尚未就绪的来源保留展示。用户暂停属于source_preferences，不回写sources[].enabled。source_state与fetch_run按school_id/source_id隔离，last_success_at表示最近一次配置采集范围内完整成功；部分成功、失败或中断均保留这个时间及旧通知。来源类别提示不是通知实际覆盖的保证。

需要登录的来源通过 `access.mode=login_required` 显式声明；登录入口必须通过官方域与来源允许主机校验。加载器、SourceService和界面都保持这类来源不可采集。现有包中明确“需要登录”的待接入说明可兼容识别；一般HTTP错误不作登录推断。浏览器入口与登录会话采集是不同能力，当前仅实现前者，说明见 [来源登录入口](source-access.md)。

0.1.2 的 `auto_discovery.max_pages` 支持1—48，陌生大学草案默认48；已有社区包的设置保留。扫描预算包括导航、列表与正文验证，不等于下载48个完整栏目；分页历史不作为新栏目。
