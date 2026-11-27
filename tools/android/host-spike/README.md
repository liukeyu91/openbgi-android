# P2-1 Android host API 原型

P2 迁移以现有 Android 分叉为基线。接入上游组件后，用原应用、完整引擎配置和完整汉化资源做回归。

原型使用上游 v1.0（`257a20d`）的 `openbgi.h` 和 `libopenbgi-arm64.so`。测试 APK 的包名是 `org.openbgi.hostspike`，内置一个合成 ZIP，由与正式应用共用的库条目初始化代码挂载。关闭启动提示后即可查看日英混排，按住画面会变绿并播放短音，松手恢复蓝色。

实验包读取自己私有目录里已选中的库条目。首次启动时写入样本。profile、资源语言和缩放读取正式版的 `settings.ini`，补丁沿用条目的活动补丁。

实验包和正式版共用 Android 游戏时钟。实验包进入后台或暂时失去绘制 Surface 时暂停时钟，恢复绘制后继续计时。Linux 回归会在脚本等待和样本音轨播放期间模拟后台 5 秒。

## 构建

在 Linux 上配置 Android NDK、SDK Platform 35、Build Tools 35.0.0 和 JDK。将 ARM64 静态 FreeType 安装至 `$ANDROID_DEPS/arm64`，然后在上游 v1.0 源码目录构建核心：

```sh
make -j8 PLATFORM=android ARCH=arm64 ANDROID_API=28 \
  ANDROID_NDK="$ANDROID_NDK_HOME" ANDROID_DEPS="$ANDROID_DEPS" \
  VORBIS=stb MOVIES=none EMBED_FONTS= \
  LIB_LINK="-shared -Wl,-soname,libopenbgi-arm64.so -Wl,--exclude-libs,ALL -Wl,-z,max-page-size=16384" lib
```

在 Android 项目目录打包：

```sh
export FREETYPE_INCLUDE="$ANDROID_DEPS/arm64/include/freetype2"
export FREETYPE_LIB="$ANDROID_DEPS/arm64/lib/libfreetype.a"
sh tools/android/host-spike/package.sh /path/to/upstream-v1.0 /path/to/spike-build
```

APK 使用试验目录中的调试密钥。可以通过 `adb logcat -s OpenBGIHostSpike` 查看启动、首帧、触碰坐标和 AAudio 状态。

Linux 主机回归使用同一份启动配置、文件回调表、帧转换、字体渲染、音频输出和上游共享库。覆盖自动与指定 profile、繁中资源选择、补丁和缩放设置。直读目录、ZIP、ISO 和 UDF 均检查点击响应、汉化覆盖、存档以及 440 Hz 样本音轨的 48 kHz 重采样。模拟 AAudio 覆盖写入故障后的重开，以及暂停和恢复。另有 CP932 确认框样本，验证等待答复以及「是」「否」两个脚本分支。传入 `BGI_TEST_CJK_FONT` 可用同一款 Noto CJK 字体对比 CP932、UTF-8、GBK 和 CP950 的混排像素。命令末尾可传入样本的 ISO 或 UDF 镜像。

```sh
BGI_TEST_CJK_FONT=/path/to/NotoSansCJKjp-Regular.otf \
  sh tools/android/host-spike/test-storage.sh /path/to/upstream-v1.0
```

## 双核心对照包

`org.openbgi.hostcompare` 使用正式版的游戏库和 `GameActivity`。从同一个库条目点「开始游戏」，可选择 OpenBGI Android 或本地适配的上游 1.0。两种入口使用相同的游戏文件、活动补丁、配置和存档路径。本地上游核心支持指定 GBK、CP950 和 UTF-8，Android 字体渲染使用相同的文本编码。非 ASCII 资源路径仍按上游的路径编码规则处理。

完成常规 Android 原生构建后，在同一个 SDK/NDK 环境运行：

```sh
sh tools/android/host-spike/package-compare.sh /path/to/upstream-v1.0 \
  /path/to/android-native-output /path/to/compare-build
```

第二个目录内应有 `arm64-v8a/libopenbgi.so`。脚本生成本地签名前的 APK。兼容性扫描使用 Android 分叉的指令表。上游分支沿用 Android 的消息框、手势、快捷键和音频焦点，脚本输入法和 FFmpeg 待接。

## Android 适配映射

