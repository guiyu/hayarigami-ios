# 离线复跑与后续安装 SOP

## 当前阶段

现在可以复跑分析、资源库编译、游戏代码离线改写/验证与容器生成。**当前没有安装命令或可玩的 App**；不要安装实验性 Guest.framework，也不需要连接手机。所有生成目录必须是本仓库 `.local/` 的子目录；脚本拒绝越界路径和输出覆盖。

在仓库根目录执行。先准备 Python 环境（已存在环境可复用，精确依赖在 requirements.txt）：

```sh
python3 -m venv .local/venv
.local/venv/bin/python -m pip install -r requirements.txt
```

本次实际使用的是上层工作目录的 `work/venv/bin/python`，依赖与上述文件一致；无需为复看既有结果重复安装。Xcode/LLVM 已存在，本项目没有安装或改变它们。

下面变量由本地使用者填入文件路径，**不填密钥内容**。不要 source 旧项目 local.env，也不要复用其签名或设备配置。

```sh
PY="$PWD/.local/venv/bin/python"
NSP="/absolute/path/to/010095b01af94000.nsp"
KEYS="/absolute/path/to/prod.keys"
REFERENCE="/absolute/path/to/d2r-mobile"
LLVM_BIN="/opt/homebrew/opt/llvm@22/bin"
```

## 1. 读取与分析

```sh
"$PY" -m unittest discover -s tests -v
"$PY" scripts/analyze.py --nsp "$NSP" --keys "$KEYS" --out "$PWD/.local/analysis-new"
```

预期：标题和版本正确，4 个模块、30 个资源。新目录包括 ExeFS、解压模块、索引报告以及用于验证的约 5 MB 标题纹理归档；其他数 GB 资源保持在原 NSP 中按需读取。输出目录已存在时明确失败；重新运行使用新目录名。工具仅支持本次已观察的 PFS0/NCA3/标准 key-area/CTR/IVFC 形态，遇到其他加密、补丁或 Title ID 即停止，不猜测解包。

输出 report.json 包含动态符号和资源索引，属于本地派生分析材料。不要将 `.local/` 内容加入 Git，不要使用 `git add -f`。

## 2. 编译实际资源读取组件

```sh
sh scripts/build_resource_reader.sh
"$PY" scripts/verify_resources.py --nsp "$NSP" --keys "$KEYS" \
  --library "$PWD/.local/native/libpsfs.dylib"
```

预期：C 边界测试通过，生成 iOS arm64 `.a`，真实输入报告 `archives=22, members=4932, all_entries_match=true`。零覆盖、错误标题和解析差异均失败。Mac 上的运行验证不等于该库在 iPhone 上验证。

## 3. 记录参考工具并复跑改写

```sh
"$PY" scripts/audit_reference.py --analysis "$PWD/.local/analysis-new/report.json" \
  --reference "$REFERENCE" --out "$PWD/.local/audit-new.json"
"$PY" scripts/rewrite_probe.py --reference "$REFERENCE" \
  --audit "$PWD/.local/audit-new.json" --analysis "$PWD/.local/analysis-new" \
  --llvm-bin "$LLVM_BIN" --out "$PWD/.local/rewrite-new" --equivalence
```

等价验证需要允许 Unicorn 本地 JIT 的执行环境。Codex 沙箱不允许时，预检会退出；在获准的本地执行环境重跑，不能跳过后声称验证通过。所有日志进入新目录，命令结果记录在 commands.json。

参考工具变化后脚本拒绝沿用旧审计；重新审查变更再产生新审计文件。没有 vendoring 整个 D2R 工程，未来独立化运行时之前仍需这个只读工具依赖。仅记录 Git commit 不足以识别本地工具，故同时检查文件 SHA-256。

通过后 `candidate/Guest.framework/Guest` 是实验性 iOS Mach-O 容器。`file` 与 `xcrun vtool -show-build` 可检查架构和平台。它没有应用宿主与可玩性验收；新版可能带链接器式 ad-hoc 签名区，但仍没有适配 App 身份的分发签名，不是安装产物。

初次完整复跑保留在 `.local/repro-rewrite/`。2026-10-20 新分析为 `.local/analysis-upstream-20261020/report.json`，新工具审计为 `.local/audit-upstream-20261020.json`，等价试验与容器为 `.local/rewrite-upstream-20261020/`；新旧模块字节相同。后续诊断启动可直接使用新目录。

## 后续真机阶段的进入条件

1. 实现独立最小宿主，验证 SDK 启动、模块重定位、TLS、系统调用与失败处理；不静默把未知调用当作成功。
2. 原始游戏代码产生可见标题画面，确认本作 NVN 调用、NMPLTEX1 纹理与着色器的 Metal 路径。补齐 AudioRenderer/影片、输入、生命周期和本作独立存档目录。
3. 构建真正包含宿主与本地游戏代码的 iOS App，配置独立 Bundle ID 和独立数据容器；签名资料保持本地，不读取或改写旧 D2R 配置。
4. 只有到此阶段才通知用户连接 iPhone，核对待安装的是新 App。禁止卸载/替换暗黑、迁移或触碰旧存档。
5. 安装后分别记录：能启动、真实标题画面、进入剧情、选项/音频/影片、保存退出与恢复、后台恢复及持续运行。没有完成的项目标为未验证。

