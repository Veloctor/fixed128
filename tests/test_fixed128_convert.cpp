// double <-> fixed128 SIMD conversions against the reference implementation.
#include "mismatch_log.hpp"
#include "test_helpers.hpp"

using namespace fx128;

namespace
{
	// Runs the SIMD double -> (u)fixed128 conversion on 4 inputs at a time and compares with the reference.
	template <typename Convert>
	void check_double_to_fixed128(const std::vector<double>& inputs, bool is_signed, Convert convert, mismatch_log& log)
	{
		for (size_t i = 0; i < inputs.size(); i += 4) {
			lanes_f64 in{};
			for (size_t l = 0; l < 4 && i + l < inputs.size(); ++l) in[l] = inputs[i + l];
			__m256i hi, lo;
			convert(from_lanes(in), hi, lo);
			const lanes_i64 h = to_lanes(hi), o = to_lanes(lo);
			for (size_t l = 0; l < 4 && i + l < inputs.size(); ++l) {
				const ref::u128 expected = ref::double_to_fixed128(in[l], kFracBits, kRounding);
				if (h[l] != expected.hi || o[l] != expected.lo)
					log.add("%s: in=%a got=%016llx_%016llx expected=%016llx_%016llx", is_signed ? "signed" : "unsigned", in[l],
						(unsigned long long)h[l], (unsigned long long)o[l],
						(unsigned long long)expected.hi, (unsigned long long)expected.lo);
			}
		}
	}

	template <typename Convert>
	void check_fixed128_to_double(const std::vector<ref::u128>& inputs, bool is_signed, uint64_t max_ulp, Convert convert, mismatch_log& log)
	{
		for (size_t i = 0; i < inputs.size(); i += 4) {
			lanes_i64 h{}, o{};
			for (size_t l = 0; l < 4 && i + l < inputs.size(); ++l) {
				h[l] = inputs[i + l].hi;
				o[l] = inputs[i + l].lo;
			}
			const lanes_f64 got = to_lanes(convert(from_lanes(h), from_lanes(o)));
			for (size_t l = 0; l < 4 && i + l < inputs.size(); ++l) {
				const double expected = ref::fixed128_to_double(inputs[i + l], kFracBits, is_signed);
				if (ref::ulp_distance(got[l], expected) > max_ulp)
					log.add("%s: in=%016llx_%016llx got=%a expected=%a (%llu ulp)", is_signed ? "signed" : "unsigned",
						(unsigned long long)h[l], (unsigned long long)o[l], got[l], expected,
						(unsigned long long)ref::ulp_distance(got[l], expected));
			}
		}
	}

	std::vector<double> random_doubles(int count, int max_log2, bool is_signed)
	{
		std::mt19937_64 rng(kSeed);
		std::vector<double> v;
		for (int i = 0; i < count; ++i) v.push_back(random_scaled_double(rng, -8, max_log2, is_signed));
		return v;
	}

	std::vector<double> halfway_doubles(int count, bool is_signed)
	{
		std::mt19937_64 rng(kSeed + 1);
		std::vector<double> v;
		for (int i = 0; i < count; ++i) v.push_back(random_halfway_double(rng, is_signed));
		return v;
	}

	// Random 128-bit patterns with a random number of leading zero/sign bits, to cover all magnitudes.
	std::vector<ref::u128> random_fixed128(int count, bool is_signed)
	{
		std::mt19937_64 rng(kSeed + 2);
		std::vector<ref::u128> v;
		for (int i = 0; i < count; ++i) {
			ref::u128 x = ref::shr(random_u128(rng), static_cast<int>(rng() % 128));
			if (is_signed && (rng() & 1)) x = ref::neg(x);
			v.push_back(x);
		}
		return v;
	}

	std::vector<ref::u128> edge_fixed128(bool is_signed)
	{
		std::vector<ref::u128> v = {{0, 0}, {0, 1}, {0, ~0ull}, {1, 0}};
		for (int p = 0; p < 128; ++p) {
			ref::u128 one = ref::shl({0, 1}, p);
			v.push_back(one);
			v.push_back(ref::sub(one, {0, 1}));
		}
		const size_t n = v.size();
		if (is_signed)
			for (size_t i = 0; i < n; ++i) v.push_back(ref::neg(v[i]));
		return v;
	}
}

// KNOWN ISSUE (not fixed yet, the library keeps its original numeric behavior):
// with FIXED128_ROUND_HALF_AWAY_FROM_ZERO, double_to_(u)fixed128_full adds 0.5 to the *unscaled* double, i.e. half of
// a whole unit instead of half of 2^-FIXED128_FRAC_BITS; and the floating-point addition v + 0.5 itself rounds, which
// breaks inputs just below 0.5 and inputs >= 2^52. Truncation mode is exact.
// may_fail keeps these cases running and reporting without failing the suite; remove it once the rounding is fixed.
#define KNOWN_ROUNDING_ISSUE doctest::may_fail(FIXED128_ROUND_HALF_AWAY_FROM_ZERO != 0)

TEST_SUITE("double -> fixed128")
{
	TEST_CASE("signed: edge values" * KNOWN_ROUNDING_ISSUE)
	{
		mismatch_log log;
		check_double_to_fixed128(edge_scaled_values(true), true, double_to_fixed128_full, log);
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("signed: random values over the full range" * KNOWN_ROUNDING_ISSUE)
	{
		mismatch_log log;
		check_double_to_fixed128(random_doubles(kRandomCount, 127, true), true, double_to_fixed128_full, log);
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("signed: values exactly half-way between two fixed-point values" * KNOWN_ROUNDING_ISSUE)
	{
		mismatch_log log;
		check_double_to_fixed128(halfway_doubles(kRandomCount, true), true, double_to_fixed128_full, log);
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("unsigned: edge values" * KNOWN_ROUNDING_ISSUE)
	{
		mismatch_log log;
		check_double_to_fixed128(edge_scaled_values(false), false, double_to_ufixed128_full, log);
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("unsigned: random values over the full range" * KNOWN_ROUNDING_ISSUE)
	{
		mismatch_log log;
		check_double_to_fixed128(random_doubles(kRandomCount, 128, false), false, double_to_ufixed128_full, log);
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("unsigned: values exactly half-way between two fixed-point values" * KNOWN_ROUNDING_ISSUE)
	{
		mismatch_log log;
		check_double_to_fixed128(halfway_doubles(kRandomCount, false), false, double_to_ufixed128_full, log);
		CHECK_NO_MISMATCH(log);
	}
}

TEST_SUITE("fixed128 -> double")
{
	// Documented accuracy: at most 1 ULP from the correctly rounded result.
	constexpr uint64_t kMaxUlp = 1;

	TEST_CASE("signed: edge values")
	{
		mismatch_log log;
		check_fixed128_to_double(edge_fixed128(true), true, kMaxUlp, fixed128_to_double, log);
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("signed: random values")
	{
		mismatch_log log;
		check_fixed128_to_double(random_fixed128(kRandomCount, true), true, kMaxUlp, fixed128_to_double, log);
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("unsigned: edge values")
	{
		mismatch_log log;
		check_fixed128_to_double(edge_fixed128(false), false, kMaxUlp, ufixed128_to_double, log);
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("unsigned: random values")
	{
		mismatch_log log;
		check_fixed128_to_double(random_fixed128(kRandomCount, false), false, kMaxUlp, ufixed128_to_double, log);
		CHECK_NO_MISMATCH(log);
	}
}
