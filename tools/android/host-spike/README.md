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

## 迁移评估（2026-11-25）

**契约规模** 上游 v1.0 的 `openbgi.h` 共 18 组宿主表、155 个回调指针，全部可为 NULL（缺失即该项失败）。与本分叉相关的是：`Files` 39、`Window` 26、`TextEntry` 14、`FontRenderer` 12、`Machine` 11、`ChildWindows` 8、`Shell` 8、`CdAudio` 6、`FontFiles` 5、`MoviePlayer` 5、`MovieDecoder` 5、`Registry` 4、`DebugWindow` 3、`MovieFile` 2。原型只接通了 `Files` 的读取、`Window`、触摸与按键、`OpenBGI_AudioRead` 和 `NextRequest`/`Answer`，`host.c` 全篇 343 行——足以证明契约能跑通，不足以说明可以整体替换。

| 层 | 分叉现状 | 迁移成本 | 结论 |
| --- | --- | --- | --- |
| 文件 | `src/os/posix/*` 已有路径、属性、查找、时间和原生路径分离，外加 ISO/UDF/ZIP 只读容器与覆盖包 | 小：把现有实现包成表；Android 上 `nativeSep` 取 `/`、`nativeSjis` 取 0 | 先做。上游后续的存储修复可复用，风险最低 |
| 画布 | `renderer.c` 走 EGL/GLES 纹理上传与 viewport 缩放 | 中：把 `OpenBGI_Frame_t` 的像素、行距、脏矩形接到现有 GL 管线，保留缩放与坐标换算 | 第二位，先做主机像素对照 |
| 输入 | `window.c` 的事件队列加 Java 手势与长按映射 | 小到中：映射 `Pointer`/`Button`/`Key`/`Wheel`/`Focus`；IME 另走 `TextEntry` | 与画布同期，IME 是独立子项 |
| 音频 | `audio_output.c` 用 AAudio，自带重采样 | 中：`AudioRead`/`RunAudio`/`SetAudioLead` 换混音输入；上游按 44.1 kHz 取数，与现有输出协商不同 | 后置，需按同一素材复核音轨 |
| 影片 | `movie.c` 自带 FFmpeg 后端 | 大：要实现 `OpenBGI_MovieDecoder_t`（含画面与音轨），否则放弃上游解码 | 暂不迁 |
| 文字与编码 | CP932/CP936/CP950 三张表，SJIS/UTF-8/GBK/BIG5 四模式管线 | **阻塞项**：上游 `textEncoding` 只有 UTF-8 与 Shift-JIS 两种状态，`typeface` 只认字符集码页，不提供 GBK/CP950 的语句文本转换 | 不迁。迁掉等于丢汉化兼容 |
| 服务与对话框 | `services.c` 是阻塞式对话框加 Java 界面 | 中：改成 `NextRequest`/`Answer` 的请求应答模型，界面复用 | 可与输入同期 |

**结论** 不整体迁移。上游 host API 的价值是让引擎核心不碰 OS，而本分叉的差异化——容器直读、汉化编码、Android 音频与影片后端——正好都在 host 侧；换掉 host 侧等于把这些能力重写一遍。只在需要跟进上游修复时按层对齐：先文件，再画布与输入，音频随后；影片与文字不迁。将来若要迁文字，前提是上游提供可插拔的文本编码钩子（GBK/CP950 双向转换），否则本分叉必须保留自己的转换管线。

