# Android 0.6.0：设置与输入验证

日期：2026-11-12。设备：小米 8 / Android 10 / ARM64。
本地测试版，versionCode 8，覆盖安装 0.5.1；本轮未推送 GitHub。

## 使用

游戏库中选择游戏，再选“游戏设置”。可设置编码、引擎 profile、缩放、
触摸预设、屏幕按键入口、手柄 A/B 交换，以及菜单/回看/存读档键。
保存只影响当前游戏，下次启动游戏生效。取消不写文件。
默认自动识别 profile；未知作品仍可通过游戏目录 games.json 配置产品条目。
手动 profile 只指定引擎版本，不强制改产品名。

设置保存在游戏目录 `.openbgi/settings.ini`，旧 encoding.txt 仍作编码回退。
请使用设置页修改；原生端读取的是设置页生成的 ASCII 键值格式，
不承诺支持任意手工编写的 Java Properties 转义或带空格变体。

| 操作 | 默认行为 |
| --- | --- |
| 单击 | 左键 |
| 长按 / 双指轻点 | 右键 |
| 阅读预设滑动 | 滚轮 |
| 拖拽预设移动 | 按住左键拖动，放开或取消时松键 |
| Android 返回 | Escape |
| 屏幕菜单 / 回看 / 存档 / 读档 | Escape / 滚轮上 / S / L，可按游戏重设 |
| 手柄 A / B | Enter / Escape，可交换 |
| 手柄 X / Y / L1 / R1 | Space / PageUp / PageUp / PageDown |
| 手柄方向、左摇杆 / L2、R2 | 方向键 / Ctrl |

功能键由游戏脚本定义，不保证所有作品的 S/L 都是存读档。
拉伸会改变长宽比；整数倍放不下时改为等比缩小。
引擎渲染、点击反变换和 Android 编辑控件使用一致的缩放布局。

## 已通过

- `sh tools/android/tests/run-input-tests.sh`：48 项设置/输入检查，270 组 C/Java 缩放计算一致。
  覆盖设置隔离、取消/失败路径、旧编码回退、键码/引用计数、手柄轴、手势取消等。
- Android 原生库与 Java 编译；APK v3 签名与对齐检查。
- 独立真机宿主：13 项完整回归再次 ALL PASSED，包含 IME 的 ASCII/GBK/SJIS/UTF-8、
  单/双栏及生日表单、四种对话框分支、后台引擎时钟冻结。
- 独立输入宿主记录：单击左键、长按一次右键、滚轮、键盘 S/L/F5/PageUp、
  注入的手柄 A/B 和交换；事件在引擎线程处理，按下/松开状态符合预期。
- 拖出画面后松开、拖动中按 Home 都释放左键；未留下按住状态。
- 整数倍、拉伸模式中心及偏移点击落在预期逻辑坐标；四个屏幕键可见且产生正确事件。
  虚拟键关闭生效；不合法 profile 参数不会进入引擎命令行。
- Fureraba 设置页保存 profile 1.616/整数倍后重新打开仍存在；取消不生成配置。
  对应引擎参数实测生效，普通存档 001 可读；屏幕 S/L 和回看可打开对应界面。
  测后恢复 GBK / 自动识别 / 保持比例 / 阅读预设。
- Kanojo Step 的设置独立且保留 GBK / 自动识别 / 保持比例。
  冷启动快速读取回到测试前对白；屏幕 S/L 打开对应界面，普通 NO.001 读取显示对应对白。
- NOISY 广播后暂停提示、手动继续及 Home/回前台恢复再次通过。
- 升级前额外快速保存并备份；测试后六个 `.sud` 文件与该备份 SHA-256 全部相同。
  临时宿主测试应用已卸载，主应用游戏、配置和存档保留。PC 原始游戏未改动。

本地证据在工作目录 `work/runtime/`：`p1-input-public-tests.log`、
`input-device-events-*.txt`、`input-hosttest-13-results.txt`、
`input-fure-*.png`、`input-kanojo-*.png` 和 `input-baseline/comparison.json`。
这些包含本地游戏画面/私有存档的验证材料不纳入公共源码。

## 范围与下一步

实体手柄、鼠标、模拟轴真实设备和多系统版本仍待实测。
独立宿主使用自有测试画面；它验证事件契约，不能替代每部游戏的实际操作和全流程验证。
真实 SIM 来电、物理有线/蓝牙断开、混合编码、视频等旧待办继续保留。
主机完整套件最近基线为 17/19：bpc、win32wide 仍失败，不称 CI 全绿。
本次原生变更仅限 Android 后端，下一步为 EGL 纹理上传、脏区域和 vsync 限帧。

## 独立宿主复现

按 Android 构建说明配置依赖，先运行 `make android-native ANDROID_HOST_TEST=1`，
再运行 `sh tools/android/package-hosttest.sh`。它生成独立包 org.openbgi.hosttests，
使用自有测试目录，不需要游戏。`files/games/hosttest/input-only` 文件选择输入事件测试；
F12 退出。没有该文件则执行 13 项交互式宿主测试。
测试 fixture 需建立 `files/active-game.txt`（内容 hosttest）和对应目录；
可在 fixture `.openbgi/settings.ini` 中测试缩放、触摸与按键配置。
生产 APK 不包含测试入口替代函数或测试 Java 类。
