# Android 使用与构建

## 运行要求

Android 9 及以上的 ARM64 设备。应用只提供引擎，游戏资源需自行准备。

## 使用方法

1. 安装 APK，在游戏库选择“添加游戏”。
2. 选择已解压的完整游戏根目录。全功能包可在授权后直接运行，SAF 包通过系统文件选择器复制导入。
3. 也可选择“使用光盘镜像（ISO）”或“使用游戏压缩包（ZIP）”，在存储中选取 ISO/UDF 镜像或 ZIP 文件，游戏直接从来源运行，不复制、不解包。
4. 在游戏设置中确认编码、引擎配置、缩放和操作方式，然后启动游戏。

直接目录模式原地读取游戏资源，存档和展开的汉化资源写入应用存储。SAF 包只读取所选目录，导入中断后重新打开应用可重试。两种包的游戏库和存档独立。
镜像与压缩包同样原地读取：添加时只记录来源路径与索引摘要，启动时由原生层挂载来源。存档、设置与展开资源写入该条目的私有目录，来源自带的存档在添加时复制过去。ZIP 的 Stored 条目直接读取，Deflate 条目在首次打开时解压并校验 CRC-32，支持 ZIP64 与未列出的父目录；单条目解压上限 256 MiB。两者都不能就地展开汉化资源，请改用覆盖包（ZIP）。来源文件被替换或移走后条目仍在，但需要重新添加才能启动。
编码自动检测无法确定时，可使用编码预览手动选择。
汉化资源的展开在手机本地完成，支持外部 EVB、EXE 内嵌 EVB 与配套密钥的 CPK v2；原生 ARC 汉化直接使用。已知配置由共享 JSON 生成，会应用编码与引擎设置。无需电脑转换或安装 Python。
已有游戏可从卡片菜单选择“处理汉化补丁”，需先退出游戏。已有根目录覆盖资源与存档会保留；包内 Windows EXE、DLL 和存档不会作为展开资源写入。尚未识别的运行时字体/逻辑钩子仍需适配，资源解包成功不代表全路线兼容。
0.10.21 修正 EVB 压缩前后长度相同时的误判。此前已导入的游戏，退出游戏后再运行一次“处理汉化补丁”；只替换与补丁包中错误压缩载荷完全相同的旧文件，保留其他覆盖资源与存档，替换失败会恢复原文件。
更改设置后需重启游戏。游戏内快捷键的实际作用取决于游戏本身。

导入完成后会显示程序指令检查结果。已有游戏可从卡片菜单选择“兼容性检查”重新扫描。
检查列出 `._bp` 程序中的缺失指令、文件名和位置；无法完整识别时会注明。结果按游戏保存，静态检查不代表完整流程兼容。

覆盖安装需使用相同包名和签名密钥；卸载会清除应用内数据。
主应用与独立 UI Preview 的数据彼此独立。运行日志可在关于页导出。

## 补丁体检

安装 ZIP 或文件夹覆盖包时，应用先检查文件冲突、ARC 文件头和索引、目标游戏和抽样文本。警告可以确认后安装，拒绝会保留当前补丁。上次体检可在游戏的「汉化覆盖包」菜单查看。

补丁根目录的 `.openbgi/patch.json` 可提供以下可选字段。 `bootSha256` 使用原游戏解压后的 `ipl._bp` 的 SHA-256，`engine` 使用 OpenBGI 引擎配置名。

```json
{
  "target": {"product": "LovepicalPoppy", "engine": "1.680"},
  "encoding": "big5",
  "fontFamily": "Noto Sans CJK TC",
  "fontSize": 24,
  "textBoxWidth": 760
}
```

编码按场景文本抽样。字形检查读取声明或选定字体的字符映射。只有同时声明字号和文本框宽度时才给出行宽估算，实际断行和字体回退需在游戏中查看。

## 将安装器转成覆盖 ZIP

