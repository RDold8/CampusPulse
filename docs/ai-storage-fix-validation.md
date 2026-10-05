# AI 配置本地保存修复（2026-10-05）

用户截图显示 `本机 AI 配置保存失败：providers.json`。该提示来自本地保存，并不证明 API 鉴权失败。原实现合并 QSaveFile 的打开、写入与提交检查，隐藏了具体失败阶段。

在实际 `AppLocalDataLocation/ai-providers` 目录使用无密钥合成文件复现：打开、写入成功，提交失败，系统返回 `ERROR_NOT_SAME_DEVICE`（17）。同目录中关闭文件后直接 MoveFileEx 也失败；系统临时目录及项目目录的相同原子保存成功。原目录有 Encrypted 属性，普通文件写入成功，不能仅凭目录属性断言是权限或 EFS 密钥问题。未确定更底层驱动为何返回跨设备错误；未更改用户文件系统权限或加密属性。证据位于 `evidence/ai-storage-20261005/`。

修复使用现有 Qt SQL/SQLite 保存 AI 配置，不再依赖将临时 JSON 重命名为最终文件。数据库 `ai-providers.sqlite` 的 `documents(name,payload)` 表保留原配置和 DPAPI 文档结构；两份文档同事务提交。`saveProvider` 同时提交配置、凭据与活动提供方，失败时不改变已有文件内容与内存状态。原子性由 SQLite 的事务与 FULL 同步保证，保留真实存储错误，不启用直接覆盖 JSON 的降级方式。

旧 JSON 先按原有严格字段契约读取；首次成功保存迁入 SQLite。旧文件保留，迁移后仅 SQLite 生效。旧版本程序不能读取新格式；回退旧程序会看到迁移前的旧配置。当前用户 DPAPI 绑定保持兼容，明文 Key 始终只在进程内使用。

界面连接前执行真实写入事务再回滚，本地失败时不调用模型。若连接完成后的最终保存失败，保留选择的模型与 Key，允许仅重试保存。数据库错误显示打开、结构创建、写入或提交阶段与系统原因，界面明确说明整次保存尚未提交。

生产存储探针在原目录进行不提交的预检，并在其临时子目录保存合成配置和 DPAPI Key，重新创建 Store 后核对配置、启用状态、Key 恢复及数据库无明文；不使用真实 Key、不调用 API，退出时清理测试子目录。真实 DeepSeek 连接及搜索需要单独验收，不能用本地保存通过来代替。

0.1.3 最终构建的 CTest 23/23 组通过。新增回归通过第二份文档写入时的 SQL 触发器注入故障，验证前一份写入与内存状态一并回滚；覆盖旧 JSON 迁移、密钥撤销、不可写目录、保存前停止请求及保留模型的本地重试。原目录生产探针通过，包含 DPAPI 恢复、无明文与测试目录清理，`api_calls=0`。

复现命令（Qt 运行库已在 PATH 时）：

```powershell
build/Release/campus_ai_provider_storage_probe.exe '<失败的 AI 配置目录>'
ctest --test-dir build -C Release -R 'ai_provider_contracts|ai_provider_desktop_contracts' --output-on-failure
```

参考：[Qt QSaveFile 的原子提交与直接覆盖限制](https://doc.qt.io/qt-6/qsavefile.html)。
