# GitHub 源码保存

2026-10-05，源码仓库为公开的 `RDold8/CampusPulse`，主分支为 `main`。本次 `v0.1.2-r1` 为 0.1.2 更新预览版，保存通用采集、日期证据、学生服务指南及回归样本改进；软件内版本号保持 0.1.2。原 `v0.1.2`、`v0.1.1`、`v0.1.0` 标签与附件保留，不移动既有版本标签。源码通过新的 Git 提交保存，安装包、便携包及依赖源码作为 Release 附件提供。项目采用MIT许可证，大学页面与第三方依赖的权利分别保留。安装包见[Release](https://github.com/RDold8/CampusPulse/releases/tag/v0.1.2-r1)与[下载与安装](windows-release.md)。

目录外高校接入已通过有界实网测试，具体覆盖、遗漏和构建范围见[五校对比](five-school-universal-validation.md)与[北邮教务指南验收](bupt-generic-crawling-validation.md)。修复后的 AI 检索及来源补充完整链路仍待单独复验；发布程序包不代表全校全量覆盖、手机导入或系统通知已经验证。此前 AI 状态见[验收记录](universal-onboarding-validation.md)。

## 保存范围

Git保存C++源码、CMake构建入口、原创图标、通用学校配置、模板、Schema、项目说明、开发工具，以及脱敏离线测试样本。GitHub保存同一提交，后续修改通过新的Git提交和push更新。

本机的 `build/`、`dist/`、虚拟环境、下载依赖、`evidence/`、运行数据库、日志及环境密钥文件由 `.gitignore` 排除。Git保存源码与打包入口；安装程序、便携包及对应依赖源码作为Release附件发布，不进入Git历史。本机原型继续保留在 `dist/CampusPulse/`。

部分大学官网测试样本包含联系邮箱与手机号，发布副本已替换为占位文本；原始样本仅保留在本机忽略目录。`tests/fixtures/publication.json` 记录原始与发布副本哈希、脱敏字段和范围。学校原始URL、通知标题、时间、解析结构和附件链接保留，未下载名单附件。

## 克隆与验证

```powershell
git clone https://github.com/RDold8/CampusPulse.git
cd CampusPulse
py -m pip install -r requirements-design.txt
py tools/validate_school_configs.py
py -X utf8 -m unittest discover -s tests -p test_ai_resource_contract.py -v
```

C++构建见 `docs/desktop-prototype.md`。安装Qt 6.8.3及Visual Studio 2022 C++工具后，可向 `tools/build-desktop.ps1` 传入本机Qt路径。配置阶段自动创建本机 `evidence/` 目录，以便测试写入报告；该目录不进入Git。

旧版本真实SQLite快照属于本机验收资料，不随源码发布。全新克隆仍执行合成数据库迁移测试；要求本机旧快照的测试明确跳过，不能把这种跳过解释为真实旧库已在新机器验证。原生桌面2、手机导入、实网来源与真实DS调用仍需各自环境验证。

当前随附社区学校包只保留东北电力大学，核心与包契约供社区扩展。吉林大学、北华大学、长春理工大学的种子保留为独立回归测试材料，不进入软件社区目录或安装包。自 0.1.1 起可以尝试发现目录外的 `.edu.cn` 官网，自动生成的身份与来源保留待核验状态；不代表已覆盖全国高校或保证官网内容全量。

## 后续保存

在项目根目录执行 `git status` 查看变化，选择需要的源码文件执行 `git add`，再执行 `git commit` 与 `git push origin main`。密钥默认通过运行环境或当前进程提供，Windows 可选在本机使用当前用户 DPAPI 加密保存；不放入学校包、测试样本、发布包或提交记录。
