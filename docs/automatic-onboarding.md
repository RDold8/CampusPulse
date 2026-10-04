# 官网后台自动接入

在“大学”页输入官网首页或域名，点击“接入 / 切换大学”。社区学校配置优先匹配；未预装的大学可从公开的 `.edu.cn` 首页自动识别学校名称，生成本机草案，再发现栏目、保存样本、检查列表和正文、生成配置。失败保留当前学校并显示原因。

例如输入 `www.sdu.edu.cn`，无需先把山东大学加入源码目录。自动身份保存为 `auto-schools/identities/<自动学校ID>.json`，标记 `school.identity_provenance: automatic_homepage`、`status: draft`、`reviewed_at: ""`。这是首页识别结果，没有经过社区人工审核；不会自动发布到 GitHub。重启后加载本机草案，社区配置优先于本机识别。

已生成配置缓存在当前用户 LocalAppData 下的 `CampusPulse/CampusPulse/auto-schools/<学校ID>/school.json`，同目录 `report.json` 记录成功来源、列表行数、失败URL和样本SHA-256；`samples` 保存网页快照。第二次输入同一官网直接加载缓存配置，再用“更新官网”读取最新列表。

生成配置保存 `onboarding_version`。共享识别规则升级时，再次输入官网会重新扫描；普通加载已有配置不扫描。考试安排、补考和重修类栏目优先发现，避免被大量招聘入口挤占有限扫描预算。AI可作为可选补充层，详见[DeepSeek补充官网栏目](ai-supplement.md)。

文件先完整写入同目录临时文件再提交。Windows默认采用替换移动；本机加密目录对同目录移动返回错误17时，改用Windows支持的复制移动并等待写入落盘，日志明确标注非原子提交。其他错误继续报出。复制提交期间断电不具备原子保证；损坏配置在下次输入官网时重新扫描，而通知数据库及既有待办独立保存。非Windows平台继续使用QSaveFile。

运行时使用 C++、Qt Network 和 Lexbor，模型调用次数为零，不需要模型API密钥，也不会上传网页给模型。开发时使用的助手对话仍会消耗对话token。后台只读取HTTP响应中的HTML，不启动浏览器渲染页面；联网、首次扫描和更新仍需要时间，随后界面使用本地缓存。

## 识别与边界

- 已有学校匹配社区配置，接受官网根域与 `www` 写法；陌生学校初步仅支持 `[www.]<学校域>.edu.cn` 首页，并从 title / 学校名称元数据识别高校名称。`.edu.cn` 也属于其他教育机构，域名后缀不是高校身份证明。非教育域的陌生学校、无法识别的首页仍需社区核验配置。
- 普通网站、假后缀、IP、账号、显式或空端口、路径、查询、片段、编码伪装拒绝。每次请求检查 DNS 全部地址，拒绝内网、回环及保留地址，并固定公网连接地址、保留原官网 Host 与 TLS 校验名，避免再次解析绕过。仅访问本校 HTTPS 根域与子域；跳转重新检查，最多3次。不接受跨校跳转、附属中小学、网页正文或脚本自称的学校身份。
- 通过导航文字、通知栏目路径、正文链接模式和DOM结构识别公开栏目。列表至少识别3条通知，且首条正文通过验证后才启用来源。页面变化导致解析失败时显示错误，保留已有缓存。
- 首次扫描最多24次页面请求、深度3、每次间隔至少3秒，优先教务、本科生院等学习入口。只验证当前列表页和首条正文；不保证该栏目全部正文都可解析，不采集全量历史。报告中的列表行数包含栏目间重复，数据库会去重。
- 兼容常见 `/info/数字/数字.htm` 静态CMS和吉林大学就业网公开HTML路径。登录、验证码、纯JS/API页面及其他模板需要贡献共享适配器。
- 发布日期不明保留为空，放在“日期待核实”，不从标题学年、活动时间或当前年份猜测。网页发布日期不会变成缴费、报名等办理截止时间。

安装包的社区目录仍是东北电力大学、吉林大学、北华大学、长春理工大学；陌生大学通过本机自动发现增加。通用发现不等于支持任意 CMS 或全校全量；纯动态列表、验证码、登录限制和特殊模板仍需适配器。自动识别的通知来源后续更新与正文读取也使用同样的公网与 HTTPS 保护。

## 社区添加学校

维护者提供学校稳定ID、名称、经核验的官网、时区和通常使用的抓取参数。在 `auto_discovery` 中可提供少量经核验的教务、学工、团委、资助及就业部门起始URL；只提供官网也能扫描，但分散在多个部门的栏目可能超出导航深度及请求预算。配置 `sources: []`，不必先写选择器。

```json
"auto_discovery": {
  "department_urls": ["https://jwc.jlu.edu.cn/"],
  "max_pages": 24
}
```

将种子加入 `configs/schools` 后运行配置检查与离线样本回归，再实网生成并审核报告。可参考本机自动生成的草案，但人工核验官网身份、栏目、日期与样本后才能成为社区配置；不能把自动识别标记改写成已经审核。已经保存的自动配置不会每次重建；共享发现算法升级会使旧缓存重新扫描。

## 开发验证

```powershell
py tools/validate_school_configs.py
& .\build\Release\CampusPulse.exe --onboard https://www.jlu.edu.cn/ --onboard-output .\evidence\jlu-auto-final | Out-Null
& .\build\Release\CampusPulse.exe --verify-school --config .\evidence\jlu-auto-final\cn-jlu\school.json --database .\evidence\jlu-live.sqlite --evidence .\evidence\jlu-live-proof.json | Out-Null
```

`--onboard` 可匹配社区种子或自动识别陌生大学。`--verify-school` 实际更新来源并验证首条通知正文，记录数据库及窗口证据。独立 `campus_unknown_university_live_probe` 可在不打开桌面、不接触个人数据库的情况下验证陌生学校。Windows GUI程序须等待退出后读取结果，避免把已启动当成已完成。离线测试见 `UnknownUniversityTests.cpp` / `UniversityRegistryTests.cpp` / `OnboardingTests.cpp`。
