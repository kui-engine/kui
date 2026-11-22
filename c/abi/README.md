# KUI C ABI 冻结契约

`kui_c_3.exports` 是 product major 3 的公共导出基线；当前 Windows x64
Full Profile 共 492 个导出。`kui_c_3.signature.sha256` 冻结规范化后的全部
函数签名。二者必须与 `include/kui_c.h` 声明、`src/*.cpp` 实现和实际 PE
export table 完全一致。

兼容政策：

- 同一 product major 内不得删除、改名或改变既有函数签名、enum 数值与
  已冻结 public struct 布局。
- 新能力优先新增函数；需要扩展结构时新增带版本后缀的结构/入口。
- 破坏性变更必须提升 product major 并建立新 manifest。
- `KUI_C_API_VERSION_*` 跟随根 `VERSION`，表示产品版本，不替代 ABI 基线。
- 每次发布必须对 Release/Debug 源码产物和解包后的 DLL 执行
  `python scripts/check_c_abi.py --binary <kui_c.dll>`。

manifest 或签名摘要变化属于公共 ABI 评审事项，必须与实现、测试和工作日志
一同提交，不能作为无说明的自动生成噪声。
