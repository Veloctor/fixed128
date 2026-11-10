# fixed128

Header-only C++17 library for **fast conversions between `double` and 128-bit fixed-point numbers**, 4 lanes at a time with AVX2 (and a portable backend for other CPUs, e.g. Apple Silicon).

It exists for software where positions span huge ranges at fine precision (≥ 1 million light years across, ≤ 0.1 mm resolution).
Positions are stored as 128-bit fixed point, where addition and subtraction are exact; differences are converted to `double`
for everything else (distances, sqrt, division, integration). The conversion is therefore the bottleneck, and AVX-512's
`vcvtqq2pd` family is not widely available, so this library implements it with AVX2 only.
Background: [How to efficiently perform double/int128 conversions with AVX2?](https://stackoverflow.com/questions/73312512/how-to-efficiently-perform-double-int128-conversions-with-avx2)

Supported operations are deliberately few: addition, subtraction, conversion from `double`, conversion to `double`.

## Requirements

- x86-64 CPU with **AVX2** (Intel Haswell / AMD Excavator and newer) for the AVX2 backend: the whole program is compiled for AVX2, there is no runtime dispatch.
- Any other 64-bit CPU (e.g. ARM / Apple Silicon) uses the portable scalar backend, which gives bit-identical results, only slower. See [Backends](#backends).
- C++17; tested with MSVC, GCC and Clang.
- **No fast-math** (`-ffast-math`, `-Ofast`, `/fp:fast`): the conversions depend on exact IEEE 754 semantics and fail to compile with it.

## Quick start

```cpp
#include <fixed128/fixed128.hpp>
using namespace fx128;

const fixed128x4<36> base{9.46e21};                          // 36 fractional bits; ~1 million light years, in meters
const fixed128x4<36> p = base + fixed128x4<36>{double4{1.0, 2.0, 3.0, 4.0}};
const fixed128x4<36> q = base + fixed128x4<36>{double4{0.5, 0.5, 0.5, 0.5}};
const double4 delta = static_cast<double4>(p - q);           // exact subtraction, then convert
```

See [`examples/`](examples/) for complete programs.

## Integration

**CMake, as a subdirectory** (e.g. a git submodule; works offline):

```cmake
add_subdirectory(third_party/fixed128)
target_link_libraries(my_target PRIVATE fixed128::fixed128)
```

**CMake, FetchContent:**

```cmake
include(FetchContent)
FetchContent_Declare(fixed128 GIT_REPOSITORY https://github.com/Veloctor/Int128.git GIT_TAG main)
FetchContent_MakeAvailable(fixed128)
target_link_libraries(my_target PRIVATE fixed128::fixed128)
```

**CMake, installed package:**

```bash
cmake -S . -B build && cmake --install build --prefix <prefix>
```

```cmake
find_package(fixed128 REQUIRED)
target_link_libraries(my_target PRIVATE fixed128::fixed128)
```

Linking `fixed128::fixed128` adds the include directory, the configuration macros (including the resolved backend) and, for the AVX2 backend, the AVX2 compiler flag.
When fixed128 is not the top-level project, its tests, benchmarks and examples are not built.

**Other build systems (xmake, plain makefiles, ...):** add `include/` to the include path, on x86-64 compile with `-mavx2` (GCC/Clang) or `/arch:AVX2` (MSVC), and optionally define the configuration macros below.

## Configuration

**Number of fractional bits.** It is a template parameter without default, not a configuration macro: a value of
`fixed128<FracBits>` equals its integer bits / 2^`FracBits`, and `FracBits` 0 makes it a plain int128.
Every use names the scale, so a value can never silently be interpreted at another one.
One program can use several scales; values of different scales are distinct types and do not convert into each other.

```cpp
fx128::fixed128x4<20> coarse{1.5};   // 20 fractional bits
fx128::fixed128x4<36> fine{1.5};     // 36 fractional bits
```

| Macro / CMake cache variable | Default | Meaning |
|---|---|---|
| `FIXED128_ROUND_HALF_AWAY_FROM_ZERO` | `1` / `ON` | `double → fixed128` SIMD conversion rounds half away from zero; `0` / `OFF` truncates. Round-half-to-even is not supported. |
| `FIXED128_BACKEND_AVX2=1` / `FIXED128_BACKEND_SCALAR=1`; CMake: `FIXED128_BACKEND` = `AUTO` / `AVX2` / `SCALAR` | automatic | See [Backends](#backends). |

Every translation unit of a program must see the same values (otherwise the program violates the ODR, silently).
With CMake, set the cache variables, e.g. `-DFIXED128_ROUND_HALF_AWAY_FROM_ZERO=OFF`; they are propagated to every consumer of the target.

## Backends

The 4-lane types and conversions have two compile-time backends with **bit-identical results** (including the rounding error of `fixed128 → double`, so simulations stay deterministic across platforms):

- **AVX2**: the default on x86-64. If AVX2 is not enabled, compilation fails rather than silently falling back to the slow path; select the scalar backend explicitly if that is what you want.
- **Scalar**: portable C++, 4 lanes as plain loops that compilers can auto-vectorize; the default on every other architecture. On x86 with only SSE2 it is about 3× (`fixed128 → double`) to 6× (`double → fixed128`) slower than AVX2.

There is no runtime dispatch. With CMake, `FIXED128_BACKEND=AUTO` is resolved once (by `CMAKE_SYSTEM_PROCESSOR`) and the explicit choice is propagated to consumers.

## API overview

Everything is in namespace `fx128`. The 4-lane value types are plain structs with the same layout on every backend:
`double4`, `long4`, `ulong4` (`alignas(32)`, members `x, y, z, w` = lanes 0..3) and `float4` (`alignas(16)`).

| Type | Contents |
|---|---|
| `fixed64<FracBits>` | Scalar signed 64-bit fixed point. `fixed64_maxvalue<FracBits>` / `fixed64_minvalue<FracBits>` are its extremes. |
| `fixed128<FracBits>` | Scalar signed 128-bit fixed point (`upper`, `lower`). `+ - += -=`, conversion from/to `double` (from `double`: truncates). |
| `fixed64x4<FracBits>` | 4 lanes of `fixed64` (`bits`, a `long4`). |
| `fixed128x4<FracBits>` | 4 lanes of `fixed128`, structure of arrays (`upper`, `lower`, both `ulong4`). `+ - += -=`, `operator[]`, conversion from/to `double4`. |

Each type has `static constexpr int frac_bits`.
The constructors taking 4 scalars take them in lane order: the **first argument is lane 0** (`x`).

With the AVX2 backend, `fx128::avx2::to_m256d` / `to_m256i` / `to_double4` / `to_long4` / `to_ulong4` convert between these types and `__m256d` / `__m256i` for code that uses intrinsics directly (see [`examples/xyz_lane_packing.cpp`](examples/xyz_lane_packing.cpp)).

Free functions in [`convert.hpp`](include/fixed128/convert.hpp) and [`limited.hpp`](include/fixed128/limited.hpp) (the `_limited` ones trade range for speed: a few instructions, valid only inside the stated range) (a fixed128 lane is passed as its `upper` and `lower` halves):

| Function | Valid input range | Accuracy |
|---|---|---|
| `double_to_fixed128_full`, `double_to_ufixed128_full` | full (u)fixed128 range | exact (rounded half away from zero, or truncated) |
| `fixed128_to_double`, `ufixed128_to_double` | full (u)fixed128 range | ≤ 1 ULP; correctly rounded below 2^96 units (see below) |
| `double_to_int64_fast`, `double_to_uint64_fast` | full (u)int64 range | exact, truncates |
| `int64_to_double_fast_precise`, `uint64_to_double_fast_precise` | full (u)int64 range | correctly rounded |
| `double_to_int64_limited` | [−2^51, 2^51], integral or not (rounds to nearest) | exact for integral inputs |
| `double_to_uint64_limited` | [0, 2^52) | exact for integral inputs |
| `int64_to_double_limited` | [−2^51, 2^51] | exact |
| `uint64_to_double_limited` | [0, 2^52) | exact |

The four (u)fixed128 functions are templates over the scale, e.g. `double_to_fixed128_full<20>(v, upper, lower)`; the scale must always be given.

`-0.0` converts to `0` (there is no negative zero in fixed point).

**Accuracy of `fixed128 → double`** (measured by `fixed128_accuracy_report`, the same at every scale): for magnitudes below 2^96 units
(at 36 fractional bits: about 120 light years in meters) every result is correctly rounded. Above, about 1.7 % of the results
are 1 ULP below the correctly rounded value, always towards −∞, so the conversion is not exactly odd there
(f(−x) ≠ −f(x) for those values). The typical use converts differences of nearby positions, which are far below 2^96 units.

**Invalid inputs.** NaN, ±Inf and inputs outside the valid range give undefined results. The conversions from `double`
can check them: `FIXED128_CHECK_INPUTS` (default: on when `NDEBUG` is not defined, i.e. in Debug builds; off in Release,
where no checking code exists at all) reports each invalid lane to `fx128::input_error_handler`, which by default prints
the problem and aborts; assign your own function to log, break into the debugger or throw instead.

Suffix meaning: `_limited` — documented restricted range only; `_fast` — full range of the target type; `_full` — full fixed128 range.

## Building the tests, benchmarks and examples

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Third-party code (doctest, nanobench) is vendored in [`third_party/`](third_party/), so no network access is needed.

**Tests** compare everything against an exact, pure-integer reference implementation ([`tests/reference.hpp`](tests/reference.hpp)),
which is itself tested against hand-computed answers and, on GCC/Clang, against `unsigned __int128`. They cover 8 scales
(0, 1, 36, 52, 53, 64, 100, 127 fractional bits) with structured exhaustive grids (every exponent × mantissa patterns ×
neighbors; every bit position × rounding cases), edge values, half-way points and random inputs; plus metamorphic properties
(monotonicity, round trip, lane independence), the input checks, and a determinism test that both backends give
bit-identical results. There is one executable per rounding mode and backend.
[`experimental/candidates/`](experimental/candidates/README.md) is a registry of conversion implementations: every
candidate there runs through the same contract tests, the accuracy report and the benchmarks, which makes it easy to try
new designs test-first.

| Tool | Purpose |
|---|---|
| `fixed128_accuracy_report` | ULP statistics and signed bias of every candidate's results |
| `fixed128_bench` | throughput per working-set size, latency, realistic scenarios; JSON output; see [tools/bench](tools/bench/README.md) |
| `fixed128_orbit_drift` | long integration of Jupiter's moons with double / Kahan / fixed128 positions against a double-double reference |
| [`tools/mutation/Run-Mutations.ps1`](tools/mutation/Run-Mutations.ps1) | mutation testing: deliberate bugs that the tests must catch |
| `fixed128_fuzz` | libFuzzer differential fuzzing against the reference (Clang, `-DFIXED128_BUILD_FUZZERS=ON`) |
| [`tools/uops`](tools/uops/README.md) | instruction latency / throughput lookup in the uops.info database |

**Results of the first full verification (2026-11-10).**

- The contract holds at all 8 scales, on both backends, in both rounding modes.
- `fixed128 → double` is 100 % correctly rounded below 2^96 units; above, about 1.7 % of results are 1 ULP towards −∞ (known, not changed).
- `double → fixed128`: rounding mode has mean error ≈ 0; truncation has mean error −0.44 units along the direction of the magnitude (biased).
- Orbit drift (Jupiter system, Forest–Ruth 4, 10 years, median of 3 perturbations): position error double ≈ 1 km, Kahan ≈ 0.7 m, fixed128 ≈ 0.9 m.
  Translated by 10²¹ m, double fails completely (10¹⁰ m), Kahan ≈ 1.1 m, fixed128 is bit-identical to the result at the origin.
  Truncation and rounding give nearly the same result (0.86 m vs 0.87 m): the rounding mode of `double → fixed128` is irrelevant there, the error is dominated by the double velocity and gravity computation.
  A 100-year leapfrog stress test (translated by 10²¹ m): fixed128 43 m, Kahan 66 m; the energy error grows with exponent ≈ 0.6 for fixed128 (unbiased random walk: 0.5).
  Conclusion: no evidence yet that correct rounding of `fixed128 → double` is worth any cost.
- Mutation testing: 15 deliberate bugs (magic number off by 1 ULP, lost borrow, missing rounding increment, wrong shift direction, off-by-one input checks, …) are all caught.
- CI: all 9 jobs passed (MSVC Release and Debug with `/fp:strict`, GCC including `-march=x86-64-v3 -ffp-contract=fast`, Clang with ASan + UBSan, macOS ARM, Linux ARM, 60 s libFuzzer).

The specification of every function: [`docs/contract.md`](docs/contract.md). The reasoning behind the tests and benchmarks: [`docs/test-strategy.md`](docs/test-strategy.md).

## Layout

```
include/fixed128/        the library (fixed128.hpp includes everything)
tests/                   doctest tests, the reference implementation, accuracy report, fuzz target
experimental/candidates/ registry of conversion implementations under test
benchmarks/              nanobench benchmarks and the kernels for static analysis
validation/              long-running experiments (orbit drift)
examples/                usage examples
tools/                   benchmark comparison, mutation testing, uops.info lookup
third_party/             vendored doctest and nanobench
docs/adr/                architecture decision records
CONTEXT.md               glossary of the project's terms
```

## Credits

- Limited-range `double <-> int64` conversions (`limited.hpp`): Mysticial's answer, and full-range `int64 -> double` conversions: wim's answer, both to
  [How to efficiently perform double/int64 conversions with SSE/AVX?](https://stackoverflow.com/questions/41144668/how-to-efficiently-perform-double-int64-conversions-with-sse-avx)
  on Stack Overflow. Stack Overflow contributions are licensed under [CC BY-SA](https://stackoverflow.com/help/licensing); see [ADR 0004](docs/adr/0004-stack-overflow-code-attribution.md).
- Everything else: 矢速 (Velctor). Licensed under the [MIT License](LICENSE).
