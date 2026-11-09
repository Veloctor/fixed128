#pragma once

// Compile-time configuration of the fixed128 library.
// Every translation unit in a program must see the same values; with CMake, set them through the
// FIXED128_FRAC_BITS / FIXED128_ROUND_HALF_AWAY_FROM_ZERO / FIXED128_BACKEND cache variables, which are propagated to
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

// Backend of the x4 types and the conversions. Every backend gives bit-identical results per lane.
//  FIXED128_BACKEND_AVX2=1:   AVX2 intrinsics; the whole program must be compiled with /arch:AVX2 or -mavx2.
//  FIXED128_BACKEND_SCALAR=1: portable C++, 4 scalar lanes (written so that compilers can auto-vectorize it).
// Define at most one of them to 1. If neither is defined, x86-64 selects AVX2 (and fails to compile without AVX2,
// so that a missing -mavx2 does not silently cost performance) and every other architecture selects scalar.
#if defined(FIXED128_BACKEND_AVX2) && FIXED128_BACKEND_AVX2 && defined(FIXED128_BACKEND_SCALAR) && FIXED128_BACKEND_SCALAR
#error "fixed128: define only one of FIXED128_BACKEND_AVX2 and FIXED128_BACKEND_SCALAR."
#endif
#if !(defined(FIXED128_BACKEND_AVX2) && FIXED128_BACKEND_AVX2) && !(defined(FIXED128_BACKEND_SCALAR) && FIXED128_BACKEND_SCALAR)
#undef FIXED128_BACKEND_AVX2
#undef FIXED128_BACKEND_SCALAR
#if defined(_M_X64) || defined(__x86_64__)
#define FIXED128_BACKEND_AVX2 1
#define FIXED128_BACKEND_SCALAR 0
#else
#define FIXED128_BACKEND_AVX2 0
#define FIXED128_BACKEND_SCALAR 1
#endif
#elif defined(FIXED128_BACKEND_AVX2) && FIXED128_BACKEND_AVX2
#undef FIXED128_BACKEND_SCALAR
#define FIXED128_BACKEND_SCALAR 0
#else
#undef FIXED128_BACKEND_AVX2
#define FIXED128_BACKEND_AVX2 0
#endif

#if FIXED128_BACKEND_AVX2 && !defined(__AVX2__)
#error "fixed128: the AVX2 backend requires AVX2: compile with /arch:AVX2 (MSVC) or -mavx2 (GCC/Clang), or select FIXED128_BACKEND_SCALAR."
#endif

// The conversions rely on exact IEEE 754 semantics and on the order of floating-point additions.
#if defined(__FAST_MATH__) || defined(_M_FP_FAST)
#error "fixed128 must not be compiled with fast-math (-ffast-math, -Ofast, /fp:fast): it breaks the conversions."
#endif
// Excess precision (x87) would round intermediate results differently.
#if defined(__FLT_EVAL_METHOD__) && __FLT_EVAL_METHOD__ != 0
#error "fixed128 requires FLT_EVAL_METHOD == 0 (e.g. SSE2 instead of x87 floating point)."
#endif
