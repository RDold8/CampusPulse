# 提醒声音与验收

2026-10-06，CampusPulse 0.1.3本地桌面预览版。

## 使用

打开“我的待办 → 提醒声音”，或使用托盘菜单“提醒声音”。选择轻柔铃声、双响提示、闹钟提示或静音，调整0—100音量；“试听”只播放一次，“停止”可以中断。保存后用于本机待办提醒；取消不保存，关闭设置会停止试听。打开设置和调整选项不会自动播放。

默认轻柔铃声、音量60、勾选重复提示。每批新提醒立即播放一次，重复时每12秒播放一次，最多持续1分钟；单次模式只播放一次。静音或音量0保留弹窗但不请求播放。多条新提醒共用一个声音序列，后来的提醒重新开始这个序列，不叠加多路音频。

弹窗“停止声音”停止当前一批声音和后续重复，保留提醒与待办状态；以后到点的新提醒仍使用保存的声音偏好。“知道了”关闭选中提醒，关闭最后一条或整个窗口（包括Esc）时停止声音。弹窗“声音设置”打开设置前暂停当前声音；保存后按新设置提示，取消则保持暂停。

程序使用Windows默认输出设备。软件音量控制自身PCM幅度，不修改Windows主音量；系统静音、输出设备选择和扬声器/耳机状态仍影响实际可听效果。播放错误显示在设置或提醒窗口，弹窗保留；不以静默失败或其他系统提示音替代错误。程序完全退出、关机和睡眠时不播放；应用运行范围和最近5分钟补发规则见[待办](tasks.md)与[日历](calendar.md)。

## 实现

- `ReminderSound.*`：三种原创短音由C++合成，16位44.1kHz单声道PCM。Windows `waveOut` 播放边界，持有缓冲区直到设备归还；中断使用reset，归还后unprepare和close。
- `ReminderAudio.*`：本机声音偏好、可注入播放回调、12秒重复与60秒上限。失败时停止后续重复。
- `ReminderSoundDialog.*`：选择、试听、停止、保存和取消；设置存入当前应用QSettings，兼容`--settings-dir`。
- `ReminderPopup.*`：到点窗口、停止当前声音、关闭与设置入口。`TaskPage`和托盘提供入口。

声音无外部音频素材，无新增Qt Multimedia、FFmpeg或模型依赖；Windows构建链接系统`winmm`。其他平台当前明确返回声音播放不可用，通用待办模型不依赖此Windows后端。

播放资源管理依据Microsoft的[waveOutWrite](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutwrite)、[waveOutReset](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutreset)、[waveOutUnprepareHeader](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutunprepareheader)与[waveOutClose](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutclose)契约实现；上述文档分别说明排队播放、停止并归还、解除缓冲准备及释放设备。

## 已完成的检查

Release构建成功；最终CTest 26/26组通过，日志`evidence/sound-ctest-final.log`。新增`reminder_audio_contracts`验证偏好往返及坏值、静音和音量0、单次与有上限的重复、停止、首次及后续失败、重复开始、试听保存和取消；测试使用临时设置与注入回调，不改用户数据，也不用于证明扬声器发声。新增Esc回归先复现隐藏后仍保留两条提醒的失败，再统一由`done()`清理并停音；依据[Qt 6.8.3 QDialog源码](https://github.com/qt/qtbase/blob/v6.8.3/src/widgets/dialogs/qdialog.cpp)处理关闭入口。声音设置挂在应用范围的提醒窗口下，避免学校会话异步替换时销毁仍在执行的设置对话框。

真实Windows设备探针`campus_reminder_sound_native_probe`运行通过，证据`evidence/sound-native-20261006/sound-native.json`：三种声音均收到开始和完成，第四次播放中途停止成功；耗时4684ms，无错误，无网络/模型调用，也未读取用户数据。该证据证明原生音频设备接受、归还和停止；`physical_speaker_audibility_verified=false`，不声称替用户听到了声音。

本次最终安装包路径为`dist/release-0.1.3-sound-20261006-r1/downloads`。本地安装与界面检查证据在同一声音验收目录内；GitHub公开下载不因本地打包而更新。

最终安装器退出码0，已安装0.1.3的exe与包内exe一致，SHA-256为`334f56a6824cb83908ceb4fffb06defd4f484251e3dedfaac8fc60c43e59aedb`。安装前后5份用户数据与设置文件哈希一致；界面试听并保存声音后，4份通知/待办和AI存储仍保持原哈希。声音偏好写入当前设置，不创建测试待办，原有1条已取消事项保持不变。证据为`install-proof.json`与`post-ui-integrity.json`。

生产界面UI Automation实测：试听状态进入“正在试听”，停止显示“试听已停止”；将音量改为35后取消，重新打开仍为轻柔铃声/60/重复；保存写入三项声音偏好。点击测试提醒后显示重复提示状态，停止按钮显示声音已停止，Esc关闭弹窗。最终停在“我的待办”。证据`installed-ui-proof.json`，截图`installed-sound-settings.png`、`installed-sound-popup.png`、`installed-task-page.png`。

安装包SHA-256为`80ed1f699a4fb6250efa27791cdaa59313581480c73e5c8eeca53d0fa1229241`，完整分发哈希见下载目录`SHA256SUMS.txt`。许可检查保留177份通知与3份固定依赖源码归档；声音自身由原创代码合成。
