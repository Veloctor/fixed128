// Accuracy report (not a pass/fail test): how far every candidate's results are from the exact values, and whether
// the errors are biased. Bias, not the size of a single error, is what accumulates over long integrations.
//
// Usage: fixed128_accuracy_report [--count N] [--seed S]      prints Markdown tables
//
// fixed128 -> double: each result is compared with the correctly rounded value (round to nearest, ties to even):
//   "= CR" exact match, "+1"/"-1" one ULP towards +infinity / -infinity, ">1" more. Counted separately for positive and
//   negative inputs; bias = (count(+1) - count(-1)) / count, in ULP. Unbiased rounding has bias ~0 on both sides;
//   an error that is always towards +infinity, say, gives a positive bias on both sides (towards zero for negatives).
// double -> fixed128: the error result - exact value, in units of 2^-FracBits, over inputs that have a fractional part.
//   Round half away from zero has mean error ~0; truncation has mean error ~-0.5 units in the direction of the magnitude.
// subtract_to_double / subtract_to_float: like fixed128 -> double, for a - b (float ULPs for the float result).
#include "reference.hpp"
#include "test_helpers.hpp"

#include <candidates/candidates.hpp>

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace fx128;

// The errors do not depend on the scale (the numbers are identical for every frac bits), so three scales suffice.
using report_frac_bits = std::tuple<frac<0>, frac<36>, frac<127>>;

namespace
{
	struct ulp_counts
	{
		long long total = 0, cr = 0, plus1 = 0, minus1 = 0, more = 0;

		template <typename T> // double or float
		void add(T got, T correctly_rounded)
		{
			++total;
			if (got == correctly_rounded) ++cr;
			else if (ref::ulp_distance(got, correctly_rounded) > 1) ++more;
			else if (got > correctly_rounded) ++plus1;
			else ++minus1;
		}

		double pct(long long n) const { return total ? 100.0 * static_cast<double>(n) / static_cast<double>(total) : 0.0; }
		double bias() const { return total ? static_cast<double>(plus1 - minus1) / static_cast<double>(total) : 0.0; }
	};

	// Magnitude classes of the fixed128 input: below 2^53 every value is exactly representable as a double.
	constexpr int kBuckets = 4;
	const char* const kBucketNames[kBuckets] = {"< 2^53", "2^53..2^64", "2^64..2^96", ">= 2^96"};

	int bucket_of(ref::u128 magnitude)
	{
		if (ref::is_zero(magnitude)) return 0;
		const int p = ref::msb(magnitude);
		return p < 53 ? 0 : p < 64 ? 1 : p < 96 ? 2 : 3;
	}

	template <class C, int F>
	void report_fixed128_to_double(bool is_signed, long long count, uint64_t seed)
	{
		ulp_counts pos[kBuckets], neg[kBuckets];
		std::mt19937_64 rng(seed);
		for (long long i = 0; i < count; i += 4) {
			ref::u128 x[4];
			lanes_i64 hi, lo;
			for (int l = 0; l < 4; ++l) {
				x[l] = ref::shr(random_u128(rng), static_cast<int>(rng() % 128));
				if (is_signed) {
					x[l] = ref::shr(x[l], 1); // keep it positive, then choose the sign
					if (rng() & 1) x[l] = ref::neg(x[l]);
				}
				hi[l] = x[l].hi, lo[l] = x[l].lo;
			}
			lanes_f64 got{};
			if (is_signed) {
				if constexpr (candidates::has_fixed128_to_double<C>::value) got = to_lanes(C::template fixed128_to_double<F>(from_lanes(hi), from_lanes(lo)));
			}
			else {
				if constexpr (candidates::has_ufixed128_to_double<C>::value) got = to_lanes(C::template ufixed128_to_double<F>(from_lanes(hi), from_lanes(lo)));
			}
			for (int l = 0; l < 4; ++l) {
				const bool negative = is_signed && (x[l].hi >> 63);
				const ref::u128 magnitude = negative ? ref::neg(x[l]) : x[l];
				(negative ? neg : pos)[bucket_of(magnitude)].add(got[l], ref::fixed128_to_double(x[l], F, is_signed));
			}
		}
		for (int b = 0; b < kBuckets; ++b) {
			for (int s = 0; s < (is_signed ? 2 : 1); ++s) {
				const ulp_counts& c = s ? neg[b] : pos[b];
				if (!c.total) continue;
				std::printf("| %s | %s | %d | %s | %s | %lld | %.3f%% | %.3f%% | %.3f%% | %.3f%% | %+.5f |\n", C::name,
					is_signed ? "fixed128" : "ufixed128", F, kBucketNames[b], s ? "negative" : "positive", c.total, c.pct(c.cr),
					c.pct(c.plus1), c.pct(c.minus1), c.pct(c.more), c.bias());
			}
		}
	}

