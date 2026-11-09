// Scalar fixed64 / fixed128 conversions and the lane layout of the x4 types.
#include "mismatch_log.hpp"
#include "test_helpers.hpp"

#include <cstddef>
#include <type_traits>

using namespace fx128;

TEST_SUITE("scalar types")
{
	TEST_CASE("fixed128(double) truncates towards zero")
	{
		mismatch_log log;
		std::mt19937_64 rng(kSeed);
		std::vector<double> inputs = edge_scaled_values(true);
		for (int i = 0; i < kRandomCount; ++i) inputs.push_back(random_scaled_double(rng, -8, 127, true));
		for (double v : inputs) {
			const fixed128 got{v};
			const ref::u128 expected = ref::double_to_fixed128(v, kFracBits, ref::rounding::trunc);
			if (got.upper != expected.hi || got.lower != expected.lo)
				log.add("in=%a got=%016llx_%016llx expected=%016llx_%016llx", v, (unsigned long long)got.upper,
					(unsigned long long)got.lower, (unsigned long long)expected.hi, (unsigned long long)expected.lo);
		}
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("double(fixed128) is within 1 ULP")
	{
		mismatch_log log;
		std::mt19937_64 rng(kSeed);
		for (int i = 0; i < kRandomCount; ++i) {
			ref::u128 x = ref::shr(random_u128(rng), static_cast<int>(rng() % 128));
			if (rng() & 1) x = ref::neg(x);
			const double got = static_cast<double>(fixed128{static_cast<int64_t>(x.hi), static_cast<int64_t>(x.lo)});
			const double expected = ref::fixed128_to_double(x, kFracBits, true);
			if (ref::ulp_distance(got, expected) > 1)
				log.add("in=%016llx_%016llx got=%a expected=%a", (unsigned long long)x.hi, (unsigned long long)x.lo, got, expected);
		}
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("fixed64 round trip through double")
	{
		// Values exactly representable both as double and as fixed64.
		for (double v : {0.0, 1.0, -1.0, 1e6, -1e6}) {
			const fixed64 f{v};
			CHECK(static_cast<double>(f) == v);
		}
		if (kFracBits >= 4)
			for (double v : {0.5, -1234.5625}) {
				const fixed64 f{v};
				CHECK(static_cast<double>(f) == v);
			}
		CHECK(fixed64{1.0}.sbits == (int64_t{1} << kFracBits));
		CHECK(fixed64{1.0} < fixed64{2.0});
		CHECK(fixed64{-1.0} != fixed64{1.0});
	}
}

TEST_SUITE("x4 types")
{
	TEST_CASE("fixed128x4 constructors and operator[] lane order")
	{
		const fixed128 a{0, 1}, b{0, 2}, c{0, 3}, d{0, 4};
		// The 4-argument constructors take the lanes in order: the first argument is lane 0 (x).
		const fixed128x4 v{a, b, c, d};
		CHECK(v[0].lower == 1);
		CHECK(v[1].lower == 2);
		CHECK(v[2].lower == 3);
		CHECK(v[3].lower == 4);
		CHECK(v.lower.x == 1);
		CHECK(v.lower.w == 4);
		const fixed128x4 broadcast{fixed128{7, 8}};
		for (int i = 0; i < 4; ++i) {
			CHECK(broadcast[i].upper == 7);
			CHECK(broadcast[i].lower == 8);
		}
	}

	TEST_CASE("the public 4-lane types are plain structs with lane 0 first")
	{
		static_assert(sizeof(double4) == 32 && alignof(double4) == 32);
		static_assert(sizeof(long4) == 32 && sizeof(ulong4) == 32 && sizeof(float4) == 16);
		static_assert(std::is_trivially_copyable_v<double4> && std::is_standard_layout_v<double4>);
		static_assert(sizeof(fixed128x4) == 64 && offsetof(fixed128x4, lower) == 32);
		const double4 x{1.0, 2.0, 3.0, 4.0};
		CHECK(to_lanes(x) == lanes_f64{1.0, 2.0, 3.0, 4.0});
	}

	TEST_CASE("fixed128x4 double conversions match the free functions")
	{
		const double4 x{1.25, -3.5, 1e20, -1e-3};
		const fixed128x4 v{x};
		ulong4 hi, lo;
		double_to_fixed128_full(x, hi, lo);
		CHECK(to_lanes(v.upper) == to_lanes(hi));
		CHECK(to_lanes(v.lower) == to_lanes(lo));
		CHECK(to_lanes(static_cast<double4>(v)) == to_lanes(fixed128_to_double(hi, lo)));
		for (int l = 0; l < 4; ++l) CHECK(v[l].upper == to_lanes(hi)[l]);
	}

	TEST_CASE("fixed64x4 conversions")
	{
		const double4 x{1.0, -2.0, kFracBits >= 1 ? 0.5 : 3.0, 1024.0};
		const fixed64x4 v{x};
		const lanes_i64 bits = to_lanes(v.bits);
		CHECK(bits[0] == uint64_t{1} << kFracBits);
		CHECK(static_cast<int64_t>(bits[1]) == -(int64_t{2} << kFracBits));
		CHECK(to_lanes(static_cast<double4>(v)) == to_lanes(x));
		const lanes_f64 single{to_lanes(static_cast<double4>(fixed64x4{3.0}))};
		CHECK(single == lanes_f64{3.0, 3.0, 3.0, 3.0});
		const fixed64x4 from_int{long4{1, -2, 3, 1024}};
		CHECK(to_lanes(static_cast<double4>(from_int)) == lanes_f64{1.0, -2.0, 3.0, 1024.0});
	}

	TEST_CASE("fixed64x4 float conversions")
	{
		const float4 x{1.0f, -2.0f, 0.25f, 1024.0f};
		const fixed64x4 v{x};
		const float4 back = static_cast<float4>(v);
		CHECK(back.x == 1.0f);
		CHECK(back.y == -2.0f);
		CHECK(back.z == (kFracBits >= 2 ? 0.25f : 0.0f));
		CHECK(back.w == 1024.0f);
	}
}