`src/host/android/` 按上游的宿主目录组织，统一提供 Host API 回调表和文件、字体、帧、音频、消息适配。它复用 `src/os/android/` 和 `src/os/posix/` 的平台实现。实验包的 Activity、样本和测试入口放在 `tools/android/host-spike/`，与主机回归共用同一份回调表。

| 现有模块 | 上游接口 | 迁移 |
| --- | --- | --- |
| `activity.c`、`window.c` 的调度与 OS 窗口画布 | `OpenBGI_Start`、`RunFrame`、`PumpEvents`、`Stop` | 将 OS 循环和画布改为 host 驱动。保留现有 Java Activity 和 JNI 生命周期入口。 |
| `renderer.c`、`viewport.h` | `OpenBGI_Frame_t` 的像素、行距和脏矩形 | 原型已复用 GLES2 渲染器。RGB555 和 BGRX 转换成 RGBA，按脏矩形范围更新。 |
| 输入 | `OpenBGI_Pointer`、`Button`、`Key`、`Wheel`、`Focus` | 原型的触摸使用实际绘制的视口，支持常用按键和鼠标滚轮。失焦与暂停时释放按住的输入。 |
| `audio_output.c` | `OpenBGI_AudioRead`、`RunAudio`、`SetAudioLead` | 播放线程从上游读取 44.1 kHz 双声道 PCM，复用现有的 AAudio 协商、重采样和断线重开。 |
| 消息框 | `OpenBGI_NextRequest`、`Answer` | 原型在 Android 界面线程显示原生消息框，引擎线程在关闭后提交实际按钮结果。 |
| `storage_paths.c`、POSIX 路径层 | `OpenBGI_Files_t` | 原型已消费正式应用的库条目和文件层。现有 Android Activity 仍启动分叉核心。 |
| `movie.c` | `OpenBGI_MovieDecoder_t` | 评估由上游 FFmpeg decoder 接管；验证 Android 暂停与跳过。 |
| `src/os/posix/font.c` | `OpenBGI_FontRenderer_t` | 原型复用 Android 的字体选择和 CJK 度量，接收上游的 CP932 与 UTF-8 编码切换。 |
| `sjis.c`、`compat_jni.c`、`crash.c` | 文本编码、公开 profile 接口、host 错误与日志 | GBK/Big5 文本、指令预检及本地引擎差异分别核对后再迁。 |

## 迁移评估（2026-11-25）

**契约规模** 上游 v1.0 的 `openbgi.h` 共 18 组宿主表、155 个回调指针。原型覆盖文件、画面、触摸、按键、音频取数和对话框请求。核心初始化要求提供毫秒时钟、高精度计时和本地时间三个回调。

| 层 | 分叉现状 | 迁移成本 | 结论 |
| --- | --- | --- | --- |
| 文件 | `src/os/posix/*` 提供路径、属性、查找、时间、容器与覆盖包 | `files.c` 将这些操作接入上游回调。Android 的原生路径是 UTF-8，游戏路径跟随上游的 SJIS／UTF-8 状态 | 原型接入，真机待验 |
| 画布 | `renderer.c` 负责 GLES2 纹理和缩放 | 原型主机像素对照通过 | Android EGL 和 Surface 生命周期待真机验证 |
| 输入 | 正式应用使用 Java 手势和按键映射 | 原型接入触摸、常用按键、滚轮和焦点切换 | 主机验证触摸坐标映射和上游核心点击响应 |
| 音频 | AAudio 播放线程与重采样 | 已接入上游混音，按设备能力转换采样率、声道与格式 | 主机检查音高、时长和流恢复，真机输出待验 |
| 影片 | `movie.c` 自带 FFmpeg 后端 | 大：要实现 `OpenBGI_MovieDecoder_t`（含画面与音轨），否则放弃上游解码 | 暂不迁 |
| 文字与编码 | CP932/CP936/CP950 三张表，SJIS/UTF-8/GBK/BIG5 四模式管线 | 字体渲染可接入现有实现，上游脚本文本解析目前支持 CP932 和 UTF-8 | 原型接入字体渲染。正式应用的 GBK/CP950 继续走分叉核心 |
| 服务与对话框 | 正式应用已有消息框和输入界面 | 原型已接入上游消息框请求，保留引擎绘制输入表单的能力 | 消息框待真机验证 |

文件、画布、输入、音频、消息框和字体渲染已在原型接入。接入原应用前还需验证启动配置、GBK/CP950 脚本文本和影片支持。

