// Every backend must give bit-identical results, also where the contract allows some error (fixed128 -> double is
// within 1 ULP, and that ULP must be the same everywhere). This hashes the results of all conversions and x4 operations
// over fixed inputs and compares the hash with the value recorded from the AVX2 backend.
// The inputs use only raw mt19937_64 output, which the standard specifies exactly (the distribution classes are not).
#include <doctest/doctest.h>

#include "test_helpers.hpp"

#include <cstdio>

using namespace fx128;

namespace
{
	struct fnv1a
	{
		uint64_t h = 0xcbf29ce484222325ull;

		void add(uint64_t x)
		{
			for (int i = 0; i < 8; ++i) {
				h ^= (x >> (8 * i)) & 0xFF;
				h *= 0x100000001b3ull;
			}
		}

		void add(const ulong4& v) { for (uint64_t x : to_lanes(v)) add(x); }
		void add(const long4& v) { for (uint64_t x : to_lanes(v)) add(x); }
		void add(const double4& v) { for (double x : to_lanes(v)) add(ref::bits_of(x)); }
	};

	// |v| * 2^frac_bits in [2^min_log2, 2^max_log2), from raw generator output only.
	double raw_double(std::mt19937_64& rng, int min_log2, int max_log2, int frac_bits, bool allow_negative)
	{
		const int e = min_log2 + static_cast<int>(rng() % static_cast<uint64_t>(max_log2 - min_log2)) - frac_bits;
		const uint64_t sign = allow_negative ? (rng() & 1) << 63 : 0;
		return ref::double_of(sign | (static_cast<uint64_t>(e + 1023) << 52) | (rng() & 0xFFFFFFFFFFFFFull));
	}

	double4 raw_double4(std::mt19937_64& rng, int min_log2, int max_log2, int frac_bits, bool allow_negative)
	{
		double4 v;
		v.x = raw_double(rng, min_log2, max_log2, frac_bits, allow_negative);
		v.y = raw_double(rng, min_log2, max_log2, frac_bits, allow_negative);
		v.z = raw_double(rng, min_log2, max_log2, frac_bits, allow_negative);
		v.w = raw_double(rng, min_log2, max_log2, frac_bits, allow_negative);
		return v;
	}

	uint64_t raw_int(std::mt19937_64& rng) { return rng() >> (rng() % 64); }

	ulong4 raw_ulong4(std::mt19937_64& rng)
	{
		ulong4 v;
		v.x = raw_int(rng);
		v.y = raw_int(rng);
		v.z = raw_int(rng);
		v.w = raw_int(rng);
		return v;
	}

	template <int FracBits>
	uint64_t hash_all_results()
	{
		fnv1a h;
		std::mt19937_64 rng(kSeed);
		for (int i = 0; i < 1 << 14; ++i) {
			ulong4 hi, lo;
			double_to_fixed128_full<FracBits>(raw_double4(rng, -8, 127, FracBits, true), hi, lo);
			h.add(hi), h.add(lo);
			h.add(fixed128_to_double<FracBits>(hi, lo));
			double_to_ufixed128_full<FracBits>(raw_double4(rng, -8, 128, FracBits, false), hi, lo);
			h.add(hi), h.add(lo);
			h.add(ufixed128_to_double<FracBits>(hi, lo));

			ulong4 rhi = raw_ulong4(rng), rlo = raw_ulong4(rng);
			h.add(fixed128_to_double<FracBits>(rhi, rlo));
			h.add(ufixed128_to_double<FracBits>(rhi, rlo));

			h.add(double_to_int64_fast(raw_double4(rng, -4, 63, 0, true)));
			h.add(double_to_uint64_fast(raw_double4(rng, -4, 64, 0, false)));
			const ulong4 ints = raw_ulong4(rng);
			const long4 sints{static_cast<int64_t>(ints.x), -static_cast<int64_t>(ints.y >> 1), static_cast<int64_t>(ints.z),
				-static_cast<int64_t>(ints.w >> 1)};
			h.add(int64_to_double_fast_precise(sints));
			h.add(uint64_to_double_fast_precise(ints));

			const fixed64x4<FracBits> f64{raw_double4(rng, -8, 62, FracBits, true)};
			h.add(f64.bits);
			h.add(static_cast<double4>(f64));

			const fixed128x4<FracBits> a{rhi, rlo}, b{hi, lo};
			const fixed128x4<FracBits> sum = a + b, diff = a - b, sum_lo = a + lo, diff_lo = a - lo;
			for (const fixed128x4<FracBits>* r : {&sum, &diff, &sum_lo, &diff_lo}) h.add(r->upper), h.add(r->lower);
		}
		return h.h;
	}

	struct golden
	{
		int frac_bits;
		int round_half_away;
		uint64_t hash;
	};

	// Recorded from the AVX2 backend. Change only together with an intended change of the numeric behavior.
	constexpr golden kGolden[] = {
		{36, 1, 0x469714c9cfe26938ull},
		{36, 0, 0xedb82735adc905d3ull},
		{0, 1, 0xe2bafe041f60ef50ull},
		{0, 0, 0xb60fe3dbc91df283ull}, // recorded from the macro-based headers of 391d796 (AVX2 and scalar agree)
	};
}

TEST_SUITE("backend determinism")
{
	TEST_CASE_TEMPLATE("all results hash to the value recorded from the AVX2 backend", F, FIXED128_TEST_FRAC_BITS)
	{
		const uint64_t got = hash_all_results<F::value>();
		const golden* expected = nullptr;
		for (const golden& g : kGolden)
			if (g.frac_bits == F::value && g.round_half_away == FIXED128_ROUND_HALF_AWAY_FROM_ZERO) expected = &g;
		if (!expected) {
			std::printf("no recorded hash for this configuration; result hash = 0x%016llx\n", (unsigned long long)got);
			return;
		}
		char text[32];
		std::snprintf(text, sizeof text, "0x%016llx", (unsigned long long)got);
		INFO("result hash = ", text);
		CHECK(got == expected->hash);
	}
}
