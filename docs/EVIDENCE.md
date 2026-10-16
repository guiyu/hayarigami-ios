# 技术证据 · 2026-10-16

## 输入与身份

对原 NSP 以只读方式按区段读取，未复制整包。大小为 4,444,363,536 字节，PFS0 头与文件表共 272 字节，4 个内容分别为 Program、Manual、Control、Meta。Control 的日文 NACP 名称为「流行り神 １・２・３パック」，发行方为 Nippon Ichi Software, Inc.，显示版本 1.0.0；CNMT 为 Application，版本号 0，Title ID 为 `010095B01AF94000`。

本地 hactool 已能读取元数据并提取 ExeFS。独立区段读取脚本提取的全部 5 个 ExeFS 文件与 hactool 结果逐字节相同。脚本验证 NCA 文件系统头的 SHA-256 和 NSO 已启用的段哈希。

边界：没有完整验证 RomFS 的 IVFC 哈希树或官方签名；观察到四个 NCA 的 fixed-key 签名区均为零。身份核对、解密成功、局部哈希检查不能称为官方原版真实性认证。初期使用的稀疏 NCA 探针因缺失内容触发 hactool 资源解析失败，已由直接区段读取替代；不能把该失败归因于原包损坏。

## 程序与平台

NPDM flags 为 `0x27`，64 位程序标志置位；动态符号为 ELF64，重定位包括 AArch64 ABS64、GLOB_DAT、JUMP_SLOT、RELATIVE。SDK 字符串包含 `NintendoSdk_BuildTarget-64Bit`、`NintendoSdk_nnSdk-14_3_0-Release` 和 `NintendoSDK_NVN-14_3_0-Release`。subsdk0 含 `NintendoSDK_movie-14_3_0-Release`。

| 模块 | 压缩 NSO 大小 | text 字节 | 导入/定义动态符号 | SHA-256 |
|---|---:|---:|---:|---|
| main | 2,248,368 | 2,925,696 | 327 / 64 | `2b6f3473c1b4f7fdfa68cbad2262ac14fcec6731911e1f80ba3345199fe6118b` |
| rtld | 7,207 | 5,968 | 19 / 0 | `a3b723feeb3766bb09f7402c9f7d408dc08def4ffc9d3f3dfef0278261317711` |
| sdk | 6,025,528 | 6,145,344 | 13 / 27,394 | `6022d3c89f56ada7c579601d5a23b5da9169c5d965bc78b2ed465b9e1f96cd8a` |
| subsdk0 | 3,385,994 | 3,402,784 | 660 / 10,886 | `ad5b29eeefdf4ab0c5be9044eaa4f9c186a95cc52b108401decbc4957ce745bf` |

main 的 build ID 全为零，因此不能用它唯一识别版本，复跑校验采用完整 NSO SHA-256。`nnMain` 符号位于 main 相对偏移 2,759,856；这里只记录静态位置，不作为已运行的证明。

引擎线索为 `Nmpl::Core`、`Nmpl::Gra`、FOAF 游戏代码和 `NMPLTEX1` 纹理头。没有足够证据把它命名为公开通用引擎，或声称可借 Unity/Unreal 工程直接导出 iOS。主要平台依赖涉及 nn::fs、nn::hid、nn::account、nn::os、nn::audio、nn::codec、movie 与 nvnBootstrapLoader。NPDM 服务权限是声明权限，不能当作实际运行调用清单。

## 资源适配

30 个 RomFS 文件中有 22 个 `PS_FS_V1` 归档，成员结构为 16 字节头、每项 64 字节索引（48 字节名称、64 位大小和偏移）。4,932 个成员全部通过边界检查，并由原生 C 与独立 Python 解析器逐项对照。

| 后缀 | 数量 | 已知边界 |
|---|---:|---|
| .nltx | 3,372 | 样本头为 NMPLTEX1；像素格式、压缩和 GPU 布局未完整解码 |
| .adpcm | 1,260 | 文件后缀及 SDK 的 ParseAdpcmHeader 导入；尚未验证音频播放 |
| .opus | 172 | 文件后缀及 OpusDecoder 导入；尚未验证封装及播放 |
| .dat | 72 | 包含剧情/数据库命名线索，脚本语义未解析 |
| 其余 | 56 | .did/.tre/.ftx/.bin/.fnt/.ftd/.fad/.pak |

另有 8 个 MP4 文件，抽查头部为 ISO BMFF/avc1 线索；未解码或验证完整影片。不能把后缀识别等同于内容解码。

