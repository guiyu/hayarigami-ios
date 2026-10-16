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

通过后 `candidate/Guest.framework/Guest` 是实验性 iOS Mach-O 容器。`file` 与 `xcrun vtool -show-build` 可检查架构和平台。它没有宿主、重定位后的实际运行状态、GPU 翻译、签名或应用入口，不是安装产物。

本次完整复跑的结果位于 `.local/repro-rewrite/`；初始分析为 `.local/run1/report.json`，参考审计为 `.local/audit.json`。

## 后续真机阶段的进入条件

1. 实现独立最小宿主，验证 SDK 启动、模块重定位、TLS、系统调用与失败处理；不静默把未知调用当作成功。
2. 原始游戏代码产生可见标题画面，确认本作 NVN 调用、NMPLTEX1 纹理与着色器的 Metal 路径。补齐 AudioRenderer/影片、输入、生命周期和本作独立存档目录。
3. 构建真正包含宿主与本地游戏代码的 iOS App，配置独立 Bundle ID 和独立数据容器；签名资料保持本地，不读取或改写旧 D2R 配置。
4. 只有到此阶段才通知用户连接 iPhone，核对待安装的是新 App。禁止卸载/替换暗黑、迁移或触碰旧存档。
5. 安装后分别记录：能启动、真实标题画面、进入剧情、选项/音频/影片、保存退出与恢复、后台恢复及持续运行。没有完成的项目标为未验证。

Personal Team 可用性和实际签名期限以到时 Xcode 返回结果为准；本次未创建签名、未检查设备、未安装。
