# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> `AGENTS.md` 与本文件是**同一文件的硬链接**，内容永远一致。修改时请保持硬链接不被破坏（见文末“硬链接”一节）。

## 这个库要解决的问题

用 AVX2 在 `double` 与 128 位定点数之间做 4 路并行的高速转换（AVX-512 的原生指令不普及），非 x86 平台（如 Apple Silicon）走结果逐位一致的可移植标量 backend。场景是超大尺度坐标：位置存为 fixed128 以便精确加减，差值转成 `double` 做其他数学运算，所以**转换速度**是核心瓶颈。起因见作者的 Stack Overflow 问题（README 有链接）。支持的操作刻意很少：加、减、从 `double` 转换、转为 `double`。

- 用户文档、接入方式、API 一览、已知问题：`README.md`
- 项目术语（Fixed128、Frac bits、Lane、x4、Backend、`_limited`/`_fast`/`_full`、Reference implementation）：`CONTEXT.md`。命名和讨论都用这里的词。
- 已做的架构决策及原因：`docs/adr/`。动到 backend 结构（ADR 0007，取代 0001）、第三方依赖、测试基准、SO 代码许可、frac bits 模板参数（ADR 0008，取代 0005）、C++17 这些方向前先读对应 ADR。

## 构建与测试

CMake + Ninja，`CMakePresets.json` 提供 `release` / `debug`。本机是 VS2026 (v145)，cmake/ninja/clang 在 VS 安装目录里，需要先进开发者环境：

```powershell
& 'D:\Microsoft Visual Studio\18\Professional\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
cmake --preset release; cmake --build --preset release; ctest --preset release
```

- 测试按舍入方式 × backend 各编一个可执行文件（`fixed128_tests_round_avx2` / `fixed128_tests_trunc_scalar` 等；x86-64 上两个 backend 都编，标量版不开 AVX2），因为配置宏必须在一个程序的所有翻译单元里一致。frac bits 是模板参数，每个可执行文件都用 `TEST_CASE_TEMPLATE` 测 `tests/test_helpers.hpp` 里 `FIXED128_TEST_FRAC_BITS` 列出的各个值。`test_backend_determinism.cpp` 把所有结果哈希后与 AVX2 记录值比较，只有经作者确认的数值行为改动才能更新那些值。新增测试源文件要加进 `tests/CMakeLists.txt` 的 `FIXED128_TEST_SOURCES`。
- 所有测试可执行文件都以 `FIXED128_CHECK_INPUTS=1` 编译，`tests/main.cpp` 把 `fx128::input_error_handler` 换成抛异常：测试输入必须在定义域内，否则测试会因异常失败（这曾抓到过测试自身的越界输入）。
- **候选实现擂台**：`experimental/candidates/candidates.hpp` 的 `all` 里每个候选都自动跑契约测试（`test_contract.cpp`）、性质测试、精度报告和 benchmark；新转换算法先注册为候选再做（见该目录 README）。
- 本机可用 VS 自带的 `clang++`（`-DCMAKE_CXX_COMPILER=clang++`）验证 GCC 风格的 `-Wall -Wextra -Werror`；GCC、sanitizer、fuzz、Debug、`/fp:strict`、`-march=x86-64-v3 -ffp-contract=fast` 由 CI（`.github/workflows/ci.yml`）覆盖。
- 精度：`fixed128_accuracy_report`。性能：`fixed128_bench`（只看 Release，方法见 `tools/bench/README.md`，`--json` + `tools/bench/Compare-Bench.ps1` 对比）。长期积分：`fixed128_orbit_drift`。测试强度：`tools/mutation/Run-Mutations.ps1`（每个变异都必须被测试抓到）。指令延迟：`tools/uops/`。源码里的延迟/吞吐注释以 Ice Lake 为参照，用 uiCA 对 `fixed128_kernels` 的汇编静态复现。

## 必须知道的约定与坑

