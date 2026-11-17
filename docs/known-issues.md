# Android 1.0 支持范围与已知限制

应用提供 BGI / Ethornell ARM64 引擎，要求 Android 9 及以上；不包含游戏。版本号 1.0 不代表所有作品、引擎指令或汉化补丁均已兼容。

- 原目录运行需要用户授予存储访问，目录须可读写；原目录移动或存储卡离线后需恢复位置。游戏库“移除”保留原文件。旧的应用内副本不会自动迁移或删除。
- 新增原目录流程、汉化字体适配和大图修复已通过主机测试及 Android 编译，尚未完成用户手机上的新一轮场景与路线回归。MakingHD/KanojoStep 的完整流程没有在本次发布中重新验证。
- 普通大 CBG 图片等待加载可在后台读取解码；最终位图创建以及快进中的同步加载仍可能造成停顿。
- 已知 EVB、内嵌 EVB、CPK v2 资源及声明的编码/字体要求可适配；Windows DLL 的任意逻辑、未知字符映射和引擎修改不会自动执行。混合编码与特殊排版需按实际补丁验证。
- 补丁更新仅自动替换有历史归属记录且未被修改的生成资源，保留手工补译；缺少记录的旧覆盖文件不会自动认领。
- 常规影片通过内置 FFmpeg 播放；流式 BF_Movie 帧序列仍不支持。兼容性静态检查不能证明完整流程可运行。
- 主包与独立 UI Preview 包使用不同的应用数据。更新需覆盖相同包名，签名保持原证书；复制导入的应用私有存档应通过 ZIP 导出保存。

English: Android 1.0 is an ARM64 BGI/Ethornell engine for Android 9+. It does not guarantee compatibility with every title or translation DLL. The new direct-folder flow, translation font adapters and large-image fixes passed host tests and Android builds, but have not received a fresh on-device/full-route regression pass. Fast-forward synchronous loads may still stall; arbitrary Windows DLL logic, unknown character maps and streamed BF_Movie sequences remain unsupported. Existing copied imports are not automatically migrated or deleted.
