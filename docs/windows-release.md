# Windows 下载与安装

CampusPulse支持Windows 10 1809及以上、Windows 11 x64。当前0.1.4桌面预览版（`v0.1.4`，2026-10-06）包含闹钟待办卡片、持续提醒弹窗、托盘后台提醒、三种提示音和简化AI接入；程序与安装器版本统一0.1.4。见[更新说明](release-0.1.4.md)与[发布验收](release-0.1.4-validation.md)。0.1.3仅在本机迭代，上一公开版本为0.1.2-r1。

## 下载

- [下载0.1.4安装程序](https://github.com/RDold8/CampusPulse/releases/download/v0.1.4/CampusPulse-0.1.4-windows-x64-setup.exe)
- [下载0.1.4便携版](https://github.com/RDold8/CampusPulse/releases/download/v0.1.4/CampusPulse-0.1.4-windows-x64-portable.zip)
- [发布说明与其他附件](https://github.com/RDold8/CampusPulse/releases/tag/v0.1.4)
- [SHA256SUMS.txt](https://github.com/RDold8/CampusPulse/releases/download/v0.1.4/SHA256SUMS.txt)

此前的[0.1.2-r1](https://github.com/RDold8/CampusPulse/releases/tag/v0.1.2-r1)、[0.1.2](https://github.com/RDold8/CampusPulse/releases/tag/v0.1.2)、[0.1.1](https://github.com/RDold8/CampusPulse/releases/tag/v0.1.1)与[0.1.0](https://github.com/RDold8/CampusPulse/releases/tag/v0.1.0)保留为历史版本。

双击安装程序，选择简体中文或英文，按向导完成安装。默认安装到当前用户的 `%LOCALAPPDATA%\Programs\CampusPulse`，无需管理员权限；可从开始菜单启动，桌面快捷方式可在安装时选择。安装完成不会自动启动程序。Qt 与 Visual C++ 运行库已随包提供，不需要 Python 或 Qt SDK。普通采集无需浏览器；需要公开网页验证时使用独立安装的 Microsoft Edge WebView2 Runtime。缺失或版本过旧会提示安装/更新，[微软官方入口](https://developer.microsoft.com/en-us/microsoft-edge/webview2/)。安装程序不会自动安装浏览器运行时。

升级前从托盘“退出CampusPulse”，再安装。便携版为`CampusPulse-0.1.4-windows-x64-portable.zip`，解压完整目录后打开`CampusPulse.exe`，不要只复制主程序；两种包使用相同运行文件。随附学校仅东北电力大学，其他学校可在“大学”页输入官网尝试自动发现。

## 数据与当前范围

默认数据库由程序保存在当前用户的 LocalAppData 中，与安装目录分开。卸载移除安装文件和快捷方式，保留个人数据库、订阅、待办及设置；卸载不是个人数据清理入口。

仅随附东北电力大学社区包，来源覆盖仍有限；其他三校种子只供源码回归，不进入软件目录。此前五校通用采集、北邮指南与北京化工大学AI补充分别保留独立实网验收，本次发布没有新增模型调用。登录、动态页面与验证码可能阻断接入。手机ICS导入、系统横幅与实际听感需目标环境确认，移动App和同步尚未实现。

在“我的待办 → 提醒声音”选择铃声、音量或静音并试听；“测试提醒”不创建待办。默认声音每12秒提示，最多1分钟，停止声音不完成待办。Windows托盘可用时关闭主窗口继续后台运行，彻底退出、关机和睡眠期间不检查，重新运行只补发最近5分钟内有效且未投递的提醒。

此预览安装程序没有代码签名证书。请通过本仓库 Release 下载，附件 `SHA256SUMS.txt` 用于核对文件完整性：

```powershell
Get-FileHash .\CampusPulse-0.1.4-windows-x64-setup.exe -Algorithm SHA256
```

## 构建与分发材料

源码构建使用 Visual Studio 2022 C++、Qt 6.8.3、CMake。安装器使用 Inno Setup 7.1.0；构建入口不会自动安装工具或运行软件。

```powershell
.\tools\build-desktop.ps1 -QtRoot 'D:\Qt\6.8.3\msvc2022_64'
.\tools\package-desktop.ps1 -QtRoot 'D:\Qt\6.8.3\msvc2022_64' `
  -Destination '.\dist\release-0.1.4\CampusPulse' `
  -VcRuntimeDir '<Visual Studio>\VC\Redist\MSVC\<版本>\x64\Microsoft.VC143.CRT'
py -X utf8 tools/prepare-release-licenses.py `
  --package-dir dist/release-0.1.4/CampusPulse `
  --qt-root 'D:\Qt\6.8.3\msvc2022_64' `
  --source-dir .deps/release-sources --output-dir dist/release-0.1.4/downloads `
  --version 0.1.4 --source-release-version 0.1.4
# 从本文件的“安装帮助文本”生成包内 INSTALL-README.txt，再编译安装程序。
.\tools\build-installer.ps1 -PackageDir '.\dist\release-0.1.4\CampusPulse' `
  -OutputDir '.\dist\release-0.1.4\downloads' -Version '0.1.4' -IsccPath '<Inno Setup>\ISCC.exe'
```

`package-desktop.ps1` 要求目标目录为空；再次打包应使用新的干净目录。`prepare-release-licenses.py` 需要事先下载的 Qt Base 6.8.3、Lexbor 2.5.0、libical 3.0.20 源码包，以及微软 VC Runtime 用户许可 DOCX；它验证三个源码包的固定 SHA-256、准备许可与 SPDX，并把源码附件复制到输出目录。具体文件名和哈希见工具中的 `SOURCES` 与包内 `licenses/dependency-sources.json`。

官方原始下载位置：

- Qt Base：<https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz>
- Lexbor：<https://codeload.github.com/lexbor/lexbor/zip/refs/tags/v2.5.0>
- libical：<https://codeload.github.com/libical/libical/zip/refs/tags/v3.0.20>
- VC Runtime 条款：<https://visualstudio.microsoft.com/wp-content/uploads/2021/09/Visual-C-Runtime-2015-2022-License-1.docx>

0.1.4附带3份未经修改的固定依赖源码，安装目录licenses包含121份文件（含Qt SPDX、来源与哈希）；旧脚本的177把56个目录也计入了文件数，本次纠正统计，不减少许可材料。许可脚本以`--version 0.1.4 --source-release-version 0.1.4`指向本版附件。Qt动态链接，用户可替换兼容DLL，改变ABI或工具链时从CMake源码重建。Qt6.8.3源码commit为`c07c2d5a527a644d36e7853d55132ae38921682f`，构建配置保存在SPDX中。提示音由原创C++合成，使用Windows系统winmm，无新增音频素材或多媒体运行库。

依赖许可分别适用：Qt LGPL-3.0、Lexbor Apache-2.0、libical MPL-2.0、SQLite public domain、Microsoft Runtime 独立条款、WebView2 SDK BSD-3-Clause（浏览器 Runtime 单独安装）。CampusPulse 原创代码、配置及图标仍为 MIT。详见 [第三方说明](../THIRD_PARTY_NOTICES.md)。

## 安装帮助文本

以下内容同时保存为包内 UTF-8 `INSTALL-README.txt`，供安装完成页显示：

```text
CampusPulse 0.1.4 — Windows 桌面预览版 / Windows desktop preview (v0.1.4, 2026-10-06)

从开始菜单启动 CampusPulse，或打开安装目录中的 CampusPulse.exe。
Start CampusPulse from the Start menu or run CampusPulse.exe in the installation directory.

支持 Windows 10 1809+ / Windows 11 x64，无需 Python、Qt SDK 或管理员权限。
Windows 10 1809+ / Windows 11 x64; no Python, Qt SDK or administrator rights required.

公开网页验证需要单独安装 Microsoft Edge WebView2 Runtime；本安装器不会自动安装它。
Public browser verification requires the separately installed Microsoft Edge WebView2 Runtime.
官方入口 / Official download: https://developer.microsoft.com/en-us/microsoft-edge/webview2/

首次使用以东北电力大学为例；大学页可尝试发现未收录的 .edu.cn 官网，覆盖仍需验证。
Starts with Northeast Electric Power University; discovery can try unconfigured .edu.cn homepages.

请核对官方原文后设置待办时间。提醒需要程序运行；ICS 导入不会持续同步。
Confirm task dates against official notices. Reminders require the app to run; ICS is not live sync.

AI配置填写地址和Key，自动发现模型并检查连接，主动搜索消耗token。
AI setup uses an address and key with automatic model discovery; requests consume tokens.

升级前从托盘退出正在运行的CampusPulse，个人数据与凭据保留。
Exit the running app through its tray menu before upgrading. Personal data is retained.
我的待办可选择提示音、音量或静音并试听，默认每12秒重复、最多1分钟。
Task sound settings offer tones, volume, mute and preview; repeats stop within one minute.
停止声音不完成待办，关闭最后一条提醒或按Esc会停音。
Stopping sound does not complete tasks. Dismissing the last reminder or pressing Esc stops it.

卸载保留个人数据。移动端和双端同步尚未实现。本包不包含任何真实 API Key。
Uninstall preserves personal data. Mobile/sync are pending. No real API key is included.

许可证、依赖源码下载与替换 Qt DLL 说明：安装目录 licenses 文件夹。
Component licenses, dependency source downloads and Qt DLL replacement: the licenses folder.

反馈与更新 / Feedback and updates:
https://github.com/RDold8/CampusPulse/releases
https://github.com/RDold8/CampusPulse/issues

初版，有许多不足之处，请见谅。
This is an initial version with many shortcomings. Thank you for your understanding.
```
