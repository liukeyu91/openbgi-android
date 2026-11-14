# 与她＊心渐进 / Kanojo Step：Android 验证

2026-11-12，小米 8，Android 10，OpenBGI Android 0.4.1。

## 配置

产品 `KanojoStep`，profile `1.636`，汉化对白 `gbk`。
原版 EXE 静态版本字符串为 1.636.1 / Compatibility 1.72。
游戏目录的 games.json 使用 tools/android/profiles/KanojoStep.json。
不在 APK 中加入任何游戏数据。记录为序章与存档验证，不代表全线通关。

## 此汉化包需要先展开 EXE 内资源

给定目录的 KanojoStep_CHS_20200101.exe 是 Enigma Virtual Box 打包程序。
ARC 原档之外还有 EXE 内的汉化覆盖文件；只复制原来的 ARC 会漏掉汉化。
使用 https://github.com/mos9527/evbunpack 的 `--ignore-pe` 静态提取模式，
不运行 Windows EXE。此次使用源码提交 ee7b647acf2a46617b0c448dfa83d63132ea917b。
解出 746 个文件，共 843,097,198 字节；对手机导入的这 746 个文件逐一验证
SHA-256，全部一致。抽样脚本含 GBK 中文，指令中的文本引用也有更新。

准备步骤：

1. 复制原游戏到一个新的工作目录，保留全部原文件和目录。
2. `evbunpack --ignore-pe "原目录/KanojoStep_CHS_20200101.exe" "提取输出目录"`
3. 把提取目录里的文件放到游戏工作目录根部。OpenBGI 优先读散装文件，再读 ARC。
4. 把本项目 tools/android/profiles/KanojoStep.json 复制为工作目录根部 games.json。
5. 手机上通过“添加游戏目录”导入工作目录，在游戏菜单中设置 GBK 后启动。

本次手机工作目录为 Download/OpenBGI-KanojoStep-Test；PC 原目录未写入。
PC 提取资源保存在 work/runtime/kanojostep-unpacked，仅供本地自备游戏使用。
准备后的汉化资源不属于引擎源码，不能加入公共引擎仓库。

## 引擎修复

- Android 字体枚举返回 Noto CJK 字体族，不再返回空列表。
- 80 25 支持 NULL 缓冲区查询总字节数、max=0 无上限，以及实际填充后的名称数。
- 补充 80 26 的目录名称查询和填充（游戏实际使用末参数 0）。
- 补充 81 39 的 ARC 名称列表查询/填充（依据本游戏两阶段调用验证）。
- 引擎 stderr 写入可导出的引擎日志，自动识别结果和未实现指令错误可留存。

这些是有测试覆盖的最小兼容实现；非零标志、其他世代引擎的边界行为仍需进一步
对照原引擎验证，不能据此宣称全系列精确兼容。

## 验证结果

- 系统 SAF 导入准备后的完整目录，保留独立游戏条目和 GBK 配置。
- 自动识别日志明确选择 1.636，进入汉化标题及序章。
- 中文对白显示，点击推进。
- 快速保存 KanojoStep206.sud；推进后读回相同位置。
- 正常退出，切到 Fureraba，读取已有普通存档 FriendToLoverHD005.sud。
- 重新启动 Kanojo Step，从标题快速读档回到相同中文对白。
- 两部游戏的测试存档 SHA-256 在切换和读取前后均保持一致。
- 新增 VM 测试覆盖文件/目录大小查询、填充、空结果、缓冲区不足，以及 ARC 内名称列表。
- Android 字体枚举本地契约测试通过；原生/Java 编译和 APK 签名验证通过。
- 19 组主机测试 17 组通过；bpc 和 win32wide 两组失败与原基线一致。

## 已知限制

默认姓名和部分设置项有 SJIS/GBK 混合编码乱码。尚未实现 Android IME。
本轮没有验证后续分支、全程视频、所有特效或本游戏普通存档槽。
MPG OP/ED 视频仍属于后续视频后端工作。旧版本目录迁移本轮也未覆盖。

## 本地证据

- kanojostep-device-title-ready.png：汉化标题。
- kanojostep-device-quickloaded-settled.png：推进后快速读回。
- kanojostep-device-cold-restored.png：切换游戏后冷启动读回。
- kanojostep-switch-fureraba-restored.png：Fureraba 普通存档恢复。
- kanojostep-list-test.log / kanojostep-all-tests.log：VM 与全套主机回归。
- kanojostep-overlay-hashes.json / kanojostep-overlay-device-hashes.txt：资源校验。

以上证据文件均位于本地 work/runtime，不包含在公共源码补丁中。