`src/psfs.c` 是已经实现的原生资源组件：无游戏硬编码地址，拒绝越界、整数溢出、非法名称和截断索引。Mac 动态库对真实资源验证通过；iOS 构建输出 `libhayarigami_resources_ios.a`，目标 arm64、最低 iOS 15.0。这个版本号只属于该 C 库，不代表游戏 App 的系统支持范围。

## 离线改写验证

参考工程当前提交 `1156cd4868c0e3e806e00f4eb57674626d242cb7`。仅读取其通用改写器、独立验证工具与 Mach-O 封装工具。新项目先记录工具 SHA-256，再校验输入模块 SHA-256，强制 `DM_RULESET=native`、`DM_PRIVATE_GATES=0`、`PYTHONOPTIMIZE=0`，拒绝非空 cuts/obs/params。

| 模块 | 结构检查改写点 | 等价试验站点 | 试验次数 | 有效试验 | 故意破坏负例 |
|---|---:|---:|---:|---:|---:|
| main | 1,909 | 1,909 | 5,895 | 5,242 | 50 / 50 检出 |
| rtld | 4 | 0 | — | — | — |
| sdk | 14,183 | 14,065 | 43,012 | 38,027 | 50 / 50 检出 |
| subsdk0 | 10,541 | 10,541 | 32,216 | 28,358 | 50 / 50 检出 |

以上结构检查全部通过，三模块所有等价站点均至少获得 2 次有效试验，无分歧。sdk 的 118 个系统调用/计时器门和 rtld 的 4 个系统调用门只做结构验证，未证明宿主实现的语义。等价工具对独占指令采取两侧相同的测试替换，不能作为并发原子性、完整程序、所有输入状态的证明。

初次 Unicorn 测试在受限沙箱内收到 SIGILL，进程池停滞。单条合成 ARM64 指令在沙箱外成功，随后所有等价测试在允许 JIT 的本地环境通过。新入口增加预检，失败即停，不把空日志当成通过。

Mach-O 容器约 31.2 MB，含四模块离线代码，platform IOS / minos 26.0；它由封装脚本生成，未包含应用宿主，未签名、未安装、未调用游戏入口。

## 兼容层差距与路线选择

本作与 D2R 的 SDK SHA-256 和 build ID 不同。旧工程 main.c 带 D2R 专用初始化旁路、存档常量和触摸行为，图形脚本 nvn_slots.py 硬编码旧 main 的加载函数与 text 尾，着色器事实表也属于旧游戏。均不能直接带入。

本作 main 的 224 个 nn 导入只有 53 个在旧 host/hle*.c 的 HLE_FORCE 注册中直接同名；其余 171 个不能直接叫作未实现，因为一部分由 SDK 自己实现。同名同样不能证明结构布局、ABI 或运行语义兼容。已明确看到本作使用 AudioRenderer、Voice、SubMix、I3dl2Reverb，而旧音频层没有这些同名直接接管；需要实现相应宿主服务或验证可保留的 SDK 上层。

本作有 535 个 NVN 名称字符串，相比旧名字表多 14 个字符串，其中一个是 bootstrap 名，另外 13 个是额外 API 候选。尚未逐一定位实际调用点，不能宣称都被使用，也不能套用旧工程的 NVN 覆盖率。

当前选择：继续离线 AArch64 改写加本作专用平台兼容。理由是模块已可改写、资源可索引，且有已有机制可验证；不是已证明全游戏可玩。源码移植缺少游戏和引擎源码；重写剧情引擎则还缺少剧情格式、行为与存档语义，现阶段范围更大。

下一里程碑必须是：最小宿主完成模块加载/重定位与本作 SDK 启动，记录每个不支持的服务并明确失败，随后由原始游戏代码生成标题画面。完成实际 NVN/纹理/着色器输出、音频和存档隔离后，才进入 iPhone 安装验证。当前缺少的是这些实现与验证，不是密钥或手机连接。

## 工具与资料

Xcode 26.3（17C529）、iPhoneOS SDK 26.2、LLVM 22；Python 包精确版本见 requirements.txt。程序分析使用 cryptography、lz4；改写/验证使用 capstone、unicorn。参考源码路径和工具摘要记录于本地 `.local/audit.json`。

容器格式依据 [hactool NCA 定义](https://github.com/SciresM/hactool/blob/master/nca.h)、[区段与密钥处理实现](https://github.com/SciresM/hactool/blob/master/nca.c)、[RomFS 定义](https://github.com/SciresM/hactool/blob/master/ivfc.h) 和 [XTS 扇区处理](https://github.com/SciresM/hactool/blob/master/aes.c) 交叉核对。没有下载游戏源码、游戏内容或外部密钥。
