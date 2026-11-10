# 候选实现擂台

这里放 double ↔ fixed128 转换的**候选实现**：库自己的实现、对照基线，以及探索中的新设计。
`candidates.hpp` 里的 `all` 列出的每个候选，都会自动经过：

- **契约测试**（`tests/test_contract.cpp`）：与纯整数参考实现逐位比对 double → fixed128，检查 fixed128 → double 的误差上限；覆盖结构化穷举网格、边界、中点、随机输入和所有被测的小数位数。这是硬门槛。
- **精度报告**（`fixed128_accuracy_report`）：最大 ULP、正确舍入的比例、带符号误差的均值（是否有偏）。
- **性能测试**（`fixed128_bench`）：吞吐和延迟，与其他候选并排比较。

## 加一个候选

1. 新建 `<名字>.hpp`，写一个结构体，提供下面四个函数中的任意几个（没提供的方向会被跳过）：

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
   };
   ```

   `F` 是小数位数；fixed128 按"高 64 位 upper、低 64 位 lower"两半传递，有符号时是二进制补码。定义域见 `docs/test-and-benchmark-plan.md` 第一部分。

2. 在 `candidates.hpp` 里 `#include` 它，并加进 `all`。
3. 构建并运行 `ctest --preset release`（契约测试）、`fixed128_accuracy_report`、`fixed128_bench`。

推荐用 TDD：先把候选注册进来（函数可以先返回错误结果），看契约测试变红，再实现到变绿，最后看性能对比决定去留。

候选可以只在某个 backend 下存在（例如需要 AVX-512 的实现），用 `#if` 包住它的定义和在 `all` 中的条目即可。
