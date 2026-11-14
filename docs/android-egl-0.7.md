# Android 0.7.0：EGL/GLES2 显示与真机回归

日期：2026-11-12。本地测试版，versionCode 9，ARM64 / Android 9+ / GLES2。
设备：小米 8，Android 10，Adreno 630。覆盖安装 0.6.0，签名保持一致。

## 实现范围

原实现每次展示都锁住 ANativeWindow，按物理屏幕大小逐像素执行 CPU 最近邻缩放。
现在保留引擎已有的逻辑 RGBA 画布，首次/尺寸变化/上下文重建时完整上传，
后续将本帧改动合并为一个矩形，通过 glTexSubImage2D 上传。
ES2 不依赖行跨度扩展：窄矩形先复制到连续缓冲，全宽矩形可直接上传连续画布行。
GPU 以最近邻采样缩放画布，重新绘制完整四边形和黑边，因此不依赖交换后缓冲内容保留。
黑色填充的 alpha 在显示着色器中固定为 1。

Java Choreographer 在前台运行且有 Surface 时发帧信号，原生端每个新信号最多提交一帧，
并设置 EGL swap interval 1。静止画面不上传、不交换缓冲；后台停止帧回调。
引擎 VM、合成器、源像素转换仍使用 CPU，未声称整套引擎变为 GPU 渲染。

所有 EGL/GLES 调用在引擎线程执行，驱动调用期间不持有宿主 UI/生命周期锁。
渲染器单独持有 ANativeWindow 引用；Surface generation 变化后重新创建 EGL 资源。
后台消息泵暂停时释放 EGL；恢复后从逻辑画布完整重建，保留未展示的变化。
驱动返回错误时记录日志、清理资源，一秒后重试。实际 GPU 驱动重置没有故障注入验证。
渲染、触摸和 Android 编辑控件继续共用 0.6.0 的布局计算。

API 依据：[Android Choreographer](https://developer.android.com/reference/android/view/Choreographer)、
[Khronos eglSwapInterval](https://registry.khronos.org/EGL/sdk/docs/man/html/eglSwapInterval.xhtml)。

## 实测证据

独立宿主只使用自有色块，不需要游戏。保持比例、整数倍、拉伸三种模式各通过同一组 16 项检查：

- RGB 通道与行方向：GPU 纹理读回和手机屏幕截图四象限均为上红/绿、下蓝/白。
- 10×7 窄矩形上传恰为 280 字节，邻接像素保持原样。
- 负坐标裁剪、16 位 RGB555 转换、缩放裁剪、全屏黑色填充均符合预期。
- 画布 1280×720 → 640×360 → 1280×720 重分配、内容恢复通过。
- 静止 150ms 未增加交换次数；250ms 连续请求仅提交 15–16 帧，对应 14–16 个新帧信号，
  允许测试开始时已有的一个尚未使用信号。这是提交限帧检查，不是整部游戏稳定帧率基准。
- Home 暂停三秒时引擎时钟冻结；恢复后等待一个新的呈现帧，纹理内容和完整上传计数正确。
- 驱动错误计数为零，GL 错误检查通过。

另完成：

- 独立宿主 13 项交互回归再次 ALL PASSED：四种对话框返回、ASCII/GBK/SJIS/UTF-8 编辑、
  中文拼音候选提交、字节长度、单栏/双栏/生日表单、后台时钟冻结。
- 48 项输入/设置检查及 270 组 C/Java 布局计算对照再次通过。
- 主应用两部游戏启动、原普通存档读回；Kanojo Step 快速存档恢复；屏幕存读档键、Fureraba 回看。
- 主应用连续五次 Home/前台恢复，以及 NOISY 广播暂停与手动继续通过，未出现黑屏或渲染错误日志。
- 测试后六个 `.sud` 与升级前备份 SHA-256 完全相同。手机回到测试前的 Kanojo Step 快速存档对白。
  PC 原始游戏未修改，临时独立宿主包已卸载。
- 原生/Java 编译、APK 签名/对齐验证；发布包排除测试入口、个人存档和密钥。

工作目录证据：`work/runtime/egl-hosttest-results.txt`、`egl-hosttest-integer.txt`、
`egl-hosttest-stretch.txt`、`egl-hosttest-13-results.txt`、`egl-validation-summary.json`、
`egl-quadrants-*.png`、`egl-main-final-log.txt`、`egl-baseline/comparison.json`。
游戏画面和存档备份仅保留本地，不纳入公共源码。

## 复现和限制

按 tools/android/README.md 构建。原生库新增链接系统 libEGL、libGLESv2。
独立宿主构建方法沿用 0.6.0；fixture 游戏根目录放 `render-only` 空文件即运行 EGL 检查。
该模式最后要求 Home 三秒并返回；删除此标记则执行原 13 项交互测试。
fixture `.openbgi/settings.ini` 的 scale=0/1/2 可复测不同缩放。
hosttest.c 的 GL 读回、压力循环与 Engine_Main 替代入口不进入生产库。

当前实测仅一个 GPU/系统。Mali、其他 Adreno、刷新率切换、长时间温度/功耗和真实驱动 context loss
仍需验证；不能根据提交次数宣称功耗改善百分比或全程稳定 60fps。
主机完整套件最近基线仍为 17/19，bpc、win32wide 两组失败未在本轮处理。
P1 后续为编码抽样探测、首句预览和混合编码；实体手柄和音频硬件验证也继续保留。
