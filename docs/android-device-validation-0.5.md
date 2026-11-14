# Android 0.5.0 宿主迁移与输入验证

日期：2026-11-12；设备：小米 8，Android 10 / API 29，ARM64。
本轮为本地测试版本，未公开发布，也不代表 P1 全部完成。

## 改动

NativeActivity 已替换为 GameActivity + SurfaceView + JNI。引擎仍在独立
`:engine` 进程的一个工作线程运行；Java 仅排队输入与传递生命周期状态。
窗口绘制/释放共用锁，触摸反变换保留原 viewport；移动事件合并，队列溢出
会释放旧输入状态，避免按键卡住。每次会话结束后退出引擎进程，避免复用 VM 全局状态。

OS_Edit* 接上透明 EditText：位置、字号/字宽比例、选区、焦点、颜色、
字节限制、禁止 ASCII、销毁、IME 状态和编码后的文字回传。revision 防止旧 UI
回调覆盖脚本刚设置的内容。拼音组合期间保留 composing spans，提交后再限制字节。
UTF-8 通过 byte[] 传 JNI，避免修改版 UTF-8 对非 BMP 字符的影响。

OS_DialogInput1/2 和 OS_DialogProfile 使用 Android 表单，取消不写输出；
姓名按各字段 10 字节限制并检查全角，生日使用从零开始的月/日索引，二月 29 天。
横屏键盘上方表单占满可用区域，较长内容可滚动。
OS_MessageBox 不再自动点确认，四种按钮组合等待真实选择；销毁会唤醒等待线程。

onPause 使工作线程在消息泵等待条件变量，现有 AAudio 前台开关停止混音拉取。
OS_TicksMs 和 OS_PerfCounterNs 共同扣除后台时间；OS_LocalTime 保持真实时间。

## 验证证据

独立包 org.openbgi.hosttests 链接相同的 Android 后端，通过 --wrap=Engine_Main
运行自写测试，不包含游戏。普通包不链接此包装函数。

13 项通过：YESNO 否/是、OKCANCEL 取消、RETRYCANCEL 重试；ASCII 六字节上限；
GBK 中文拼音 zhongwen → 中文 + AB；SJIS 日文及 UTF-8 中文/emoji 编辑往返；
后台停留期间毫秒与高精度时钟完全不变；单栏取消保留输出、单栏确认按字节限制、
双栏字段独立、姓名和二月日期回传。

GameTextCodecTest 的 10 项主机检查覆盖三种编码、替换选区、不可编码字符、
UTF-8 标量/代理对边界、ASCII 限制和控制字符。

Fureraba：旧普通档已读回，另在空槽 FriendToLoverHD001.sud 保存中文备注“中文AB”；
原 FriendToLoverHD005.sud 保留。Kanojo Step：升级前当前场景已快速保存并另行备份，
新宿主冷启动能读回。迁移初版两部启动、触摸、后台返回画面已验证。
最终包 0.5.0：Fureraba001 中文备注在冷启动列表中保留，成功读回；
Kanojo Step206 快速档也已冷读回到升级前场景。旧005与206存档和升级前备份逐字节一致。
后台 CPU 采样保存到本地 p1-vm-background.json；原生线程名在此 ROM 的 proc 视图中
未能辨认，不将该采样解释为逐线程证明。条件变量等待与冻结时钟由独立测试验证。
布局修改后四项输入表单复测全部通过。

主机 19 组仍为 17 组通过，bpc 和 win32wide 为原有失败，未宣称全绿。
本轮不重复崩溃信号注入；之前的日志导出功能保留，stderr 捕获未移除。

## 限制与下一步

渲染仍为原 CPU 缩放；音频焦点/真实来电/耳机拔出、更多输入映射、启动器设置、
EGL 均未完成。混合编码的默认姓名/设置乱码依然存在。
只测了一个设备和一个中文输入法，不能代替所有作品的姓名流程与全流程验证。
Android 杀死进程后靠游戏存档恢复，尚无 VM 内存快照恢复。

## 重现宿主测试

按 tools/android/README.md 设置 NDK、FreeType、Ogg/Vorbis、SDK 环境。
使用相同构建参数增加 ANDROID_HOST_TEST=1，再执行
`sh tools/android/package-hosttest.sh`，安装 bin/android-hosttest/openbgi.apk。
独立包需要 files/active-game.txt 内容 hosttest，以及
files/games/hosttest/ipl._bp（空占位）和 .openbgi 目录。
从独立包启动器选择该测试目录；不要使用真实游戏数据。
正常顺序按英文提示选按钮、输入、后台停留再返回。可放置游戏目录文件 forms-only
仅测试四个输入表单。详细结果写入该测试包自己的 files/logs/engine-current.txt。
测试安装和读取私有测试日志采用已授权 root ADB；正式应用功能不需要 root。

纯编码检查可用 Java 8+：编译 GameTextCodec.java 与 GameTextCodecTest.java，
运行 org.openbgi.port.GameTextCodecTest；测试类不打入普通 APK。