	// subtract_to_double / subtract_to_float: a - b with b at random positions, bucketed by the magnitude of a - b.
	template <class C, int F, bool to_float>
	void report_subtract(long long count, uint64_t seed)
	{
		constexpr int max_log2 = candidates::subtract_max_log2<C>::value;
		ulp_counts pos[kBuckets], neg[kBuckets];
		std::mt19937_64 rng(seed);
		for (long long i = 0; i < count; i += 4) {
			ref::u128 d[4];
			lanes_i64 ah, al, bh, bl;
			for (int l = 0; l < 4; ++l) {
				d[l] = ref::shr(ref::shr(random_u128(rng), 1), static_cast<int>(rng() % 128)); // non-negative, all magnitudes
				if (max_log2 < 127 && !ref::is_zero(d[l]) && ref::msb(d[l]) >= max_log2) d[l] = ref::shr(d[l], 128 - max_log2);
				if (rng() & 1) d[l] = ref::neg(d[l]);
				const ref::u128 b = random_u128(rng), a = ref::add(b, d[l]);
				ah[l] = a.hi, al[l] = a.lo, bh[l] = b.hi, bl[l] = b.lo;
			}
			const fixed128x4<F> a{from_lanes(ah), from_lanes(al)}, b{from_lanes(bh), from_lanes(bl)};
			const auto got = [&] {
				if constexpr (to_float) return to_lanes(C::template subtract_to_float<F>(a, b));
				else return to_lanes(C::template subtract_to_double<F>(a, b));
			}();
			for (int l = 0; l < 4; ++l) {
				const bool negative = d[l].hi >> 63;
				ulp_counts& c = (negative ? neg : pos)[bucket_of(negative ? ref::neg(d[l]) : d[l])];
				if constexpr (to_float)
					c.add(got[l], ref::fixed128_to_float(d[l], F));
				else
					c.add(got[l], ref::fixed128_to_double(d[l], F, true));
			}
		}
		for (int bucket = 0; bucket < kBuckets; ++bucket) {
			for (int s = 0; s < 2; ++s) {
				const ulp_counts& c = s ? neg[bucket] : pos[bucket];
				if (!c.total) continue;
				std::printf("| %s | %s | %d | %s | %s | %lld | %.3f%% | %.3f%% | %.3f%% | %.3f%% | %+.5f |\n", C::name,
					to_float ? "subtract_to_float" : "subtract_to_double", F, kBucketNames[bucket], s ? "negative" : "positive", c.total,
					c.pct(c.cr), c.pct(c.plus1), c.pct(c.minus1), c.pct(c.more), c.bias());
			}
		}
	}

	struct unit_error
	{
		long long total = 0;
		double sum = 0, sum_magnitude_direction = 0, max_abs = 0;
	};

	// result - exact, in units, for one lane; v must have a fractional part at this scale for the error to be nonzero.
	double error_in_units(double v, ref::u128 result, int frac_bits)
	{
		const uint64_t b = ref::bits_of(v);
		const bool negative = b >> 63;
		const int biased = static_cast<int>((b >> 52) & 0x7FF);
		const uint64_t mant = (b & 0xFFFFFFFFFFFFFull) | (biased ? 1ull << 52 : 0);
		const int k = (biased ? biased - 1075 : -1074) + frac_bits;
		const ref::u128 magnitude = negative ? ref::neg(result) : result;
		if (k >= 0) return 0.0; // exact integer: the contract tests require error 0
		const int s = -k;
		const uint64_t q0 = s >= 64 ? 0 : mant >> s;
		const double fraction = s >= 64 ? std::ldexp(static_cast<double>(mant), -s)
		                                : std::ldexp(static_cast<double>(mant & ((1ull << s) - 1)), -s);
		const double magnitude_error = static_cast<double>(static_cast<int64_t>(magnitude.lo - q0)) - fraction;
		return negative ? -magnitude_error : magnitude_error;
	}

