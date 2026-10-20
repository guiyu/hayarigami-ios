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

初次参考工程提交 `1156cd4868c0e3e806e00f4eb57674626d242cb7`。仅读取其通用改写器、独立验证工具与 Mach-O 封装工具。新项目先记录工具 SHA-256，再校验输入模块 SHA-256，强制 `DM_RULESET=native`、`DM_PRIVATE_GATES=0`、`PYTHONOPTIMIZE=0`，拒绝非空 cuts/obs/params。

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

后续里程碑：由原始游戏代码生成标题画面。完成实际 NVN/纹理/着色器输出、音频和存档隔离后，才进入 iPhone 安装验证。当前缺少的是这些实现与验证，不是密钥或手机连接。

## 原生运行时与音频验证增量

独立宿主取代 rtld，加载 main/subsdk0/sdk，完成 8,806 / 19,189 / 34,271 次重定位，共 62,266 次。在 Mac arm64 上实际调用本作 SDK Start，运行模块构造与游戏初始化；首次 AudioRenderer 边界前已创建 11 个游戏工作线程。未实现系统调用会以退出码 42 明确停止，不当作启动成功。

已实现受限内存映射/堆、线程与地址仲裁、只读 RomFS 和输入状态桥接。内存测试覆盖借用源保护、解除映射后的目的页保护、禁止借用期间收缩堆、重复借用与并发映射；线程优先级读写通过 TSan。文件读取测试覆盖偏移、EOF、过期句柄、目录穿越和符号链接逃逸。合成 ARM64 测试实际执行 RELATIVE 重定位，并拒绝代码页重定位及越界 MOD0；验证不依赖 Python assert，优化模式不会跳过检查。

边界：4 KiB 客体页和 16 KiB 宿主页的共享边缘无法精确隔离保护；仅完整宿主页执行源借用保护。线程句柄回收、若干事件与同步服务、后台生命周期仍不完整。iOS 签名页映射使用已签名框架代码页 remap，目前只有交叉编译证据，没有真机证明。

全部 30 个 RomFS 文件已流式提取到忽略目录并记录 SHA-256；未复制 NSP 整包，未宣称完成 IVFC 全树验证。

DSP ADPCM 解码器使用 64 位中间值、明确负数舍入和 16 位饱和。合成边界测试通过 ASan/UBSan。三部作品各取首、中、末 3 个 ADPCM 成员，共 9 个完整流、3,046,472 个 PCM 样本，与独立构建 vgmstream 一致。报告记录两端二进制 SHA-256。两端共用从 Nmpl 文件提取的 payload/coefficients，因此这是解码一致性证据，不是 Nmpl 头解析的独立证明。FFmpeg 的 ADPCM_THP 路径与 DSP 的舍入不同，未用它作为通过判据。

原 SDK 包含 CPU/manual AudioRenderer。隔离探针选择参数中的 CPU/manual 模式，实际打开渲染器（工作区 3,534,848 字节），使用 PCM16 voice、内存池、FinalMix 和 CircularBufferSink 生成 4 × 240 = 960 帧双声道 PCM。忽略首 tick 的 FIR 启动阶段后，验证左右 2:1 增益，允许有限 FIR 带来的 1% unity-gain 偏差；状态回报为滞后一个 tick 的 720 样本。此测试证明 SDK 软件混音和输出数据可用，不证明扬声器播放或 Switch 全部效果位精确一致。

Apple AudioUnit 输出队列的合成测试覆盖左右声道、满队列拒绝、环绕和欠载静音，通过 ASan/UBSan 与 TSan。真实游戏初始化已打开 AudioUnit，但尚未验收实际剧情语音与背景音乐。

实际运行曾在 AudioUnit 启动附近出现堆破坏。GuardMalloc 定位到原 SDK CircularBufferSink 写入：SDK 按完整 240 样本通道块推进后才环绕，旧 16,384 字节 sink 并非该块的整数倍。新增尾部哨兵复现越界；现按 5 个完整渲染周期分配有效 sink 长度（双声道 4,800 字节、六声道 14,400 字节），内存池仍满足 16 KiB 对齐。两种声道模式均通过原 SDK 额外 120 tick 环绕测试；公共读取接口返回的 480 帧与内部 PCM 逐样本相等。

六声道到双声道的通道顺序暂按 FL/FR/C/LFE/SL/SR；尚未用本作真实内容独立确认，不作为最终音频验收。

