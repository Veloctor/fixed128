#pragma once
#include "reference.hpp"

#include <fixed128/fixed128.hpp>

#include <array>
#include <cstdint>
#include <random>
#include <vector>

constexpr int kFracBits = FIXED128_FRAC_BITS;
constexpr ref::rounding kRounding = FIXED128_ROUND_HALF_AWAY_FROM_ZERO ? ref::rounding::half_away_from_zero : ref::rounding::trunc;

// Fixed seed: failures are reproducible.
constexpr uint64_t kSeed = 0x5EED'F1ED'0128'2022ull;
constexpr int kRandomCount = 1 << 18;

using lanes_i64 = std::array<uint64_t, 4>;
using lanes_f64 = std::array<double, 4>;

inline lanes_i64 to_lanes(__m256i v)
{
	alignas(32) lanes_i64 a;
	_mm256_store_si256(reinterpret_cast<__m256i*>(a.data()), v);
	return a;
}

inline lanes_f64 to_lanes(__m256d v)
{
	alignas(32) lanes_f64 a;
	_mm256_store_pd(a.data(), v);
	return a;
}

inline __m256i from_lanes(const lanes_i64& a) { return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data())); }
inline __m256d from_lanes(const lanes_f64& a) { return _mm256_loadu_pd(a.data()); }

// Random double whose scaled magnitude |v| * 2^kFracBits lies in [2^min_log2, 2^max_log2).
inline double random_scaled_double(std::mt19937_64& rng, int min_log2, int max_log2, bool allow_negative)
{
	std::uniform_int_distribution<int> exp_dist(min_log2, max_log2 - 1);
	const uint64_t mant = rng() & 0xFFFFFFFFFFFFFull;
	const int e = exp_dist(rng) - kFracBits; // |v| in [2^e, 2^(e+1))
	const uint64_t sign = allow_negative ? (rng() & 1) << 63 : 0;
	return ref::double_of(sign | (static_cast<uint64_t>(e + 1023) << 52) | mant);
}

// v = (k + 0.5) / 2^kFracBits: exactly half-way between two representable fixed-point values.
inline double random_halfway_double(std::mt19937_64& rng, bool allow_negative)
{
	const uint64_t k = rng() >> 12; // < 2^52, so 2k+1 < 2^53 is exact
	double v = static_cast<double>(2 * k + 1) * ref::pow2(-1 - kFracBits);
	return allow_negative && (rng() & 1) ? -v : v;
}

inline ref::u128 random_u128(std::mt19937_64& rng) { return {rng(), rng()}; }

// Hand-picked scaled magnitudes (as log2 or exact values) that stress piece boundaries and rounding.
inline std::vector<double> edge_scaled_values(bool is_signed)
{
	const double unit = ref::pow2(-kFracBits);
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
