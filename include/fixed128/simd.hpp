#pragma once
#include "avx2_convert.hpp"
#include "scalar.hpp"
#include "types.hpp"

#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>

// 4-lane SIMD fixed-point types: fixed64x4 and fixed128x4.
// Note: the constructors taking 4 scalars follow _mm256_set_epi64x order, so the first argument is lane 3.

namespace fx128
{
	struct fixed64x4
	{
		long4 bits;

		explicit fixed64x4(double fp)
		{
			fp *= std::pow(2.0, FIXED128_FRAC_BITS);
			auto vfp = _mm256_set1_pd(fp);
			bits = double_to_int64_fast(vfp);
		}

		explicit fixed64x4(int64_t i)
		{
			i <<= FIXED128_FRAC_BITS;
			bits = _mm256_set1_epi64x(i);
		}

		explicit fixed64x4(const long4& i)
		{
			bits = _mm256_slli_epi64(i, FIXED128_FRAC_BITS);
		}

		explicit fixed64x4(double4 vfp)
		{
			const auto vfactor = _mm256_set1_pd(std::pow(2.0, FIXED128_FRAC_BITS));
			vfp = _mm256_mul_pd(vfp, vfactor);
			bits = double_to_int64_fast(vfp);
		}

		explicit fixed64x4(float4 vfp32)
		{
			const auto vfactor = _mm_set_ps1(static_cast<float>(std::pow(2.0f, FIXED128_FRAC_BITS)));
			vfp32 = _mm_mul_ps(vfp32, vfactor);
			auto vfp = _mm256_cvtps_pd(vfp32);
			bits = double_to_int64_fast(vfp);
		}

		explicit operator double4() const
		{
			const auto vfactor = _mm256_set1_pd(std::pow(2.0, -FIXED128_FRAC_BITS));
			auto vfp = int64_to_double_fast_precise(bits);
			return _mm256_mul_pd(vfp, vfactor);
		}

		explicit operator float4() const
		{
			const auto vfactor = _mm_set_ps1(static_cast<float>(std::pow(2.0f, -FIXED128_FRAC_BITS)));
			auto vfp = int64_to_double_fast_precise(bits);
			auto vfp32 = _mm256_cvtpd_ps(vfp);
			return _mm_mul_ps(vfp32, vfactor);
		}
	};

	struct fixed128x4
	{
		long4 upper, lower;

		explicit fixed128x4(const double4& x)
		{
			double_to_fixed128_full(x, upper, lower);
		}

		explicit fixed128x4(double x)
		{
			auto x4 = _mm256_set1_pd(x);
			*this = fixed128x4{x4};
		}

		fixed128x4()
		{
			upper = _mm256_setzero_si256();
			lower = _mm256_setzero_si256();
		}

		fixed128x4(fixed64 x, fixed64 y, fixed64 z, fixed64 w)
		{
			upper = _mm256_setzero_si256();
			lower = _mm256_set_epi64x(x.sbits, y.sbits, z.sbits, w.sbits);
		}

		fixed128x4(fixed64 x)
		{
			upper = _mm256_setzero_si256();
			lower = _mm256_set1_epi64x(x.sbits);
		}

		fixed128x4(fixed128 x)
		{
			upper = _mm256_set1_epi64x(x.upper);
			lower = _mm256_set1_epi64x(x.lower);
		}

		fixed128x4(fixed128 x, fixed128 y, fixed128 z, fixed128 w)
		{
			upper = _mm256_set_epi64x(x.upper, y.upper, z.upper, w.upper);
			lower = _mm256_set_epi64x(x.lower, y.lower, z.lower, w.lower);
		}

		fixed128x4(long4 upper, long4 lower)
		{
			this->upper = upper;
			this->lower = lower;
		}

		//索引器
		fixed128 operator[](int i) const
		{
			assert(i >= 0 && i < 4);
			alignas(32) int64_t u[4], l[4];
			_mm256_store_si256(reinterpret_cast<__m256i*>(u), upper);
			_mm256_store_si256(reinterpret_cast<__m256i*>(l), lower);
			return fixed128{u[i], l[i]};
		}

		explicit operator double4() const
		{
			return fixed128_to_double(upper, lower);
		}

		fixed128x4 operator+(fixed128x4 value) const
		{
			const long4 sign_mask = _mm256_set1_epi64x(LLONG_MIN);
			auto retlower = _mm256_add_epi64(lower, value.lower);
			long4 carry = _mm256_cmpgt_epi64(_mm256_add_epi64(value.lower, sign_mask), _mm256_add_epi64(retlower, sign_mask));
			auto retupper = _mm256_sub_epi64(_mm256_add_epi64(upper, value.upper), carry);
			return {retupper, retlower};
		}

		fixed128x4 operator+(long4 rlower) const
		{
			const long4 sign_mask = _mm256_set1_epi64x(LLONG_MIN);
			auto retlower = _mm256_add_epi64(lower, rlower);
			long4 carry = _mm256_cmpgt_epi64(_mm256_add_epi64(rlower, sign_mask), _mm256_add_epi64(retlower, sign_mask));
			auto retupper = _mm256_sub_epi64(upper, carry);
			return {retupper, retlower};
		}

		fixed128x4 operator-(fixed128x4 value) const
		{
			const long4 sign_mask = _mm256_set1_epi64x(LLONG_MIN);
			long4 up = _mm256_sub_epi64(upper, value.upper);
			long4 carry = _mm256_cmpgt_epi64(_mm256_add_epi64(value.lower, sign_mask), _mm256_add_epi64(lower, sign_mask));
			auto retlower = _mm256_sub_epi64(lower, value.lower);
			auto retupper = _mm256_add_epi64(up, carry);
			return {retupper, retlower};
		}

		fixed128x4 operator-(long4 value) const
		{
			const long4 sign_mask = _mm256_set1_epi64x(LLONG_MIN);
			long4 carry = _mm256_cmpgt_epi64(_mm256_add_epi64(value, sign_mask), _mm256_add_epi64(lower, sign_mask));
			auto retlower = _mm256_sub_epi64(lower, value);
			auto retupper = _mm256_add_epi64(upper, carry);
			return {retupper, retlower};
		}
		fixed128x4& operator+=(long4 value) { return *this = *this + value; }
		fixed128x4& operator+=(fixed128x4 value) { return *this = *this + value; }
		fixed128x4& operator-=(long4 value) { return *this = *this - value; }
		fixed128x4& operator-=(fixed128x4 value) { return *this = *this - value; }
	};
}
