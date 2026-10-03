# CampusPulse

把散落在高校官网的通知和实用资源，整理成直观的校园信息、学习入口、待办、日历和提醒；先做 C++ 桌面软件，移动 App 后续接入。

面向全国高校的通用开源平台，由社区添加和维护大学包。统一支持考试、竞赛、奖助学金、校园活动、教务通知、就业招聘。东北电力大学是参考学校包；当前优先可安装的桌面软件，预留移动端数据和业务接口。

## 当前交付

源码仓库：[RDold8/CampusPulse](https://github.com/RDold8/CampusPulse)。Git保存范围、脱敏样本与克隆检查见[GitHub源码保存](docs/github-source.md)。本机运行数据和软件包不进入源码仓库，本次未发布二进制Release。

2026-10-03：C++桌面原型0.1已实现第1—4步的软件功能，包括来源管理、本地订阅、个人待办、内置月历、ICS导出和应用运行期间的本地提醒。新增独立“学校资源”页，支持学习与办事入口发现、官网出处、访问状态、类别/阶段筛选和持久收藏，以东北电力大学作为实网示例。目录收录东北电力大学、吉林大学、北华大学、长春理工大学；后三校采用经核验的学校身份与部门入口，后台发现公开栏目、保存HTML样本、验证列表与正文并生成配置，规则采集与资源发现不调用模型。数据库版本7；原创图标与明暗主题已接入。各校公开来源覆盖不等同于全校全量，登录受限/403/动态栏目保留明确状态。手机日历导入和Windows系统通知送达仍需目标环境实测；完整社区SDK/CI、大学包发布、移动同步未实现。

- [C++桌面代码实施路线](docs/cpp-implementation-roadmap.md)：当前编码入口；每步模块、产出、验收和学习内容。
- [桌面原型使用与构建说明](docs/desktop-prototype.md)：当前运行范围、编译脚本、实网验证与源码阅读顺序。
- [官网后台自动接入](docs/automatic-onboarding.md)：输入流程、零模型调用、社区最小种子与接入边界。
- [需要学校账号的来源入口](docs/source-access.md)：登录状态、官方浏览器入口、通用配置及当前会话边界。
- [学校资源使用与架构](docs/school-resources.md)：资源分类、全部学习阶段、官网发现、收藏和社区通用配置。
- [东北电力资源验收](docs/school-resources-validation.md)：实网采集、原生界面、数据保留与当前覆盖边界。
- [DeepSeek补充官网栏目](docs/ai-supplement.md)：可选原生检索入口、用量、候选过滤和真实官网校验。
- [DS资源说明契约](docs/ai-resource-contract.md)：按公开JSON接口设计的输入、输出、引文校验、未知项与审核边界；资源说明API尚未接入界面。
- [图书馆与阅读工具核实](docs/library-access-validation.md)：公开入口、学校使用说明、历史采购与试用范围分别记录。
- [吉林大学与AI入口验收](docs/jlu-ai-validation.md)：教务遗漏修复、默认目录写入、实网数据及桌面2检查。
- [开源通用平台总框架](docs/platform-framework.md)：通用核心、适配器、大学包、社区生命周期、部署和验收。
- [大学包配置契约](docs/university-package-contract.md)：当前字段、稳定身份、兼容升级与能力边界。
- [当前分步实施计划](docs/implementation-plan.md)：已确认的来源、订阅、待办、内置日历与ICS范围，阶段状态和验收条件。
- [第1步验收记录](docs/stage1-validation.md)：数据库升级、来源运行、官网输入保护、界面检查与程序包证据。
- [第2步验收记录](docs/stage2-validation.md)：订阅操作、共用匹配、年份变化、多来源关系、数据库迁移与实网检查。
- [本地订阅规则与数据](docs/subscriptions.md)：当前可运行的规则语义、表结构和源码阅读入口。
- [个人待办与确认时间](docs/tasks.md)：事项操作、时间精度、状态、原文复核及版本4数据契约。
- [日历与ICS使用规则](docs/calendar.md)：时间确认、全天/准确时间、稳定UID与版本及手机导入范围。
- [第4步验收记录](docs/stage4-validation.md)：日历、提醒、旧库升级、两校实网与界面证据。
- [北华与长春理工接入](docs/new-schools-validation.md)：公开栏目和登录/动态限制。
- [第3步验收记录](docs/stage3-validation.md)：待办、Qt界面、正文刷新、旧库升级与程序包证据。
- [参考项目与完善顺序](docs/open-source-references.md)：官方开源项目的可复用机制、当前差距与下一阶段验收。
- [多站点与轻量采集设计](docs/multi-source-crawling.md)：来源发现、公开接口、静态解析和增量优化边界。
- [社区贡献指南](CONTRIBUTING.md)：新增学校、修复来源和适配器扩展流程。
- [系统架构](docs/architecture.md)：处理链路、订阅语义、日历更新、API 与可靠性。
- [双端与校园办事框架](docs/dual-client-framework.md)：通用办事链、双端职责和未来同步；桌面/移动形态按v0.4路线落实。
- [数据模型](docs/data-model.md)：实体、关系、唯一性、时间证据、版本和消息幂等。
- [东北电力大学来源核查](docs/neepu-sources.md)：实际官网来源、发现的边界与待核实事项。
- [东北电力大学配置](configs/schools/neepu.example.json)：六份来源已启用，就业网动态列表保持关闭；多站样本、学工前两页及缴费详情用于验证。
- [订阅配置草案](configs/subscriptions/neepu-student.example.json)：六类主题、学生人群、日历与提醒偏好的示例。
- [通用重修流程模板](configs/workflows/retake.example.json)：报名、缴费、核对结果等候选步骤，各校实例依据原文配置。
- [新大学配置模板](templates/university.example.json)与[学校配置 Schema](schemas/school.schema.json)：社区接入的当前入口。

## 项目定位

可选“AI补充”入口使用DeepSeek原生网页搜索，寻找规则采集遗漏的官网栏目；候选链接仍须通过本校域、列表和正文校验。默认关闭，开启后消耗API token，显示实际用量；API Key只保留在内存。当前环境未提供Key，尚未验证真实API连接。

一名学生选择学校、主题和适用范围，即可获得通知信息流；将相关重修、补考、奖学金申请加入个人计划后，按报名、缴费、提交和核对结果等步骤跟踪。首版计划提供本地保存、桌面提醒和ICS导出；持续手机订阅与双端同步在移动/服务端阶段实现。

项目起点是同学漏看教务处重修缴费通知而错过重修。核心成功条件是用户能看到与自己相关的操作、期限和未完成状态，并获得可验证的提醒路径，而不仅是汇总通知。

通用核心提供采集调度、解析、统一模型、订阅、待办、日历、提醒与双端；大学包描述学校身份、公开来源、适配器参数、校内流程、样本及维护信息。新增学校尽量只添加包；特殊站点贡献共享适配器。公开实例不执行学校包内任意代码。

“支持全国高校”指核心模型和接入契约通用，学校配置可以由社区贡献，不代表现阶段已经覆盖全国高校，也不代表任意官网无需适配就能采集。每校可以逐步覆盖栏目，公开显示主题覆盖与来源健康。

## 初期技术决策

当前采用 C++ + Qt 桌面方向；建议 C++20、CMake、Qt Widgets 功能原型、Qt Network 异步请求、Qt SQL/SQLite 本地存储。界面采用原生Qt控件、系统字体、明暗主题及可访问焦点，当前固定Qt 6.8.3、Lexbor 2.5.0和libical 3.0.20。Python仅保留为开发期配置检查工具，应用运行时不依赖Python。后续移动/服务端通过应用层接口与统一数据模型接入。

原创代码和配置采用MIT，见LICENSE；第三方依赖和官网内容权利独立，见THIRD_PARTY_NOTICES.md。已建立本地HTML/CSS适配器、样本回归、构建与打包脚本；正式公开发布前仍需完善社区CI、发布机制及依赖分发材料。

## 桌面原型

本机可打开 `dist/CampusPulse/CampusPulse.exe`；源码构建和功能范围见桌面原型说明。程序包带Qt运行库，运行不需要Python或Qt SDK。软件按用户点击更新读取学校包内启用的栏目；应用运行时每15秒检查本机已启用的待办提醒，支持ICS文件导出；移动App与持续同步尚未实现。

```powershell
.\tools\build-desktop.ps1 -QtRoot 'D:\CampusPulseSDK\6.8.3\msvc2022_64'
.\tools\package-desktop.ps1 -QtRoot 'D:\CampusPulseSDK\6.8.3\msvc2022_64'
```

## 当前可运行检查

在项目根目录执行：

```powershell
py -m pip install -r requirements-design.txt
py tools/validate_school_configs.py
```

检查配置结构、身份唯一性、时区和允许域引用；不联网、不执行采集、不证明来源覆盖或提醒送达。

## 下一开发入口

第1、2、3步已建立来源管理、官方首页输入、本地订阅、个人待办与确认日期。日历、ICS导出和最小本地提醒已实现；接下来根据真实学校来源继续完善公开内容覆盖，并在目标手机验收ICS。新增学校由社区学校包扩展。当前exe为本地原型，尚未公开发布，手机未连接。
