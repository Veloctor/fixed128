// Scalar fixed64 / fixed128 conversions, the lane layout of the x4 types, and several scales in one program.
#include "mismatch_log.hpp"
#include "test_helpers.hpp"

#include <cmath>
#include <cstddef>
#include <type_traits>

using namespace fx128;

TEST_SUITE("scalar types")
{
	TEST_CASE_TEMPLATE("fixed128(double) truncates towards zero", F, FIXED128_TEST_FRAC_BITS)
	{
		mismatch_log log;
		std::mt19937_64 rng(kSeed);
		std::vector<double> inputs = edge_scaled_values(true, F::value);
		for (int i = 0; i < kRandomCount; ++i) inputs.push_back(random_scaled_double(rng, -8, 127, true, F::value));
		for (double v : inputs) {
			const fixed128<F::value> got{v};
			const ref::u128 expected = ref::double_to_fixed128(v, F::value, ref::rounding::trunc);
			if (got.upper != expected.hi || got.lower != expected.lo)
				log.add("in=%a got=%016llx_%016llx expected=%016llx_%016llx", v, (unsigned long long)got.upper,
					(unsigned long long)got.lower, (unsigned long long)expected.hi, (unsigned long long)expected.lo);
		}
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE_TEMPLATE("double(fixed128) is within 1 ULP", F, FIXED128_TEST_FRAC_BITS)
	{
		mismatch_log log;
		std::mt19937_64 rng(kSeed);
		for (int i = 0; i < kRandomCount; ++i) {
			ref::u128 x = ref::shr(random_u128(rng), static_cast<int>(rng() % 128));
			if (rng() & 1) x = ref::neg(x);
			const double got = static_cast<double>(fixed128<F::value>{static_cast<int64_t>(x.hi), static_cast<int64_t>(x.lo)});
			const double expected = ref::fixed128_to_double(x, F::value, true);
			if (ref::ulp_distance(got, expected) > 1)
				log.add("in=%016llx_%016llx got=%a expected=%a", (unsigned long long)x.hi, (unsigned long long)x.lo, got, expected);
		}
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE_TEMPLATE("fixed64 round trip through double", F, FIXED128_TEST_FRAC_BITS_64)
	{
		using fixed64_t = fixed64<F::value>;
		// Values exactly representable both as double and as fixed64 (fixed64 holds magnitudes below 2^(63 - F)).
		for (double v : {0.0, 1.0, -1.0, 1e6, -1e6}) {
			if (std::fabs(v) >= ref::pow2(63 - F::value)) continue;
			const fixed64_t f{v};
			CHECK(static_cast<double>(f) == v);
		}
		if (F::value >= 4)
			for (double v : {0.5, -1234.5625}) {
				const fixed64_t f{v};
				CHECK(static_cast<double>(f) == v);
			}
		CHECK(fixed64_t{1.0}.sbits == (int64_t{1} << F::value));
		CHECK(fixed64_maxvalue<F::value>.sbits == INT64_MAX);
		CHECK(fixed64_minvalue<F::value>.sbits == INT64_MIN);
		CHECK(fixed64_t{1.0} < fixed64_t{2.0});
		CHECK(fixed64_t{-1.0} != fixed64_t{1.0});
	}
}

TEST_SUITE("x4 types")
{
	TEST_CASE_TEMPLATE("fixed128x4 constructors and operator[] lane order", F, FIXED128_TEST_FRAC_BITS)
	{
		using fixed128_t = fixed128<F::value>;
		const fixed128_t a{0, 1}, b{0, 2}, c{0, 3}, d{0, 4};
		// The 4-argument constructors take the lanes in order: the first argument is lane 0 (x).
		const fixed128x4<F::value> v{a, b, c, d};
		CHECK(v[0].lower == 1);
		CHECK(v[1].lower == 2);
		CHECK(v[2].lower == 3);
		CHECK(v[3].lower == 4);
		CHECK(v.lower.x == 1);
		CHECK(v.lower.w == 4);
		const fixed128x4<F::value> broadcast{fixed128_t{7, 8}};
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
		static_assert(sizeof(fixed128x4<36>) == 64 && offsetof(fixed128x4<36>, lower) == 32);
		const double4 x{1.0, 2.0, 3.0, 4.0};
		CHECK(to_lanes(x) == lanes_f64{1.0, 2.0, 3.0, 4.0});
	}

	TEST_CASE_TEMPLATE("fixed128x4 double conversions match the free functions", F, FIXED128_TEST_FRAC_BITS)
	{
		// magnitudes about 2^50 to 2^101 units, valid for every scale
		const double u = ref::pow2(60 - F::value);
		const double4 x{1.25 * u, -3.5 * u, 1.5 * ref::pow2(100 - F::value), -1e-3 * u};
		const fixed128x4<F::value> v{x};
		ulong4 hi, lo;
		double_to_fixed128_full<F::value>(x, hi, lo);
		CHECK(to_lanes(v.upper) == to_lanes(hi));
		CHECK(to_lanes(v.lower) == to_lanes(lo));
		CHECK(to_lanes(static_cast<double4>(v)) == to_lanes(fixed128_to_double<F::value>(hi, lo)));
		for (int l = 0; l < 4; ++l) CHECK(v[l].upper == to_lanes(hi)[l]);
	}

	TEST_CASE_TEMPLATE("fixed64x4 conversions", F, FIXED128_TEST_FRAC_BITS_64)
	{
		using fixed64x4_t = fixed64x4<F::value>;
		const double4 x{1.0, -2.0, F::value >= 1 ? 0.5 : 3.0, 1024.0};
		const fixed64x4_t v{x};
		const lanes_i64 bits = to_lanes(v.bits);
		CHECK(bits[0] == uint64_t{1} << F::value);
		CHECK(static_cast<int64_t>(bits[1]) == -(int64_t{2} << F::value));
		CHECK(to_lanes(static_cast<double4>(v)) == to_lanes(x));
		const lanes_f64 single{to_lanes(static_cast<double4>(fixed64x4_t{3.0}))};
		CHECK(single == lanes_f64{3.0, 3.0, 3.0, 3.0});
		const fixed64x4_t from_int{long4{1, -2, 3, 1024}};
		CHECK(to_lanes(static_cast<double4>(from_int)) == lanes_f64{1.0, -2.0, 3.0, 1024.0});
	}

	TEST_CASE_TEMPLATE("fixed64x4 float conversions", F, FIXED128_TEST_FRAC_BITS_64)
	{
		const float4 x{1.0f, -2.0f, 0.25f, 1024.0f};
		const fixed64x4<F::value> v{x};
		const float4 back = static_cast<float4>(v);
		CHECK(back.x == 1.0f);
		CHECK(back.y == -2.0f);
		CHECK(back.z == (F::value >= 2 ? 0.25f : 0.0f));
		CHECK(back.w == 1024.0f);
	}
}

namespace
{
	// Whether double_to_fixed128_full can be called without naming the scale (it must not: there is no default).
	template <typename V, typename = void>
	struct convert_without_scale : std::false_type {};
	template <typename V>
	struct convert_without_scale<V, std::void_t<decltype(double_to_fixed128_full(std::declval<V>(),
		std::declval<ulong4&>(), std::declval<ulong4&>()))>> : std::true_type {};
	// Positive control for the detector above.
	template <typename V, typename = void>
	struct convert_with_scale : std::false_type {};
	template <typename V>
	struct convert_with_scale<V, std::void_t<decltype(double_to_fixed128_full<36>(std::declval<V>(),
		std::declval<ulong4&>(), std::declval<ulong4&>()))>> : std::true_type {};
}

TEST_SUITE("scales")
{
	static_assert(fixed128<12>::frac_bits == 12 && fixed128x4<12>::frac_bits == 12, "");
	// The scale has no default: it is always named explicitly.
	static_assert(convert_with_scale<double4>::value && !convert_without_scale<double4>::value, "");
	// Values of different scales do not mix silently.
	static_assert(!std::is_convertible<fixed128<36>, fixed128<0>>::value, "");
	static_assert(!std::is_convertible<fixed64<36>, fixed128<0>>::value, "");
	static_assert(!std::is_convertible<fixed128<36>, fixed128x4<0>>::value, "");

	TEST_CASE("several scales in one program use their own scale")
	{
		const double4 x{3.0, 3.0, 3.0, 3.0};
		const fixed128x4<36> a{x};
		const fixed128x4<0> b{x};
		CHECK(a[0].upper == 0);
		CHECK(a[0].lower == uint64_t{3} << 36);
		CHECK(b[0].upper == 0);
		CHECK(b[0].lower == 3);
		CHECK(static_cast<double>(fixed128<36>{0.5}) == 0.5);
		CHECK(fixed128<0>{0.5}.lower == 0);
		CHECK(to_lanes(static_cast<double4>(a)) == to_lanes(x));
		CHECK(to_lanes(static_cast<double4>(b)) == to_lanes(x));
	}
}
