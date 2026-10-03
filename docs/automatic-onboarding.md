# 官网后台自动接入

在“大学”页输入 `www.jlu.edu.cn` 或 `https://www.jlu.edu.cn/`，点击“加载大学配置”。第一次接入由后台完成发现、样本保存、列表识别、首条正文验证、配置生成和切换；切换后空数据库会自动读取通知。界面显示进度，扫描期间禁用重复提交。失败保留当前学校并显示原因。

已生成配置缓存在当前用户 LocalAppData 下的 `CampusPulse/CampusPulse/auto-schools/<学校ID>/school.json`，同目录 `report.json` 记录成功来源、列表行数、失败URL和样本SHA-256；`samples` 保存网页快照。第二次输入同一官网直接加载缓存配置，再用“更新官网”读取最新列表。

生成配置保存 `onboarding_version`。共享识别规则升级时，再次输入官网会重新扫描；普通加载已有配置不扫描。考试安排、补考和重修类栏目优先发现，避免被大量招聘入口挤占有限扫描预算。AI可作为可选补充层，详见[DeepSeek补充官网栏目](ai-supplement.md)。

文件先完整写入同目录临时文件再提交。Windows默认采用替换移动；本机加密目录对同目录移动返回错误17时，改用Windows支持的复制移动并等待写入落盘，日志明确标注非原子提交。其他错误继续报出。复制提交期间断电不具备原子保证；损坏配置在下次输入官网时重新扫描，而通知数据库及既有待办独立保存。非Windows平台继续使用QSaveFile。

运行时使用 C++、Qt Network 和 Lexbor，模型调用次数为零，不需要模型API密钥，也不会上传网页给模型。开发时使用的助手对话仍会消耗对话token。后台只读取HTTP响应中的HTML，不启动浏览器渲染页面；联网、首次扫描和更新仍需要时间，随后界面使用本地缓存。

## 识别与边界

- 官网输入只接受目录中已核验的官方首页精确主机名；普通网站、假后缀、IP、内网、账号密码、显式端口和栏目路径拒绝。输入校验完成前不请求该地址。
- 扫描只访问本校域及其子域的HTTPS地址，重定向重新校验；生成的来源限定实际主机名。
- 通过导航文字、通知栏目路径、正文链接模式和DOM结构识别公开栏目。列表至少识别3条通知，且首条正文通过验证后才启用来源。页面变化导致解析失败时显示错误，保留已有缓存。
- 首次扫描最多24次页面请求、深度2、每次间隔至少3秒。只验证当前列表页和首条正文；不保证该栏目全部正文都可解析，不采集全量历史。报告中的列表行数包含栏目间重复，数据库会去重。
- 兼容常见 `/info/数字/数字.htm` 静态CMS和吉林大学就业网公开HTML路径。登录、验证码、纯JS/API页面及其他模板需要贡献共享适配器。
- 发布日期不明保留为空，放在“日期待核实”，不从标题学年、活动时间或当前年份猜测。网页发布日期不会变成缴费、报名等办理截止时间。

当前目录只有东北电力大学和吉林大学，通用架构不等于已覆盖全国高校。吉大自动种子在 `configs/schools/jlu.auto.json`，没有学校专属CSS选择器；东北电力大学既有配置照常工作，其就业网动态来源仍待适配。

## 社区添加学校

维护者提供学校稳定ID、名称、经核验的官网、时区和通常使用的抓取参数。在 `auto_discovery` 中可提供少量经核验的教务、学工、团委、资助及就业部门起始URL；只提供官网也能扫描，但分散在多个部门的栏目可能超出导航深度及请求预算。配置 `sources: []`，不必先写选择器。

```json
"auto_discovery": {
  "department_urls": ["https://jwc.jlu.edu.cn/"],
  "max_pages": 24
}
```

将种子加入 `configs/schools` 后运行配置检查与离线样本回归，再实网生成并审核报告。未知学校的网页不能自行获得信任；学校身份需要社区维护，列表与正文规则由共享代码复用。已经保存的自动配置不会每次重建；开发者可用独立输出目录重新扫描后审核差异，当前界面还没有一键重新扫描按钮。

## 开发验证

```powershell
py tools/validate_school_configs.py
& .\build\Release\CampusPulse.exe --onboard https://www.jlu.edu.cn/ --onboard-output .\evidence\jlu-auto-final | Out-Null
& .\build\Release\CampusPulse.exe --verify-school --config .\evidence\jlu-auto-final\cn-jlu\school.json --database .\evidence\jlu-live.sqlite --evidence .\evidence\jlu-live-proof.json | Out-Null
```

`--onboard` 用于具有 `auto_discovery` 的受信任种子；`--verify-school` 实际更新来源并验证首条通知正文，记录数据库及窗口证据。Windows GUI程序通过管道等待进程退出，避免把已启动当成已完成。离线测试见 `tests/OnboardingTests.cpp`；自动协调器位于 `src/adapters/SchoolOnboarding.cpp`，通用DOM识别位于 `src/adapters/HtmlAdapter.cpp`，界面与核心规则分离。
