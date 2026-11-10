#pragma once
#include "check.hpp"
#include "detail/native.hpp"
#include "detail/pow2.hpp"
#include "types.hpp"

// Limited-range double<>int64 conversions from Mysticial's answer on Stack Overflow (CC BY-SA):
// https://stackoverflow.com/questions/41144668/how-to-efficiently-perform-double-int64-conversions-with-sse-avx
// Results are undefined outside the documented ranges.

namespace fx128
{
	//  Only works for inputs in the range: [0, 2^52)
	inline ulong4 double_to_uint64_limited(const double4& in) {
		using namespace detail;
		FIXED128_CHECK_INPUT("double_to_uint64_limited", in, 0.0, pow2(52));
		const vd MajikNumber = set1_d(0x0010000000000000);
		const vd x = add_d(load(in), MajikNumber);
		return to_ulong4(xor_i(
			as_i(x),
			as_i(MajikNumber)
		));
	}

	//  Only works for inputs in the range: [-2^51, 2^51]
	inline long4 double_to_int64_limited(const double4& in) {
		using namespace detail;
		FIXED128_CHECK_INPUT("double_to_int64_limited", in, -pow2(51), pow2(51) + 1.0);
		const vd MajikNumber = set1_d(0x0018000000000000);
		const vd x = add_d(load(in), MajikNumber);
		return to_long4(sub_i(
			as_i(x),
			as_i(MajikNumber)
		));
	}

	//  Only works for inputs in the range: [0, 2^52)
	inline double4 uint64_to_double_limited(const ulong4& in) {
		using namespace detail;
		const vd MajikNumber = set1_d(0x0010000000000000);
		const vi x = or_i(load(in), as_i(MajikNumber));
		return to_double4(sub_d(as_d(x), MajikNumber));
	}

	//  Only works for inputs in the range: [-2^51, 2^51]
	inline double4 int64_to_double_limited(const long4& in) {
		using namespace detail;
		const vd MajikNumber = set1_d(0x0018000000000000);
		const vi x = add_i(load(in), as_i(MajikNumber));
		return to_double4(sub_d(as_d(x), MajikNumber));
	}
}
