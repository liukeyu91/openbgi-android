# OpenBGI 1.0.2-android

Android 构建号 38，支持 Android 9 及以上的 ARM64 设备。

## 改动

- 全功能包和 SAF 包都内置双核心。启动游戏时可选 **OpenBGI Android** 或 **OpenBGI 1.0**。
- 两个核都支持 CP932（Windows-31J）。
- OpenBGI 1.0 核心新增 1.680 支持。《LovepicalPoppy》可自动识别引擎版本并启动到标题画面。
- 修复大图预加载闪退。

## 验证范围

主机回归（核心、VM、配置、游戏表）、ARM64 构建、双核心打包、签名与对齐检查均通过。上游核的 1.680 以 30 秒无界面运行到标题验证，剧情、存档、选项和分支尚未验证。参见[已知限制](known-issues.md)。

## 反馈

请附设备型号、Android 版本、安装包类型、游戏版本和复现步骤。日志可在应用的“关于”页导出。参见[真机冒烟与回传模板](android-smoke-test.md)。

---

Android build 38 supports ARM64 devices running Android 9 or later.

## Changes

- Both the full and SAF APKs ship with two cores. Choose **OpenBGI Android** or **OpenBGI 1.0** when launching a game.
- Both cores support CP932 (Windows-31J).
- The OpenBGI 1.0 core adds support for engine version 1.680. LovepicalPoppy is detected automatically and boots to its title screen.
- Fix a crash when preloading large images.

## Verification

Host regression (core, VM, configuration, game table), ARM64 builds, two-core packaging, signing and alignment checks all pass. 1.680 on the upstream core was verified by a 30-second headless run to the title screen; story, saves, choices and branches are not yet verified. See [known limitations](known-issues.md).

## Feedback

Include the device model, Android version, APK variant, game version and reproduction steps. Logs can be exported from the app's "About" page. See the [device smoke test and reporting template](android-smoke-test.md).
