# Windows 下载与安装

CampusPulse 0.1.0 是早期桌面预览版，支持 Windows 10 1809 及以上、Windows 11 的 x64 系统。

## 下载

- [下载安装程序](https://github.com/RDold8/CampusPulse/releases/download/v0.1.0/CampusPulse-0.1.0-windows-x64-setup.exe)
- [查看发布说明与其他附件](https://github.com/RDold8/CampusPulse/releases/tag/v0.1.0)

双击安装程序，选择简体中文或英文，按向导完成安装。默认安装到当前用户的 `%LOCALAPPDATA%\Programs\CampusPulse`，无需管理员权限；可从开始菜单启动，桌面快捷方式可在安装时选择。安装完成不会自动启动程序。Qt 与 Visual C++ 运行库已随包提供，不需要 Python 或 Qt SDK。

便携版附件为 `CampusPulse-0.1.0-windows-x64-portable.zip`，解压完整目录后打开 `CampusPulse.exe`；不要只复制主程序。安装包和便携包使用相同运行文件。首次启动使用东北电力大学配置，可以在“大学”页选择当前目录中的其他学校。

## 数据与当前范围

默认数据库由程序保存在当前用户的 LocalAppData 中，与安装目录分开。卸载移除安装文件和快捷方式，保留个人数据库、订阅、待办及设置；卸载不是个人数据清理入口。

当前目录有东北电力大学、吉林大学、北华大学、长春理工大学，均只覆盖部分公开来源。需要登录或动态适配的栏目可能不可采集。移动 App 与跨设备同步尚未实现；本地提醒要求程序保持运行，手机 ICS 导入及系统通知送达仍需目标环境验证。DeepSeek 为可选补充，真实 API 尚未验收。

此预览安装程序没有代码签名证书。请通过本仓库 Release 下载，附件 `SHA256SUMS.txt` 用于核对文件完整性：

```powershell
Get-FileHash .\CampusPulse-0.1.0-windows-x64-setup.exe -Algorithm SHA256
```

## 构建与分发材料

源码构建使用 Visual Studio 2022 C++、Qt 6.8.3、CMake。安装器使用 Inno Setup 7.1.0；构建入口不会自动安装工具或运行软件。

```powershell
.\tools\build-desktop.ps1 -QtRoot 'D:\Qt\6.8.3\msvc2022_64'
.\tools\package-desktop.ps1 -QtRoot 'D:\Qt\6.8.3\msvc2022_64' `
  -Destination '.\dist\release-0.1.0\CampusPulse' `
  -VcRuntimeDir '<Visual Studio>\VC\Redist\MSVC\<版本>\x64\Microsoft.VC143.CRT'
py -X utf8 tools/prepare-release-licenses.py `
  --package-dir dist/release-0.1.0/CampusPulse `
  --qt-root 'D:\Qt\6.8.3\msvc2022_64' `
  --source-dir .deps/release-sources --output-dir dist/releases
# 从本文件的“安装帮助文本”生成包内 INSTALL-README.txt，再编译安装程序。
.\tools\build-installer.ps1 -IsccPath '<Inno Setup>\ISCC.exe'
```

`package-desktop.ps1` 要求目标目录为空；再次打包应使用新的干净目录。`prepare-release-licenses.py` 需要事先下载的 Qt Base 6.8.3、Lexbor 2.5.0、libical 3.0.20 源码包，以及微软 VC Runtime 用户许可 DOCX；它验证三个源码包的固定 SHA-256、准备许可与 SPDX，并把源码附件复制到输出目录。具体文件名和哈希见工具中的 `SOURCES` 与包内 `licenses/dependency-sources.json`。

官方原始下载位置：

- Qt Base：<https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz>
- Lexbor：<https://codeload.github.com/lexbor/lexbor/zip/refs/tags/v2.5.0>
- libical：<https://codeload.github.com/libical/libical/zip/refs/tags/v3.0.20>
- VC Runtime 条款：<https://visualstudio.microsoft.com/wp-content/uploads/2021/09/Visual-C-Runtime-2015-2022-License-1.docx>

Release 同时提供三个未经修改的依赖源码包；安装目录 `licenses` 包含完整许可证、第三方归属文本、Qt SPDX、源码地址与哈希。Qt 动态链接，用户可以替换兼容 DLL、调试并运行修改后的版本，无签名或激活机制阻止替换。Qt 6.8.3 对应源码 commit 是 `c07c2d5a527a644d36e7853d55132ae38921682f`；其构建配置记录在 SPDX 中。更改 ABI 或工具链时，应从本项目公开 CMake 源码重新构建应用。

依赖许可分别适用：Qt LGPL-3.0、Lexbor Apache-2.0、libical MPL-2.0、SQLite public domain、Microsoft Runtime 独立条款。CampusPulse 原创代码、配置及图标仍为 MIT。详见 [第三方说明](../THIRD_PARTY_NOTICES.md)。

## 安装帮助文本

以下内容同时保存为包内 UTF-8 `INSTALL-README.txt`，供安装完成页显示：

```text
CampusPulse 0.1.0 — Windows 桌面预览版 / Windows desktop preview

从开始菜单启动 CampusPulse，或打开安装目录中的 CampusPulse.exe。
Start CampusPulse from the Start menu or run CampusPulse.exe in the installation directory.

支持 Windows 10 1809+ / Windows 11 x64，无需 Python、Qt SDK 或管理员权限。
Windows 10 1809+ / Windows 11 x64; no Python, Qt SDK or administrator rights required.

首次使用以东北电力大学为例；大学页提供当前收录学校，通知与资源为部分覆盖。
The initial example is Northeast Electric Power University. Source coverage is partial.

请核对官方原文后设置待办时间。提醒需要程序运行；ICS 导入不会持续同步。
Confirm task dates against official notices. Reminders require the app to run; ICS is not live sync.

卸载保留独立存储的个人数据。移动端、双端同步和真实 DeepSeek API 验收尚未完成。
Uninstall preserves personal data. Mobile, synchronization and real DeepSeek verification are pending.

许可证、依赖源码下载与替换 Qt DLL 说明：安装目录 licenses 文件夹。
Component licenses, dependency source downloads and Qt DLL replacement: the licenses folder.

反馈与更新 / Feedback and updates:
https://github.com/RDold8/CampusPulse/releases
https://github.com/RDold8/CampusPulse/issues

初版，有许多不足之处，请见谅。
This is an initial version with many shortcomings. Thank you for your understanding.
```
