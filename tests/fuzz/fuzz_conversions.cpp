// libFuzzer target (Clang): differential fuzzing of every registered candidate against the reference implementation.
// The fuzzer's bytes become 4 doubles and 4 128-bit values; inputs outside the domain are skipped.
// Build with -DFIXED128_BUILD_FUZZERS=ON (Clang), run e.g.: fixed128_fuzz -max_total_time=60
#include "../reference.hpp"

#include <candidates/candidates.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <type_traits>

namespace
{
	template <int N>
	using frac = std::integral_constant<int, N>;
	using fuzz_frac_bits = std::tuple<frac<0>, frac<1>, frac<36>, frac<53>, frac<64>, frac<127>>;

	[[noreturn]] void fail() { std::abort(); }

	bool in_signed_domain(double v, int f)
	{
		return v == v && v - v == 0 && v >= -ref::pow2(127 - f) && v < ref::pow2(127 - f);
	}

	bool in_unsigned_domain(double v, int f) { return v == v && v - v == 0 && v >= 0 && v < ref::pow2(128 - f); }

	template <class C, int F>
	void check(const double (&d)[4], const ref::u128 (&x)[4])
	{
		const ref::rounding rounding = C::round_half_away ? ref::rounding::half_away_from_zero : ref::rounding::trunc;
		if constexpr (candidates::has_double_to_fixed128<C>::value) {
			bool ok = true;
			for (double v : d) ok = ok && in_signed_domain(v, F);
			if (ok) {
				fx128::ulong4 hi, lo;
				C::template double_to_fixed128<F>({d[0], d[1], d[2], d[3]}, hi, lo);
				const uint64_t h[4] = {hi.x, hi.y, hi.z, hi.w}, l[4] = {lo.x, lo.y, lo.z, lo.w};
				for (int i = 0; i < 4; ++i)
					if (ref::u128{h[i], l[i]} != ref::double_to_fixed128(d[i], F, rounding)) fail();
			}
		}
		if constexpr (candidates::has_double_to_ufixed128<C>::value) {
			bool ok = true;
			for (double v : d) ok = ok && in_unsigned_domain(v, F);
			if (ok) {
				fx128::ulong4 hi, lo;
				C::template double_to_ufixed128<F>({d[0], d[1], d[2], d[3]}, hi, lo);
				const uint64_t h[4] = {hi.x, hi.y, hi.z, hi.w}, l[4] = {lo.x, lo.y, lo.z, lo.w};
				for (int i = 0; i < 4; ++i)
					if (ref::u128{h[i], l[i]} != ref::double_to_fixed128(d[i], F, rounding)) fail();
			}
		}
		const fx128::ulong4 hi{x[0].hi, x[1].hi, x[2].hi, x[3].hi}, lo{x[0].lo, x[1].lo, x[2].lo, x[3].lo};
		if constexpr (candidates::has_fixed128_to_double<C>::value) {
			const fx128::double4 r = C::template fixed128_to_double<F>(hi, lo);
			const double got[4] = {r.x, r.y, r.z, r.w};
			for (int i = 0; i < 4; ++i)
				if (ref::ulp_distance(got[i], ref::fixed128_to_double(x[i], F, true)) > C::max_ulp) fail();
		}
		if constexpr (candidates::has_ufixed128_to_double<C>::value) {
			const fx128::double4 r = C::template ufixed128_to_double<F>(hi, lo);
			const double got[4] = {r.x, r.y, r.z, r.w};
			for (int i = 0; i < 4; ++i)
				if (ref::ulp_distance(got[i], ref::fixed128_to_double(x[i], F, false)) > C::max_ulp) fail();
		}
	}
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	if (size < 4 * 8 + 4 * 16) return 0;
	double d[4];
	ref::u128 x[4];
	std::memcpy(d, data, sizeof d);
	for (int i = 0; i < 4; ++i) std::memcpy(&x[i], data + 32 + 16 * i, 16);
	candidates::for_each_type<candidates::all>([&](auto candidate) {
		candidates::for_each_type<fuzz_frac_bits>([&](auto scale) {
			check<typename decltype(candidate)::type, decltype(scale)::type::value>(d, x);
		});
	});
	return 0;
}
