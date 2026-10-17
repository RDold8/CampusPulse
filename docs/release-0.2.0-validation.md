# 0.2.0 发布验收

2026-10-14，Windows x64 / MSVC 2022 / Qt 6.8.3。验收区分源码、自动测试、打包运行、安装器和外部环境；测试样例不是全校信息覆盖证明。

| 层次 | 结果与证据 |
| --- | --- |
| Release 编译 | PASS。主项目完整编译成功，应用、资源和安装器版本统一0.2.0。 |
| CTest | PASS，26/26组。包括导航、资源、来源、订阅、提醒、声音、AI离线接口、HTML、ICS及数据库迁移。 |
| 配置契约 | PASS，东北电力大学包和通用模板共2份；配置检查不证明来源全覆盖。 |
| AI资源契约 | PASS，41个离线Python测试；本轮没有新增付费API调用。 |
| 原问题复现 | PASS。修复前无通知关联的个人提醒保存实际失败，错误为“关联通知不存在或不属于当前学校”；修复后保存、编辑、重启读取通过。 |
| 数据迁移 | PASS。独立冻结的v7 SQL构造既有事项及submitted投递记录，升级v8后验证关联、备注、UID、修订、投递记录及外键完整性，再写入手动提醒并重启。旧版迁移测试继续运行；没有新增失败注入测试，不能宣称所有磁盘错误路径已验证。 |
| 实际编辑器 | PASS。首页打开真实提醒编辑器，默认只显示标题与时间，保存后无通知记录，重开可读，关联通知按钮禁用。Qt offscreen截图不算显示器/DPI验收。 |
| 打包程序启动 | PASS。移除SDK PATH及Qt环境配置，使用包内运行库，在隔离的数据库、设置与学校发现目录启动实际CampusPulse.exe；Windows可见窗口与版本输出正确，数据库版本8及外键检查通过。启动样例会按既有逻辑尝试更新来源；没有把短暂启动当作实网采集成功。仅本测试进程在截图后终止，不触碰已安装程序。 |
| 原生到点提醒 | PASS。生产ReminderScheduler与ReminderPopup在隔离临时库中，对不关联通知的个人提醒按真实时钟调度；约6秒出现置顶非模态窗口，1条submitted记录、1次投递，截图保存。测试退出自动移除临时库，不读取个人数据。 |
| 原生声音接口 | PASS。gentle、double、alarm均完成播放，播放中停止成功；不修改系统音量。物理扬声器听感NOT_RUN。 |
| 分发材料 | PASS。Qt、SQLite、MSVC CRT及必要插件随包，121份许可材料、3份固定依赖源码，安装器检查无数据库、日志、测试程序及调试运行库。 |
| 安装器 | PASS编译与结构检查。按用户安装、稳定AppId、保留数据、无自动启动。没有在干净Windows虚拟机执行安装/卸载，标记NOT_RUN；不冒用现有用户安装登记进行所谓隔离安装测试。 |
| 远端CI | NOT_RUN。本地工作流草稿尚未发布；当前GitHub授权不包含workflow范围，源码发布不包含该草稿。没有把本地CTest写成GitHub Actions已通过。 |
| 外部效果 | 新一轮多校实网全覆盖、真实付费AI、手机ICS导入、系统横幅送达、移动同步均NOT_RUN或未实现。 |

## 发布与恢复入口

下载见[Windows安装](windows-release.md)和[Release](https://github.com/RDold8/CampusPulse/releases/tag/v0.2.0)。源码按新提交保存，旧标签与发布保留。二进制、依赖源码与SHA256SUMS放在Release附件，不进入Git历史。发布后另核对远端提交、Release附件列表与下载哈希；克隆验证结果以发布后的证据为准。

本机证据保存在忽略目录`evidence/release-0.2.0/`：原失败`manual-baseline-failure.txt`，最终构建`build-release.txt`，全量`ctest-release.txt`，`config-check.txt`，`ai-contract-check.txt`，`packaged-startup.json`，`native/reminder-native.json`，`native/sound-native.json`，`installer-build.txt`，及发布后的远端核对文件。证据记录本轮实际测试；历史内部升级记录不替代它们。

迁移至数据库版本8后不可用旧应用读取。升级前退出正在运行的程序；卸载保留个人数据。当前桌面交付可独立使用，移动端与同步仍属于后续阶段。
