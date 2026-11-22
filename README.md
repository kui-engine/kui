# KUI

**English** · [简体中文](README.zh-CN.md)

KUI is a **cross-platform native GUI engine**: the interface is written in HTML / CSS, you drive it from C, C++, Go or Rust, and there are packages for Windows x64, Linux x64 and macOS arm64. It has its own layout and rendering engine (**no system WebView**) and is not a browser: no JavaScript, optional embedded
Lua scripting.

From an empty project to a window, with one DOM update and one event listener: [Quick start](QUICKSTART.md).

This repository is the **KUI SDK download hub, and it also holds the C interface**: the C headers, the frozen export list,
examples and documentation, plus the runtimes for three platforms (`runtime/`), so you can build and run right after cloning; the
SDK packages and prebuilt examples packages for every language are attached to the [Releases](https://github.com/kui-engine/kui/releases). The KUI engine
itself is not open source; the SDK (binaries, headers, the C++ Wrapper and Go / Rust binding sources, examples, documentation) is
released under the MIT license and is free for commercial use.

Current version: **KUI 3.61.8** (the C ABI and the C++ Wrapper share one version line); Go / Rust bindings **3.1.0**.

Jump to: [Which one](#which-one) · [Getting it](#getting-it) · [Building and running a C example](#building-and-running-a-c-example) · [Releases](https://github.com/kui-engine/kui/releases)

### What KUI can do

<table>
<tr>
<td width="50%" valign="top"><img src="docs/media/forms.png" alt="Form controls" width="100%"><br><b>Form controls</b><br>Text, email, password, date, time and color inputs, a select, radios, checkboxes, a slider and a textarea: standard HTML form controls drawn by KUI itself.</td>
<td width="50%" valign="top"><img src="docs/media/pickers.png" alt="Date, time and color pickers" width="100%"><br><b>Date, time and color pickers</b><br>date, time, datetime-local and color inputs come with their own picker popups (the packaged kui_cpp_html_22_input_pickers example with the date picker open; the UI language was set to English for the screenshot).</td>
</tr>
<tr>
<td width="50%" valign="top"><img src="docs/media/lottie.gif" alt="Lottie animation" width="100%"><br><b>Lottie animation</b><br>Built-in Lottie player: play JSON animations with the &lt;lottie-player&gt; element.</td>
<td width="50%" valign="top"><img src="docs/media/animation.gif" alt="CSS animations and transitions" width="100%"><br><b>CSS animations and transitions</b><br>@keyframes animations and transitions, triggered by hover or focus.</td>
</tr>
<tr>
<td width="50%" valign="top"><img src="docs/media/svg.png" alt="SVG" width="100%"><br><b>SVG</b><br>SVG renders as vectors: area charts, donuts, stroked icons and gradient shapes, in &lt;img&gt;, CSS backgrounds and inline.</td>
<td width="50%" valign="top"><img src="docs/media/filters.png" alt="CSS filters" width="100%"><br><b>CSS filters</b><br>Filters such as blur, brightness, contrast, grayscale, sepia, hue-rotate and drop-shadow, plus backdrop-filter.</td>
</tr>
<tr>
<td width="50%" valign="top"><img src="docs/media/pdf.png" alt="PDF export" width="100%"><br><b>PDF export</b><br>A laid-out, paginated page is written straight to PDF (the picture is the exported page).</td>
<td width="50%" valign="top"><img src="docs/media/editor.gif" alt="Extension: code editor" width="100%"><br><b>Extension: code editor</b><br>The kui_ext_editor extension: syntax highlighting, auto indent, bracket matching (TypeScript editing from the Windows examples package).</td>
</tr>
<tr>
<td width="50%" valign="top"><img src="docs/media/3d.gif" alt="Extension: 3D" width="100%"><br><b>Extension: 3D</b><br>The kui_ext_3d extension: an HTML panel over a live 3D scene (from the particles example of the C++ Wrapper examples package).</td>
<td width="50%" valign="top"><img src="docs/media/ext-widgets.png" alt="Extension: custom widgets" width="100%"><br><b>Extension: custom widgets</b><br>Extensions can register their own HTML elements and return data to the page through typed C calls (the data table example of the C++ Wrapper examples package).</td>
</tr>
<tr>
<td width="50%" valign="top"><img src="docs/media/devtools.png" alt="DevTools" width="100%"><br><b>DevTools</b><br>Built-in DevTools: DOM tree, computed styles, console, scripts, network and layout panels.</td>
<td width="50%" valign="top"><img src="docs/media/lua-debug.png" alt="Lua scripting and breakpoint debugging" width="100%"><br><b>Lua scripting and breakpoint debugging</b><br>Page scripts are optional embedded Lua; DevTools offers breakpoints, call stacks and scope inspection.</td>
</tr>
<tr>
<td width="50%" valign="top"><img src="docs/media/mcp.gif" alt="MCP: let an AI debug the UI" width="100%"><br><b>MCP: let an AI debug the UI</b><br>tools/devtools_mcp connects DevTools to an MCP client so an AI can read and change a running KUI program (before and after one edit).</td>
<td width="50%" valign="top"><img src="docs/media/media.png" alt="Audio and video" width="100%"><br><b>Audio and video</b><br>&lt;video&gt; and &lt;audio&gt; are played by the kui_media_* plugins (the picture is the video player example).</td>
</tr>
</table>

### Built with KUI

Applications built with KUI (screenshots only). **Their sources and prebuilt programs are planned for the next SDK release; this release does not include them.**

<table>
<tr>
<td width="33%" valign="top"><img src="docs/media/winform-themes.gif" alt="WinForms control gallery" width="100%"><br><b>WinForms control gallery</b><br>A WinForms-style control library written with KUI (Button, ListView, TreeView, TabControl and more), switching between Windows XP / 7 / 10 / 11 and macOS styles with one click.</td>
<td width="33%" valign="top"><img src="docs/media/abyss-lantern.gif" alt="Abyss Lantern" width="100%"><br><b>Abyss Lantern</b><br>Abyss Lantern, a 2D game from KUI Games, with its gameplay rendered by KUI.</td>
<td width="33%" valign="top"><img src="docs/media/neon-relay.gif" alt="Neon Relay" width="100%"><br><b>Neon Relay</b><br>A 2D team capture game from KUI Games (offline training match shown), from the same repository as Abyss Lantern above.</td>
</tr>
</table>

### Real runs on each platform

The same desktop application screens (a dashboard and a settings page, both pages of the prebuilt examples packages) running on each platform. Same command on all three platforms (the packaged `kui_c_advex_host`, page 1024×768); the pictures show the page only, without a window frame. Fonts follow each platform's system fonts, so glyphs and line heights differ slightly.

<table>
<tr>
<td width="33%" valign="top"><b>Windows 10 x64</b><br>Page content, captured through KUI&#x27;s DevTools.</td>
<td width="33%" valign="top"><b>Linux x64 (Debian 12)</b><br>Xvfb software rendering in a virtual machine; the window content cropped from the screen.</td>
<td width="33%" valign="top"><b>macOS 15 arm64</b><br>Page content, captured through KUI&#x27;s DevTools (the session used has no screen-recording permission).</td>
</tr>
<tr>
<td width="33%" valign="top"><img src="docs/media/dashboard-windows.png" alt="Windows: dashboard" width="100%"></td>
<td width="33%" valign="top"><img src="docs/media/dashboard-linux.png" alt="Linux: dashboard" width="100%"></td>
<td width="33%" valign="top"><img src="docs/media/dashboard-macos.png" alt="macOS: dashboard" width="100%"></td>
</tr>
<tr>
<td width="33%" valign="top"><img src="docs/media/settings-windows.png" alt="Windows: settings page" width="100%"></td>
<td width="33%" valign="top"><img src="docs/media/settings-linux.png" alt="Linux: settings page" width="100%"></td>
<td width="33%" valign="top"><img src="docs/media/settings-macos.png" alt="macOS: settings page" width="100%"></td>
</tr>
</table>

### Which one

| Language | Source repository (usable right after clone) | SDK package on the Release | Notes |
|---|---|---|---|
| C++ | [kui-cpp](https://github.com/kui-engine/kui-cpp) (recommended) | `kui-cpp-wrapper-sdk` | Headers compile in your project with your compiler; the only binary boundary is the pure C ABI. MSVC, clang-cl, gcc and Apple clang all work; on Windows `/MD`, `/MDd`, `/MT` and `/MTd` are all fine |
| C | this repository | `kui-c-sdk` | `kui_c.h` plus the runtime; the C ABI only grows within 3.x, so upgrading means swapping the runtime |
| Go | [kui-go](https://github.com/kui-engine/kui-go) | `kui-go-sdk` | `go get github.com/kui-engine/kui-go/v3@v3.1.0` (the module carries no runtime; the runtime is in the repository's `runtime/` or the SDK package) |
| Rust | [kui-rust](https://github.com/kui-engine/kui-rust) | `kui-rust-sdk` | git dependency `kui = { git = "https://github.com/kui-engine/kui-rust", tag = "v3.1.0" }`; `kui-sys` uses the repository's `runtime/<platform>` automatically (`KUI_C_SDK` can point elsewhere) |

### Getting it

```bash
git clone --depth 1 https://github.com/kui-engine/kui.git
```

The repository carries the runtimes for all three platforms (`runtime/`, 475.7 MiB in this version), so you can build and run right after cloning. **Please use `--depth 1`**: every version commits its runtimes, so the full history grows with each release (roughly one `runtime/` per version). To get another version use `git clone --depth 1 --branch v<version> …`, or `git fetch --depth 1 origin tag v<version>` in an existing clone.

### Building and running a C example

Requirements: CMake and a C compiler (Windows: MSVC from Visual Studio 2022; Linux: gcc / clang; macOS: Xcode command line tools). The minimum CMake version is what `CMakeLists.txt` declares (3.20); the versions actually verified for this release are in the table below.

**On Linux you also need** the system libX11, libXext, libXrandr, libGL and libstdc++ (common Debian / Ubuntu package names: `libx11-6 libxext6 libxrandr2 libgl1`; server installs lack the X11 and GL runtime by default). The programs connect to a graphical session; without a desktop prefix the commands with `xvfb-run -a` (the Go / Rust `./build.sh` runs tests and needs a display server too).

From the repository root on Windows (PowerShell; the export self-check runs exactly these commands in a clean `git clone` and the program starts and keeps running):

```powershell
cmake -S c/examples -B build/c-examples "-DKuiC_DIR=$PWD/cmake"
cmake --build build/c-examples --config Release --target kui_c_example_hello_world
.\build\c-examples\Release\kui_c_example_hello_world.exe
```

Linux / macOS (bash; the same commands on macOS. Verified after export in real `git clone`s: on Linux (Debian 12, Xvfb) these exact commands build and the programs start; on macOS arm64 the build and the dependency closure pass, but **no windowed program was launched** (the build Mac is a shared machine)):

```bash
cmake -S c/examples -B build/c-examples "-DKuiC_DIR=$PWD/cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build/c-examples --target kui_c_example_hello_world
./build/c-examples/kui_c_example_hello_world
```

Toolchains actually verified for this release:

| Platform | OS | CMake and C / C++ compiler | Go | Rust |
|---|---|---|---|---|
| Windows x64 | Windows 10 | CMake 3.31.6, MSVC 19.43 (Visual Studio 2022); LLVM clang 22.1.6 for Go's cgo | 1.26.7 | 1.98.0 |
| Linux x64 | Debian 12 (Xvfb) | CMake 4.4.0, gcc 12.2.0 | 1.26.7 | 1.98.0 |
| macOS arm64 | macOS 15.7.2 | CMake 4.0.2, Apple clang 17.0.0 | 1.26.7 | 1.98.0 |

Drop `--target` to build all 11. The 225 canonical examples live in `c/examples/canonical/`: the pages and assets are copied next
to the programs only by a **full build** (the asset target depends on all 225 programs; building and running a single target fails
with `asset not found`), and the programs land in `build/c-canonical/Release/` (on Linux / macOS too). Windows (run by the export
self-check):

```powershell
cmake -S c/examples/canonical -B build/c-canonical "-DKuiC_DIR=$PWD/cmake"
cmake --build build/c-canonical --config Release --parallel 4
.\build\c-canonical\Release\kui_c_native_html_01_box_model.exe
```

Linux / macOS (run on Linux in a real clone after export, about 15 seconds on 4 cores):

```bash
cmake -S c/examples/canonical -B build/c-canonical "-DKuiC_DIR=$PWD/cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build/c-canonical --parallel 4
./build/c-canonical/Release/kui_c_native_html_01_box_model
```

**How the runtime is found**: on Windows `kui_c_copy_runtime_dlls` copies the DLLs next to the program. On Linux / macOS the
CMake-built programs carry an **absolute** RPATH to this repository's `runtime/<platform>/lib`, so they run inside the clone but
stop finding the libraries once the clone is moved or deleted. To ship to someone else, put the program and the libraries from
`runtime/<platform>/` in the same folder and give the program a relative RPATH (`$ORIGIN` on Linux, `@executable_path` on macOS;
the prebuilt examples packages are laid out this way); on Windows keep the DLLs next to the program.

In your own CMake project:
`find_package(KuiC 3 CONFIG REQUIRED)` (`-DKuiC_DIR=<this repository>/cmake`), `target_link_libraries(my_app PRIVATE KuiC::C)`, and
`kui_c_copy_runtime_dlls(my_app)` to copy the runtime next to the program. `cmake/KuiCConfig.cmake` picks the runtime from
`runtime/<platform>/` for the current platform and otherwise behaves like the packaged file (Debug configurations use the Release
runtime too, MSVC gets `/utf-8`).

### What is in runtime/

| Folder | Files | Size |
|---|---:|---:|
| `runtime/windows-x64/` | 17 | 166.7 MiB |
| `runtime/linux-x64/` | 16 | 227.7 MiB |
| `runtime/macos-arm64/` | 16 | 81.2 MiB |

Each folder holds the C package's `bin/` (`kui_c`, `kui`, extension and media plugins, pdfium, FFmpeg and its license texts) and `lib/` (the Windows import library `kui_c.lib`; `libkui_c` and `libkui` on Linux / macOS), byte-identical to the same-platform SDK package on the Release and in the same relative layout (the Linux / macOS RPATHs rely on it).

### What is on the Release

Each version's [kui Release](https://github.com/kui-engine/kui/releases/tag/v3.61.8) has three kinds of attachments:

1. **SDK packages** `kui-<language>-sdk-<version>-<platform>.zip`: the corresponding source repository packed for one platform (`runtime/<platform>/` is `bin/` and `lib/` in the package, `native/` for Go / Rust), without prebuilt example programs; download one if you prefer not to `git clone`.
2. **Prebuilt examples packages** `kui-<language>-examples-<version>-<platform>.zip`: example programs plus the runtime; extract and run them from `bin/`, no compiler needed.
3. **Checksums** `SHA256SUMS.txt` (covering the two kinds above) and the FFmpeg corresponding source.

| Package | Contents | Windows x64 | Linux x64 | macOS arm64 |
|---|---|---|---|---|
| `kui-c-sdk` | SDK (= the source repository for one platform) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-c-sdk-3.61.8-windows-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-c-sdk-3.61.8-linux-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-c-sdk-3.61.8-macos-arm64.zip) |
| `kui-cpp-wrapper-sdk` | SDK (= the source repository for one platform) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-cpp-wrapper-sdk-3.61.8-windows-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-cpp-wrapper-sdk-3.61.8-linux-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-cpp-wrapper-sdk-3.61.8-macos-arm64.zip) |
| `kui-go-sdk` | SDK (= the source repository for one platform) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-go-sdk-3.1.0-windows-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-go-sdk-3.1.0-linux-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-go-sdk-3.1.0-macos-arm64.zip) |
| `kui-rust-sdk` | SDK (= the source repository for one platform) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-rust-sdk-3.1.0-windows-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-rust-sdk-3.1.0-linux-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-rust-sdk-3.1.0-macos-arm64.zip) |
| `kui-c-examples` | Prebuilt examples (run right after extraction) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-c-examples-3.61.8-windows-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-c-examples-3.61.8-linux-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-c-examples-3.61.8-macos-arm64.zip) |
| `kui-cpp-wrapper-examples` | Prebuilt examples (run right after extraction) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-cpp-wrapper-examples-3.61.8-windows-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-cpp-wrapper-examples-3.61.8-linux-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-cpp-wrapper-examples-3.61.8-macos-arm64.zip) |
| `kui-go-examples` | Prebuilt examples (run right after extraction) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-go-examples-3.1.0-windows-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-go-examples-3.1.0-linux-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-go-examples-3.1.0-macos-arm64.zip) |
| `kui-rust-examples` | Prebuilt examples (run right after extraction) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-rust-examples-3.1.0-windows-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-rust-examples-3.1.0-linux-x64.zip) | [zip](https://github.com/kui-engine/kui/releases/download/v3.61.8/kui-rust-examples-3.1.0-macos-arm64.zip) |

### Platforms

| Platform | Status | Acceptance actually performed when packaged |
|---|---|---|
| Windows x64 | Full Profile GA | The 4 SDK packages are rebuilt from their own sources after extraction and pass the out-of-package consumer matrix; every program in the 4 prebuilt examples packages is actually launched (C 222, C++ Wrapper 190, Go 219, Rust 219). After export, the four repositories were built from real `git clone`s with the README commands and the programs launched (including the parameterless `build.ps1`) |
| Linux x64 | Release package of the same frozen C ABI | The SDK packages rebuild from their sources; the example programs are launched under Xvfb: C 221/222, C++ Wrapper 190/190, Go 218/219, Rust 218/219 — C, Go and Rust each leave out 1 tray example (Xvfb has no tray host, `kui_window_tray_create` reports not supported), allow-listed. After export, the four repositories were built from real `git clone`s with the README commands and launched under Xvfb (including the parameterless Go / Rust `build.sh`, 219 programs each) |
| macOS arm64 | Release package of the same frozen C ABI | The SDK packages rebuild from their sources (C 11 + 211, C++ Wrapper 189 + demo + 3 test suites, Go / Rust 8 + 211) without launching the rebuilt programs; the examples packages are checked for layout, manifest, byte-identical runtimes, dependencies and RPATHs, plus launching 6 windowless C examples and the C++ Wrapper demo. **Windowed programs were not launched on macOS** (the build Mac is a shared machine). After export, the four repositories were built from real `git clone`s with no parameters (including the Go / Rust `build.sh`, 219 programs each) and the dependency and rpath closure checked; only windowless programs and tests were run, **no windowed program was launched** |

All three platforms ship Release runtimes only, without debug symbols; your Debug builds link the same runtime (the C ABI passes no CRT objects across the boundary). Platform capabilities are not fully symmetric; see `docs/known_issues.md` for known differences.

### If a downloaded program does not start

The prebuilt programs and runtimes are not signed yet:

- **Windows**: for a zip downloaded by a browser, when SmartScreen says "Windows protected your PC", click "More info" → "Run anyway"; or run `Get-ChildItem -Recurse <extracted folder> | Unblock-File` in PowerShell once. Files from `git clone` carry no mark-of-the-web, so SmartScreen does not block them.
- **macOS**: Gatekeeper blocks a zip downloaded by a browser; run `xattr -dr com.apple.quarantine <extracted folder>`.
- **Linux**: if your unzip tool drops permissions, `chmod +x` the programs under `bin/` (`git clone` keeps the runtime's executable bits).

### License and third-party components

MIT, see `LICENSE` (copyright Alloy).

KUI itself is MIT. The video plugin `kui_media_video` loads FFmpeg (LGPL-3.0-or-later, an LGPL build without `--enable-gpl` / `--enable-nonfree`) as separate shared libraries; if you do not need video, leave out `kui_media_video` and the FFmpeg libraries (the audio plugin does not use FFmpeg). In the repository's `runtime/<platform>/` and in every package, `LICENSE.FFmpeg.txt` (LGPLv3) and `LICENSE.FFmpeg.GPLv3.txt` sit next to the FFmpeg libraries; all other third-party components are under permissive licenses, see `THIRD_PARTY_NOTICES.md`.

**The corresponding source of the FFmpeg libraries distributed with the binaries** is attached to the [kui Release](https://github.com/kui-engine/kui/releases/tag/v3.61.8); checksums are in `FFMPEG_SOURCE_SHA256SUMS.txt`:

- Windows x64 / Linux x64 (BtbN build `autobuild-2026-10-31-12-59`, FFmpeg `n9.0.2-39-g7adfbe40dc`): `ffmpeg-7adfbe40dcde598f93a6963d2f7b22cd75202ccc.tar.gz` (source of FFmpeg commit `7adfbe40dcde598f93a6963d2f7b22cd75202ccc`) and `FFmpeg-Builds-autobuild-2026-10-31-12-59.tar.gz` (the build scripts);
- macOS arm64 (built from the official release tarball): `ffmpeg-9.0.2.tar.xz` and its signature `ffmpeg-9.0.2.tar.xz.asc`.

Upstream: <https://github.com/FFmpeg/FFmpeg/tree/7adfbe40dcde598f93a6963d2f7b22cd75202ccc>, <https://github.com/BtbN/FFmpeg-Builds/tree/autobuild-2026-10-31-12-59>, <https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz>.

### Contents

| Path | Contents |
|---|---|
| `c/include/` | `kui_c.h` (the frozen C ABI) and the two pure C protocol headers for extensions and media plugins |
| `c/abi/` | Frozen export list and signature digest |
| `c/examples/` | 11 pure C examples and 225 canonical examples (with pages and assets) |
| `c/extensions/` | Sources of the minimal `hello/` extension and the `kui_media_audio/` audio plugin |
| `c/src/`, `c/tests/`, `c/CMakeLists.txt` | The C ABI layer implementation and C tests, **for reference only**: building them needs the non-public native C++ SDK; use the prebuilt runtime in `runtime/` |
| `cmake/` | Repository variant of the `find_package(KuiC)` config |
| `runtime/` | Runtimes for three platforms (see above) |
| `docs/` | Language-neutral documentation: features, CSS support matrix, known issues, performance, DevTools, Lua, … |
| `tools/devtools_mcp/` | DevTools MCP server for debugging a running KUI program |
