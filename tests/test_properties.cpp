// Metamorphic properties of every candidate x scale: relations that must hold without knowing the exact answer.
// They are not part of the minimal contract (that is test_contract.cpp), but a violation is a real defect, e.g. a
// non-monotonic conversion makes sorted positions unsorted after conversion.
#include "candidate_loop.hpp"
#include "mismatch_log.hpp"

#include <algorithm>

using namespace fx128;

namespace
{
	ref::u128 lane(const ulong4& hi, const ulong4& lo, int l) { return {to_lanes(hi)[l], to_lanes(lo)[l]}; }

	// Signed comparison of two's complement 128-bit values.
	bool less_signed(ref::u128 a, ref::u128 b)
	{
		if (a.hi != b.hi) return static_cast<int64_t>(a.hi) < static_cast<int64_t>(b.hi);
		return a.lo < b.lo;
	}

	template <class C, int F>
	ref::u128 to_fixed(double v)
	{
		ulong4 hi, lo;
		C::template double_to_fixed128<F>(double4{v, v, v, v}, hi, lo);
		return lane(hi, lo, 0);
	}

	template <class C, int F>
	double to_double(ref::u128 x)
	{
		return C::template fixed128_to_double<F>(ulong4{x.hi, x.hi, x.hi, x.hi}, ulong4{x.lo, x.lo, x.lo, x.lo}).x;
	}

	// Sorted doubles spread over the whole signed domain, plus runs of consecutive doubles.
	std::vector<double> sorted_doubles(int frac_bits)
	{
		std::mt19937_64 rng(kSeed + 10);
		std::vector<double> v;
		for (int i = 0; i < kRandomCount / 4; ++i) {
			const double d = random_scaled_double(rng, -8, 127, true, frac_bits);
			v.push_back(d);
			for (int k = 1; k <= 3; ++k) v.push_back(ref::double_of(ref::bits_of(d) + k)); // next larger magnitudes
		}
		std::sort(v.begin(), v.end());
		return v;
	}

	// Sorted signed 128-bit values, including runs of consecutive integers at every magnitude (where rounding to
	// double changes direction).
	std::vector<ref::u128> sorted_fixed128()
	{
		std::mt19937_64 rng(kSeed + 11);
		std::vector<ref::u128> v;
		for (int i = 0; i < kRandomCount / 8; ++i) {
			ref::u128 x = ref::shr(random_u128(rng), 1 + static_cast<int>(rng() % 127));
			if (rng() & 1) x = ref::neg(x);
			for (int k = 0; k < 8; ++k) v.push_back(ref::add(x, {0, static_cast<uint64_t>(k)}));
		}
		std::sort(v.begin(), v.end(), less_signed);
		return v;
	}
}

