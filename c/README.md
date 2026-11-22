> **本目录 / This folder** — C 包（`kui-c-sdk-3.61.8-<平台>.zip`，[Release](https://github.com/kui-engine/kui/releases/tag/v3.61.8)）里除运行库以外的部分；运行库在仓库根的 `runtime/<平台>/`，`find_package(KuiC)` 配置在仓库根的 `cmake/`。下文是包内 README 原文，其中「包根目录」的 `bin/`、`lib/` 对应 `runtime/<平台>/`，`cmake/` 对应仓库根的 `cmake/`；从本仓库编译示例见[根目录 README](../README.md)。
> Everything of the C package (`kui-c-sdk-3.61.8-<platform>.zip`, [Release](https://github.com/kui-engine/kui/releases/tag/v3.61.8)) except the runtime, which is in `runtime/<platform>/` at the repository root (the `find_package(KuiC)` config is in the top-level `cmake/`). The text below is the package README; its package-root `bin/` and `lib/` are `runtime/<platform>/` and its `cmake/` is the top-level `cmake/`. See the [top-level README](../README.md) for building from this repository.

# KUI C SDK

> **发布状态：Windows x64 Full Profile GA；Linux x64 / macOS arm64 同口径 Release 包；C ABI 版本跟随 KUI 产品版本（根 `VERSION`）。**

`kui_c.dll` 以纯 C ABI 暴露 KUI 的应用开发能力，公共头文件中不存在
STL、异常或 C++ vtable。当前 492 个导出已按 product major 3 冻结，
Release 二进制、扩展/媒体运行时和可重定位
`find_package(KuiC)` 包均进入 Windows 发布门；Linux x64 与 macOS arm64 发布同口径的 Release 包
（见下方「Linux / macOS 包」）。

> **C++ 使用方请用 C++ Wrapper 包。** 自 3.61.8 补出起，构建在这层 C ABI 上的源码式 C++ 门面
> （`<kui/kui.h>`、`KuiC::Cpp`、189 个同源原生示例与 C++ API 巡览示例）单独成包 `kui-cpp-wrapper-sdk-<版本>-<平台>.zip`，
> 带与本包逐字节相同的运行库，是用 C++ 开发 KUI 应用的默认推荐，见 [../cpp_wrapper/README.md](https://github.com/kui-engine/kui-cpp#readme)。
> 本包（`kui-c-sdk`）只留纯 C：`find_package(KuiC)` 提供 `KuiC::C`、`KuiC::ExtensionAPI`、`KuiC::MediaAPI`，
> 不再提供 `KuiC::Cpp`。

本轮 Full Profile 覆盖：Platform/Window/DOM/Event/CSS/geometry/DevTools、
networking、Lua、custom elements、dynamic extensions、media、Lottie、
Canvas2D/WebGL 所需脚本桥、PDF，以及独立 `kui_ext_3d` 调用协议。发布 ZIP 不是只携带清单：
`examples/` 内含 11 个纯 C11 Full Profile 快速示例，`examples/canonical/` 内含 225 个标准示例
（211 个对正式 C++ SDK example 产品逐一迁移 + 14 个 API 巡览示例）的纯 C11 源码、页面和素材；`bin/` 是运行库（`kui_c`、`kui`、扩展 / 媒体插件、FFmpeg），其中纯 C11
音频插件 `kui_media_audio.dll` 由打包时对着最终 staging 自举编译，源码在 `extensions/kui_media_audio/`。

**预编译示例在单独的示例包里**（2026-11-20 起）：`kui-c-examples-<版本>-<平台>.zip` 带由最终 staging 自举编译的
11 个快速示例、225 个 canonical 示例与 `kui_ext_hello`，以及与本包逐字节相同的运行库、`assets/` 与 `script_html/`，解压就能运行
（同一个 GitHub Release 下载；`PREBUILT_ARTIFACTS.json` 冻结其精确清单）。本包里的源码可以编出同样的程序（见下方「构建示例」）。

本包是公开的 SDK 包：只带 Release 运行库，不带符号，不含 KUI 引擎核心的源码。C ABI 层的实现 `src/`、C 测试 `tests/`、
构建它们的 `CMakeLists.txt` 与音频插件源码（含随附的 miniaudio）随包提供，见下方「C ABI 层源码」。

范围边界：Windows x64 的 MSVC 与 clang-cl、Linux x64 的 gcc（Debian 12，g++ 12）、macOS arm64 的
Apple clang 已验证；MinGW 不作发布声明。C ABI 对齐的是公共 Full Profile 和现有
211 个正式 example 产品所使用的公开能力；C++ SDK 的内部渲染类、任意继承扩展点或未来
新增 API 不属于“自动等价”，新增公共能力必须继续以 additive C ABI 对齐。

## 使用发布包

```cmake
find_package(KuiC 3 CONFIG REQUIRED)

add_executable(c_app main.c)
target_link_libraries(c_app PRIVATE KuiC::C)
kui_c_copy_runtime_dlls(c_app)
```

`KuiCConfig.cmake` 与 Wrapper 包里的是同一份：`KuiC::Cpp` 只在带 `cpp_wrapper/` 的 Wrapper 包里定义
（`KuiC_CPP_WRAPPER_FOUND` 为 `TRUE`），本包里链接它会在配置期报「找不到目标」。

`kui_c_copy_runtime_dlls()` 会按配置复制 `kui_c.dll`、匹配的 `kui.dll`，
并复制包内的 extension/media/3D/PDF/FFmpeg 运行时和 material sidecar。Linux / macOS 上 `libkui_c`
与 `libkui` 经 RPATH 找到、不复制，只把 `bin/` 里按目录加载的扩展 / 媒体插件放到目标旁边。

### Linux / macOS 包

`kui-c-sdk-<版本>-linux-x64.zip` / `kui-c-sdk-<版本>-macos-arm64.zip` 与 Windows 包同一份内容（对应的示例包
`kui-c-examples-<版本>-linux-x64.zip` / `-macos-arm64.zip`），差异只在平台约定：

- `lib/` 放链接用的 `libkui_c.so|.dylib` 与 `libkui.so|.dylib`（同时也是运行库）；`bin/` 放按目录加载的扩展 / 媒体
  插件（示例按可执行文件所在目录找扩展；示例包的 `bin/` 再加上程序与 `assets/`，`lib/` 相同）。
- 只有 Release：Windows 的 Debug 变体是为了 MSVC Debug CRT，POSIX 没有这个问题；`KuiC_USE_DEBUG_VARIANT`
  在这两个平台上不起作用。
- 不带构建机路径：`libkui_c` 的 RPATH 只有 `$ORIGIN`（`@loader_path`），示例包里的预编译程序是 `$ORIGIN/../lib`。自己构建的
  程序由 CMake 默认的构建 RPATH 找到包里的 `lib/`；Linux 上 `KuiC::C` 还给链接器 `-rpath-link`，以便解析
  `libkui_c` 依赖的 `libkui`。
- 包内 canonical 示例可在解包目录直接重建（`cmake -S examples/canonical -B build
  -DCMAKE_BUILD_TYPE=Release -DKuiC_DIR=<包>/cmake`）。

Windows 包只带 Release 运行库：Debug 配置同样链接并部署 `bin/` 里的 `kui_c.dll` / `kui.dll`（C ABI 不传 CRT 对象，
`/MD`、`/MDd`、`/MTd` 的使用方都已实际运行）。`KuiC_USE_DEBUG_VARIANT` 只在包里另有 `bin/debug` 时起作用，本包里
开 / 关结果相同，`KuiC_DEBUG_VARIANT_SELECTED` 为 `FALSE`。

包根另有 `THIRD_PARTY_NOTICES.md`、`BUILD_INFO.txt`、覆盖全部文件的 `SHA256SUMS.txt` 与
`verify_package.py`（解压后自检）。包里还带 `tools/devtools_mcp/`（调试运行中的 KUI 程序，用法见其 README）和 `docs/`（14 份与语言无关的文档，索引在 `docs/README.md`）；C++ 专属的入门、教程与组件 / 扩展编写指南只在 C++ 包里。

### C ABI 层源码

`src/`（14 个文件）是 `kui_c` 的实现，`tests/`（7 个 C 测试）与包根 `CMakeLists.txt` 是 KUI 维护方构建、测试它的工程。
它要 `find_package(Kui)`，即原生 C++ SDK（`kui-cpp-sdk`）的头文件与导入库：

```powershell
cmake -S . -B build-kui-c -DKui_DIR=<kui-cpp-sdk 解压目录>/cmake
cmake --build build-kui-c --config Release
```

原生 C++ SDK 目前不公开，所以这份源码对公开用户是参考材料（看清某个 C 函数怎样映射到引擎），**用不着自己构建**：
包里 `bin/` / `lib/` 就是这份源码构建出的 `kui_c`，发版验收会用同一次产出的原生 C++ SDK 按这份 `CMakeLists.txt`
重建 `kui_c`、核对导出与冻结清单精确一致并跑 7 个 C 测试。

### 构建示例

解压后的包可直接构建示例：

```powershell
cmake -S examples -B build-c-examples -DKuiC_DIR=$PWD/cmake
cmake --build build-c-examples --config Release

cmake -S examples/canonical -B build-canonical-c-examples -DKuiC_DIR=$PWD/cmake
cmake --build build-canonical-c-examples --config Release
```

不想编译就下载示例包 `kui-c-examples-<版本>-<平台>.zip`，直接运行其中的 `bin/kui_c_example_*.exe`、`bin/kui_c_*.exe`。
这些 Release 程序与 DLL、`assets/` 位于同一目录；它们不是从仓库构建目录复制，而是打包时仅使用最终 C SDK staging 重新
配置和编译的。加载 `kui_ext_hello` 的扩展类示例要它在程序旁边：示例包里带着；自己编示例时用 `extensions/hello/` 编出来
放过去（本包 `bin/` 不带这个示例扩展）。

纯 C extension authoring 使用独立协议 target：

```cmake
find_package(KuiC 3 CONFIG REQUIRED)
add_library(my_extension SHARED my_extension.c)
target_link_libraries(my_extension PRIVATE KuiC::ExtensionAPI)
```

包内 `extensions/hello/` 是可独立重建的完整例子，`extensions/kui_media_audio/` 是纯 C11 的生产级媒体插件源码
（带 miniaudio 单头文件与许可，`cmake -S extensions/kui_media_audio -B build-audio -DKuiC_DIR=<包>/cmake` 可直接重建）。
3D host entry points 虽然是
纯 C ABI，但现有 `kui_ext_3d` 实现链接 `Kui::Kui` 并使用 native engine、
Filament 与 Bullet 类型，因此不能诚实地由纯 C SDK 构建；其预编译 runtime
仍随 C SDK 交付，扩展本体开发需使用 native C++ SDK。

现有 production runtime 对 C 与 Wrapper 应用（Wrapper 包带同一份运行库）已可直接使用：`Document::initExtensions()` /
`ExtensionManager::invoke()` 覆盖 Editor、PDF 与 3D，`Element` 的 media 方法覆盖 Audio/
Video，`kui_c_copy_runtime_dlls()` 会部署包内相应 DLL 与 sidecar。扩展源码构建则分层处理：
Editor、PDF、Video、3D 这些 production 扩展与媒体插件只以预编译运行库随包交付，实现源码不随包；Audio 插件的源码随包；
自己编写扩展 / 媒体插件用包内协议头（`KuiC::ExtensionAPI` / `KuiC::MediaAPI`），参考 `extensions/hello/` 与 `extensions/kui_media_audio/`。

## 发布矩阵

| 项目 | Windows x64 状态 |
|---|---|
| C ABI 导出 | 492 个，声明/实现/PE export/签名摘要完全一致（Linux / macOS 按 ELF / Mach-O 导出同样精确核对） |
| 配置 | Release（带 import library），不带符号；使用方的 Debug 配置同样链接它 |
| 消费编译器 | MSVC 2022、clang-cl 22 |
| consumer CRT | `/MD`、`/MDd`、`/MTd` 均已实际运行 |
| 消费语言 | 纯 C11（C++17 Wrapper 在单独的 `kui-cpp-wrapper-sdk` 包，同一矩阵另行验收） |
| C ABI examples | 11 个快速示例与 225/225 个 canonical 纯 C11 源码（211 个迁移 + 14 个 API 巡览）；解包后 Release/Debug 全编译，运行重编译的 6 个无窗口快速示例与 8 项 Full Profile 代表；staging 自举的 236 个 Release EXE 在示例包里，全部启动 |
| C ABI 层源码 | `src/` + `tests/` + `CMakeLists.txt`：用同一次产出的原生 C++ SDK 重建 `kui_c`（导出与冻结清单精确一致）、11 个示例与 7 个 C 测试并跑 ctest |
| Extension/media | hello 与纯 C Audio 的源码都随包，解包重编并分别校验 4 / 26 个 exports；随包的 Audio DLL 由 staging 自举；其他 production runtimes/sidecars 随包 |
| Linux x64 / macOS arm64 | Release 包；冻结导出门；音频插件由最终 staging 自举，11 + 225 + hello 进示例包；解包后重建全部示例、hello 与音频插件、`kui_c` 与使用方并运行代表程序（共享 Mac 上不启动 GUI 示例） |
| 跳过 | MinGW |

包验证入口：

```powershell
python scripts/check_c_abi.py --binary sdk/c/build/Release/kui_c.dll
python scripts/verify_c_sdk_package.py `
  --package dist/kui-c-sdk-<版本>-windows-x64.zip `
  --version <版本>
```

发版时还给 `--cpp-sdk-package dist/kui-cpp-sdk-<版本>-<平台>.zip`（重建 `kui_c` 与 C 测试）。C++ Wrapper 包的验收是
`scripts/verify_cpp_wrapper_sdk_package.py`（同样的参数），示例包的验收是 `scripts/verify_examples_packages.py`。

## API 约定

- 所有对象经 opaque handle 传递。
- 所有输入字符串均为 UTF-8 `(pointer, length)`；长度为 0 时读取 NUL 结尾。
- 输出字符串使用两次调用模式，调用方分配和释放内存。
- 所有失败经 `KuiStatus` 返回；异常不得跨越 ABI。
- 返回的 Document/Element/HTTP response 等拥有引用，必须调用对应
  `kui_*_release()`；回调参数中的 `KuiEvent*` 与 element handle 仅借用。
- `kui_http_client_fetch_async` 的 response handle 由回调接管并释放；它的回调在 HTTP 工作线程上执行。
  要在回调里操作 DOM，改用回调在主线程执行的 `kui_http_client_fetch_async_on_main_thread`。
- 线程：除 `kui_platform_post_to_main_thread` 外，所有函数只能在主线程（调用
  `kui_platform_init` 的线程）调用；其他线程的结果用它交回主线程。平台关闭时还在排队
  的任务不执行，改调可选的 `discard_fn` 释放状态。
- 同一 product major 内不得删除或修改已冻结函数、enum 数值或公开结构布局。

## 最小示例

```c
#include <kui_c.h>

int main(void) {
    KuiDocument* document = NULL;
    KuiScriptEngine* engine = NULL;
    int success = 0;
    char error[256];
    size_t error_len = 0;

    if (kui_document_create(&document) != KUI_OK) return 1;
    kui_document_load_html(document, "<h1>Hello from C</h1>", 0);
    if (kui_script_engine_create(&engine) != KUI_OK) return 2;
    kui_script_engine_bind_document(engine, document);
    kui_script_engine_eval(engine, "value = 6 * 7", 0, "main", 0,
                           &success, error, sizeof error, &error_len);
    kui_script_engine_release(engine);
    kui_document_release(document);
    return success ? 0 : 3;
}
```

详细里程碑见 [ROADMAP.md](./ROADMAP.md)，ABI 冻结规则见
[abi/README.md](./abi/README.md)，源码式 C++ 门面（单独的 Wrapper 包）见
[../cpp_wrapper/README.md](https://github.com/kui-engine/kui-cpp#readme)。

## License

MIT，见仓库根目录 `LICENSE`。
