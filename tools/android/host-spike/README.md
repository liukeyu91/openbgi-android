# P2-1 Android host API 原型

P2 迁移以现有 Android 分叉为基线。接入上游组件后，用原应用、完整引擎配置和完整汉化资源做回归。

原型使用上游 v1.0（`257a20d`）的 `openbgi.h` 和 `libopenbgi-arm64.so`。测试 APK 的包名是 `org.openbgi.hostspike`，内置一个合成 ZIP。启动后画面为蓝色，按住画面会变绿并播放短音，松手恢复蓝色。

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
sh tools/android/host-spike/package.sh /path/to/upstream-v1.0 /path/to/spike-build
```

APK 使用试验目录中的调试密钥。可以通过 `adb logcat -s OpenBGIHostSpike` 查看启动、首帧、触碰坐标和 AAudio 状态。

## Android 适配映射

| 现有模块 | 上游接口 | 迁移 |
| --- | --- | --- |
| `activity.c`、`window.c` 的调度与 OS 窗口画布 | `OpenBGI_Start`、`RunFrame`、`PumpEvents`、`Stop` | 将 OS 循环和画布改为 host 驱动。保留现有 Java Activity 和 JNI 生命周期入口。 |
| `renderer.c`、`viewport.h` | `OpenBGI_Frame_t` 的像素、行距和脏矩形 | 保留 EGL/GLES、缩放和坐标换算。改为从上游帧上传纹理。原型用 CPU 输出验证帧契约。 |
| `window.c` 的事件队列 | `OpenBGI_Pointer`、`Button`、`Key`、`Wheel`、`Focus` | 保留 Java 手势与按键映射。用 host 调用替换旧原生状态表。 |
| `audio_output.c` | `OpenBGI_AudioRead`、`RunAudio`、`SetAudioLead` | 保留 AAudio 输出与现有采样率转换。替换混音输入；原型请求上游原生的 44.1 kHz。 |
| `services.c` | `OpenBGI_NextRequest`、`Answer`、`Text` 和帧里的 `textRect` | 将阻塞式 OS 对话框和 IME 调用改为 host 请求。复用 Java 对话框和输入界面。 |
| `storage.c`、POSIX 路径层 | `OpenBGI_Files_t`、`--overlay`、`--patch` | 保留游戏库、存档导入导出和汉化兼容层。把只读来源、私有可写目录和覆盖包注册到 host。 |
| `movie.c` | `OpenBGI_MovieDecoder_t` | 评估由上游 FFmpeg decoder 接管；验证 Android 暂停与跳过。 |
| `sjis.c`、`compat_jni.c`、`crash.c` | `OpenBGI_FontFiles_t`、公开 profile 接口、host 错误与日志 | 字体供应和日志归 host。GBK/Big5 文本、指令预检及本地引擎差异分别核对后再迁。 |

