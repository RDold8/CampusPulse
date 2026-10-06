# 官网后台自动接入

在“大学”页输入官网首页或域名，点击“接入 / 切换大学”。社区学校配置优先匹配；未预装的大学可从公开的 `.edu.cn` 首页自动识别学校名称，生成本机草案，再发现栏目、保存样本、检查列表和正文、生成配置。失败保留当前学校并显示原因。

例如输入 `www.sdu.edu.cn`，无需先把山东大学加入源码目录。自动身份保存为 `auto-schools/identities/<自动学校ID>.json`，标记 `school.identity_provenance: automatic_homepage`、`status: draft`、`reviewed_at: ""`。这是首页识别结果，没有经过社区人工审核；不会自动发布到 GitHub。重启后加载本机草案，社区配置优先于本机识别。

已生成配置缓存在当前用户 LocalAppData 下的 `CampusPulse/CampusPulse/auto-schools/<学校ID>/school.json`，同目录 `report.json` 记录成功来源、列表行数、失败URL和样本SHA-256；`samples` 保存网页快照。第二次输入同一官网直接加载缓存配置，再用“更新官网”读取最新列表。

生成配置保存 `onboarding_version`。共享识别规则升级时，再次输入官网会重新扫描；普通加载已有配置不扫描。考试安排、补考和重修类栏目优先发现，避免被大量招聘入口挤占有限扫描预算。AI可作为可选补充层，详见[DeepSeek补充官网栏目](ai-supplement.md)。

文件先完整写入同目录临时文件再提交。Windows默认采用替换移动；本机加密目录对同目录移动返回错误17时，改用Windows支持的复制移动并等待写入落盘，日志明确标注非原子提交。其他错误继续报出。复制提交期间断电不具备原子保证；损坏配置在下次输入官网时重新扫描，而通知数据库及既有待办独立保存。非Windows平台继续使用QSaveFile。

运行时使用 C++、Qt Network 和 Lexbor，模型调用次数为零，不需要模型API密钥，也不会上传网页给模型。开发时使用的助手对话仍会消耗对话token。普通页面只读取 HTTP HTML。0.1.2 遇到 HTTP 403/412/503 且符合已识别 JavaScript 验证特征的公开网页时，Windows 使用 WebView2 独立临时会话；验证取得真实 HTTP 200 网页后，按主机在内存保留短期公开 Cookie 与浏览器 User-Agent，随后继续普通 HTTP 采集。不读取用户浏览器资料，不复用学校账号。Cookie 不写入数据库或学校包；浏览器临时目录在关闭后清理。WebView2 Runtime 需独立安装，缺失/过旧时明确失败；[官方安装入口](https://developer.microsoft.com/en-us/microsoft-edge/webview2/)。联网和首次扫描仍需要时间。

## 识别与边界

- 已有学校匹配社区配置，接受官网根域与 `www` 写法；陌生学校初步仅支持 `[www.]<学校域>.edu.cn` 首页，并从 title / 学校名称元数据识别高校名称。`.edu.cn` 也属于其他教育机构，域名后缀不是高校身份证明。非教育域的陌生学校、无法识别的首页仍需社区核验配置。
- 普通网站、假后缀、IP、账号、显式或空端口、路径、查询、片段、编码伪装拒绝。每次请求检查 DNS 全部地址，拒绝内网、回环及保留地址，并固定公网连接地址、保留原官网 Host 与 TLS 校验名，避免再次解析绕过。仅访问本校 HTTPS 根域与子域；跳转重新检查，最多3次。不接受跨校跳转、附属中小学、网页正文或脚本自称的学校身份。
- 通过导航文字、title/aria-label/图片 alt、通知栏目路径、正文链接模式和 DOM 结构识别公开栏目。教务、双创教育等明确部门首页本身也尝试解析列表；竞赛、重修、补考、缴费等栏目作为列表候选，教学服务及办事指南继续跟进导航。分页历史入口不会作为新栏目挤占发现预算。列表至少识别3条通知，且首条正文通过验证后才启用来源。页面变化导致解析失败时显示错误，保留已有缓存。
- 陌生学校首次扫描最多48次页面请求（已有社区包可配置1—48）、深度3、每次间隔至少3秒，优先教务、本科生院等学习入口。发现阶段验证当前列表页和首条正文；找到常见 CMS 或标准“下一页”链接且同来源域校验通过时，生成的来源在后续更新中最多读取3页。不存在可验证的下一页仍只读取首个列表页，不猜造分页地址；不保证该栏目全部正文都可解析，不采集全量历史。报告中的列表行数包含栏目间重复，数据库会去重。报告同时保存 budget_exhausted、discovery_complete 和 pending_frontier，预算耗尽不表示全校扫描完成。
- 兼容常见 `/info/数字/数字.htm` 静态CMS和吉林大学就业网公开HTML路径。账号登录、需要人工操作的验证码、纯动态 API 列表及其他模板仍需相应适配器。
- 发布日期不明保留为空，放在“日期待核实”，不从标题学年、活动时间或当前年份猜测。网页发布日期不会变成缴费、报名等办理截止时间。

安装包的社区目录只保留东北电力大学；其他大学通过输入官网进行本机自动发现。用户已经自动接入的学校独立保存，不属于随附社区配置。吉林大学、北华大学和长春理工大学的种子仅用于源码回归测试，不进入安装包。通用发现不等于支持任意 CMS 或全校全量；纯动态列表、验证码、登录限制和特殊模板仍需适配器。自动识别的通知来源后续更新与正文读取也使用同样的公网与 HTTPS 保护。

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

## 0.1.2 公开浏览器验证与北邮

独立浏览器仅允许原已验证主机的 HTTPS GET，固定该主机公网地址；拒绝其他域、表单提交、附件下载和账号登录，拒绝证书错误。一次验证最多25秒、64个允许的网络请求及8次导航；媒体请求拦截后不占网络请求预算。浏览器内的验证导航单独计数，后续采集重定向仍最多3次。失败保留当前学校；不是通用验证码破解或登录采集。当前只在 Windows 提供此验证组件。详情见[北邮实网验收](bupt-onboarding-validation.md)。

需要重新发现已有学校的栏目时，可以使用上述显式 `--onboard` 命令指定官网和输出目录；它会执行新扫描，不直接复用旧的扫描结果。共享算法版本已升级到4，旧版配置在再次接入时会重扫。`--settings-dir <目录>` 可为验收建立独立 INI 设置，与 `--database`、`--onboard-output` 一起隔离测试数据。
