// double <-> int64 SIMD conversions against the reference implementation.
#include "mismatch_log.hpp"
#include "test_helpers.hpp"

#include <fixed128/detail/limited.hpp>

using namespace fx128;

namespace
{
	// Random double with |v| in [2^min_log2, 2^max_log2).
	double random_double(std::mt19937_64& rng, int min_log2, int max_log2, bool allow_negative)
	{
		std::uniform_int_distribution<int> exp_dist(min_log2, max_log2 - 1);
		const uint64_t sign = allow_negative ? (rng() & 1) << 63 : 0;
		return ref::double_of(sign | (static_cast<uint64_t>(exp_dist(rng) + 1023) << 52) | (rng() & 0xFFFFFFFFFFFFFull));
	}

	template <typename Convert>
	void check_double_to_int64(int min_log2, int max_log2, bool is_signed, Convert convert, mismatch_log& log)
	{
		std::mt19937_64 rng(kSeed);
		for (int i = 0; i < kRandomCount; i += 4) {
			lanes_f64 in;
			for (double& d : in) d = random_double(rng, min_log2, max_log2, is_signed);
			const lanes_i64 got = to_lanes(convert(from_lanes(in)));
			for (int l = 0; l < 4; ++l) {
				const uint64_t expected = ref::double_to_int64_trunc(in[l]);
				if (got[l] != expected)
					log.add("in=%a got=%016llx expected=%016llx", in[l], (unsigned long long)got[l], (unsigned long long)expected);
			}
		}
	}

	// int64 -> double must be correctly rounded, i.e. identical to the scalar hardware conversion.
	template <typename Convert, typename Scalar>
	void check_int64_to_double(uint64_t mask, Convert convert, Scalar scalar, mismatch_log& log)
	{
		std::mt19937_64 rng(kSeed);
		for (int i = 0; i < kRandomCount; i += 4) {
			lanes_i64 in;
			for (uint64_t& x : in) x = (rng() >> (rng() % 64)) & mask;
			if (i == 0) in = {0, 1, mask, mask >> 1};
			const lanes_f64 got = to_lanes(convert(from_lanes(in)));
			for (int l = 0; l < 4; ++l) {
				const double expected = scalar(in[l]);
				if (got[l] != expected)
					log.add("in=%016llx got=%a expected=%a", (unsigned long long)in[l], got[l], expected);
			}
		}
	}
}

TEST_SUITE("double <-> int64")
{
	TEST_CASE("double_to_int64_fast truncates over the full int64 range")
	{
		mismatch_log log;
		check_double_to_int64(-4, 63, true, double_to_int64_fast, log);
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("double_to_uint64_fast truncates over the full uint64 range")
	{
		mismatch_log log;
		check_double_to_int64(-4, 64, false, double_to_uint64_fast, log);
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("int64_to_double_fast_precise is correctly rounded")
	{
		mismatch_log log;
		check_int64_to_double(~0ull, int64_to_double_fast_precise,
			[](uint64_t x) { return static_cast<double>(static_cast<int64_t>(x)); }, log);
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("uint64_to_double_fast_precise is correctly rounded")
	{
		mismatch_log log;
		check_int64_to_double(~0ull, uint64_to_double_fast_precise,
			[](uint64_t x) { return static_cast<double>(x); }, log);
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("_limited conversions inside their documented ranges")
	{
		mismatch_log log;
		// double_to_uint64_limited: [0, 2^52), integral inputs
		check_int64_to_double((1ull << 52) - 1, detail::uint64_to_double_limited,
			[](uint64_t x) { return static_cast<double>(x); }, log);
		std::mt19937_64 rng(kSeed);
		for (int i = 0; i < kRandomCount; i += 4) {
			lanes_i64 u;
			lanes_i64 s;
			for (int l = 0; l < 4; ++l) {
				u[l] = rng() >> 12;                                                         // [0, 2^52)
				s[l] = static_cast<uint64_t>(static_cast<int64_t>(rng()) >> 13);            // [-2^50, 2^50)
			}
			lanes_f64 ud, sd;
			for (int l = 0; l < 4; ++l) {
				ud[l] = static_cast<double>(u[l]);
				sd[l] = static_cast<double>(static_cast<int64_t>(s[l]));
			}
			const lanes_i64 u_back = to_lanes(detail::double_to_uint64_limited(from_lanes(ud)));
			const lanes_i64 s_back = to_lanes(detail::double_to_int64_limited(from_lanes(sd)));
			const lanes_f64 s_dbl = to_lanes(detail::int64_to_double_limited(from_lanes(s)));
			for (int l = 0; l < 4; ++l) {
				if (u_back[l] != u[l]) log.add("double_to_uint64_limited(%a) = %llx", ud[l], (unsigned long long)u_back[l]);
				if (s_back[l] != s[l]) log.add("double_to_int64_limited(%a) = %llx", sd[l], (unsigned long long)s_back[l]);
				if (s_dbl[l] != sd[l]) log.add("int64_to_double_limited(%llx) = %a", (unsigned long long)s[l], s_dbl[l]);
			}
		}
		CHECK_NO_MISMATCH(log);
	}
}
