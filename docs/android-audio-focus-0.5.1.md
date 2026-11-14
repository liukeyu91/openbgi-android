# Android 0.5.1：音频焦点和中断

2026-11-12，本地测试版；小米 8 / Android 10 / ARM64。

## 行为

GameAudioFocus 使用 AudioFocusRequest，USAGE_GAME / CONTENT_TYPE_MUSIC，
接受延迟授权，并声明遇到 duck 请求时暂停。只有 Activity 位于前台且已获得焦点时，
JNI 才让 VM、混音线程和引擎时钟一起继续。音量键固定控制媒体音量，不修改系统音量。
短暂抢占/duck 结束后自动恢复；永久抢占、拒绝授权或耳机断开需点击继续游戏。
等待授权时显示暂停层，可手动重试。窗口焦点改变不会主动重抢音频焦点。

后台/销毁释放请求，generation 使旧请求的排队回调失效，防止后台突然播放。
耳机断开的确认状态在同一游戏会话切后台/返回后保留。广播接收器只在前台注册，
销毁时解除；没有新增电话、联系人、位置或蓝牙运行时权限。

## 实测

AudioFocusControllerTest：29 项检查通过，覆盖瞬时/duck/永久失焦、延迟/拒绝授权、
过期 GAIN/LOSS 回调、快速后台返回、重复 resume、销毁和 NOISY 后确认。

独立 org.openbgi.focusprobe 前台服务请求真实系统焦点，不遮挡游戏 Activity：
GAIN_TRANSIENT、GAIN_TRANSIENT_MAY_DUCK、GAIN 三种请求均获授权。
瞬时/duck 时出现暂停层并停止输出，服务释放后恢复；永久失焦后释放服务仍保持暂停，
切后台返回仍保持暂停，点击继续后恢复。
系统 dumpsys audio 确认主应用登记 DELAY_OK / PAUSES_ON_DUCKABLE_LOSS，
原播放器 piid:671 在首次抢占时释放；恢复时原生日志记录音频流重新启动。

定向向主应用发送 AUDIO_BECOMING_NOISY 广播后显示耳机断开提示；
后台返回不自动继续，点击继续恢复。应用已在后台时此广播不造成下次前台误暂停。
这些是广播/焦点路径验证，不等同于物理设备路由测试。

没有拨打电话，也未宣称真实 SIM 来电、蓝牙或 USB-C 耳机断开已验证。
另一次 EXCLUSIVE 测试出现 Error: Not found; no service started，未完成，不计入通过项目。
Android 13+ 的 receiver flags 分支已编译，只有 Android 10 完成真机验证。

0.5.1 沿用 0.5.0 已验证的 OS_Edit 和引擎代码；本轮不是重新完成游戏全流程。
升级前已保存当前 Kanojo Step 进度，主机保留升级前/当前两个存档备份。
代码变更只在引擎和宿主，不修改原始 PC 游戏目录。
最终 0.5.1 已在两部游戏冷读档，Kanojo Step 恢复到升级前保存的当前场景。
两部游戏共六个 .sud 与本轮快速保存后的升级前备份逐字节一致。
最终布局调整后，Fureraba 的 NOISY 暂停/手动继续复测通过。

## 重现

纯 Java 8+ 编译 tools/android/AudioFocusController.java 和 AudioFocusControllerTest.java，
运行 org.openbgi.port.AudioFocusControllerTest。

可选测试包：设置 Android SDK/Java 环境并先构建普通测试 APK，执行
`sh tools/android/tests/focus-probe/build.sh`，安装 bin/android-focus-probe/focus-probe.apk。
开启游戏后执行：

```sh
adb shell am start-foreground-service -n org.openbgi.focusprobe/.FocusProbeService --ei gain 2
adb shell am stopservice -n org.openbgi.focusprobe/.FocusProbeService
```

gain=1 永久抢占，2 瞬时，3 可降低音量。此包只申请焦点，不播放声音，结束后卸载。
主 APK 不含此服务、测试入口或前台服务权限。部分 ROM 会限制测试服务的后台启动。

参考：[Android 音频焦点](https://developer.android.com/media/optimize/audio-focus)、
[输出设备切换](https://developer.android.com/media/platform/output)。

下一步仍为 P1 启动器设置、按键映射和 EGL；硬件中断测试继续保留在路线图。
