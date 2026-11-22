# KUI C ABI examples

这些示例全部是 C11 源码，并直接链接 `KuiC::C`：

| 示例 | 能力 |
|---|---|
| `hello_world.c` | Document、DOM、style、event、所有权 |
| `window_demo.c` | Window、timer、event loop |
| `lua_dom_demo.c` | Lua ScriptEngine 与 DOM bridge |
| `http_client_demo.c` | HTTP 同步请求、响应所有权与取消 |
| `platform_services_demo.c` | OS、monitor 和物理尺寸 |
| `custom_element_demo.c` | custom element 生命周期与 observed attributes |
| `media_lottie_demo.c` | media backend 状态与 Lottie |
| `extension_3d_demo.c` | extension loader 与独立 3D 协议 |
| `canvas_webgl_demo.c` | 通过 Lua bridge 使用 Canvas2D/WebGL |
| `pdf_export_demo.c` | PDF 导出 |
| `devtools_demo.c` | Window DevTools server |

在 C SDK ZIP 的解压根目录构建全部示例：

```powershell
cmake -S examples -B build-c-examples -DKuiC_DIR="$PWD/cmake"
cmake --build build-c-examples --config Release
```

正式 C SDK ZIP 的解包门还会从包内源码重新编译全部 11 个目标；无窗口的安全
示例会被实际执行。

完整迁移集位于发布包的 `examples/canonical/`：225 个标准示例——按正式 Windows C++ SDK 的
211 个 example 产品逐一迁移，另加 14 个 API 巡览示例——各有独立 `main.c`，全部直接链接 `KuiC::C`，并携带
自包含页面、Lua/Canvas/WebGL、media、Lottie 与 3D 素材。可独立构建：

```powershell
cmake -S examples/canonical -B build-canonical-c-examples -DKuiC_DIR="$PWD/cmake"
cmake --build build-canonical-c-examples --config Release
```

C 示例包（`kui-c-examples-*`）的 `bin/kui_c_*.exe` 是这 225 个目标由最终 C SDK staging 自举得到的
Release 产物，不依赖仓库构建目录。
