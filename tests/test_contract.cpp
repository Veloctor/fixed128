// Contract tests: every registered candidate (experimental/candidates/candidates.hpp) x every tested scale, against the
// reference implementation. double -> (u)fixed128 must be exact with the candidate's rounding; (u)fixed128 -> double
// must be within the candidate's max_ulp of the correctly rounded result.
#include "candidate_loop.hpp"
#include "grid.hpp"
#include "mismatch_log.hpp"
#include "test_helpers.hpp"

using namespace fx128;

namespace
{
	// Runs a double -> (u)fixed128 conversion on 4 inputs at a time and compares with the reference.
	template <typename Convert>
	void check_double_to_fixed128(const std::vector<double>& inputs, bool is_signed, int frac_bits, ref::rounding rounding,
		Convert convert, mismatch_log& log)
	{
		for (size_t i = 0; i < inputs.size(); i += 4) {
			lanes_f64 in{};
			for (size_t l = 0; l < 4 && i + l < inputs.size(); ++l) in[l] = inputs[i + l];
			ulong4 hi, lo;
			convert(from_lanes(in), hi, lo);
			const lanes_i64 h = to_lanes(hi), o = to_lanes(lo);
			for (size_t l = 0; l < 4 && i + l < inputs.size(); ++l) {
				const ref::u128 expected = ref::double_to_fixed128(in[l], frac_bits, rounding);
				if (h[l] != expected.hi || o[l] != expected.lo)
					log.add("%s: in=%a got=%016llx_%016llx expected=%016llx_%016llx", is_signed ? "signed" : "unsigned", in[l],
						(unsigned long long)h[l], (unsigned long long)o[l],
						(unsigned long long)expected.hi, (unsigned long long)expected.lo);
			}
		}
	}

	template <typename Convert>
	void check_fixed128_to_double(const std::vector<ref::u128>& inputs, bool is_signed, int frac_bits, uint64_t max_ulp, Convert convert, mismatch_log& log)
	{
		for (size_t i = 0; i < inputs.size(); i += 4) {
			lanes_i64 h{}, o{};
			for (size_t l = 0; l < 4 && i + l < inputs.size(); ++l) {
				h[l] = inputs[i + l].hi;
				o[l] = inputs[i + l].lo;
			}
			const lanes_f64 got = to_lanes(convert(from_lanes(h), from_lanes(o)));
			for (size_t l = 0; l < 4 && i + l < inputs.size(); ++l) {
				const double expected = ref::fixed128_to_double(inputs[i + l], frac_bits, is_signed);
				if (ref::ulp_distance(got[l], expected) > max_ulp)
					log.add("%s: in=%016llx_%016llx got=%a expected=%a (%llu ulp)", is_signed ? "signed" : "unsigned",
						(unsigned long long)h[l], (unsigned long long)o[l], got[l], expected,
						(unsigned long long)ref::ulp_distance(got[l], expected));
			}
		}
	}

	std::vector<double> random_doubles(int count, int max_log2, bool is_signed, int frac_bits)
	{
		std::mt19937_64 rng(kSeed);
		std::vector<double> v;
		for (int i = 0; i < count; ++i) v.push_back(random_scaled_double(rng, -8, max_log2, is_signed, frac_bits));
		return v;
	}

	std::vector<double> halfway_doubles(int count, bool is_signed, int frac_bits)
	{
		std::mt19937_64 rng(kSeed + 1);
		std::vector<double> v;
		for (int i = 0; i < count; ++i) v.push_back(random_halfway_double(rng, is_signed, frac_bits));
		return v;
	}

	// The neighbors one ULP below and above half-way points: the inputs most sensitive to rounding mistakes.
	std::vector<double> around_halfway_doubles(int count, bool is_signed, int frac_bits)
	{
		std::mt19937_64 rng(kSeed + 3);
		std::vector<double> v;
		for (int i = 0; i < count; i += 2) {
			const uint64_t bits = ref::bits_of(random_halfway_double(rng, is_signed, frac_bits));
			v.push_back(ref::double_of(bits - 1));
			v.push_back(ref::double_of(bits + 1));
		}
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

	// All inputs of the double -> (u)fixed128 contract for one scale.
	std::vector<double> double_inputs(bool is_signed, int frac_bits)
	{
		const int max_log2 = is_signed ? 127 : 128;
		std::vector<double> v = grid::doubles(-8, max_log2, frac_bits, is_signed);
		for (const auto& part : {edge_scaled_values(is_signed, frac_bits), random_doubles(kRandomCount, max_log2, is_signed, frac_bits),
				 halfway_doubles(kRandomCount, is_signed, frac_bits), around_halfway_doubles(kRandomCount, is_signed, frac_bits)})
			v.insert(v.end(), part.begin(), part.end());
		return v;
	}

	std::vector<ref::u128> fixed128_inputs(bool is_signed)
	{
		std::vector<ref::u128> v = grid::fixed128_values(is_signed);
		const auto random = random_fixed128(kRandomCount, is_signed);
		v.insert(v.end(), random.begin(), random.end());
		return v;
	}

}

TEST_SUITE("contract")
{
	TEST_CASE("double -> fixed128 is exact with the candidate's rounding")
	{
		for_each_candidate_and_scale([](auto candidate, auto scale) {
			using C = typename decltype(candidate)::type;
			constexpr int F = decltype(scale)::type::value;
			if constexpr (candidates::has_double_to_fixed128<C>::value) {
				INFO(config_name<C, F>());
				const ref::rounding rounding = rounding_of<C>();
				mismatch_log log;
				check_double_to_fixed128(double_inputs(true, F), true, F, rounding, C::template double_to_fixed128<F>, log);
				CHECK_NO_MISMATCH(log);
			}
		});
	}

	TEST_CASE("double -> ufixed128 is exact with the candidate's rounding")
	{
		for_each_candidate_and_scale([](auto candidate, auto scale) {
			using C = typename decltype(candidate)::type;
			constexpr int F = decltype(scale)::type::value;
			if constexpr (candidates::has_double_to_ufixed128<C>::value) {
				INFO(config_name<C, F>());
				const ref::rounding rounding = rounding_of<C>();
				mismatch_log log;
				check_double_to_fixed128(double_inputs(false, F), false, F, rounding, C::template double_to_ufixed128<F>, log);
				CHECK_NO_MISMATCH(log);
			}
		});
	}

	TEST_CASE("fixed128 -> double is within max_ulp")
	{
		const std::vector<ref::u128> inputs = fixed128_inputs(true);
		for_each_candidate_and_scale([&](auto candidate, auto scale) {
			using C = typename decltype(candidate)::type;
			constexpr int F = decltype(scale)::type::value;
			if constexpr (candidates::has_fixed128_to_double<C>::value) {
				INFO(config_name<C, F>());
				mismatch_log log;
				check_fixed128_to_double(inputs, true, F, C::max_ulp, C::template fixed128_to_double<F>, log);
				CHECK_NO_MISMATCH(log);
			}
		});
	}

	TEST_CASE("ufixed128 -> double is within max_ulp")
	{
		const std::vector<ref::u128> inputs = fixed128_inputs(false);
		for_each_candidate_and_scale([&](auto candidate, auto scale) {
			using C = typename decltype(candidate)::type;
			constexpr int F = decltype(scale)::type::value;
			if constexpr (candidates::has_ufixed128_to_double<C>::value) {
				INFO(config_name<C, F>());
				mismatch_log log;
				check_fixed128_to_double(inputs, false, F, C::max_ulp, C::template ufixed128_to_double<F>, log);
				CHECK_NO_MISMATCH(log);
			}
		});
	}
}
