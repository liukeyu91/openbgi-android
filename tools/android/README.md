# Android 使用与构建

## 运行要求

Android 9 及以上的 ARM64 设备。应用只提供引擎，游戏资源需自行准备。

## 使用方法

1. 安装 APK，在游戏库选择“添加游戏”。
2. 选择已解压、包含 `system.arc` 或 `ipl._bp` 的完整游戏根目录。
3. 在游戏设置中确认编码、引擎配置、缩放和操作方式，然后启动游戏。

导入会复制全部目录内容，需要足够的存储空间。编码自动检测无法确定时，可使用编码预览手动选择。
更改设置后需重启游戏。游戏内快捷键的实际作用取决于游戏本身。

导入完成后会显示程序指令检查结果。已有游戏可从卡片菜单选择“兼容性检查”重新扫描。
检查列出 `._bp` 程序中的缺失指令、文件名和位置；无法完整识别时会注明。结果按游戏保存，静态检查不代表完整流程兼容。

覆盖安装需使用相同包名和签名密钥；卸载会清除应用内数据。
主应用与独立 UI Preview 的数据彼此独立。运行日志可在关于页导出。

## 构建环境

- Android NDK、SDK Platform 35、Build Tools 35.0.0、JDK 17。
- CMake、Make，以及 FreeType、libogg、libvorbis 源码。
- FreeType 使用 GPLv2 许可选项；字体从 Android 系统中读取。

设置 `ANDROID_NDK_HOME`、`ANDROID_SDK_ROOT`、`FREETYPE_SOURCE`、`OGG_SOURCE` 和 `VORBIS_SOURCE`。
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
make android-native ANDROID_NDK_HOME="$ANDROID_NDK_HOME" \
    FREETYPE_INCLUDE="$FREETYPE_SOURCE/include" FREETYPE_LIB="$FT_BUILD/libfreetype.a" \
    ANDROID_VORBIS_PREFIX="$CODEC_PREFIX"
ANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" FREETYPE_SOURCE="$FREETYPE_SOURCE" \
    OGG_SOURCE="$OGG_SOURCE" VORBIS_SOURCE="$VORBIS_SOURCE" \
    sh tools/android/package-smoke.sh
```

主应用输出为 `bin/android/openbgi.apk`。同一构建环境下执行
`sh tools/android/package-ui-preview.sh` 可生成独立预览包。
打包脚本使用本地测试签名密钥；后续覆盖安装需保留同一密钥。

## 开发检查

- `make test`：引擎测试。
- `sh tools/android/tests/run-encoding-tests.sh`：编码检测与配置。
- `sh tools/android/tests/run-compatibility-tests.sh`：程序收集、归档覆盖和检查结果。
- `sh tools/android/tests/run-input-tests.sh`：输入、设置与坐标变换。
- `sh tools/android/tests/run-text-repair-tests.sh`：文字转换与输入限制。

主机检查不能代替实际设备及具体游戏的兼容性验证。
结构及兼容性概述见 [Android 概述](../../docs/android.md)。