## Metal 初始化与内存边界

原 SDK 查询确认本作 NVN API 版本 55.15。实际游戏创建 512 MiB、128 MiB、32 MiB 资源池、真实 Metal command queue，以及三组 1920×1080 RGBA8 纹理与三缓冲窗口。每张纹理块线性存储为 8,847,360 字节，游戏为每组分配 10 MiB pool。CAMetalLayer 已创建，但还没有真实屏幕呈现。

合成真实 GPU 测试验证 20,480 字节 no-copy pool 逐字节复制、19×17 非规则尺寸纹理去块线性上传、GPU 清屏及读回。共享同 pool/offset/有效布局的纹理对象使用同一 Metal image；描述符保留 backing，最后一个纹理对象销毁后重新创建仍保留 GPU 内容。19×16 的两种等尺寸但不同块高布局明确拒绝，不静默错误共享。布局算法源自只读参考的通用实现，授权与来源见 third_party/NOTICE.md；没有复制旧游戏地址或材质规则。

真实启动暴露第二个内存错误：旧 TextureView 宿主结构写入 36 字节，而游戏对象只留 32 字节，覆盖下一个对象的虚表。原 SDK 独立探针确认全部 view setter 保持第 32 字节之后哨兵；缩小结构后，边界测试通过，真实启动越过原崩溃并完成三缓冲窗口初始化。不能以旧参考头的对象大小替代本作 ABI 证据。

原游戏进一步请求 1920×1080 RGB10A2、flags=0x28 的稀疏布局。独立调用原 SDK 的纯 CPU 布局函数（不初始化或伪造设备）对照 4/8 字节像素共 20 组尺寸与瓦片参数，包括 1×1、19×16、128 边界两侧、129×129、1920×1080。当前只实现二维单层、4 字节像素或 8 字节深度/模板、无显式 packaged layout 的稀疏布局；完整稀疏瓦片为 64 KiB：4 字节像素为 128×128，8 字节为 128×64，小图落入普通 mip tail。129×129 占 262,144 字节，1920×1080 占 8,847,360 字节；跨瓦片行的偏移通过独立黄金值测试。129×129 RGB10A2 数据已完成真实 Metal 上传和逐字节读回；真实启动也已创建本作 1920×1080 RGB10A2 和 Depth32Float/Stencil8 纹理，后者占 16,711,680 字节。129×129 深度/模板图经真实 GPU 清屏后，所有像素读回深度 0.25、模板 83。Metal 使用完整驻留图像；虚拟 pool、动态稀疏映射及多级稀疏纹理仍明确拒绝，不能称为完整 sparse API 支持。

命令录制目前验证记录快照、内存耗尽回调、跨段跳转和旧 handle 保持；尚未完成绘图命令重放。GPU 同步以实际 Metal command buffer 完成状态为依据，测试用 shared event 阻塞 GPU，验证零等待/有限等待超时、解除阻塞后完成及已完成状态。队列间等待通过每代 fence 独立的 Metal shared event 排队，不阻塞 CPU；双队列测试曾复现 CPU 等待造成的进度阻塞，现覆盖该回归。flush 暂同步完成；不是性能验收。

显示事件目前为 headless 60 Hz 单调时钟，不是物理屏幕 VSync。CAMetalLayer 为 BGRA8，后续呈现需要从游戏 RGBA 目标采样转换。着色器翻译、绘图、真实标题画面、触控、存档、影片及生命周期仍未完成。iOS arm64 静态库编译通过只证明编译，未生成 App/IPA、未签名、未安装。

GPU buffer 使用现有 pool 的确定子区间，支持 CPU 映射与地址、相对范围 flush/invalidate，拒绝越界及存活 buffer 对应 pool 提前释放。窗口 crop 保存并验证纹理范围，仍待后续呈现路径使用。

统一回归入口 scripts/check_native.sh 已通过 ASan/UBSan、线程与音频 TSan、内存保护、真实 Metal 与原生 ARM64 fixture 检查；独立审查发现的 backing 生命周期和有效布局问题已修复并复审通过。

## 工具与资料

Xcode 26.3（17C529）、iPhoneOS SDK 26.2、LLVM 22；Python 包精确版本见 requirements.txt。程序分析使用 cryptography、lz4；改写/验证使用 capstone、unicorn。参考源码路径和工具摘要记录于本地 `.local/audit.json`。