Personal Team 可用性和实际签名期限以到时 Xcode 返回结果为准；尚未使用个人证书签名、未检查设备、未安装；本地容器的 ad-hoc 重签测试不属于这些验收。
## 原生运行时增量复跑

在仓库根目录执行（所有输出仍在 `.local/`）：

```sh
sh scripts/build_runtime.sh
sh scripts/check_native.sh
sh scripts/build_audio_probe.sh
.local/runtime/probe-audio .local/repro-rewrite
.local/runtime/probe-audio .local/repro-rewrite --six-channels
sh scripts/build_nvn_probe.sh
.local/runtime/probe-nvn .local/repro-rewrite
sh scripts/check_ios_compile.sh
.local/runtime/hayarigami-host .local/repro-rewrite --boot .local/romfs
```

执行真实 ARM64 映射需允许本地原生代码执行的环境；沙箱中 SIGILL 不应当作游戏故障或通过。最后一条是诊断启动，未实现的平台调用会退出 42，不是游戏可玩验收。iOS 编译脚本只产出静态库，不生成 App/IPA。

音频对照需安装 requirements.txt，并在工作目录单独构建 vgmstream CLI（关闭不需要的可选解码器）。传入其绝对路径：

```sh
python scripts/verify_audio.py --romfs .local/romfs \
  --library .local/runtime/libdsp_adpcm.dylib \
  --reference /absolute/path/to/vgmstream-cli --out .local/audio-check-new
```

输出目录必须新建且位于 `.local/`。测试临时音频仅本地使用，成功后删除，保留摘要 JSON；失败目录供诊断，不作为可发布产物。


## 本地着色器分析与转换

```sh
"$PY" scripts/extract_shaders.py .local/run1/main.bin --out "$PWD/.local/shaders/source"
"$PY" scripts/build_shaders.py --source .local/shaders/source \
  --dependencies /absolute/path/to/audited/generic/translator/libraries \
  --out "$PWD/.local/shaders/metal"
xcrun clang -fobjc-arc tests/probe_shader.m -framework Foundation -framework Metal \
  -o .local/runtime/probe-shader
.local/runtime/probe-shader .local/shaders/metal/VERTEX_KEY.metal .local/shaders/metal/FRAGMENT_KEY.metal
```

依赖目录须含 Ryujinx.Common.dll、Ryujinx.Graphics.Shader.dll、Spv.Generator.dll；工具报告记录 SHA-256。这是外部本地依赖，仓库没有附带其源码或二进制。当前128组中1组几何阶段拒绝，整批命令返回失败并保留每项报告；不应改成忽略失败。可用重复的 `--key` 对指定项独立构建。当前示例 GPU 探针针对最初基本二维采样 vertex/fragment 配对，不是任意 shader 自动验收。

诊断启动在进入 SDK Start 时设置120秒上限。默认不输出每次已知系统调用；需要详细定位时设置 `HY_TRACE_SVC=1`，保留本地日志。主动 Break 和未实现接口仍输出停止信息与可用调用栈。

## 首绘 alpha 特化与 GPU 计时复核

完整基础库和选定特化必须分目录输出，避免选定 key 的 manifest 覆盖整批记录：

```sh
"$PY" scripts/build_shaders.py --source .local/shaders/source \
  --dependencies /absolute/path/to/audited/generic/translator/libraries \
  --out "$PWD/.local/shaders/metal-v2"
"$PY" scripts/build_shaders.py --source .local/shaders/source \
  --dependencies /absolute/path/to/audited/generic/translator/libraries \
  --out "$PWD/.local/shaders/metal-v2/alpha5-ref0" --alpha-test greater-zero \
  --key 803c26109c02466dc4077dcf0644fc0ce9b020061279fe179ce683b4a33eddb0
xcrun clang -std=c11 -Wall -Wextra -Werror -fobjc-arc tests/probe_alpha.m \
  -framework Foundation -framework Metal -o .local/runtime/probe-alpha
.local/runtime/probe-alpha .local/shaders/metal-v2/alpha5-ref0/803c26109c02466dc4077dcf0644fc0ce9b020061279fe179ce683b4a33eddb0.macosx.metallib
sh scripts/check_native.sh
```

首条整批构建仍会因1个不支持的几何阶段返回1；必须检查 manifest，不能称作全阶段通过。运行时加载尚待实现，须校验来源、Profile、阶段、alpha条件及 MetalLibraries 哈希。`test_gpu_counter.m` 已纳入 native 检查，验证真实 GPU 时间戳及纳秒校准；它不使尚未实现的游戏 ReportCounter 自动完成。首次提交的诊断仍明确失败；最新停止点取代此前 BindVertexBuffers 停止点。

真实首绘VS/PS的库完整性及GPU布局回归（依赖本地游戏派生缓存，不并入无游戏文件的合成检查）：

```sh
sh tests/check_shader_draw.sh .local/shaders/source .local/shaders/metal-v2
```

该测试验证256个实际GPU像素与六个拒绝路径，不代替队列执行、标题画面、签名安装验收。安装交付标准已明确为能进入游戏，不能以诊断版替代。