	template <class C, int F>
	void report_double_to_fixed128(long long count, uint64_t seed)
	{
		if constexpr (candidates::has_double_to_fixed128<C>::value) {
			unit_error e;
			std::mt19937_64 rng(seed);
			for (long long i = 0; i < count; i += 4) {
				lanes_f64 v;
				for (double& d : v) d = random_scaled_double(rng, -8, 53, true, F); // magnitudes that have a fraction
				ulong4 hi, lo;
				C::template double_to_fixed128<F>(from_lanes(v), hi, lo);
				const lanes_i64 h = to_lanes(hi), o = to_lanes(lo);
				for (int l = 0; l < 4; ++l) {
					const double err = error_in_units(v[l], {h[l], o[l]}, F);
					++e.total;
					e.sum += err;
					e.sum_magnitude_direction += v[l] < 0 ? -err : err;
					e.max_abs = std::fmax(e.max_abs, std::fabs(err));
				}
			}
			const double n = static_cast<double>(e.total);
			std::printf("| %s | %s | %d | %lld | %+.4f | %+.4f | %.4f |\n", C::name, C::round_half_away ? "half away" : "trunc", F,
				e.total, e.sum / n, e.sum_magnitude_direction / n, e.max_abs);
		}
	}
}

int main(int argc, char** argv)
{
	long long count = 1 << 22;
	uint64_t seed = kSeed;
	for (int i = 1; i + 1 < argc; i += 2) {
		if (!std::strcmp(argv[i], "--count")) count = std::atoll(argv[i + 1]);
		else if (!std::strcmp(argv[i], "--seed")) seed = std::strtoull(argv[i + 1], nullptr, 0);
	}
	std::printf("# fixed128 accuracy report\n\n%lld samples per row group, seed 0x%016" PRIx64 ", backend %s\n\n", count, seed,
		FIXED128_BACKEND_AVX2 ? "AVX2" : "scalar");

	std::printf("## fixed128 -> double, compared with the correctly rounded result\n\n");
	std::printf("| candidate | type | frac bits | magnitude | sign | samples | = CR | +1 ULP | -1 ULP | > 1 ULP | bias (ULP) |\n");
	std::printf("|---|---|---|---|---|---|---|---|---|---|---|\n");
	candidates::for_each_type<candidates::all>([&](auto candidate) {
		using C = typename decltype(candidate)::type;
		candidates::for_each_type<report_frac_bits>([&](auto scale) {
			constexpr int F = decltype(scale)::type::value;
			if constexpr (candidates::has_fixed128_to_double<C>::value) report_fixed128_to_double<C, F>(true, count, seed);
			if constexpr (candidates::has_ufixed128_to_double<C>::value) report_fixed128_to_double<C, F>(false, count, seed);
		});
	});

	std::printf("\n## subtract_to_double / subtract_to_float, a - b compared with the correctly rounded result\n\n");
	std::printf("Bucketed by the magnitude of a - b in units; float ULPs for subtract_to_float.\n\n");
	std::printf("| candidate | operation | frac bits | magnitude | sign | samples | = CR | +1 ULP | -1 ULP | > 1 ULP | bias (ULP) |\n");
	std::printf("|---|---|---|---|---|---|---|---|---|---|---|\n");
	candidates::for_each_type<candidates::all>([&](auto candidate) {
		using C = typename decltype(candidate)::type;
		candidates::for_each_type<report_frac_bits>([&](auto scale) {
			constexpr int F = decltype(scale)::type::value;
			if constexpr (candidates::has_subtract_to_double<C>::value) report_subtract<C, F, false>(count, seed);
			if constexpr (candidates::has_subtract_to_float<C>::value) report_subtract<C, F, true>(count, seed);
		});
	});

	std::printf("\n## double -> fixed128, error in units of 2^-FracBits (inputs with a fractional part)\n\n");
	std::printf("| candidate | rounding | frac bits | samples | mean error | mean error along the magnitude | max abs error |\n");
	std::printf("|---|---|---|---|---|---|---|\n");
	candidates::for_each_type<candidates::all>([&](auto candidate) {
		using C = typename decltype(candidate)::type;
		candidates::for_each_type<report_frac_bits>([&](auto scale) { report_double_to_fixed128<C, decltype(scale)::type::value>(count, seed); });
	});
	return 0;
}