在电脑上使用 Python 3.11 或更新版本。NSIS 用 [7-Zip](https://www.7-zip.org/)，Inno Setup 用 [innoextract](https://constexpr.org/innoextract/)。在仓库根目录运行以下命令，再在游戏的「汉化覆盖包」菜单导入 `patch.zip`。

```sh
python tools/translation-compat.py installer "PatchSetup.exe" --out "patch.zip"
```

转换器提取安装器内已有的完整文件，保留相对游戏根目录的路径。NSIS 优先使用 `$INSTDIR`，Inno 使用 `{app}` 对应的 `app`。目录布局不同时用 `--root` 指定解包目录内的资源根。可用 `--select 'Archive/*'` 或 `--exclude '*.txt'` 筛选文件，也可用 `--manifest patch.json` 附带编码、字体和目标信息。可用 `--format nsis` 或 `--format inno` 选择格式，并用 `--extractor` 指定工具路径。

工具只读取安装器里的文件。仅有差分或依赖安装脚本生成资源的补丁，需要先取得完整的游戏资源文件。

## 构建环境

- Android NDK、SDK Platform 35、Build Tools 35.0.0、JDK 17。
- CMake、Make，以及 FreeType、libogg、libvorbis、FFmpeg 源码；版本与 SHA-256 固定在 `tools/ci/dependencies.json`。
- FreeType 使用 GPLv2 许可选项；字体从 Android 系统中读取。

设置 `ANDROID_NDK_HOME`、`ANDROID_SDK_ROOT`、`FREETYPE_SOURCE`、`OGG_SOURCE`、`VORBIS_SOURCE` 和 `FFMPEG_SOURCE`。
另为中间构建目录设置 `FT_BUILD`、`CODEC_BUILD` 和 `CODEC_PREFIX`。在项目根目录执行：

```sh
cmake -S "$FREETYPE_SOURCE" -B "$FT_BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF \
    -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_HARFBUZZ=ON \
    -DFT_DISABLE_PNG=ON -DFT_DISABLE_ZLIB=ON
cmake --build "$FT_BUILD"
for codec in "$OGG_SOURCE" "$VORBIS_SOURCE"; do
    build="$CODEC_BUILD/$(basename "$codec")"
    cmake -S "$codec" -B "$build" \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_INSTALL_PREFIX="$CODEC_PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_PREFIX_PATH="$CODEC_PREFIX"
    cmake --build "$build"
    cmake --install "$build"
done
mkdir -p "$FFMPEG_BUILD"
(cd "$FFMPEG_BUILD" && sh "$OLDPWD/tools/ci/ffmpeg-build.sh" "$FFMPEG_SOURCE" "$ANDROID_FFMPEG_PREFIX" android)
make android-native ANDROID_NDK_HOME="$ANDROID_NDK_HOME" \
    FREETYPE_INCLUDE="$FREETYPE_SOURCE/include" FREETYPE_LIB="$FT_BUILD/libfreetype.a" \
    ANDROID_VORBIS_PREFIX="$CODEC_PREFIX" ANDROID_FFMPEG_PREFIX="$ANDROID_FFMPEG_PREFIX"
ANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" FREETYPE_SOURCE="$FREETYPE_SOURCE" \
    OGG_SOURCE="$OGG_SOURCE" VORBIS_SOURCE="$VORBIS_SOURCE" FFMPEG_SOURCE="$FFMPEG_SOURCE" \
    sh tools/android/package-smoke.sh
```

全功能包输出至 `bin/android/openbgi.apk`。同一构建环境下运行 `ANDROID_STORAGE_VARIANT=saf sh tools/android/package-smoke.sh`，用同一把签名密钥生成 `bin/android/openbgi-saf.apk`。
原有独立 UI Preview 仍可用 `sh tools/android/package-ui-preview.sh` 构建。
正式打包必须显式提供工程外的持久签名密钥和密码环境变量，并通过已固定的证书 SHA-256 校验；脚本不会自动生成替代密钥。详见 [签名、CI 与可复现构建](../../docs/build-integrity.md)。

## 开发检查

`tools/ci/movie-check.sh /tmp/movie-check` 从固定源码构建主机 FFmpeg，并对 MPEG / WMV / MP4、音频重采样、静音、无声影片、变帧率、暂停和阻塞中止运行实际解码测试及 ASan / UBSan 检查。检查覆盖引擎适配层；第三方 FFmpeg 静态库在该检查中未启用 Sanitizer。Android 实机的输出延迟和各游戏片头仍需设备验证。

- `make test`：引擎测试。
- `sh tools/android/tests/run-disc-image-tests.sh`：镜像与 ZIP 索引、Deflate 读取、档案语言、抽样、存档迁移与摘要。
- `sh tools/android/tests/run-encoding-tests.sh`：编码检测与配置。
- `sh tools/android/tests/run-compatibility-tests.sh`：程序收集、归档覆盖和检查结果。
- `sh tools/android/tests/run-input-tests.sh`：输入、设置与坐标变换。
- `sh tools/android/tests/run-text-repair-tests.sh`：文字转换与输入限制。

主机检查不能代替实际设备及具体游戏的兼容性验证。
结构及兼容性概述见 [Android 概述](../../docs/android.md)。

### 日文字体比例回归

主机上的 `bin/test_androidfont` 直接编译 Android 使用的 FreeType 绘字和 CP932 解码源码，比较同一字体、同一汉字在 SJIS、GBK、UTF-8 下的像素、字高和字符步进。执行前设置 `BGI_TEST_CJK_FONT` 为本机 Noto Sans CJK 或 Noto Serif CJK 字体文件的路径；未提供时该项会明确显示跳过。测试字体由开发者自行提供，不打包进 APK。

```sh
make bin/test_androidfont
BGI_TEST_CJK_FONT=/path/to/NotoSansCJKjp-Regular.otf bin/test_androidfont
```

### 缩放过滤与音频输出

实际视口为相同整数倍放大时使用最近邻；非整数倍、缩小或两个轴比例不同时使用线性过滤。过滤方式会随窗口、画布和缩放模式变化自动调整。

Android 音频保留混音器原采样率，接受设备协商出的采样率，在输出端连续线性重采样；可转换立体声/单声道、I16/浮点格式。打开失败会尝试设备原生采样率及备用格式，前台按秒重试，并记录请求参数、协商结果、错误码和写入失败。输出不可用时界面提示可导出日志。
