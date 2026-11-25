# Android 支持范围与已知限制

OpenBGI 为 Android 9 及以上的 ARM64 设备提供 BGI / Ethornell 引擎，游戏资源需自行准备。

- 原目录运行需要存储访问权限。原目录移动或存储卡离线后需恢复位置。游戏存档保存在应用内，移除条目前可先导出。
- SAF 包通过系统选择器复制导入，可与全功能包同时安装。两者各自保留游戏库和存档，可用存档 ZIP 转移进度。
- `1.0.1-android` 已通过主机回归及 Android 构建。SAF 导入和启动、1.680 模糊场景，以及各游戏完整路线仍待真机复测。
- 磁盘镜像与游戏压缩包可原地直读，不复制资源。来源被移动、重命名或存储卡离线后，游戏条目与存档保留，把来源放回原位置即可继续启动；压缩包支持 Stored 与 Deflate，加密条目和 BZIP2／LZMA 等其他压缩方式会列出但读不出来，添加时会点出这些条目并说明原因。
- 单个压缩条目上限 256 MiB，超过的条目会被拒绝并提示重新打包；4 GiB 以上的实盘压缩包、UDF 实盘镜像以及替换来源后的表现尚未在真机验证。
- 普通大 CBG 图片等待加载可在后台读取解码；最终位图创建以及快进中的同步加载仍可能造成停顿。
- 已知 EVB、内嵌 EVB、CPK v2 资源及声明的编码/字体要求可适配；Windows DLL 的任意逻辑、未知字符映射和引擎修改不会自动执行。混合编码与特殊排版需按实际补丁验证。
- 补丁更新仅自动替换有历史归属记录且未被修改的生成资源，保留手工补译；缺少记录的旧覆盖文件不会自动认领。
- 常规影片通过内置 FFmpeg 播放。BF_Movie 帧序列的真机播放待验证。兼容性静态检查不能证明完整流程可运行。
- 全功能包、SAF 包和 UI Preview 各有独立的应用数据。覆盖更新需保留包名和签名证书。迁移存档可使用 ZIP 导出和导入。

English: OpenBGI is an ARM64 BGI/Ethornell engine for Android 9 and later. For `1.0.1-android`, SAF import and launch, the 1.680 blur scene, and full game routes still need device testing. Disc images and ZIP archives are read in place; entries compressed with anything but Stored or Deflate, and encrypted ones, are listed but cannot be read, and a single entry stops at 256 MiB. Synchronous asset loads can stall during fast-forward. The engine can use supported translation resources, encoding and font settings. Windows DLL logic and unknown character maps remain unsupported. BF_Movie playback still needs device testing.
