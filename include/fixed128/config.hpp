#pragma once

// Compile-time configuration of the fixed128 library.
// Every translation unit in a program must see the same values; with CMake, set them through the
// FIXED128_FRAC_BITS / FIXED128_ROUND_HALF_AWAY_FROM_ZERO cache variables, which are propagated to
// all consumers of the fixed128::fixed128 target.

// Number of fractional bits: a fixed-point value equals its integer bits / 2^FIXED128_FRAC_BITS.
// 0 turns the conversions into plain int128 conversions.
// Must be a compile-time constant; any value is zero-overhead, not limited to [0,128].
#ifndef FIXED128_FRAC_BITS
#define FIXED128_FRAC_BITS 36
#endif

// Rounding of double -> fixed128 SIMD conversions:
//  1: round half away from zero (chinese: 四舍五入), e.g. r(1.49) = 1, r(1.5) = 2, r(2.5) = 3, r(-4.5) = -5.
//     Different from the IEEE 754 default "half to nearest even", which is not supported.
//     Costs a little performance.
//  0: round towards zero (trunc).
#ifndef FIXED128_ROUND_HALF_AWAY_FROM_ZERO
#define FIXED128_ROUND_HALF_AWAY_FROM_ZERO 1
#endif

#if !defined(__AVX2__)
#error "fixed128 requires AVX2: compile with /arch:AVX2 (MSVC) or -mavx2 (GCC/Clang)."
#endif

// The conversions rely on exact IEEE 754 semantics and on the order of floating-point additions.
#if defined(__FAST_MATH__) || defined(_M_FP_FAST)
#error "fixed128 must not be compiled with fast-math (-ffast-math, -Ofast, /fp:fast): it breaks the conversions."
#endif
