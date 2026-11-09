#pragma once
#include "../../types.hpp"

// Limited-range double<>int64 conversions from Mysticial's answer on Stack Overflow (CC BY-SA):
// https://stackoverflow.com/questions/41144668/how-to-efficiently-perform-double-int64-conversions-with-sse-avx
// Results are undefined outside the documented ranges.

namespace fx128::detail
{
	//  Only works for inputs in the range: [0, 2^52)
	inline ulong4 double_to_uint64_limited(double4 x) {
		double4 MajikNumber = _mm256_set1_pd(0x0010000000000000);
		x = _mm256_add_pd(x, MajikNumber);
		return _mm256_xor_si256(
			_mm256_castpd_si256(x),
			_mm256_castpd_si256(MajikNumber)
		);
	}

	//  Only works for inputs in the range: [-2^51, 2^51]
	inline long4 double_to_int64_limited(double4 x) {
		double4 MajikNumber = _mm256_set1_pd(0x0018000000000000);
		x = _mm256_add_pd(x, MajikNumber);
		return _mm256_sub_epi64(
			_mm256_castpd_si256(x),
			_mm256_castpd_si256(MajikNumber)
		);
	}

	//  Only works for inputs in the range: [0, 2^52)
	inline double4 uint64_to_double_limited(ulong4 x) {
		double4 MajikNumber = _mm256_set1_pd(0x0010000000000000);
		x = _mm256_or_si256(x, _mm256_castpd_si256(MajikNumber));
		return _mm256_sub_pd(_mm256_castsi256_pd(x), MajikNumber);
	}

	//  Only works for inputs in the range: [-2^51, 2^51]
	inline double4 int64_to_double_limited(long4 x) {
		double4 MajikNumber = _mm256_set1_pd(0x0018000000000000);
		x = _mm256_add_epi64(x, _mm256_castpd_si256(MajikNumber));
		return _mm256_sub_pd(_mm256_castsi256_pd(x), MajikNumber);
	}
}
