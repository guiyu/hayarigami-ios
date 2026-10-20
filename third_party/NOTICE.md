# 通用参考组件

`src/runtime/gate.S` 改编自 D2R Mobile 的通用寄存器门和线程入口；MIT 许可见 D2R-Mobile-LICENSE。

`src/gfx/nvn_tex.{c,h}` 与 `nvn_deswizzle.{c,h}` 来自同一工程的通用 Tegra 块线性布局组件，仅读取复制这些文件，没有引入该游戏的 shader facts、规则、地址表或渲染旁路。`nvn_deswizzle.h` 的相对 include 路径已修改；布局与搬运新增本作 SDK 对照过的单层、1/2/4/8 字节像素及 BC7 压缩块稀疏瓦片宽度支持；预排布参数已按本作 SDK 验证的 H/D/W 位字段解码。布局算法是该工程对 deko3d 的改编，并非原版 deko3d；其 zlib 许可见 deko3d-LICENSE。原始公开来源：<https://github.com/devkitPro/deko3d/blob/master/source/dk_image.cpp>。

复制时 SHA-256（修改 include 前）：

| 文件 | SHA-256 |
|---|---|
| nvn_tex.c | 05a0fe55d48408a9b2fd924665c66a9a7eb2db62e2b206967acbf33ef7c4b890 |
| nvn_tex.h | 16b2a3b217d7fce318d8fac2f93f1e95748b4be17f2db5d28c3e2ac5eacc7f3c |
| nvn_deswizzle.c | 989859a8c0112ca430d4788dbe428efac6687e195a8675589a171dc8421d3b4d |
| nvn_deswizzle.h | 8760c6b3327088cb51c87df7d71845c97f9b866414437a0bc41498b0746a431a |

这些通用算法的存在不证明本作所有资源格式或 GPU 布局均已兼容。平台入口目前只接受已实现的 2D 单采样纹理，其余形态明确失败。

离线着色器工具 `tools/shader_xlate` 使用外部本地的 Ryujinx.Graphics.Shader、Ryujinx.Common 与 Spv.Generator 通用翻译库。源码和二进制未复制入此仓库；构建报告记录所用 DLL 的 SHA-256。生成的 SPIR-V、MSL 与 Metal 库包含用户本地游戏派生内容，均仅存 `.local/`。翻译工具未复制 D2R 的 shader facts、槽位或材质规则；只生成显式的 `float-2d-upper-left-zero-one-v1` 特化；后续绘制必须检查并拒绝不匹配的纹理和状态，几何阶段、数组、storage/image 等缺少证据的路径明确失败。

`third_party/bcdec/bcdec.h` 为 Sergii Kudlai 的 bcdec v0.985，按 MIT 选项使用，授权原文见同目录 LICENSE。文件从旧项目的通用 vendor 副本逐字节复制，未改写；公开上游为 <https://github.com/iOrange/bcdec>。本作只接入 BC7 解码，不使用旧项目的纹理事实或专用回退规则。`src/gfx/bc7.c` 为独立边界与裁切封装。合成 BC7 八种模式已与真实 GPU 解码进行独立对照。
