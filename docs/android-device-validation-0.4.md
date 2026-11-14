# OpenBGI Android 0.4 真机验证（2026-11-12）

设备：小米 8（ARM64）。从已安装 0.3 使用同签名覆盖安装 0.4，成功。
升级前应用内部只有空日志目录，内部和应用专属外部目录均无游戏数据或存档。
备份位于 work/runtime/device-0.4-backup/before-upgrade.tar。
因此，本次未验证旧版存档迁移，不把导入 PC 存档算作迁移验证。

## 已通过

- 通过 Android 系统目录选择器导入本机 Fureraba 完整目录。
- 75 个原文件，共 4,552,607,643 字节；包括非 ARC 文件、原始大小写、嵌套目录及 PC 自动存档。
- 导入完成后、首次运行前，全部 75 个文件 SHA-256 与 PC 原文件一致。
- 复制阶段仅存在 .import-<UUID>，结束后游戏目录才可见。
- 从设置菜单选择 GBK，编码文件落盘，正常退出后重新启动仍为 GBK。
- 无固定 engine/product 启动参数，进入 Fureraba 标题，正常输出 AAudio 音频启动日志。
- 读取导入的 FriendToLoverHD000.sud 自动存档，回到首句“想要女朋友！”。
- 空槽新建 FriendToLoverHD005.sud，正常结束游戏；独立 :engine 进程退出，返回游戏库。
- 重新启动产生新的引擎进程，读取新建普通存档，恢复同一句。
- 新建普通存档重启前后 SHA-256 一致：2a44edefb8b32ae053f3cf648cabec5b815aa267140c4ed12b302ee43254bee2。
- 对已保存的测试会话发送 SIGABRT，启动器存活并回到游戏库，native-crash.txt 记录信号 06。
- 通过正常“导出崩溃日志”按钮和系统保存界面生成 ZIP；取回 PC 后通过 ZIP CRC 检查，包含 native-crash.txt、engine-current.txt、engine-previous.txt 和 device.txt。

## 未验证

- 旧版真实游戏/存档从 files/game、files/openbgi 的迁移：本次设备上没有这类数据。
- 第二部完整游戏：当前仅提供了 Fureraba 游戏文件。
- 第二部游戏的实际存档隔离，以及 SJIS/UTF-8 补丁在真机中的实际显示。
- Java 异常导出和 Android 11+ 系统退出栈：本机这轮仅测试原生信号崩溃。

## 留存证据

- 原文件哈希：work/runtime/p0-source-hashes.json
- 手机导入哈希：work/runtime/p0-device-hashes.txt
- 标题截图：work/runtime/p0-device-title.png
- 冷启动读档截图：work/runtime/p0-device-cold-loaded.png
- 实际导出日志：work/runtime/p0-device-crash-logs.zip

测试后的手机停留在 OpenBGI 游戏库。导入的游戏数据、测试普通存档和 GBK 设置保留，方便继续测试。
测试用游戏原目录副本在手机 Download/OpenBGI-P0-Test，导入副本在应用 files/games/<UUID>。
本机原游戏未修改，完整文件哈希一致性在首次运行前确认。游戏正常运行会更新应用内自动存档和配置。
