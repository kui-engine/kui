# kui_media_audio

`kui_media_audio` 是 KUI 内置 `<audio>` 元素的纯 C11 miniaudio 后端。它使用独立的
`kui_media_*` C ABI，不链接 `kui.dll` 或 `kui_c.dll`。

从 C/Wrapper SDK 构建：

```powershell
cmake -S . -B build `
  -DKuiC_DIR=<kui-c-sdk 解压目录>/cmake
cmake --build build --config Release
```

正式 C SDK 包内的本目录已携带固定版本的 `third_party/miniaudio/miniaudio.h` 与
许可证，解压后无需额外下载；仓库源码树也会自动发现根 `third_party/` 下的版本。
放在别处的 miniaudio 用 `-DMINIAUDIO_DIR=<含 miniaudio.h 的目录>` 指定。

自动化测试应设置 `KUI_MEDIA_NO_DEVICE=1`，只验证 ABI、解码和状态机；真实声卡、设备
切换和驱动关闭需在交互环境设置 `KUI_MEDIA_NO_DEVICE=0` 单独验证。
