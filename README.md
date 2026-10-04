# CampusPulse

[中文](#中文) · [English](#english)

## 中文

**基于同学忘记缴费有感而发，我开始开发 CampusPulse，希望帮助大学生更好地查找学校官网的信息。**

**已公开的 Windows 0.1.0 预览版：**[下载安装包](https://github.com/RDold8/CampusPulse/releases/download/v0.1.0/CampusPulse-0.1.0-windows-x64-setup.exe) · [发布说明与便携版](https://github.com/RDold8/CampusPulse/releases/tag/v0.1.0) · [安装说明](docs/windows-release.md)。支持 Windows 10 1809+ / Windows 11 x64，按当前用户安装，无需 Python 或 Qt SDK。0.2.0 新增陌生高校发现与多套 AI 配置，先提供本地测试包，联网验收与公开发布待继续。

学校官网里有很多有用的内容：教务通知、考试安排、竞赛报名、奖助学金申请、校园活动、招聘信息，还有图书馆和学习资源入口。它们往往散落在不同部门的网站上，查找费时，也容易漏看。

我想把这些公开信息集中整理起来，让学生可以按自己的需要查找，并把需要办理的事情加入待办，通过日历和提醒继续跟进。目前先做 C++ 桌面软件，移动 App 和双端同步后续再推进。

### 我为什么做这个项目

一位同学因为没有看到教务处官网的重修缴费通知，错过了缴费时间，最终错过重修。这件事让我意识到，学校发布了信息，学生却未必能及时找到；即使看到了，也可能忘记办理。

我希望 CampusPulse 能把“找到信息、看懂要求、记下时间、完成办理”衔接起来。重修、补考、奖学金申请这些事情，通常有报名、缴费、提交材料或核对结果等环节，每一步都值得单独记录。

东北电力大学是我选择的第一个例子。我希望最终做成通用的开源项目，让其他学校的同学也能参与，逐步补充自己学校的公开来源。

### 它能帮你做什么

- **集中查看学校通知。** 整理已接入官网栏目的标题、发布日期、正文和原文链接，关注考试、竞赛、奖助学金、校园活动、教务通知和就业招聘。
- **更方便地查找信息。** 默认优先显示当前年份，支持按年份、来源、主题和关键词筛选。标题里有“重修”“补考”“缴费”时，会分别显示提示；同时出现时用 ` / ` 连接。
- **保存自己的关注方向。** 把来源、主题和关键词保存为本地订阅，更新官网后查看符合规则的通知。
- **跟进需要办理的事情。** 从通知建立个人待办，分别记录报名、缴费、申请等操作，确认时间，标记办理状态。原文变化后，相关待办会提示复核。
- **使用日历和提醒。** 已确认日期的待办进入内置日历，可以导出 ICS 文件供手机日历手动导入。桌面程序运行期间可以触发已启用的本地提醒。
- **查找学校资源。** 在独立的“学校资源”页查看图书馆、课程与培养信息、竞赛、升学、就业等学习和办事入口，按类别与学习阶段筛选，并收藏常用链接。
- **尝试接入目录外的大学。** 输入尚未收录的 `.edu.cn` 学校官网首页，程序识别首页名称，建立本机学校草案，再发现公开栏目；无需先手工编写学校配置。识别成功仍需栏目和正文校验，无法访问的页面会保留原因。
- **管理自己的 AI 服务。** 保存多套服务名称、备注、官网、可自选请求地址、认证方式与模型；支持完整 URL、Anthropic Messages 和 OpenAI 兼容格式，显示最终请求地址，获取模型并测试连接。支持搜索工具的路由补充官网栏目；普通模型接口提供待核实建议。主页面只选择模型与分类搜索模板，连接地址和密钥在提供方编辑窗口中设置。

例如，你在软件里找到重修缴费通知，打开官方原文核对要求，建立“重修缴费”待办，确认时间并开启提醒，办理后再标记完成。报名、缴费和核对结果可以分别记录，避免完成其中一步后漏掉后续操作。

办理时间需要依据原文核对，或明确设为个人计划。软件不会把通知发布日期当作截止时间，也不会代替你办理学校业务。

### 我是怎么做的

我把处理过程分成四步：

1. **找到公开来源。** 在“大学”页输入官网，已有学校优先使用社区配置。尚未收录的学校从受保护的 `.edu.cn` 首页入口识别大学名称，生成本地草案；后台再沿官网导航发现部门和栏目，检查网页列表与正文，验证通过后接入。请求检查公网 DNS、本校域名和 HTTPS，识别失败不替换当前学校。
2. **读取并整理信息。** 爬虫通过 HTTP 请求读取公开网页，解析通知与资源链接，把结果保存到本地 SQLite 数据库。普通静态页面无需启动浏览器渲染；以后打开软件优先读取缓存，点击更新后再读取官网。
3. **按关注规则筛选。** 根据年份、来源、主题和关键词整理通知，本地订阅使用同一套匹配规则。你决定哪些信息与自己有关，哪些需要加入待办。
4. **安排后续行动。** 待办保存你确认的日期和办理状态，日历、ICS 导出和运行期间的提醒围绕这些待办工作。公开通知和个人完成状态分别保存。

常规采集和资源发现不调用 AI，不消耗模型 API token。首次发现和更新仍需要联网及处理时间，缓存让已保存的信息可以更快打开。

技术上，我使用 **C++20 + Qt** 开发桌面界面与网络请求，用 **SQLite** 保存本地数据，用 **Lexbor** 解析网页，用 **libical** 生成 ICS。源码按领域模型、应用服务、适配器、存储和界面分层，希望后续添加学校与功能时仍然清晰、容易维护。Python 只用于开发期工具，桌面应用运行时不依赖 Python。

### 不同学校怎么加入

学校的官网结构各不相同，信息也分散在教务处、学工部门、团委、就业中心等站点。我把学校身份、官网、允许访问的域名、栏目和解析参数放在独立的学校配置里，采集、订阅、待办与日历共用同一套核心。

社区可以先提供经过核验的学校官网和部门入口，再逐步完善栏目配置与测试样本。遇到现有解析方式无法处理的网站，再扩展共享适配器。学校配置只描述数据，不执行任意脚本。

目前目录包含以下四所学校，均只覆盖部分公开来源：

| 学校 | 当前情况 |
| --- | --- |
| [东北电力大学](configs/schools/neepu.example.json) | 首个参考学校，已接入部分通知栏目，并作为学校资源的实网示例；就业网动态来源仍待适配。 |
| [吉林大学](configs/schools/jlu.auto.json) | 已建立学校与部门入口配置，支持后台发现、验证并接入部分公开栏目。 |
| [北华大学](configs/schools/beihua.auto.json) | 已建立学校配置，并对部分公开来源完成接入测试。 |
| [长春理工大学](configs/schools/cust.auto.json) | 已建立学校配置，并对部分公开来源完成接入测试；需要学校账号的本科教务入口保留“需要登录”状态。 |

面向全国高校是项目的设计目标，后续由社区逐步扩展。0.2.0 开始支持目录外学校的自动发现，首轮只接受 `.edu.cn` 根域或 `www` 官网首页，自动身份始终标为待核验草案。没有找到可用栏目时，会保留失败信息和原来的学校；遇到特殊域名、登录或动态网站，仍需要社区配置与共享适配器。目前不能保证找全某所学校的信息。

### AI 在这里做什么

AI 负责补充寻找规则爬虫可能遗漏的官网栏目。内置综合、重修补考与缴费、奖助学金申请、竞赛、活动、学习资源、教务和就业八种搜索模板，按所选类别使用不同提示词定向寻找栏目。我参考 CC Switch 的提供方列表、预设、编辑表单、模型选择和启用切换，重新用 Qt 实现这一入口。补充功能默认关闭，需要用户主动启用；候选链接仍要经过学校域名检查和真实官网采样，验证通过后才接入。调用会消耗 API token，界面记录实际返回的用量。Key 默认仅保留在当前进程，Windows 可选当前用户 DPAPI 加密保存，不写进学校配置或数据库。

真实 DeepSeek 模型列表与短连接请求已返回成功，模型使用 `deepseek-flash`。原生搜索也收到真实工具结果，同时触发搜索次数上限；软件已增加保留真实部分结果和显示上限状态的处理，修复后的完整检索与来源补充链仍待网络恢复后复验。自定义兼容接口不内置网页搜索，普通模型回答只作为候选建议。另一个“资源说明 AI”方向目前完成了规范与离线测试，尚未接入桌面业务。

### 目前的阶段和后续方向

截至 **2026-10-04**，源码进入 **0.2.0 桌面预览版**，已公开下载的版本仍为 **0.1.0**。0.2.0 先做本地包验收，陌生学校完整接入、修复后的真实 AI 检索及 GitHub 发布待网络恢复后继续；具体证据见 [本轮验收记录](docs/universal-onboarding-validation.md)。

目前需要注意：

- 各校公开来源只有部分覆盖，登录限制、动态页面和网页改版仍可能影响采集。
- 需要账号的来源可以打开官方登录入口，但软件不复用浏览器登录状态，登录后不会自动开始采集。资源链接能打开，也不代表账号一定具备使用权限。
- 本地提醒需要桌面程序保持运行。手机 ICS 导入和 Windows 系统通知送达仍需在目标环境实测；导出的 ICS 不会持续同步软件里的修改。
- 移动 App、账号系统和跨设备同步尚未实现。

接下来，我会继续完善学校来源与资源覆盖、采集可靠性和社区配置流程，完成 AI、手机导入与提醒的实际验证，再推进移动端和双端同步。

欢迎反馈漏掉的通知、提供学校官方栏目、帮助核实资源入口，或参与代码与学校配置维护。参与方式见 [贡献指南](CONTRIBUTING.md)。

### 更多资料

| 内容 | 文档 |
| --- | --- |
| 运行与源码构建 | [桌面原型说明](docs/desktop-prototype.md) · [源码保存说明](docs/github-source.md) |
| 整体设计与学校扩展 | [平台总框架](docs/platform-framework.md) · [大学包配置契约](docs/university-package-contract.md) |
| 官网发现与登录入口 | [自动接入](docs/automatic-onboarding.md) · [来源访问](docs/source-access.md) |
| 待办、日历与资源 | [个人待办](docs/tasks.md) · [日历与 ICS](docs/calendar.md) · [学校资源](docs/school-resources.md) |
| AI 补充与规范 | [AI 服务配置](docs/ai-provider-management.md) · [DeepSeek 补充栏目](docs/ai-supplement.md) · [资源说明契约](docs/ai-resource-contract.md) |

其他设计和阶段记录保留在 [docs](docs/) 中。原创代码、配置和图标采用 [MIT 许可证](LICENSE)，第三方依赖与学校网页内容保留各自权利，详见 [第三方说明](THIRD_PARTY_NOTICES.md)。

**初版，有许多不足之处，请见谅。**

---

## English

**Inspired by a fellow student missing a payment, I started developing CampusPulse to help university students find information on their university's official websites more easily.**

**Published Windows 0.1.0 preview:** [Installer](https://github.com/RDold8/CampusPulse/releases/download/v0.1.0/CampusPulse-0.1.0-windows-x64-setup.exe) · [Release notes and portable ZIP](https://github.com/RDold8/CampusPulse/releases/tag/v0.1.0) · [Installation guide](docs/windows-release.md). Windows 10 1809+ / Windows 11 x64; per-user installation, with no Python or Qt SDK required. Version 0.2.0 adds discovery for unconfigured universities and multiple AI profiles. It is being delivered as a local test package before further live verification and publication.

University websites contain useful information: academic notices, exam schedules, competition registration, scholarships and financial aid, campus events, recruitment, and library or learning resources. These are often scattered across departmental websites, making them time-consuming to find and easy to miss.

I want to bring this public information together so students can find what they need, add relevant actions to their tasks, and follow up through a calendar and reminders. I am starting with a C++ desktop application; a mobile app and synchronization will come later.

### Why I started this project

A fellow student did not see a course-retake payment notice on the academic affairs website, missed the deadline, and consequently missed the retake opportunity. It made me realize that publishing information does not necessarily mean students will find it in time. Even after reading a notice, they may forget to act.

I want CampusPulse to connect finding information, checking requirements, recording dates, and completing actions. Retakes, resit exams, and scholarship applications can involve registration, payment, document submission, or result checks. Each step deserves its own record.

I chose Northeast Electric Power University as the first example. My goal is a general, open-source project that students at other universities can help extend with their own university's public sources.

### What it can help you do

- **Browse university notices in one place.** Organize titles, publication dates, article text, and original links from connected sources, covering exams, competitions, scholarships and financial aid, campus events, academic affairs, and recruitment.
- **Find information more easily.** Show the current year by default and filter by year, source, topic, or keyword. Chinese titles mentioning retakes, resit exams, or payments receive individual labels, combined with ` / ` when several apply.
- **Save your interests.** Store sources, topics, and keywords as local subscriptions, then review matching notices after updating from the websites.
- **Track actions.** Create personal tasks from notices, record registration, payment, or application steps separately, confirm dates, and track their status. Changes to the original article flag related tasks for review.
- **Use a calendar and reminders.** Put tasks with confirmed dates in the built-in calendar and export ICS files for manual import into a phone calendar. Enabled local reminders can trigger while the desktop application is running.
- **Discover university resources.** Find library services, curriculum information, competitions, further study, career resources, and other learning or practical links in a dedicated page. Filter by category or study stage and save favorites.
- **Try a university outside the registry.** Enter an unconfigured `.edu.cn` university homepage. The application identifies its homepage name, saves a local draft, and discovers public sections without a manually written package. Lists and articles still require validation; inaccessible pages retain their failure reasons.
- **Manage AI services.** Save several names, notes, websites, custom request routes, authentication modes, and models. Use full URLs or API bases with Anthropic Messages or OpenAI-compatible formats, inspect the resolved request URL, retrieve models, and test connections. Routes with search tools supplement discovery; ordinary model APIs supply unverified suggestions. The main page selects a model and category search template; connection addresses and keys stay in the provider editor.

For example, you can find a retake payment notice, open the official article to check its requirements, create a payment task, confirm its date, enable a reminder, and mark it complete after paying. Registration, payment, and result verification can be recorded separately, so finishing one step does not hide the remaining actions.

Action dates must be checked against the original notice or explicitly set as personal plans. The application does not treat publication dates as deadlines or complete university procedures on your behalf.

### How I am building it

I have divided the process into four steps:

1. **Find public sources.** Enter a university homepage. Existing universities use community packages first. An unconfigured `.edu.cn` homepage can supply a university name and a local draft; the application follows official navigation, validates lists and articles, and adds verified sources. Requests check public DNS addresses, the university domain boundary, and HTTPS. Failed discovery preserves the current university.
2. **Read and organize information.** The crawler reads public pages over HTTP, parses notices and resource links, and stores the results in a local SQLite database. Ordinary static pages do not require browser rendering. Saved information loads from the cache; updating reads the websites again.
3. **Filter by your interests.** Notices are organized by year, source, topic, and keyword. Local subscriptions use the same matching rules. You decide which information applies to you and which actions belong in your tasks.
4. **Plan what comes next.** Tasks store the dates you confirm and your progress. The calendar, ICS export, and reminders during application runtime work from these tasks. Public notices and personal completion states are stored separately.

Regular crawling and resource discovery do not call AI or consume model API tokens. Initial discovery and updates still require network access and processing time; the cache makes saved information quicker to reopen.

I use **C++20 and Qt** for the desktop interface and networking, **SQLite** for local data, **Lexbor** for HTML parsing, and **libical** for ICS generation. The code separates domain models, application services, adapters, storage, and the interface to keep future additions understandable and maintainable. Python supports development tools; the desktop application does not require Python at runtime.

### How other universities can join

University websites differ, and information is spread across academic affairs offices, student services, youth organizations, career centers, and other departments. I keep university identity, official domains, allowed hosts, sections, and parsing parameters in separate configurations. Crawling, subscriptions, tasks, and the calendar share the same core.

The community can start by providing verified homepage and department links, then improve source configurations and test fixtures. Websites that existing parsers cannot handle require an extension to a shared adapter. University configurations describe data and do not execute arbitrary scripts.

The current registry includes four universities, each with partial public-source coverage:

| University | Current status |
| --- | --- |
| [Northeast Electric Power University](configs/schools/neepu.example.json) | The first reference university, with some notice sources connected and live resource-discovery checks; its dynamic employment source still needs adaptation. |
| [Jilin University](configs/schools/jlu.auto.json) | University and departmental entry points are configured, with background discovery, validation, and onboarding of some public sections. |
| [Beihua University](configs/schools/beihua.auto.json) | A university configuration is available, with onboarding checks for some public sources. |
| [Changchun University of Science and Technology](configs/schools/cust.auto.json) | A university configuration is available, with onboarding checks for some public sources; undergraduate academic affairs requiring an account remain marked as requiring login. |

Supporting universities across China is the design goal, with coverage extended gradually by the community. Version 0.2.0 starts automatic discovery outside the registry, initially limited to `.edu.cn` root or `www` homepages. Automatically identified universities remain unreviewed drafts. If no usable section is found, the application preserves the current university and reports the failure. Unusual domains, authentication, and dynamic sites still need community packages or shared adapters. Complete coverage is not guaranteed.

### What AI does here

AI supplements discovery by proposing official sections that the rule-based crawler may have missed. Eight category templates focus prompts on general discovery, retakes and payments, financial aid applications, competitions, campus activities, study resources, teaching administration, and careers. I used CC Switch's provider lists, presets, edit forms, model selection, and activation flow as references for an original Qt interface. Supplementation is disabled by default and requires user activation. Candidate links still undergo domain checks and actual website sampling. Calls consume API tokens and show returned usage. Keys stay in process memory by default, with optional current-user Windows DPAPI encryption; university packages and the database contain no key.

Real DeepSeek model-list and short connection requests succeeded using `deepseek-flash`. Native search returned real tool results alongside a search-use limit error. The application now retains valid partial results and shows that limit; the repaired search-to-source pipeline still needs another live check when networking is restored. Custom compatible interfaces have no built-in web search and produce suggestions only. A separate resource-description AI contract has offline tests but is not connected to the desktop workflow.

### Current stage and next steps

As of **October 4, 2026**, the source is at **0.2.0 desktop preview**, while the published download remains **0.1.0**. Version 0.2.0 is receiving local package verification. Full onboarding for an unconfigured university, the repaired native AI search, and GitHub publication will continue when networking is restored. See the [validation record](docs/universal-onboarding-validation.md).

Current limitations:

- University source coverage is partial. Login restrictions, dynamic pages, and website changes can affect collection.
- The application can open official login pages, but it does not reuse browser sessions or begin crawling after login. A working resource link does not establish account eligibility.
- Local reminders require the desktop application to remain running. Phone ICS import and Windows notification delivery still need verification in their target environments. Exported ICS files do not continuously synchronize later changes.
- The mobile app, account system, and cross-device synchronization are not implemented.

Next, I want to improve university source and resource coverage, collection reliability, and community configuration workflows. I will also verify AI calls, phone calendar imports, and reminder delivery before continuing with the mobile app and synchronization.

Contributions can include reports of missing notices, official university sections, checks of resource entry points, code, or university configurations. See the [contribution guide](CONTRIBUTING.md).

### Further reading

Most detailed documentation is currently in Chinese.

| Topic | Documentation |
| --- | --- |
| Running and building from source | [Desktop prototype](docs/desktop-prototype.md) · [Source publication](docs/github-source.md) |
| Overall design and university extensions | [Platform framework](docs/platform-framework.md) · [University package contract](docs/university-package-contract.md) |
| Website discovery and login entry points | [Automatic onboarding](docs/automatic-onboarding.md) · [Source access](docs/source-access.md) |
| Tasks, calendar, and resources | [Personal tasks](docs/tasks.md) · [Calendar and ICS](docs/calendar.md) · [University resources](docs/school-resources.md) |
| AI supplementation and contracts | [DeepSeek supplementation](docs/ai-supplement.md) · [Resource-description contract](docs/ai-resource-contract.md) |

Additional design documents and stage records are available in [docs](docs/). Original code, configurations, and icons use the [MIT license](LICENSE). Third-party dependencies and university website materials retain their own rights; see [third-party notices](THIRD_PARTY_NOTICES.md).

**This is an initial version with many shortcomings. Thank you for your understanding.**
