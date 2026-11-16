# 离线汉化兼容工具

把汉化补丁中的资源转换成 OpenBGI 已支持的根目录覆盖文件。同一种资源机制共用一个适配器，游戏差异写在 JSON 配置中。

Android 0.10.19 已把这些资源机制接入导入流程，并增加已有游戏“处理汉化补丁”入口；手机使用纯 Java 解码器，不依赖 Python 或 Windows DLL。本文说明保留的电脑端工具：其输出需要合入独立游戏副本，设置建议手动应用。App 的已知游戏配置从同一组 JSON 生成。

## 已实现的机制

| 适配器 | 作用 | 真实样本 |
| --- | --- | --- |
| `bgi-arc` | 读取 PackFile / BURIKO ARC20，保留原生压缩流 | フレラバ HD 的 GBK 开场脚本 |
| `evb-external` | 读取外部 EVB 目录与块压缩数据，不依赖文件扩展名 | MakingHD 的 KLT、KanojoStep 的 Update0.pck |
| `evb-pe` | 静态扫描 PE 中结构完整的 EVB，复用外部 EVB 解包器 | KanojoStep 汉化 EXE |
| `cpk-v2` | 使用配套 key 解析 Camellia/EAX+zlib 资源包 | MakingHD CPK 的 9 个开场/界面脚本 |

本次 MakingHD EVB 完整导出 721 个资源；KanojoStep 内嵌 EVB 与更新包合并导出 745 个资源，跳过 DLL。这里的数量是转换验证结果，不是全流程运行验证。MakingHD 的开场中文显示、回看已另作桌面运行小样；混合编码、字体钩子与排版仍需适配。

`inspect` 还提供 PE32/PE32+ 导入、导出、节表和字体/API 字符串候选。它们是静态线索，不被自动当作 DLL 行为或兼容性结论。壳破坏常规 PE 导入表时，嵌入 EVB 的检测仍独立进行。

## 使用

需要 Python 3.11 或更新版本。`inspect` 和 ARC 转换只用标准库。CPK 转换需要可选的 `cryptography`（本次验证 50.0.2；CI 使用固定 Debian 快照中的版本）。

从仓库根目录运行：

```powershell
python tools/translation-compat.py inspect D:/download/MakingHD --report inspection.json
python tools/translation-compat.py plan D:/download/MakingHD --profile tools/translation_compat/profiles/makinghd-opening.json --report plan.json
python tools/translation-compat.py convert D:/download/MakingHD --profile tools/translation_compat/profiles/makinghd-opening.json --out D:/converted-makinghd-opening
```

输出目录必须不存在，父目录必须已存在，而且不能在原游戏目录内。报告文件也不会覆盖已有文件。

EVB 的 aPLib 解压通过独立 Python 进程调用，可选依赖由使用者另行安装；不会链接进引擎或 APK：

```powershell
python -m venv decoder-venv
decoder-venv/Scripts/python.exe -m pip install aplib==0.8
python tools/translation-compat.py convert D:/download/MakingHD --profile tools/translation_compat/profiles/makinghd-evb.json --out D:/converted-makinghd --aplib-python decoder-venv/Scripts/python.exe
```

Linux 的解释器路径对应 `decoder-venv/bin/python`。工具只运行命令行明确指定的解压环境，不从游戏或 JSON 配置中接受执行命令。输出目录附带 `openbgi-translation-manifest.json`，记录每项资源大小、SHA-256、来源和运行设置建议；不包含密钥字节。

## 增加游戏配置

例如，译文已经写进原生 ARC 的游戏只需要配置资源选择与编码：

```json
{
  "schema": 1,
  "id": "my-game-translation",
  "runtime": {"engine": "1.616", "text_encoding": "gbk"},
  "sources": [
    {"format": "bgi-arc", "path": "Archive/script.arc", "select": ["intro*"]}
  ]
}
```

支持 `select` / `exclude` 的大小写无关通配符。多个来源出现同名资源会报冲突，需要在低优先级来源明确排除旧版本；KanojoStep 配置演示了更新包覆盖。内嵌 EVB 默认只接受唯一的有效候选，有多个时可以配置已核实的 `base_offset`。嵌套 EVB 文件夹需要新布局适配器，当前不会悄悄展平。

电脑工具新增容器机制只需实现 `entries` / `read(entry)` 接口并注册到 `formats.READERS`；不需要修改游戏指令解释器。App 端对应适配器独立实现，已知 `runtime` 配置会在导入时应用。字体别名等运行时钩子的自动替代尚未实现，电脑 CLI 的 `runtime` 仍仅是设置建议。遇到新的 DLL，应先用实际资源/指令证据判断能否沿用这些机制。

## 校验与边界

- 所有转换先验证索引，选定载荷全部成功后才发布新输出目录；拒绝越界、重复名、路径逃逸、重叠载荷和解压长度错误。
- 排除全局进度、`.sud` 等存档、BGI 缓存、DLL、EXE 和密钥文件；不会复制游戏现有存档目录。
- 原生 ARC 的 DSC/CBG 流保持原样，由 OpenBGI 加载时解码。CPK 中的已展开像素按引擎支持的 RawImageHeader 原样输出并记录类型；EVB 图片通常更节省空间。
- EAX 资源当前能验证解压校验和、边界及长度，未获得可验证的认证标签，不宣称加密身份认证。
- 资源转换不自动解决混合编码、Windows 特定逻辑、字形比例、路线兼容或未知指令。

测试全部使用自造容器与 PE，没有商业游戏素材：

```powershell
python -m unittest discover -s tools/translation_compat/tests -v
```

格式依据包括项目的 `src/core/arcread.c`、`inc/bgi/codec.h`、本地补丁静态分析，以及 [EVB 格式参考](https://github.com/mos9527/evbunpack/tree/ee7b647acf2a46617b0c448dfa83d63132ea917b)（Apache-2.0）和 [Crypto++ EAX 实现](https://github.com/weidai11/cryptopp/blob/master/eax.cpp)。可选 aPLib 包由其作者以 GPLv3 发布，通过独立进程使用；工具包不附带该依赖或任何游戏资源。