- **数值行为是契约。** 重构、整理、移植都不改变任何转换结果；改数值行为是单独的、经作者确认的任务。测试发现的现有缺陷标记为已知失败（doctest `may_fail`）并报告，而不是顺手修。
- **舍入在整数尾数上做**：`double_to_(u)fixed128_full` 的四舍五入是对尾数右移时保留一位舍入位再 `(x + 1) >> 1`，因此是精确的。不要改回在浮点域加 0.5（旧实现这样做，既加错了量级，浮点加法本身也会舍入）。
- 定义域外输入（NaN、Inf、超范围）：作者的要求是 Release 下**零运行时开销**。检查由 `FIXED128_CHECK_INPUTS`（`check.hpp`）控制，默认 Debug 开、Release 关；关闭时 `FIXED128_CHECK_INPUT` 展开为空。新增"从 double 转换"的入口都要加上这个检查。
- 已知但不修的数值特性（`may_fail` 标记）：`fixed128_to_double` 在 |x| ≥ 2^96 个单位时约 1.7% 的结果朝 −∞ 偏 1 ULP，因此不是奇函数；更小的值全部正确舍入。
- 正确性只以 `tests/reference.hpp`（纯整数精确实现）为准；标量 `fixed128` 也是被测对象，不是基准答案（ADR 0003）。
- 浮点运算顺序是正确性的一部分（如 `(v_hi - magic) + v_lo`）。保持原有顺序；`config.hpp` 会在 fast-math 下 `#error`。
- frac bits 是没有默认值的模板参数（`fixed128<FracBits>`、`double_to_fixed128_full<FracBits>` 等），不是宏，也不提供默认精度的别名（ADR 0008）。魔数常量是 `detail` 里以 `FracBits` 为参数的 `constexpr double` 变量模板，在函数内用 `set1_d` 广播，没有全局动态初始化。
- `include/` 下的头文件保持 C++17，并且在 MSVC `/W4` 与 GCC/Clang `-Wall -Wextra` 下零警告（测试 target 以警告为错误）。进位 intrinsic 只在 x86-64 上用（`unsigned long long` 临时变量），其他平台走可移植代码。
- 公开 API 在命名空间 `fx128`；仅供实现/测试用的放 `fx128::detail`。公开的 4-lane 类型（`double4`、`long4`、`ulong4`、`float4`）是各 backend 布局相同的 POD（`x, y, z, w` = lane 0..3）；x4 类型接受 4 个标量的构造函数也是 lane 0 在前。intrinsic 只出现在 `detail/avx2/` 与 `fx128::avx2` 互操作函数里。
- **转换算法只写一次**，用 `detail` 的向量原语（`detail/avx2/native.hpp`、`detail/scalar/native.hpp`）。AVX2 原语必须恰好是一条 intrinsic，并且直接收发 `__m256i` / `__m256d`（包成结构体会让 MSVC 不再展开调用方的循环）；标量原语必须精确模拟对应指令（移位数 ≥ 64、按字节的 blendv 等）。改动后对比改前改后的汇编指令多重集（MSVC 与 Clang），见 ADR 0007。
- `third_party/` 是原样提交的 doctest / nanobench，升级时按 `third_party/README.md` 替换并更新版本表。
- 代码风格：Tab 缩进、函数/类型的大括号换行、控制语句大括号同行；`.clang-format` 描述现有风格，只用于新代码或编辑过的行，不做整库格式化。

## 硬链接

`CLAUDE.md` 与 `AGENTS.md` 是硬链接。许多编辑器/工具“保存”时会写新文件并替换，从而悄悄断开链接；`git` 也不会保存硬链接，克隆后两者会变成独立文件。修改后用 PowerShell 检查或重建：

```powershell
(Get-Item AGENTS.md).LinkType        # 应为 HardLink
Remove-Item AGENTS.md; New-Item -ItemType HardLink -Path AGENTS.md -Target CLAUDE.md
```
