# Android 常规影片

Android 将 FFmpeg 8.1.3 的 avformat、avcodec、avutil、swscale、swresample 静态链接到 `libopenbgi.so`。构建关闭网络、GPL/nonfree 扩展与自动发现，只开放本地文件协议；不调用 PATH 中的可执行程序。FFmpeg 使用 LGPL 2.1 许可，许可证位于 APK 的 `assets/licenses/FFmpeg-LGPLv2.1.txt`；完整对应源码由依赖锁文件的官方下载地址和 SHA-256 标识，配置与构建步骤位于 `tools/ci/ffmpeg-build.sh`，未修改第三方源码。

`src/sys/movieav.c` 使用独立的解封装、视频和音频线程：每个流的压缩包队列最多 128 包 / 32MiB，视频使用三个 BGRA 缓冲帧，音频使用两秒 44.1kHz 立体声环形缓冲。分离解封装与解码，避免 MPEG 连续视频包阻塞音频补给。最大画面为 4096×2160 像素，输入探测设超时，异常数据返回错误。音轨经 swresample 转成混音器格式，再由现有 Android 输出层适配设备格式。音频消费位置作为时钟，无音轨或混音器时使用可暂停的引擎时钟。

`src/os/android/movie.c` 将帧画到原有游戏画布，通过原有完成通知结束影片。视频时钟扣除 AAudio 已写入但尚未读取的输出帧延迟，降低设备缓冲造成的画面超前。暂停不推进播放时钟；解码线程只预读到缓冲区上限。停止先撤下混音回调，再唤醒并回收解码线程，避免释放后访问。点击、Esc、Enter 在起播 200ms 后可跳过，结束后恢复游戏画面。`92 F0` 直接读取游戏档案中的影片条目，按脚本指定的矩形播放。

支持的解封装器和解码器以构建脚本中的明确列表为准，包括常见 MPEG-1/2、MPEG-4、H.264、WMV/VC-1、AAC、MP2/MP3、WMA；不包括 DRM 或 Windows 外部滤镜。游戏脚本驱动的 BF_Movie 帧序列由引擎解码。`92 F1` 请求流式播放时，`0x10000` 和 `0x10001` 只预读文件头和索引，解码时逐帧读取。

验证：`tools/ci/test-movieav.sh` 使用真实编码的合成素材测试解码生命周期；`tools/ci/movie-check.sh` 在固定构建环境中执行普通及 ASan/UBSan 检查。测试不代表所有游戏影片已在 Android 上实测，也不覆盖 FFmpeg 内部的 Sanitizer 检查。