TEST_SUITE("properties")
{
	TEST_CASE("double -> fixed128 is monotonic and odd (f(-v) == -f(v))")
	{
		for_each_candidate_and_scale([](auto candidate, auto scale) {
			using C = typename decltype(candidate)::type;
			constexpr int F = decltype(scale)::type::value;
			if constexpr (candidates::has_double_to_fixed128<C>::value) {
				INFO(config_name<C, F>());
				mismatch_log log;
				const std::vector<double> v = sorted_doubles(F);
				ref::u128 prev = to_fixed<C, F>(v[0]);
				for (size_t i = 1; i < v.size(); ++i) {
					const ref::u128 cur = to_fixed<C, F>(v[i]);
					if (less_signed(cur, prev)) log.add("not monotonic: f(%a) < f(%a)", v[i], v[i - 1]);
					if (v[i] != -ref::pow2(127 - F) && to_fixed<C, F>(-v[i]) != ref::neg(cur)) log.add("not odd at %a", v[i]);
					prev = cur;
				}
				CHECK_NO_MISMATCH(log);
			}
		});
	}

	TEST_CASE("fixed128 -> double is monotonic")
	{
		const std::vector<ref::u128> v = sorted_fixed128();
		for_each_candidate_and_scale([&](auto candidate, auto scale) {
			using C = typename decltype(candidate)::type;
			constexpr int F = decltype(scale)::type::value;
			if constexpr (candidates::has_fixed128_to_double<C>::value) {
				INFO(config_name<C, F>());
				mismatch_log log;
				double prev = to_double<C, F>(v[0]);
				for (size_t i = 1; i < v.size(); ++i) {
					const double cur = to_double<C, F>(v[i]);
					if (cur < prev)
						log.add("not monotonic: f(%016llx_%016llx) = %a < f(previous) = %a", (unsigned long long)v[i].hi,
							(unsigned long long)v[i].lo, cur, prev);
					prev = cur;
				}
				CHECK_NO_MISMATCH(log);
			}
		});
	}

	// KNOWN ISSUE (2026-11-10, not fixed: changing it changes numeric results): the library's fixed128 -> double is not
	// odd, i.e. f(-x) != -f(x) for some x, so its (<= 1 ULP) error is not symmetric between positive and negative values.
	// The accuracy report (fixed128_accuracy_report) measures whether this biases results. may_fail keeps the case
	// running and reporting; remove it if the conversion becomes odd.
	TEST_CASE("fixed128 -> double is odd (f(-x) == -f(x))" * doctest::may_fail())
	{
		const std::vector<ref::u128> v = sorted_fixed128();
		for_each_candidate_and_scale([&](auto candidate, auto scale) {
			using C = typename decltype(candidate)::type;
			constexpr int F = decltype(scale)::type::value;
			if constexpr (candidates::has_fixed128_to_double<C>::value) {
				INFO(config_name<C, F>());
				mismatch_log log;
				for (const ref::u128& x : v) {
					const double pos = to_double<C, F>(x), negated = to_double<C, F>(ref::neg(x));
					if (negated != -pos)
						log.add("f(%016llx_%016llx) = %a but f(-x) = %a", (unsigned long long)x.hi, (unsigned long long)x.lo, pos, negated);
				}
				CHECK_NO_MISMATCH(log);
			}
		});
	}

	TEST_CASE("round trip: doubles exactly representable in fixed point come back unchanged")
	{
		for_each_candidate_and_scale([](auto candidate, auto scale) {
			using C = typename decltype(candidate)::type;
			constexpr int F = decltype(scale)::type::value;
			if constexpr (candidates::has_double_to_fixed128<C>::value && candidates::has_fixed128_to_double<C>::value) {
				INFO(config_name<C, F>());
				mismatch_log log;
				std::mt19937_64 rng(kSeed + 12);
				for (int i = 0; i < kRandomCount; ++i) {
					// k * 2^-F with |k| < 2^126 and at most 53 significant bits: exact in both representations.
					const int shift = static_cast<int>(rng() % 74);
					const double k = static_cast<double>(static_cast<int64_t>(rng() >> 11)) * ref::pow2(shift);
					const double v = ((rng() & 1) ? -k : k) * ref::pow2(-F);
					const double back = to_double<C, F>(to_fixed<C, F>(v));
					if (back != v) log.add("%a -> %a", v, back);
				}
				CHECK_NO_MISMATCH(log);
			}
		});
	}

	TEST_CASE("lanes are independent: a lane's result does not depend on the other lanes")
	{
		for_each_candidate_and_scale([](auto candidate, auto scale) {
			using C = typename decltype(candidate)::type;
			constexpr int F = decltype(scale)::type::value;
			INFO(config_name<C, F>());
			mismatch_log log;
			std::mt19937_64 rng(kSeed + 13);
			for (int i = 0; i < kRandomCount / 16; ++i) {
				lanes_f64 d;
				lanes_i64 hi, lo;
				for (int l = 0; l < 4; ++l) {
					d[l] = random_scaled_double(rng, -8, 127, true, F);
					hi[l] = rng() >> (rng() % 64);
					lo[l] = rng();
				}
				if constexpr (candidates::has_double_to_fixed128<C>::value) {
					ulong4 h, o;
					C::template double_to_fixed128<F>(from_lanes(d), h, o);
					for (int l = 0; l < 4; ++l)
						if (lane(h, o, l) != to_fixed<C, F>(d[l])) log.add("double -> fixed128, lane %d, input %a", l, d[l]);
				}
				if constexpr (candidates::has_fixed128_to_double<C>::value) {
					const lanes_f64 got = to_lanes(C::template fixed128_to_double<F>(from_lanes(hi), from_lanes(lo)));
					for (int l = 0; l < 4; ++l)
						if (ref::bits_of(got[l]) != ref::bits_of(to_double<C, F>({hi[l], lo[l]}))) log.add("fixed128 -> double, lane %d", l);
				}
			}
			CHECK_NO_MISMATCH(log);
		});
	}
}
