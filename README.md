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

const fixed128x4 base{9.46e21};                                  // ~1 million light years, in meters
const fixed128x4 p = base + fixed128x4{double4{1.0, 2.0, 3.0, 4.0}};
const fixed128x4 q = base + fixed128x4{double4{0.5, 0.5, 0.5, 0.5}};
const double4 delta = static_cast<double4>(p - q);               // exact subtraction, then convert
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

| Macro / CMake cache variable | Default | Meaning |
|---|---|---|
| `FIXED128_FRAC_BITS` | `36` | Number of fractional bits: value = integer bits / 2^`FIXED128_FRAC_BITS`. `0` makes the types plain int128. Must be a compile-time constant. |
| `FIXED128_ROUND_HALF_AWAY_FROM_ZERO` | `1` / `ON` | `double → fixed128` SIMD conversion rounds half away from zero; `0` / `OFF` truncates. Round-half-to-even is not supported. |
| `FIXED128_BACKEND_AVX2=1` / `FIXED128_BACKEND_SCALAR=1`; CMake: `FIXED128_BACKEND` = `AUTO` / `AVX2` / `SCALAR` | automatic | See [Backends](#backends). |

Every translation unit of a program must see the same values (otherwise the program violates the ODR, silently).
With CMake, set the cache variables, e.g. `-DFIXED128_FRAC_BITS=32`; they are propagated to every consumer of the target.

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
| `fixed64` | Scalar signed 64-bit fixed point. |
| `fixed128` | Scalar signed 128-bit fixed point (`upper`, `lower`). `+ - += -=`, conversion from/to `double` (from `double`: truncates). |
| `fixed64x4` | 4 lanes of `fixed64` (`bits`, a `long4`). |
| `fixed128x4` | 4 lanes of `fixed128`, structure of arrays (`upper`, `lower`, both `ulong4`). `+ - += -=`, `operator[]`, conversion from/to `double4`. |

The constructors taking 4 scalars take them in lane order: the **first argument is lane 0** (`x`).

With the AVX2 backend, `fx128::avx2::to_m256d` / `to_m256i` / `to_double4` / `to_long4` / `to_ulong4` convert between these types and `__m256d` / `__m256i` for code that uses intrinsics directly (see [`examples/xyz_lane_packing.cpp`](examples/xyz_lane_packing.cpp)).

Free functions in [`convert.hpp`](include/fixed128/convert.hpp) (a fixed128 lane is passed as its `upper` and `lower` halves):

| Function | Valid input range | Accuracy |
|---|---|---|
| `double_to_fixed128_full`, `double_to_ufixed128_full` | full (u)fixed128 range | exact (rounded half away from zero, or truncated) |
| `fixed128_to_double`, `ufixed128_to_double` | full (u)fixed128 range | ≤ 1 ULP |
| `double_to_int64_fast`, `double_to_uint64_fast` | full (u)int64 range | exact, truncates |
| `int64_to_double_fast_precise`, `uint64_to_double_fast_precise` | full (u)int64 range | correctly rounded |

`-0.0` converts to `0` (there is no negative zero in fixed point).
NaN, ±Inf and inputs outside the valid range give undefined results: the conversions never pay a runtime cost to check them.
(Planned: optional range checks enabled by default in Debug builds only.)
Suffix meaning: `_limited` — documented restricted range only; `_fast` — full range of the target type; `_full` — full fixed128 range.

## Building the tests, benchmarks and examples

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Third-party code (doctest, nanobench) is vendored in [`third_party/`](third_party/), so no network access is needed.
The tests compare everything against an exact, pure-integer reference implementation ([`tests/reference.hpp`](tests/reference.hpp)) in three configurations (`FIXED128_FRAC_BITS` 36 / 0, rounding on / off), each for the scalar backend and, on x86-64, also for AVX2; a determinism test checks that both backends give bit-identical results.

Benchmarks (always use a Release build): `build/release/benchmarks/fixed128_bench [name-substring]`.
They report throughput over L1-resident arrays and the latency of a `double → fixed128 → double` dependency chain.
The per-instruction latency/throughput comments in the source were derived for Ice Lake; [uiCA](https://uica.uops.info) reproduces them statically from the compiled loop.

## Layout

```
include/fixed128/   the library (fixed128.hpp includes everything)
tests/              doctest tests and the reference implementation
benchmarks/         nanobench benchmarks
examples/           usage examples
third_party/        vendored doctest and nanobench
docs/adr/           architecture decision records
CONTEXT.md          glossary of the project's terms
```

## Credits

- Limited-range `double <-> int64` conversions (`detail/limited.hpp`): Mysticial's answer, and full-range `int64 -> double` conversions: wim's answer, both to
  [How to efficiently perform double/int64 conversions with SSE/AVX?](https://stackoverflow.com/questions/41144668/how-to-efficiently-perform-double-int64-conversions-with-sse-avx)
  on Stack Overflow. Stack Overflow contributions are licensed under [CC BY-SA](https://stackoverflow.com/help/licensing); see [ADR 0004](docs/adr/0004-stack-overflow-code-attribution.md).
- Everything else: 矢速 (Velctor). Licensed under the [MIT License](LICENSE).
