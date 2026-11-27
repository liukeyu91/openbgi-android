# OpenBGI 1.0.1-android

Android 构建号 37，支持 Android 9 及以上的 ARM64 设备。

## 改动

- 汉化覆盖包可从 ZIP 或目录导入，并可导出为兼容上游的 ZIP。导入保留资源子目录，支持旧式日文文件名。
- 全功能包原地读取游戏，存档和展开的补丁资源保存在应用内。SAF 包通过系统选择器复制导入，两包可同时安装。
- 游戏库可直接添加 ISO／UDF 镜像与 ZIP 压缩包。单个 Deflate 条目上限为 256 MiB。
- 界面支持八种语言。可选择游戏随附的简中、繁中或英文资源，加入 Big5（CP950）编码。
- 加入 1.492 配置，改善 1.636 起的输入框对齐，修复 1.680 场景模糊、立绘缺失和跨版本档案枚举。
- 导出的日志 ZIP 加入最近一次游戏的运行摘要。
- 安装汉化覆盖包前可查看并保存[补丁体检](../tools/android/README.md#补丁体检)结果。

## 验证范围

已通过主机回归、ARM64 构建、签名和权限检查。SAF 导入与启动、1.680 场景和完整游戏路线待真机复测。参见[已知限制](known-issues.md)。

## 反馈

请附设备型号、Android 版本、安装包类型、游戏版本和复现步骤。日志可在应用的“关于”页导出。参见[真机冒烟与回传模板](android-smoke-test.md)。

---

Android build 37 for ARM64 devices running Android 9 or later.

## Changes

- Import translation overlays from a ZIP or directory and export an upstream-compatible ZIP. Import preserves resource subdirectories and supports legacy Japanese filenames.
- The full APK reads resources in place and keeps saves and extracted patches in app storage. The SAF APK copies a folder selected through the Android file picker and installs alongside the full APK.
- Open ISO/UDF images and ZIP archives from the library. Each Deflate entry is limited to 256 MiB.
- Add eight interface languages, support bundled Simplified Chinese, Traditional Chinese and English resource variants, and add Big5 (CP950).
- Add a 1.492 profile, improve text input alignment from 1.636, fix scene blur and missing sprites in 1.680, and fix archive enumeration across engine versions.
- Include a summary of the latest game session in exported logs.
- Check translation overlays before installation and save the latest report per game.

## Verification

Host regression tests, ARM64 builds, signing and permission checks passed. SAF import and launch, the 1.680 blur scene, and full game routes still need device testing. See [known limitations](known-issues.md).

## Feedback

Include your device model, Android version, APK variant, game version and steps to reproduce. Export logs from the app’s About page.