容器格式依据 [hactool NCA 定义](https://github.com/SciresM/hactool/blob/master/nca.h)、[区段与密钥处理实现](https://github.com/SciresM/hactool/blob/master/nca.c)、[RomFS 定义](https://github.com/SciresM/hactool/blob/master/ivfc.h) 和 [XTS 扇区处理](https://github.com/SciresM/hactool/blob/master/aes.c) 交叉核对。没有下载游戏源码、游戏内容或外部密钥。

## 状态与采样器验证增量

Buffer 映射与地址保留 pool 内偏移；flush 在子范围内验证，仍有 live buffer 时拒绝销毁 pool。窗口 crop 保留请求矩形并检查边界。测试覆盖两类拒绝路径和真实子区间映射。

六类纯 CPU render-state 对象和 SamplerBuilder 的 Set/Get 直接保留原 SDK 实现；独立探针验证对象边界及参数往返。CommandBuffer 绑定保存状态和 descriptor-pool 快照；timestamp counter 目前仅录制请求，尚无执行结果，不能当作 GPU 计时完成。

采样器保存完整不可变参数，由 ARC holder 随描述符持有；实际绘制绑定时才创建 Metal sampler。合成 GPU 测试已验证双线性插值、重复、边缘钳制和透明边框。游戏初始化请求的 wrap=2 镜像钳制尚无直接 Metal 等价实现，保留原始值；实际使用时必须转换或明确失败，不得替换成默认采样。

最新完整 native 回归、真实 Metal 测试及 iOS arm64 编译检查通过；原始游戏启动推进至 nvnProgramInitialize。尚未执行游戏着色器、提交游戏绘图或产出可安装 App/IPA。


## 2026-10-20 着色器、资源加载与线程增量

原 SDK 控制段与 SPH 对照确认 128 组内嵌着色器的实际代码长度、常量长度、对齐偏移和阶段；提取器不再把尾部填充当成有效负载。运行时 Program 保存独立不可变的 control/code/constants 快照，在 live pool 范围内复制并拒绝越界、重复阶段与 SPH 不一致。合成测试验证源内存变化和 pool 释放后快照仍然有效。

外部只读通用翻译库生成 127 组 Mac/iOS Metal 库，1 组几何阶段明确失败。当前采用显式 `float-2d-upper-left-zero-one-v1` 特化，未来每次绘制必须校验二维浮点资源、坐标/深度、顶点和输出语义；不匹配必须拒绝。不能把生成库的数量称为游戏渲染通过。最初一组实际 vertex/fragment 代码在独立 GPU 探针中生成 32×32 图像，1024 像素颜色和 UV 方向全部正确；尚未执行游戏命令队列、产生标题画面。

真实启动曾卡在恰好 64 个保留的资源句柄：三部作品的归档重复打开需要更多句柄。96 个同时打开的回归用例先失败后通过，容量扩至 128；游戏现已完成相关资源打开并继续创建 BC7 纹理。新增离线网络初始化和仅应用内的 Local Player，不连接 Nintendo 服务，不读取旧应用身份；独立可写存档尚未实现。

二维单层布局对照扩大为 1/2/4/8 字节像素和 BC7 压缩块的 70 组自动布局，加 6 组显式预排布布局，均与本作 SDK 的纯 CPU 尺寸/瓦片计算一致。新增 R8 跨瓦片上传和 RG11B10Float 清屏读回。BC7 在 CPU 解码为 RGBA8 供 iOS 使用；合成 64 块覆盖八种模式，4096 字节像素与 Mac 原生 GPU BC7 解码完全一致，负例正确检出一字节差异。257×257 上传测试的输入由宿主 swizzle 生成，证明往返、裁切、解码与上传，不是独立的真实游戏像素排列证明。

预排布 setter 的原 SDK 仅读取两字节，H/D/W 分别为低/中/高四位；getter 写四字节。旧宿主读取八字节，跨保护页回归复现越界；改为正确长度并处理显式 W 后通过。D 非零、多级显式宽瓦片等未覆盖路径明确拒绝。实际启动已创建 1920×1080、1295×700 等多张 BC7 游戏纹理；这不证明纹理在真实画面中正确排列。

窗口刷新间隔设置现在保存请求；呈现与刷新节奏仍待真实提交路径。固定分辨率变化事件保持阻塞并可由销毁唤醒，不产生虚假的变化通知。触屏状态读取以准确的 656 字节 ABI 返回无触点和递增采样号，测试覆盖尾部哨兵；UIKit 触点输入尚未接入。

游戏工作线程新增需要 SVC 0x1c/0x1d 条件变量。独立实现原子解锁、条件等待、通知后重新取得 mutex、有限/零超时；TSan 回归验证锁被持有时不会提前返回。相对超时采用单调截止时间加 Darwin 相对等待，不依赖系统挂钟。行为参照 Atmosphere KConditionVariable，未复制其实现。当前不包含完整优先级继承或线程取消支持。

真实启动已越过上述资源和服务阶段并进入绘图状态命令录制，深度范围与 clamp 仅保存命令；尚无游戏绘图重放、标题画面、App/IPA 或真机安装证据。旧版“停止于 nvnProgramInitialize”是历史停止点，已被本增量取代。


## 上游同步与解析复核（2026-10-20）

已对 `jhaizhou-ops/d2r-mobile` 执行 fetch。最新上游为 `53e7c5bb9ca3ea53e2160005be8fbe920cd9cb53`；本地参考 HEAD `ad4e22bc07ca7c290b77fd30fca7e079a0a1cd30` 已包含它，工作区保持干净，不需要重复 merge。原版本审计保留，新审计 `.local/audit-upstream-20261020.json` 固定当前五个工具的 SHA-256。

新版解析保留 ELF symbol binding、visibility、section，并对 MOD0、负 dynamic 偏移、hash 表头、完整 symbol/string 表与 RELA 范围进行检查。新报告四模块的23个未找到模块内定义的符号均为弱导入，强导入0，未知绑定0。此统计仍是静态同名覆盖；不代表已经实现所有 SDK 平台服务。宿主独立合成测试确认缺失弱导入为零、已有客体/宿主定义仍解析、缺失强导入退出42；解析及相关 Python 测试共22例通过。

新上游 x18 寄存器字段预筛选应用于本作四模块，结构检查全部通过。26,515 个站点的81,123次试验、71,627次有效试验无分歧，150个变异负例全部检出。新旧四模块的 patched.bin、fixups.tsv、gates.tsv 逐字节相同：此次更新增加扫描筛选，不改变本作已验证的改写语义。未测量速度倍数。

新 Mach-O 容器为31,838,224字节，增加 LC_CODE_SIGNATURE 与链接器式 ad-hoc 签名预留区。原始预签名未绑定 bundle 资源，不作为完整框架签名验收。在忽略目录的普通名称测试副本上，使用系统 codesign、无个人证书的 ad-hoc 重签后，严格验签通过。`.framework` 目录在当前 FileProvider 路径会被重新附加 FinderInfo，严格签名拒绝；同目录改用普通名称的副本后通过，真实 App 构建仍须使用不带该元数据的打包暂存目录。此测试不证明个人团队签名、zsign、真机安装或iOS代码页映射。

新版产物位于 `.local/rewrite-upstream-20261020/`，原产物没有覆盖。实际启动新版模块已创建游戏纹理并录制渲染目标、颜色及深度模板清屏请求，停在 `nvnCommandBufferBindProgram`。渲染目标持有 view/backing 的快照，命令字节被部分覆盖或资源池释放时回收；合成 Metal/ASan 验证通过。清屏仍仅录制，尚无游戏命令提交重放和标题画面。


程序绑定现在持有不可变 shader 数组的命令 lease，原 Program 销毁或替换后仍可用；覆盖旧命令后释放。Uniform buffer 绑定保留 pool、Metal buffer、精确偏移和长度，并保留提交前的数据更新，不拍摄旧数据副本。资源池释放会使存放于其中或依赖该池的旧录制引用失效；跨池释放和越界绑定已通过合成 ASan/Metal 回归。尚不允许执行已因资源销毁失效的旧命令。

已按 SDK ABI 录制模板读/写掩码与参考值、alpha参考值、16组顶点属性与顶点流；后续重放必须处理补足/禁用状态及着色器特化限制。新版模块最新真实启动停止于 `nvnCommandBufferBindVertexBuffers`。最新完整 native 回归（含弱导入、跨池资源、Program绑定）、22例Python与iOS编译均通过；仍未提交游戏绘图、未生成可安装IPA。

## 首次提交命令与 GPU 探针（2026-10-20）

新增 vertex/index buffer、texture-to-texture copy、tiled cache、barrier、命令内 fence 的录制，保留原 SDK ABI、精确范围和资源生命周期。DrawElements 捕获索引类型、数量和对应 live pool；copy 保存两个 image/view 与两个24字节区域，不把不同格式复制预先解释成原始字节复制。Fence 录制持有独立引用，即使原 Sync 对象销毁也不悬空；录制本身不提前提交或发出完成信号。WindowAcquire 目前只发放尚未呈现的初始图像，耗尽后明确失败，没有静默复用正在使用的图像。

新版模块实际到达 nvnQueueSubmitCommands，诊断遍历取得首批35条命令：1920×1080 RGB10A2 颜色与0x36深度模板目标、清屏、VS/PS、128字节顶点常量、16字节顶点步长、4个 ushort 索引的 triangle strip，以及 RGB10A2→RGBA8 复制、两次计时和 fence。当前提交函数只检查并输出录制内容，最后明确退出42 `Metal command replay not implemented (submission inspected)`；没有执行这些游戏绘制或完成其 fence。不同格式复制的转换语义尚需核实。

本次真实首绘使用的 PS alpha 条件为 Greater、参考值0。转换器增加 `greater-zero` 特化，元数据记录条件/参考值及 Mac/iOS metallib SHA-256。使用该原始片段着色器和合成顶点的 GPU 测试，alpha=0 的16个像素保留蓝色清屏，alpha=0.5与1.0各16像素输出正确红色和alpha；旧 Always 库在同一用例失败。此为独立着色器探针，不是游戏命令重放或标题画面。新版整批库仍为127成功、1个几何阶段失败。

GPU 时间戳验证使用 Metal timestamp counter set，前后 sampleTimestamps 配对换算为纳秒，128位中间乘法覆盖全宽数值、退化区间和区间外样本。空 blit 编码器实测没有产生样本，读回为0；含可校验 GPU fill 的编码器产生四个非零有序样本，全部处于配对区间内，GPU写入逐字节校验通过。运行时计数命令尚未接入该采样执行；GetTimestamp 读取16字节 counter-data 的后8字节，要求未来 writer 已完成纳秒归一化。

完整 native 回归（含 ASan/UBSan、TSan、Metal counter、copy与fence生命周期）、22项Python测试和iOS arm64编译检查通过。证据仍不包含实际游戏绘图、可安装App/IPA或真机安装。

独立审查发现提交诊断器对截断viewport等命令缺少payload长度检查；ASan真实复现堆越界。现已在任何参数读取或SDK getter调用前，按opcode校验固定长度、顶点可变长度与数量，拒绝未知opcode。六组畸形输入均退出42；修复后真实游戏35条提交命令仍全部通过检查，并保持相同的未实现重放停止点。

复审另发现可通过单独篡改opcode使合法lease被按错误类型解释。lease现保存录制时的原opcode，读取资源时要求opcode和命令总长度精确匹配；新增合法render-target记录被改为copy的回归，实测在对象解引用前退出42。此类型变体的修复前运行因自动审批超时未执行，不声称已取得修复前ASan证据。

## iPhone 安装请求复核与首绘库加载（2026-10-20）

用户明确要求安装包必须能进入游戏，不接受仅验证安装/原生启动的诊断版。当前仍没有符合该标准的App/IPA，没有安装空壳应用。只读设备清单有多个已配对设备；其中一台有线连接且开发者模式开启，不把它自动认作原先目标iPhone。

原签名产物中最新 provisioning profile 尚在有效期内（至北京时间2026-10-27 13:55），但绑定的是旧D2R应用身份。复用签名身份不等于复用该profile安装新Bundle ID；新游戏须使用独立应用身份并取得相匹配的profile。此处只离线解析了既有profile，未导出私钥、未修改旧工程或已安装应用，也未确认钥匙串身份目前可用。

新增运行时 Metal 库加载函数，验证控制段key、代码/常量长度和SHA-256、阶段、Profile、alpha条件，以及当前平台metallib的SHA-256。用被校验的同一份NSData创建Metal库，避免校验文件后再次按路径加载另一份内容。当前仅允许vertex/fragment与已验证的Always或Greater/0特化；没有对其他资源/状态兼容性作保证。

使用真实首绘VS/PS，经此加载函数，在16×16目标上用本作对应的Float3位置、RGBA8颜色、ushort索引与128字节identity常量布局绘制四边形，256像素全部正确。源码单字节变化、非零alpha参考值、库单字节变化、错误Profile、错误阶段与非字典JSON共六个负例均退出42。此探针使用合成顶点和常量，尚未接入游戏命令队列，不是标题画面或可玩性证明。


## 真机片头与按键退出调查（2026-10-20，验收进行中）

独立 UIKit App 已使用用户授权的原开发团队身份签名，独立 Bundle ID 为 `com.guiyu.hayarigami`；实际安装到目标 iPhone 16。用户确认标题、颜色显示与片头均可到达，但片头按虚拟键仍曾闪退。因此不将安装或标题视为最终可玩验收，也尚未导出最终 IPA。

此前真机日志明确记录 linear texture 的 Metal 对齐拒绝；线性 R8/RG8 视频纹理改为普通 Metal 图像，依据实际 row stride 上传。随后日志出现不同纹理布局复用同一地址的拒绝；仅当旧 Texture 对象全部 finalize 后才允许不同布局重新分配，旧命令/描述符仍保留独立旧图像。同布局仍复用原 backing，避免丢失 GPU 内容。padded RG8 上传、swizzle、旧快照保留与 R8 重分配测试通过。

第 2 版真机 LLDB 捕获 `decoded_video` 中 `memcpy` 的空目标地址：`output=nil`、1920×1080、时间戳13秒、已解码约388帧；ready=0、pending=16。回调无局部 autorelease pool，已消费帧仍滞留。新增弱引用回归修复前失败，局部池修复后120次全高清帧消费即释放。审查随后发现 send_input 的压缩包及AAC输出也在逐包池外创建；AAC消费后弱引用回归修复前失败，加入入口局部池后通过。原Apple视频颜色、12个B帧展示顺序、6个AAC包24576字节PCM检查通过。

第 3 版真机按键触发片头线程退出时，LLDB 再捕获 `NSConcreteMutableData dealloc` 向游戏栈写入的 protection fault，SP=0x3302b06d0；游戏线程同时在 CloseHandle 的 pthread_join 中等待。旧 hy_thread_returned 在游戏栈上先发布 exited、随后直接 pthread_exit，允许SDK在系统TLS/ObjC清理结束前撤销栈。修复为在宿主 run 中保存返回上下文，退出游戏时先回到原宿主栈，再发布退出并返回 pthread。独立真切栈测试修复前退出138，修复后100次“等待退出后立即保护游戏栈，再join”的清理验证通过。

第 4 版包含以上两类内存生命周期修复，已签名、安装并连接真机调试器。已直接从真机正在呈现的Metal纹理读回片头帧，而非用模拟画面替代；按键切换、剧情、保存恢复与持续运行仍在验收。

第 4 版进一步真机按键后，hy_fail 断点取得新 BC7 1920×1080、flags0x20、offset424674816、size2621440，与旧线性RG8 960×540、stride1920、offset424186304、size1036800重叠；旧objects=1、refs=5。该路径旧视频对象寿命长于CPU分配的使用期。仅为“旧线性视频图像→新CPU上传BC7图像”支持独立Metal图像，不放开一般GPU写入纹理的不同布局别名。新增live旧纹理的重叠BC7回归修复前退出42，修复后验证BC7解码像素与旧图像不变、释放及后续R8重分配通过；已纳入完整native检查。第5版已签名安装，真机继续验收。完整native（含ASan/UBSan、TSan、真实Metal、切栈）与合成媒体检查通过。

复核同时封闭该CPU采样路径的GPU写入边界：录制复制命令时明确拒绝以linear纹理为目的端（渲染目标原已有usage检查）。新增同格式R8复制负例修复前返回成功、修复后退出42并给出明确原因，避免CPU副本与GPU写入静默失步。

第5版实际切后台再前台后，LLDB取得send_input中result=-12903，SDK头文件确认kVTInvalidSessionErr。保留最近IDR至当前输入的压缩包（上限64MiB/1024包），失效时重建VT会话并重放当前GOP；按本GOP已收到的PTS去重，保留pending/ready的既有帧。新增第5个B帧包后主动Invalidate测试修复前-12903中止，修复后12帧完整、一次且展示顺序正确。UIKit inactive/active现驱动媒体暂停；审查发现“前台入队、后台执行”竞态，新增阻塞worker的排队回归修复前失败。worker开始及每次视频提交前重新等待active后，该回归和媒体生命周期检查全部通过。第7版已构建签名，仍需真机按键、剧情及前后台恢复验收。

打包自检发现早期xcodegen生成的Info.plist固定CFBundleVersion=1，虽然每次二进制签名/安装路径不同，但命令行CURRENT_PROJECT_VERSION不会反映到应用版本。已将源plist改为构建设置替换，并在boot.log加入构建号。第7次迭代候选归档完整性通过（压缩4202260779字节、553文件），不作为已验收最终包。此轮USB清单未发现iPhone，CoreDevice只报告localNetwork；网络安装超过8分钟仍未完成、未返回安装成功，已中断该安装并请求用户恢复数据线。构建8将用于下一轮实际安装与启动版本核对，剧情/存档及持续运行尚未验收。

构建8候选IPA已导出并通过codesign --verify --deep --strict、profile身份/有效期及ZIP CRC检查；553文件、4202260846字节，SHA-256 65082ee68c15a4cac3cadffd511982b16178ef3b998722b4c576fde98e519cf2，CFBundleVersion=8。仍未完成该版本的真机安装、按键、剧情和存档验收，不标记最终稳定版。旧候选归档已在新版验证完整性后删除，游戏资源与签名信息仍仅留本地忽略目录。

恢复有线连接后，构建8安装成功；设备实际应用清单确认bundleVersion=8，启动日志出现Hayarigami build 8 start，已用LLDB连接该进程并读回其真实标题帧，按A到游戏选择及第一代Now Loading。此项仅证明最新构建的安装、启动和当前输入，片头跳转及剧情仍在本轮真机验收中。

### 2026-10-20 build 9: measured symbol-lookup bottleneck

- User confirmed build 8 remained very slow after LLDB detached. A 10-second iPhone Time Profiler recording (PID 95505, `.local/runtime/device-build8-performance.trace`) captured 7,533 running CPU samples on the game/render thread, including 5,908 in `lookup`; symbolication shows repeated `hy_symbol` linear scans and string comparison. The audio renderer also repeatedly resolves exports. These are CPU sample proportions, not frame-time percentages.
- Replaced per-call export scans with one immutable sorted index per loaded module, built before relocation. Binary search preserves module order, undefined exclusion, absolute values, and the first original symbol for duplicate names. No mutable runtime cache or rendering behavior changes.
- Regression: original linear implementation fails the comparison-count assertion for 40,000 exports; indexed implementation resolves all exports and a missing name with 654,481 comparisons. Weak/strong imports, host definitions, duplicate exports and absolute exports remain covered. ASan/UBSan, the full native suite including TSan and actual Metal checks pass (`perf-native.log`); build 9 signs successfully. Device performance comparison and user acceptance are still pending.

- Build 9 was installed and its actual boot log identifies `Hayarigami build 9 start`; no STOP/SIGNAL appeared in the inspected current-run log. A second detached 10-second Time Profiler recording (`device-build9-performance.trace`) no longer lists export scanning among its leading hot paths. It sampled the first-generation loading/intro path, whereas build 8 sampled story play, so the total sample counts are not a controlled same-scene FPS comparison.
- The user explicitly reported **明显改善** while playing build 9 without LLDB attached. This validates a noticeable improvement, not a measured 60 FPS guarantee. Remaining samples include linear video texture uploads, shader/resource setup and synchronous GPU submission; no additional speculative rendering changes were bundled. Manual save/reload and cold-launch verification remain pending.

- Build 9 candidate IPA export passed strict/deep App signature checks, matching bundle/profile and unexpired provisioning, and all ZIP entry CRCs: 553 files, 4,202,261,113 bytes, SHA-256 `9a0c3c20b78bdd05dcd09a77b90ce269bc1dc49d83b000c5200335cb6c954606`. The archive is local-only; the tested installation source is the same signed App. Provisioning expires 2026-10-27 09:00:50 UTC. The phone registry confirms bundle version 9. All 22 Python unit tests pass.
- Read-only device copies of Documents/Save completed successfully but contained no files before the requested manual save. This does not validate save/reload; awaiting an actual in-game manual save before terminating the running game.
