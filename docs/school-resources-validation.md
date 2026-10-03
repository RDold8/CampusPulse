# 学校资源验收：东北电力大学

日期：2026-10-03。交付为本地 C++ 桌面原型的通用资源模块，东北电力大学作为实际采集示例。

## 实网与缓存

使用生产 `ResourceDiscovery`、HTML 解析器、分类器、`ResourceService` 与 SQLite 仓库。先后进行了两次有界扫描，每次最多 32 个请求；第二次验证培养方案的版本入口和标题清理。最终缓存为 90 项：

| 类别 | 缓存数 | 实际示例 |
| --- | ---: | --- |
| 学习规划 | 36 | 培养方案、2025版/2021版入口、多个专业方案、博士/硕士培养方案 |
| 课程资料 | 3 | 校本部、教务处、电气学院的课程建设入口 |
| 图书馆 | 20 | 图书馆、电子资源、数据库导航、借阅和入馆指南、官网推荐的数据库 |
| 办事服务 | 22 | 重修安排查询、成绩查询与核对、选课补选、学籍、实习、竞赛获奖证书、教室申请 |
| 就业 | 5 | 就业信息和服务入口 |
| 校园服务 | 3 | 校历和校园服务入口 |
| 其他 | 1 | 常用阅读器下载说明页 |

最终缓存访问状态为 `verified=21`、`discovered=69`。其中 15 项是学校官网推荐的外部链接，全部保持 `official_recommended / discovered`，不自动访问第三方。21 表示本次任务中已经成功检查、缓存当前标为可访问的条目；两轮各有请求上限，第二轮不一定重查每条历史缓存，可查看每项实际 `last_checked_at`。

课程建设入口不代表课程教材或课件已经下载。竞赛和学术支持的分类能力已实现，但这两类在本次缓存中没有独立条目，不将通知、公示和报道填充成资源；竞赛证书办理指南归入办事服务。

“培养方案 · 2025版”已实际检查可访问。版本标签只在已知学习规划页面中继承“培养方案”上下文；裸年份不会因此成为资源或发布日期。研究生列表中的日期前缀也与标题分开，保留标题中的年份及适用年级。

原文与证据：

- `evidence/neepu-resources-live.json`：最终资源全集、类别、状态、官网出处和检查时间。
- `evidence/neepu-resources-live.log`：第二轮实际请求进度及 32 次上限。
- `evidence/neepu-resources-first.json`、`evidence/neepu-resources-first.log`：首次扫描记录。
- `evidence/stage5-neepu-resources/cn-neepu`：实际 HTML、地址、检查时间、文件哈希与访问错误。
- `evidence/resources/seed-evidence.json`：四校 10 份公开入口样本的地址、时间、HTTP 状态、字节数及 SHA-256。样本不是四校完整接入验收。

实网入口：

```powershell
.\build\Release\campus_resource_live_probe.exe `
  --config .\configs\schools\neepu.example.json `
  --database .\evidence\neepu-resources-live.sqlite `
  --evidence .\evidence\neepu-resources-live.json `
  --samples .\evidence\stage5-neepu-resources
```

应用运行不需要 Python。该开发验收探针须能找到 Qt Network/SQL 运行库；构建环境使用 Qt SDK bin 路径。正式程序包已包含所需运行库。

## 测试与原生界面

最终构建成功，CTest **17/17** 套全部通过，记录为 `evidence/resources-build-tests.log`。三套新增资源契约分别覆盖：

- 数据：v5迁移、原结构与数据保留、事务回滚、学校隔离、收藏经刷新与重启保留、未知阶段与明确通用受众分开。
- 发现：官方域、原始账号/端口边界、第三方不采集、文件不下载、请求/深度/大小限制、取消、失败/登录/静态空壳、实际官网样本、指南与报道反例、培养方案版本上下文。
- 界面：缓存即时展示、组合筛选、收藏、非法链接禁开、URL handler 拦截、重复发现通知合并、取消后的最终结果、资源采集时的大学入口保护及 AI 配置延后切换。

首次构建中原桌面套件发现了采集 `started` 时输入未立即锁定的问题。已在开始信号立即锁定，完成后检查通知和资源双方是否仍忙；最终整套通过。首次失败保留在 `evidence/resources-build-first-failure.log` 与 `evidence/resources-desktop-first-failure.txt`。

生产 MainWindow 与 ResourcePage 在桌面 2 原生显示实际爬取资源库的副本，没有填充演示条目。八个标签、90行缓存、实际图书馆详情、官网出处及打开按钮均通过；截图为 `evidence/resource-native.png`。证据为 `evidence/resource-native.json` 与 `evidence/resource-native-environment.json`，确认不打开浏览器、不触发提醒、不联网、不改变原采集库、收藏或默认设置，前台窗口与当前桌面保持不变。

程序包正常模式已启动在桌面 2，窗口标题“CampusPulse · 东北电力大学”，原生窗口检查通过且包含“学校资源”。证据为 `evidence/resources-package-window-proof.json`。没有切换桌面、键鼠输入或前台激活操作。

## 用户数据库与程序包

确认旧应用已退出后，通过资源服务导入上述实际采集结果，自动事务升级默认库 **v5→v6**，新增 90 项东电资源。比较导入前、导入后和正常启动后：原有 9 张业务表的行数、数据 SHA-256 与表结构 SHA-256 全部相同。

原有 375 条通知、416 条修订、388 条来源关系及 53 次来源运行均保留；既有订阅、个人待办与提醒表也未变更。证据为 `evidence/resources-default-before.json`、`evidence/resources-default-after.json`、`evidence/resources-default-final.json`。收藏保护另有真实 SQLite 回归，默认库本轮没有测试收藏写入。

交付程序为 `dist/CampusPulse/CampusPulse.exe`。打包 exe 与最终构建 exe 的 SHA-256 一致；`evidence/resources-package.log` 保留部署输出。Qt 部署工具提示翻译目录与 VCINSTALLDIR 信息缺失，正常包已实际使用自身运行库启动并通过原生检查；界面中文为程序文本。最终程序哈希及各证据汇总在 `evidence/school-resources-delivery.json`。

## 当前边界

资源发现保持默认至多32请求、3秒间隔、8秒超时、2MiB HTML、深度3，不代表全校资源或所有历史页面已经找全。已发现、未访问、登录受限、不可访问和外部官网推荐明确区分。

“我的图书馆”的 HTTP/IP/8080 入口按当前安全规则排除。登录入口只交给系统浏览器，未读取学校账号或登录后的数据。静态页面可访问不保证数据库授权、校外VPN、附件或每个子服务可用。规则发现没有调用 DeepSeek，未消耗模型 API token；可选 AI 栏目补充保持独立入口。本轮没有进行手机同步、ICS手机导入或系统通知送达验收。
