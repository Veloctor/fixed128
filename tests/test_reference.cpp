// The reference implementation is the single source of truth (ADR 0003), so it is tested on its own:
// hand-computed answers, and on GCC/Clang a second implementation on the compiler's unsigned __int128.
#include "mismatch_log.hpp"
#include "test_helpers.hpp"

using ref::rounding;

namespace
{
	ref::u128 fix(double v, int f, rounding r) { return ref::double_to_fixed128(v, f, r); }
	constexpr ref::u128 minus(uint64_t x) { return {~0ull, 0 - x}; }
}

TEST_SUITE("reference implementation")
{
	TEST_CASE("hand-computed double -> fixed128")
	{
		const rounding T = rounding::trunc, H = rounding::half_away_from_zero;
		CHECK(fix(0.0, 36, H) == ref::u128{0, 0});
		CHECK(fix(-0.0, 36, H) == ref::u128{0, 0});
		CHECK(fix(1.0, 36, T) == ref::u128{0, 1ull << 36});
		CHECK(fix(0x1p-37, 36, H) == ref::u128{0, 1});      // exactly half a unit: away from zero
		CHECK(fix(0x1p-37, 36, T) == ref::u128{0, 0});
		CHECK(fix(0x1.fffffffffffffp-38, 36, H) == ref::u128{0, 0}); // just below half
		CHECK(fix(-0x1.8p-36, 36, H) == minus(2));          // -1.5 units
		CHECK(fix(-0x1.8p-36, 36, T) == minus(1));
		CHECK(fix(2.5, 0, H) == ref::u128{0, 3});
		CHECK(fix(-4.5, 0, H) == minus(5));
		CHECK(fix(0x1p64, 0, T) == ref::u128{1, 0});
		CHECK(fix(0x1.8p100, 0, T) == ref::u128{3ull << 35, 0});
		CHECK(fix(-0x1p127, 0, T) == ref::u128{1ull << 63, 0});
		CHECK(fix(0x1p-1074, 1074, T) == ref::u128{0, 1});  // smallest subnormal, scaled to one unit
	}

	TEST_CASE("hand-computed fixed128 -> double (round to nearest, ties to even)")
	{
		CHECK(ref::fixed128_to_double({0, 1ull << 36}, 36, true) == 1.0);
		CHECK(ref::fixed128_to_double(minus(1), 0, true) == -1.0);
		CHECK(ref::fixed128_to_double(minus(1), 0, false) == 0x1p128);                        // 2^128 - 1 rounds up
		CHECK(ref::fixed128_to_double({0, (1ull << 53) + 1}, 0, true) == 0x1p53);              // tie, even stays
		CHECK(ref::fixed128_to_double({0, (1ull << 53) + 3}, 0, true) == 0x1.0000000000002p53); // tie, odd rounds up
		CHECK(ref::fixed128_to_double({1, 1}, 0, true) == 0x1p64);                             // sticky far below
		CHECK(ref::fixed128_to_double({1ull << 63, 0}, 0, true) == -0x1p127);
		CHECK(ref::fixed128_to_double({0, 3}, 1, true) == 1.5);
	}

	TEST_CASE("hand-computed fixed128 -> float (round to nearest, ties to even)")
	{
		CHECK(ref::fixed128_to_float({0, 1ull << 36}, 36) == 1.0f);
		CHECK(ref::fixed128_to_float(minus(1), 0) == -1.0f);
		CHECK(ref::fixed128_to_float({0, (1ull << 24) + 1}, 0) == 0x1p24f);              // tie, even stays
		CHECK(ref::fixed128_to_float({0, (1ull << 24) + 3}, 0) == 0x1.000004p24f);       // tie, odd rounds up
		CHECK(ref::fixed128_to_float({1, 1}, 0) == 0x1p64f);                             // sticky far below
		CHECK(ref::fixed128_to_float({~0ull >> 1, ~0ull}, 0) == 0x1p127f);               // 2^127 - 1 rounds up
		CHECK(ref::fixed128_to_float({1ull << 63, 0}, 0) == -0x1p127f);
		CHECK(ref::fixed128_to_float({0, 1}, 127) == 0x1p-127f);                         // a float subnormal, exact
		CHECK(ref::fixed128_to_float({0, 3}, 1) == 1.5f);
	}

	TEST_CASE("fixed128 -> float agrees with the hardware int64 -> float conversion")
	{
		mismatch_log log;
		std::mt19937_64 rng(kSeed + 21);
		for (int i = 0; i < kRandomCount; ++i) {
			const int64_t x = static_cast<int64_t>(rng() >> (1 + rng() % 63));
			const int64_t v = rng() & 1 ? -x : x;
			const ref::u128 r{v < 0 ? ~0ull : 0, static_cast<uint64_t>(v)};
			if (ref::fixed128_to_float(r, 0) != static_cast<float>(v)) log.add("%lld", static_cast<long long>(v));
		}
		CHECK_NO_MISMATCH(log);
	}

// Not on Windows: there clang does not link the runtime functions that convert __int128 to double.
#if defined(__SIZEOF_INT128__) && !defined(_WIN32)
	TEST_CASE("agrees with an independent implementation on unsigned __int128")
	{
		using u128 = unsigned __int128;
		auto to_ref = [](u128 x) { return ref::u128{static_cast<uint64_t>(x >> 64), static_cast<uint64_t>(x)}; };
		mismatch_log log;
		std::mt19937_64 rng(kSeed + 20);
		for (int i = 0; i < kRandomCount; ++i) {
			const int f = static_cast<int>(rng() % 128);
			// double -> fixed128, truncation: |v| * 2^f = m * 2^(e + f), computed with __int128 shifts.
			const double v = random_scaled_double(rng, -8, 127, false, f);
			const uint64_t b = ref::bits_of(v);
			const uint64_t m = (b & 0xFFFFFFFFFFFFFull) | (1ull << 52);
			const int k = static_cast<int>((b >> 52) & 0x7FF) - 1075 + f;
			const u128 expected = k >= 0 ? static_cast<u128>(m) << k : (-k >= 64 ? 0 : static_cast<u128>(m >> -k));
			if (fix(v, f, rounding::trunc) != to_ref(expected)) log.add("double -> fixed128: %a, frac bits %d", v, f);

			// fixed128 -> double: the compiler's conversion of unsigned __int128 rounds to nearest even.
			const u128 x = (static_cast<u128>(rng()) << 64 | rng()) >> (rng() % 128);
			const double got = ref::fixed128_to_double(to_ref(x), 0, false);
			if (got != static_cast<double>(x)) log.add("fixed128 -> double: %a vs %a", got, static_cast<double>(x));
		}
		CHECK_NO_MISMATCH(log);
	}
#endif
}
