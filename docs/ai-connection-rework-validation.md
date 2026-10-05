# AI 简化接入与学校加载验证（2026-10-05）

本轮针对旧 DeepSeek Anthropic 地址与 Chat Completions 混用造成错误请求的问题，重做基础连接、自动模型选择、真实官网证据筛选及学校接入进度。当前为本机 0.1.2 AI 重构构建，尚未更新公开 r1 安装包。

Release x64 构建成功。完整 CTest 23/23 组通过；之后复核学校接入与通知/资源采集并发状态，分离两个 busy 原因并再次通过 desktop_contracts。学校配置契约 5份/0失败。新测试覆盖官方与自定义路由的地址推断、模型筛选、取消重入、特殊完整请求地址、旧混合配置凭据加载、连接失败恢复、默认两字段表单、真实网页URL约束、部分工具结果，以及加载期间禁止重复提交和两个完成顺序。

生产 Qt 控件通过离屏图像探针，配置窗口有两个可见文本输入；协议、认证和JSON编辑均不显示，模型与提供方管理折叠。图像已人工查看。离屏渲染不能证明 Windows 桌面交互、真实 API 连接或网络覆盖；没有使用虚拟桌面2冒充实测。

本轮运行环境没有 DEEPSEEK_API_KEY，也没有可复用的已保存应用密钥，因此未调用真实收费 API。此前旧版本的模型响应不能当作本次自动连接与重构搜索验收。真实 DeepSeek或第三方路由需由用户填写当前有效Key后，在界面执行一次连接与搜索，核对实际返回模型、用量和校验通过的来源。基本地址为 https://api.deepseek.com，搜索模型由模型目录自动选取。

本机证据位于 evidence/ai-rework-20261005：ctest-final.log、desktop-final.log、implementation-proof.json 与 render/ai-provider-render.json、两张界面截图。测试结果和渲染探针不含真实Key，合成连接结果明确仅用于UI事件回归。

接口依据与开源参考见 [AI 提供方配置](ai-provider-management.md)，搜索预算与准入见 [AI补充](ai-supplement.md)。学校规则接入仍为零模型调用。

本机安装更新退出码0，安装后的exe与已验证构建SHA-256一致。安装前后当前测试库与独立设置文件字节哈希均一致。更新后在桌面1启动，窗口为“CampusPulse · 北京化工大学”，UIAutomation确认AI标签已选中；这证明Windows程序实际启动与页面定位，仍不代替真实API连接。过程证据为post-install-state.json、launched.json和install.log。
