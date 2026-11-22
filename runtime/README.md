# runtime/

三个平台的 KUI 运行库，与 Release 上同平台 SDK 包里的那份逐字节相同、相对位置不变（不要单独挪动 `bin/` 与 `lib/`：Linux / macOS 的 RPATH 依赖它们的相对位置）。用法见仓库根目录的 README。

KUI runtimes for three platforms, byte-identical to the same-platform SDK package on the Release and in the same relative layout (do not move `bin/` and `lib/` apart: the Linux / macOS RPATHs rely on it). See the top-level README for usage.

| Folder | Files | Size |
|---|---:|---:|
| `runtime/windows-x64/` | 17 | 166.7 MiB |
| `runtime/linux-x64/` | 16 | 227.7 MiB |
| `runtime/macos-arm64/` | 16 | 81.2 MiB |

Each folder holds the C package's `bin/` (`kui_c`, `kui`, extension and media plugins, pdfium, FFmpeg and its license texts) and `lib/` (the Windows import library `kui_c.lib`; `libkui_c` and `libkui` on Linux / macOS), byte-identical to the same-platform SDK package on the Release and in the same relative layout (the Linux / macOS RPATHs rely on it).
