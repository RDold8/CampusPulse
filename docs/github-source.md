# GitHub 源码保存

2026-10-04，当前源码准备保存到公开仓库 `RDold8/CampusPulse`，主分支为 `main`。项目采用MIT许可证，大学页面与第三方依赖的权利分别保留。

## 保存范围

Git保存C++源码、CMake构建入口、原创图标、通用学校配置、模板、Schema、项目说明、开发工具，以及脱敏离线测试样本。GitHub保存同一提交，后续修改通过新的Git提交和push更新。

本机的 `build/`、`dist/`、虚拟环境、下载依赖、`evidence/`、运行数据库、日志及环境密钥文件由 `.gitignore` 排除。这次保存源码，没有发布包含Qt运行库的二进制Release；本机可运行软件包继续保留在 `dist/CampusPulse/`。

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

当前学校包适用于东北电力大学、吉林大学、北华大学、长春理工大学，核心与包契约供社区扩展；不代表已覆盖全国高校或保证官网内容全量。

## 后续保存

在项目根目录执行 `git status` 查看变化，选择需要的源码文件执行 `git add`，再执行 `git commit` 与 `git push origin main`。密钥只通过运行环境或当前进程提供，不放入学校包、测试样本或提交记录。
