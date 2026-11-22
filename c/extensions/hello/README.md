# `kui_ext_hello` extension example

This is a pure-C extension using KUI's separate `kuiext_*` protocol. It does
not link `kui.dll` or `kui_c.dll`; the host passes all services through
`KuiExtHostAPI` function pointers.

From an extracted C ABI / C++ Wrapper SDK:

```powershell
cmake -S extensions/hello -B build-hello-extension `
  -DKuiC_DIR="$PWD/cmake"
cmake --build build-hello-extension --config Release
```

From an extracted native C++ SDK, use `-DKui_DIR="$PWD/cmake"` instead. Formal
Windows packages already contain the resulting `bin/kui_ext_hello.dll`; the
command above proves extension authors can rebuild it from the shipped source.

The production 3D extension is different: its host-facing entry points are a C
ABI, but its implementation links `Kui::Kui` and uses native engine types, so
it requires the native C++ SDK plus Filament and Bullet rather than `KuiC`.
