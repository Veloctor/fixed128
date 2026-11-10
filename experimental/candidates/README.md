# 候选实现擂台

这里放 double ↔ fixed128 转换和融合运算（`subtract_to_double`、`subtract_to_float`、fixed128x4 + double4）的**候选实现**：库自己的实现、对照基线，以及探索中的新设计。
`candidates.hpp` 里的 `all` 列出的每个候选，都会自动经过：

- **契约测试**（`tests/test_contract.cpp`）：与纯整数参考实现逐位比对 double → fixed128，检查 fixed128 → double 和融合运算的误差上限；覆盖结构化穷举网格、边界、中点、随机输入和所有被测的小数位数。这是硬门槛。
- **精度报告**（`fixed128_accuracy_report`）：最大 ULP、正确舍入的比例、带符号误差的均值（是否有偏）。
- **性能测试**（`fixed128_bench`）：吞吐和延迟，与其他候选并排比较。

## 加一个候选

1. 新建 `<名字>.hpp`，写一个结构体，提供下面函数中的任意几个（没提供的会被跳过）：

   ```cpp
   struct my_idea
   {
       static constexpr const char* name = "my idea";
       static constexpr bool round_half_away = true; // double -> fixed128 的舍入：true 四舍五入（远离零），false 截断
       static constexpr unsigned max_ulp = 1;        // fixed128 -> double 允许的最大误差（ULP），正确舍入可写 0

       template <int F> static void double_to_fixed128(const fx128::double4& in, fx128::ulong4& upper, fx128::ulong4& lower);
       template <int F> static void double_to_ufixed128(const fx128::double4& in, fx128::ulong4& upper, fx128::ulong4& lower);
       template <int F> static fx128::double4 fixed128_to_double(const fx128::ulong4& upper, const fx128::ulong4& lower);
       template <int F> static fx128::double4 ufixed128_to_double(const fx128::ulong4& upper, const fx128::ulong4& lower);

       // 融合运算，契约见 docs/contract.md 的 D4
       template <int F> static fx128::double4 subtract_to_double(const fx128::fixed128x4<F>& a, const fx128::fixed128x4<F>& b);
       template <int F> static fx128::float4 subtract_to_float(const fx128::fixed128x4<F>& a, const fx128::fixed128x4<F>& b);
       template <int F> static fx128::fixed128x4<F> add_double(const fx128::fixed128x4<F>& a, const fx128::double4& b);

       // 可选：做成范围更小的 _limited 变体时声明定义域，测试和基准只用域内的输入
       static constexpr int subtract_max_log2 = 99; // |a − b| < 2^99 个单位（默认 128：全部输入）
       static constexpr int add_max_log2 = 63;      // −2^63 ≤ b·2^F < 2^63（默认 127：转换的全部定义域）
   };
   ```

   `F` 是小数位数；fixed128 按"高 64 位 upper、低 64 位 lower"两半传递，有符号时是二进制补码。定义域见 `docs/contract.md`。`max_ulp` 同时是 `subtract_to_double`（double ULP）和 `subtract_to_float`（float ULP）的误差上限；`add_double` 必须与先转换再相加逐位相同（舍入按 `round_half_away`）。

2. 在 `candidates.hpp` 里 `#include` 它，并加进 `all`。
3. 构建并运行 `ctest --preset release`（Windows 上是 `clang-cl-release`；契约测试）、`fixed128_accuracy_report`、`fixed128_bench`。

融合运算只允许无分支实现；`_limited` 变体只有在基准里明显快于全范围版本时才值得进库（见 `docs/contract.md` 的 D4）。基准里的场景测试（相机相对、两两引力、积分步进）还有双双精度位置作为对照（`benchmarks/dd4.hpp`），那是 fixed128 在实际用途中真正的竞争对手。

推荐用 TDD：先把候选注册进来（函数可以先返回错误结果），看契约测试变红，再实现到变绿，最后看性能对比决定去留。

候选可以只在某个 backend 下存在（例如需要 AVX-512 的实现），用 `#if` 包住它的定义和在 `all` 中的条目即可。
