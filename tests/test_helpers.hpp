#pragma once
#include "reference.hpp"

#include <doctest/doctest.h>
#include <fixed128/fixed128.hpp>

#include <array>
#include <cstdint>
#include <random>
#include <tuple>
#include <type_traits>
#include <vector>

// The scales every scale-dependent test runs with, one program for all of them. Chosen where the magic numbers and
// shifts change behavior: 0 (plain int128), 1, 36 (typical), 52 / 53 (fraction as wide as the double mantissa),
// 64 (fraction fills the lower half), 100 and 127 (magic numbers near 1).
// FIXED128_TEST_FRAC_BITS: doctest TEST_CASE_TEMPLATE type list; test_frac_bits: the same as a std::tuple.
// fixed64 holds only 64 bits, so its tests use the scales up to 52.
template <int N>
using frac = std::integral_constant<int, N>;
#define FIXED128_TEST_FRAC_BITS frac<0>, frac<1>, frac<36>, frac<52>, frac<53>, frac<64>, frac<100>, frac<127>
#define FIXED128_TEST_FRAC_BITS_64 frac<0>, frac<1>, frac<36>, frac<52>
using test_frac_bits = std::tuple<FIXED128_TEST_FRAC_BITS>;
TYPE_TO_STRING_AS("frac0", frac<0>);
TYPE_TO_STRING_AS("frac1", frac<1>);
TYPE_TO_STRING_AS("frac36", frac<36>);
TYPE_TO_STRING_AS("frac52", frac<52>);
TYPE_TO_STRING_AS("frac53", frac<53>);
TYPE_TO_STRING_AS("frac64", frac<64>);
TYPE_TO_STRING_AS("frac100", frac<100>);
TYPE_TO_STRING_AS("frac127", frac<127>);

constexpr ref::rounding kRounding = FIXED128_ROUND_HALF_AWAY_FROM_ZERO ? ref::rounding::half_away_from_zero : ref::rounding::trunc;

// Fixed seed: failures are reproducible.
constexpr uint64_t kSeed = 0x5EED'F1ED'0128'2022ull;
// Random inputs per test; fewer in unoptimized (Debug, sanitizer) builds, where the structured grids still run in full.
#ifdef NDEBUG
constexpr int kRandomCount = 1 << 18;
#else
constexpr int kRandomCount = 1 << 14;
#endif

using lanes_i64 = std::array<uint64_t, 4>;
using lanes_f64 = std::array<double, 4>;

inline lanes_i64 to_lanes(const fx128::ulong4& v) { return {v.x, v.y, v.z, v.w}; }
inline lanes_i64 to_lanes(const fx128::long4& v)
{
	return {static_cast<uint64_t>(v.x), static_cast<uint64_t>(v.y), static_cast<uint64_t>(v.z), static_cast<uint64_t>(v.w)};
}
inline lanes_f64 to_lanes(const fx128::double4& v) { return {v.x, v.y, v.z, v.w}; }

// Converts to whichever of long4 / ulong4 the function under test takes.
struct int_lanes
{
	lanes_i64 a;
	operator fx128::ulong4() const { return {a[0], a[1], a[2], a[3]}; }
	operator fx128::long4() const
	{
		return {static_cast<int64_t>(a[0]), static_cast<int64_t>(a[1]), static_cast<int64_t>(a[2]), static_cast<int64_t>(a[3])};
	}
};

inline int_lanes from_lanes(const lanes_i64& a) { return {a}; }
inline fx128::double4 from_lanes(const lanes_f64& a) { return {a[0], a[1], a[2], a[3]}; }

// Random double whose scaled magnitude |v| * 2^frac_bits lies in [2^min_log2, 2^max_log2).
inline double random_scaled_double(std::mt19937_64& rng, int min_log2, int max_log2, bool allow_negative, int frac_bits)
{
	std::uniform_int_distribution<int> exp_dist(min_log2, max_log2 - 1);
	const uint64_t mant = rng() & 0xFFFFFFFFFFFFFull;
	const int e = exp_dist(rng) - frac_bits; // |v| in [2^e, 2^(e+1))
	const uint64_t sign = allow_negative ? (rng() & 1) << 63 : 0;
	return ref::double_of(sign | (static_cast<uint64_t>(e + 1023) << 52) | mant);
}

// v = (k + 0.5) / 2^frac_bits: exactly half-way between two representable fixed-point values.
inline double random_halfway_double(std::mt19937_64& rng, bool allow_negative, int frac_bits)
{
	const uint64_t k = rng() >> 12; // < 2^52, so 2k+1 < 2^53 is exact
	double v = static_cast<double>(2 * k + 1) * ref::pow2(-1 - frac_bits);
	return allow_negative && (rng() & 1) ? -v : v;
}

inline ref::u128 random_u128(std::mt19937_64& rng) { return {rng(), rng()}; }

// Hand-picked scaled magnitudes (as log2 or exact values) that stress piece boundaries and rounding.
inline std::vector<double> edge_scaled_values(bool is_signed, int frac_bits)
{
	const double unit = ref::pow2(-frac_bits);
	std::vector<double> v = {
		0.0, unit, 2 * unit, 0.5 * unit, 1.5 * unit, 2.5 * unit,
		ref::double_of(ref::bits_of(0.5) - 1) * unit, // just below half a unit
		ref::double_of(ref::bits_of(0.5) + 1) * unit, // just above half a unit
	};
	for (int p : {1, 31, 32, 47, 48, 49, 52, 53, 63, 64, 65, 95, 96, 97, 100, 120, 126}) {
		v.push_back(ref::pow2(p) * unit);
		v.push_back(ref::double_of(ref::bits_of(ref::pow2(p)) - 1) * unit); // largest double below 2^p
		v.push_back(ref::double_of(ref::bits_of(ref::pow2(p)) + 1) * unit); // smallest double above 2^p
	}
	v.push_back(ref::double_of(ref::bits_of(ref::pow2(127)) - 1) * unit); // largest magnitude < 2^127
	if (!is_signed)
		v.push_back(ref::double_of(ref::bits_of(ref::pow2(128)) - 1) * unit); // largest magnitude < 2^128
	if (is_signed) {
		const size_t n = v.size();
		for (size_t i = 0; i < n; ++i) v.push_back(-v[i]);
		v.push_back(-ref::pow2(127) * unit); // most negative fixed128
	}
	return v;
}
