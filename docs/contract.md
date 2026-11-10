# 契约（Contract）

每个公开转换函数的规格：输入定义域、输出、舍入方式、精度保证、定义域外的行为。测试只检查写在这里的东西；正确性的判定依据是 `tests/reference.hpp`（ADR 0003）。测试与性能的方法见 [`test-strategy.md`](test-strategy.md)。

| 函数 | 定义域 | 结果 | 精度 |
|---|---|---|---|
| `double_to_fixed128_full` | \|v·2^f\| < 2^127（负数可到 −2^127） | 二进制补码 fixed128 | 精确；四舍五入（远离零）或截断 |
| `double_to_ufixed128_full` | 0 ≤ v·2^f < 2^128 | ufixed128 | 同上 |
| `fixed128_to_double` / `ufixed128_to_double` | 全部 128 位模式 | double | 与正确舍入值相差 ≤ 1 ULP |
| `double_to_int64_fast` / `double_to_uint64_fast` | (u)int64 范围 | 截断 | 精确 |
| `int64_to_double_fast_precise` / `uint64_…` | 全部 64 位模式 | double | 正确舍入（与硬件转换逐位相同） |
| `double_to_int64_limited` | [−2^51, 2^51] | 四舍五入到最近整数（整数输入精确） |
| `double_to_uint64_limited` | [0, 2^52) | 同上 |
| `int64_to_double_limited` | [−2^51, 2^51] | 精确 |
| `uint64_to_double_limited` | [0, 2^52) | 精确 |
| `fixed128` 标量构造 / `operator double` | 同上 | 截断 / ≤ 1 ULP | — |
| 加减法（标量与 x4） | 全部 | 模 2^128 回绕 | 精确 |

**规格决定**（作者 2026-11-10 拍板）：

- **D1 定义域外（NaN、±Inf、超范围）**：Release 下**零运行时开销**，结果未定义。检查做成编译期开关 `FIXED128_CHECK_INPUTS`，未定义时默认值为 `!defined(NDEBUG)`（Debug 开、Release 关），违规时调用可覆盖的处理函数（默认 `assert` 式中止；测试里替换成抛异常，以便验证检查确实会触发）。和 `assert` 一样，同一程序里各翻译单元的开关应一致。
  测试：Debug 配置下验证每类非法输入都会触发检查、合法边界值不会误报；Release 配置下用反汇编或基准确认检查代码完全消失。
- **D2 −0.0 → 0**：已写进 README，并由边界用例覆盖。
- **D3 fixed128 → double 要不要花开销换正确舍入**：**先测量再决定**。依据是 `test-strategy.md` 第一部分第 5 节的精度统计（重点看带符号误差的均值，即是否有偏）和第四部分的轨道漂移实验（结论：目前没有证据需要付出开销，见 README）。只有当实验显示 ≤ 1 ULP 的误差在长期积分中造成可观的系统性漂移时，才考虑付出开销。
