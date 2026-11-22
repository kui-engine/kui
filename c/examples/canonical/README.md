# KUI 正式 Examples 的纯 C11 镜像

本目录提供 **225 个**逐项独立的 `main.c`：以 Windows C++ SDK 实际交付集合为权威口径的 211 个
（189 个 `examples/native` 目标，加 22 个当前正式顶层/工具产物），另加 14 个 API 巡览示例（`kui_api_*`，
源在 `examples/api_tours/`）。数量只由 `scripts/derived_sdk_checks.py --canonical-example-count` 读一处。所有
目标只链接 `KuiC::C`；公共窗口、资源、Lua、network/media/extensions/3D 宿主
位于 `runtime/`，逐例 HTML/Lua 页面位于 `assets/pages/`。

仓库递归可见 214 个 `main.cpp`，但其中 6 个是未进入正式 211 产品集合的旧式/开发
入口：`engine_demo`、`gl_context_sharing`、`interop_example`、`kui_3d_demo`、
`model_viewer`、`tank_game`。前四个 3D/GL 入口直接使用 native engine、Filament、
OpenGL 或 C++ ExtensionManager，`interop_example` 直接嵌入 Lua 库；这些依赖不是
`KuiC::C` 公共应用 ABI，不能在“只利用 C ABI SDK”的约束下诚实地逐行移植。
其可公开的 3D/Lua 能力已由本集合的 26 个 3D、5 个 Lua 及对应 C ABI 产品覆盖。

生成物来自 `scripts/generate_c_examples.py`，不得手改：

```powershell
python scripts/generate_c_examples.py --check
python scripts/generate_c_examples.py
```

独立构建（使用解压后的 C SDK 或仓内 `sdk/c` staging）：

```powershell
cmake -S examples/c -B build-canonical-c-examples -DKuiC_DIR=<sdk>/cmake
cmake --build build-canonical-c-examples --config Release --parallel 8
```

每个生成源都保留原目标名、权威 C++ 源路径和源文件 SHA-256 前缀。视觉页面与
内嵌 Lua 被复制到 C 版本；Canvas2D/WebGL 直接运行原始 Lua HTML 资源；Lua、
HTTP、media、extension 和 3D 目标由纯 C 宿主分别走对应 C ABI。复杂 C++
lambda 不会被伪装成 C++：公共交互通过 C 回调或页面内 Lua 实现。
