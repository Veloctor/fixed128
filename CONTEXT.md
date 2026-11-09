# fixed128

用 AVX2（或可移植标量实现）在 `double` 与 128 位定点数之间做高速 4 路转换的 header-only 库，服务于超大尺度坐标（≥ 百万光年范围、≤ 0.1 mm 精度）的表示与运算。

## Language

### 数值类型

**Fixed128**:
有符号 128 位二进制补码定点数，数值 = 整数位 / 2^Frac bits。库的核心概念；Frac bits 为 0 时即普通 int128。
_Avoid_: Int128（仅作 Frac bits = 0 的特例时使用）

**UFixed128**:
无符号版本的 Fixed128。
_Avoid_: uint128

**Fixed64**:
有符号 64 位定点数，与 Fixed128 共用同一 Frac bits。

**Frac bits**:
小数位数，整个程序内为同一个编译期常量。
_Avoid_: 精度、scale

**Upper / Lower**:
Fixed128 的高 64 位与低 64 位两半。

### SIMD 形态

**Lane**:
一个 SIMD 寄存器中的一个元素；本库的 x4 类型有 4 个 lane。

**x4**（如 Fixed128x4）:
4 个独立数值按 SoA 打包的 SIMD 类型（Upper 一个寄存器、Lower 一个寄存器），每个 lane 的语义与对应标量类型完全一致。

**Backend**:
x4 类型与转换函数背后的某一套实现，编译期选定。现有 AVX2 与 Scalar（可移植标量）两个，将来可能有 NEON；各 backend 的每个 lane 结果必须逐位一致。

**Primitive**（向量原语）:
`detail` 里供转换算法使用的最小向量操作（如 `sllv`、`blendv`）。每个 backend 各实现一套：AVX2 下是一条 intrinsic，Scalar 下逐 lane 精确模拟该指令。

**Lane 类型**（`double4`、`long4`、`ulong4`、`float4`）:
公开 API 使用的 4-lane POD 结构体，成员 `x, y, z, w` 即 lane 0..3，在所有 backend 上布局相同。
_Avoid_: 把 `__m256d` 等寄存器类型当作公开类型

### 转换函数的有效范围后缀

**_limited**:
只对文档写明的受限输入范围有效，超范围结果未定义。

**_fast**:
对目标类型的全范围有效，但不保证最优舍入。

**_full**:
对 Fixed128 的全部可表示范围有效。

### 正确性

**Reference implementation**:
只存在于测试中的纯整数精确实现，是判定正确性的唯一依据。
_Avoid_: 把标量 Fixed128 称为"参照实现"——它也是被测对象
