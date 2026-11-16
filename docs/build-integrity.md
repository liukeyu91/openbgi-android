# 签名、存档与构建保障

## 更新身份与存档

Android 覆盖安装必须保留包名和签名证书。现有主包和 UI 预览包的公钥证书摘要记录在 `tools/android/release-certificates.json`，没有更换既有身份。正式打包默认运行证书预检，缺密钥、错误密码或不同证书都会失败，不会创建替代密钥，不会覆盖已有 APK。

签名私钥必须在工程目录之外；`make clean` 如发现遗留的 P12/PFX/JKS/keystore，会在删除任何目录之前拒绝清理。`.gitignore` 是防误提交措施，不能代替私钥保管。维护者必须另行保留可恢复的离线副本，并妥善保存密码。同一电脑的两个目录副本只能防工程清理，不能抵御磁盘损坏或账号丢失。

Windows 可用 `tools/android/Invoke-AndroidPackage.ps1` 加载 `%USERPROFILE%\.openbgi\signing` 中的密钥及 `release.credential.xml`。该凭据应由维护者通过 `Get-Credential | Export-Clixml` 创建；它绑定 Windows 用户，不是可跨机器恢复的密码备份。不要将私钥、密码或凭据上传到 GitHub。本仓库 CI 不接触正式签名私钥。

手动发布环境：

```sh
export OPENBGI_KEYSTORE=/protected/outside-checkout/android.p12
export OPENBGI_KEY_ALIAS=openbgi-smoke
# 通过安全凭据管理器注入 OPENBGI_STORE_PASS；不要把密码写入命令历史。
sh tools/android/package-smoke.sh
```

分叉项目若使用自己的身份，必须显式设置 `OPENBGI_EXPECTED_CERT_SHA256`，并理解它不能覆盖安装原包。原项目维护者不应使用该变量绕过固定证书检查。

**卸载仍会删除应用私有存档。** 签名稳定允许覆盖更新，不能改变 Android 卸载行为。卸载、换设备或尝试不同签名包前，使用游戏库的“导出存档”保存到应用外，再用“导入存档”恢复。CI 的存档测试验证导出/导入逻辑，不等同于每种手机文件提供器的实测。

## 固定环境与可复现产物

`tools/ci/Dockerfile` 固定 Debian 镜像摘要与签名软件包快照；`tools/ci/dependencies.json` 固定 NDK r27d、SDK Build Tools 35、API 35、FreeType、Ogg、Vorbis 的下载地址和 SHA-256。缓存也每次验证，不匹配即失败。构建使用 API 28/ARM64。

```sh
docker build -t openbgi-build -f tools/ci/Dockerfile .
mkdir -p ../deps ../reproduction
docker run --rm -v "$PWD:/src:ro" -v "$PWD/../deps:/deps" openbgi-build sh tools/ci/android-deps.sh /deps
docker run --rm -v "$PWD:/src:ro" -v "$PWD/../deps:/deps:ro" -v "$PWD/../reproduction:/reproduction" openbgi-build python3 tools/ci/reproduce.py /reproduction /deps
```

重建程序复制同一提交到两个不同长度的路径，各自从空 obj/bin 构建。源提交时间作为 `SOURCE_DATE_EPOCH`，编译器映射源码绝对路径，ZIP 条目顺序、权限和时间戳统一。两次生成的 ELF、DEX 和对齐后的**未签名 APK** 必须逐字节相同，结果写入 `reproducibility.json`。比较失败即 CI 失败。该保证限定于固定 Linux/amd64 工具链，不承诺任意编译器、平台或签名流程均相同。

CI 产物明确命名 `openbgi-unsigned.apk`，不能直接用于覆盖安装正式包。正式发布要在保管密钥的环境运行签名打包并再次校验证书。没有在公共 CI 自动签发正式 APK。

## 内存检查及边界

`make sanitizer-test CC=clang` 使用 AddressSanitizer 和 UndefinedBehaviorSanitizer，异常即失败，编译产物独立放在 `obj/sanitize-address-undefined` 和 `bin/sanitize-address-undefined`。GitHub Actions 在真实 Linux 容器中开启泄漏检查，另外运行常规 C 测试、Android Java 合约、字体与构建保护测试。

Linux/Android 默认启用栈保护、FORTIFY、格式字符串检查、RELRO/NOW 与不可执行栈。Android 正式包不包含开发用 sanitizer 运行库。

这些措施能发现已执行路径中的问题，不代表 C 引擎已经内存安全，也不能证明所有游戏资源、脚本路径或第三方库均安全。尚未引入覆盖率驱动模糊测试，手机端也没有运行 HWASan；后续应持续补充畸形资源、解码器和 VM 边界用例。
