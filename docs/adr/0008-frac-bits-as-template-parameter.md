# Fractional bits are a template parameter without default

Supersedes [ADR 0005](0005-frac-bits-as-global-macro.md).

The number of fractional bits is the template parameter of `fixed64<FracBits>`, `fixed128<FracBits>`, `fixed64x4<FracBits>` and `fixed128x4<FracBits>`, of `fixed64_maxvalue` / `fixed64_minvalue`, and of the four (u)fixed128 conversion functions (`double_to_fixed128_full<FracBits>` etc.), on every backend. One program can use several scales, and values of different scales are distinct types that do not convert into each other, so mixing them is a compile error instead of a wrong value.

The parameter has no default, and there are no aliases for a default scale: every use names its scale. A default would make code that forgot about the scale compile anyway and quietly use 36, which is the kind of silent error this change exists to remove; the extra `<36>` is a cheap price for that constraint. A program that uses one scale throughout can define its own constant or alias.

The macro `FIXED128_FRAC_BITS` and its CMake cache variable were removed outright, without a deprecation period, because the library had no users yet.

Numeric results are unchanged for every scale and backend: the magic numbers became `constexpr` variable templates with the same expressions, the floating-point operations kept their order, and the scalar code still uses `std::pow(2.0, FracBits)`. This was checked by hashing every output of the macro-based headers and the template headers over the same inputs, for several scales, both rounding modes and both backends; the recorded hashes of the backend determinism test are unchanged.

`FIXED128_ROUND_HALF_AWAY_FROM_ZERO` and the backend selection are still global macros, with the same ODR risk; making them template parameters would be separate decisions. The tests therefore still build one executable per rounding mode and backend, and each runs every scale listed in `FIXED128_TEST_FRAC_BITS` (`tests/test_helpers.hpp`).
