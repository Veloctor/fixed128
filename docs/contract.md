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
| `fixed128x4 + - += -= double4` | b 同 `double_to_fixed128_full` | 与 `a ± fixed128x4{b}` 逐位相同（舍入跟随配置），模 2^128 回绕 | 精确 |
| `subtract_to_double(a, b)` | 全部（a − b 模 2^128 回绕） | double | 与正确舍入值相差 ≤ 1 ULP |
| `subtract_to_float(a, b)` | 全部（同上） | float | 与正确舍入的 float 相差 ≤ 1 个 float ULP |

**规格决定**（作者 2026-11-10 拍板）：

- **D1 定义域外（NaN、±Inf、超范围）**：Release 下**零运行时开销**，结果未定义。检查做成编译期开关 `FIXED128_CHECK_INPUTS`，未定义时默认值为 `!defined(NDEBUG)`（Debug 开、Release 关），违规时调用可覆盖的处理函数（默认 `assert` 式中止；测试里替换成抛异常，以便验证检查确实会触发）。和 `assert` 一样，同一程序里各翻译单元的开关应一致。
  测试：Debug 配置下验证每类非法输入都会触发检查、合法边界值不会误报；Release 配置下用反汇编或基准确认检查代码完全消失。
- **D2 −0.0 → 0**：已写进 README，并由边界用例覆盖。
- **D3 fixed128 → double 要不要花开销换正确舍入**：**先测量再决定**。依据是 `test-strategy.md` 第一部分第 5 节的精度统计（重点看带符号误差的均值，即是否有偏）和第四部分的轨道漂移实验（结论：目前没有证据需要付出开销，见 README）。只有当实验显示 ≤ 1 ULP 的误差在长期积分中造成可观的系统性漂移时，才考虑付出开销。
- **D4 融合运算**（`subtract_to_double`、`subtract_to_float`、`fixed128x4 ± double4`，用于相机相对坐标、两两引力和积分步进；作者 2026-11-10 拍板）：
  - **全范围、不带后缀**：不对差值或位移的大小做假设（游戏里两两引力可能跨越银河系，单步位移也说不准）。
  - **只要求 ≤ 1 ULP**，不要求正确舍入或反对称（f(b,a) = −f(a,b)）：依据 D3，单次 1 ULP 的方向性误差不会在长期积分中累积成可观误差，因此留给实现更大的优化空间。结果可以与 `(double4)(a - b)` 不同。
  - **`fixed128x4 ± double4` 与先转换再加减逐位相同**，所以积分结果不因使用融合运算而改变。
  - **只做 x4**，标量 `fixed128` 不加。
  - **只允许无分支实现**：按数据走快路径的版本在分支预测失败率高的数据（如远近天体交替）上可能反而更慢，库函数的耗时不应取决于调用方的数据分布。
  - **`_limited` 变体**（范围更小、速度更快，如差值 < 2^99 个单位）可以存在，由调用方按自己的数据决定用哪个；**只有在基准里明显快于全范围版本时才保留**，否则是冗余。同理，`subtract_to_float` 若没有比 `(float)subtract_to_double` 更快的实现，也应删除；它们现在是占位实现，候选在 `experimental/candidates` 里先行比较。
