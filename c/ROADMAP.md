# KUI C SDK Roadmap

## 当前状态（2026-08-26；交付形态 2026-11-20 更新）

Windows x64 Full Profile 已完成实现、冻结、双配置构建、源码 Wrapper、
11 个纯 C 快速示例、211 个 canonical 纯 C11 迁移与 189 个 Wrapper native
examples 的包内源码及 staging 自举 Release 产物交付、解包重编译、GUI 启动和
消费闭环。

交付形态（3.61.8 补出起）：C 包 `kui-c-sdk` 只留纯 C（11 + 211 示例、hello 扩展、纯 C 音频插件）；源码 C++ Wrapper
与 189 个 Wrapper 示例单独成包 `kui-cpp-wrapper-sdk`，带与 C 包逐字节相同的运行库，是 C++ 使用方的默认推荐。
下面 M2 / M3 的「随包」「ZIP」描述的是当时 C 包里含 Wrapper 的形态，Wrapper 部分现以 Wrapper 包为准。

### M1：Core ABI Freeze — DONE

- 建立 opaque handle、UTF-8 length、状态码、所有权与异常隔离约定。
- 完成 Platform、Window、Document、Element、Event、style、geometry、DevTools。
- 建立 product-major export manifest、函数签名 SHA-256、POD/enum tripwire。

### M2：Core Implementation / Package — DONE

- `kui_c.dll` 支持仓内和对已发布 C++ SDK 的 standalone 构建。
- Release/Debug C/core DLL、lib、PDB、CMake config 和源码 Wrapper 随包。
- MSVC/clang-cl、C/C++、`/MD`/`/MDd`/`/MTd` 独立消费通过。

### M3：Windows Full Profile — DONE

- 新增 networking、Lua、custom elements、dynamic extensions、media、Lottie、
  PDF、窗口/平台专业接口及 3D extension invoke 协议。
- Canvas2D/WebGL 通过已对齐的 Lua/DOM/Window 链路运行。
- C++ Wrapper 补齐 native include shim 和高层对象模型。
- native example 兼容集由 105 扩展到 189，新增迁移 84 个专项 example。
- C SDK `examples/` 提供 11 个纯 C11 专项示例；`examples/canonical/` 对正式
  Windows C++ SDK 的 211 个 example 产品逐一提供纯 C11 入口、页面与素材；ZIP 的
  `cpp_wrapper/examples/native/` 物化全部 189 个同源 main，并随包提供运行素材。
- 解包硬门对 11 个快速 C examples 执行 Release/Debug 全编译与 6 项运行，对
  canonical C examples 执行精确 211 计数、Release/Debug 211/211 全编译和
  预编译 10 项、重编译 8 项代表运行，对 Wrapper
  examples 执行精确 189 计数和 Release 189/189 全编译。
- 正式 ZIP 的 `bin/` 交付最终 C staging 自举的 C 11、canonical C 211、
  Wrapper 189、Wrapper
  demo 1、hello extension 1 和纯 C audio plugin 1；manifest 与解包门拒绝数量、
  版本或来源漂移。
- C SDK 交付 extension/media 纯 C 协议头与 `KuiC::ExtensionAPI` /
  `KuiC::MediaAPI` authoring targets；hello extension 可从解压包独立重建。
- `kui_media_audio` production plugin 已迁为纯 C11，源码、固定 miniaudio 单头/
  许可证与 staging 自举 DLL 随包，解包后再次重编和校验 26 个 exports；Editor、
  PDF、Video 的独立 CMake 可直接消费上述 authoring targets，内部仍保留 C++ 实现
  与各自第三方依赖。
- C SDK 包包含 extension/media/3D/PDF/FFmpeg runtime 与 sidecar。
- ABI 基线为 492 个 product-major-3 导出（3.61.8 在 3.61.7 的 195 个之上追加 297 个：表单控件当前状态 6 个，主线程投递与主线程异步 HTTP 2 个，常用窗口 / DOM / 平台操作 33 个（含按属性名读取计算样式），菜单栏 / 托盘 / 快捷键 / Toast / 截图 25 个，路由 / 数据绑定 / 动画状态 / Observer 40 个，弹出层 / `<input>` 选择器 / 游戏音频 37 个，媒体与 Lottie 播放控制 9 个，扩展与 DevTools 生命周期 8 个，拖放数据 9 个，脚本引擎控制 15 个，自定义元素自绘 / tick / 输入 22 个，组件 65 个，文档样式配置 2 个，默认右键菜单开关 2 个，事件的 relatedTarget 1 个，宿主集成 21 个——窗口创建属性 / 启用 / 单帧渲染 / DevTools 令牌开关、惯性滚动与滚入视图、脚本定时器 tick、滚轮增量、捕获阶段监听、基准路径与渲染 DPR、控制台 / 调用栈 / 崩溃报告、关闭输入法）。

## 后续范围

- Linux x64 / macOS arm64：Release 正式包已发布（3.61.8，`T-DEBT-SDK-C-PACKAGE-PLATFORM-PARITY`），
  与 Windows 同一冻结清单与验收口径。
- MinGW：明确跳过，需建立编译、打包与运行证据后才能扩大发布声明。
- 新增 C++ 公共 API：必须同步设计 additive C ABI、Wrapper 和回归，不能仅
  依赖 189 examples 的静态覆盖。
- Go/Rust 等绑定有独立版本线和发布门，不因本里程碑自动成为 GA。
- 内部 renderer/backend 类、C++ 继承式自定义 paint/layout 等不适合作为稳定
  跨编译器 ABI；若要公开，应设计数据驱动或回调式 C 协议，不能暴露 C++ 类型。
- `kui_ext_3d` 的 host-facing 协议虽为 C ABI，但当前实现链接 `Kui::Kui` 并使用
  native engine/Filament/Bullet 类型；纯 C SDK 可运行随包 DLL，不能用于重建
  该 production extension，除非未来先完成实现层解耦。

## 维护门

```powershell
python scripts/check_c_abi.py --selftest
python scripts/check_c_abi.py --binary sdk/c/build/Release/kui_c.dll
ctest --test-dir sdk/c/build -C Release --output-on-failure
ctest --test-dir sdk/cpp_wrapper/build-full-profile -C Release --output-on-failure
python scripts/verify_c_sdk_package.py --package <kui-c-sdk.zip> --version <X.Y.Z>
python scripts/verify_cpp_wrapper_sdk_package.py --package <kui-cpp-wrapper-sdk.zip> --version <X.Y.Z>
```
