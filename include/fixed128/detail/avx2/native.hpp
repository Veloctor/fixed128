#pragma once
#include "../../types.hpp"

#include <immintrin.h>

// AVX2 backend: the vector primitives the conversions are written in. Each primitive is exactly one intrinsic
// (or none), so the compiled code is the same as hand-written intrinsics. The primitives take and return the
// native register types directly: wrapping them in a struct makes MSVC optimize the calling loops worse.

namespace fx128::detail
{
	using vi = __m256i; // 4 x 64-bit integer lanes
	using vd = __m256d; // 4 x double
	using vf = __m128;  // 4 x float

	inline vi load(const long4& a) { return _mm256_load_si256(reinterpret_cast<const __m256i*>(&a)); }
	inline vi load(const ulong4& a) { return _mm256_load_si256(reinterpret_cast<const __m256i*>(&a)); }
	inline vd load(const double4& a) { return _mm256_load_pd(&a.x); }
	inline vf load(const float4& a) { return _mm_load_ps(&a.x); }

	inline long4 to_long4(vi v)
	{
		long4 r;
		_mm256_store_si256(reinterpret_cast<__m256i*>(&r), v);
		return r;
	}

	inline ulong4 to_ulong4(vi v)
	{
		ulong4 r;
		_mm256_store_si256(reinterpret_cast<__m256i*>(&r), v);
		return r;
	}

	inline double4 to_double4(vd v)
	{
		double4 r;
		_mm256_store_pd(&r.x, v);
		return r;
	}

	inline float4 to_float4(vf v)
	{
		float4 r;
		_mm_store_ps(&r.x, v);
		return r;
	}

	inline vi set1_i(int64_t a) { return _mm256_set1_epi64x(a); }
	inline vd set1_d(double a) { return _mm256_set1_pd(a); }
	inline vf set1_f(float a) { return _mm_set_ps1(a); }
	inline vi zero_i() { return _mm256_setzero_si256(); }
	inline vd zero_d() { return _mm256_setzero_pd(); }
	inline vi as_i(vd a) { return _mm256_castpd_si256(a); }
	inline vd as_d(vi a) { return _mm256_castsi256_pd(a); }

	// 64-bit integer lanes
	inline vi add_i(vi a, vi b) { return _mm256_add_epi64(a, b); }
	inline vi sub_i(vi a, vi b) { return _mm256_sub_epi64(a, b); }
	inline vi and_i(vi a, vi b) { return _mm256_and_si256(a, b); }
	inline vi or_i(vi a, vi b) { return _mm256_or_si256(a, b); }
	inline vi xor_i(vi a, vi b) { return _mm256_xor_si256(a, b); }
	inline vi slli(vi a, int n) { return _mm256_slli_epi64(a, n); }
	inline vi srli(vi a, int n) { return _mm256_srli_epi64(a, n); }
	inline vi sllv(vi a, vi n) { return _mm256_sllv_epi64(a, n); } // count >= 64 (unsigned) gives 0
	inline vi srlv(vi a, vi n) { return _mm256_srlv_epi64(a, n); } // count >= 64 (unsigned) gives 0
	inline vi cmpgt_i(vi a, vi b) { return _mm256_cmpgt_epi64(a, b); } // signed, all ones where a > b
	inline vi cmpeq_i(vi a, vi b) { return _mm256_cmpeq_epi64(a, b); }
	// Per byte: b where the top bit of the mask byte is set, otherwise a.
	inline vi blendv(vi a, vi b, vi mask) { return _mm256_blendv_epi8(a, b, mask); }
	// Per lane: the top 16 bits from b, the low 48 bits from a.
	inline vi blend_hi16(vi a, vi b) { return _mm256_blend_epi16(a, b, 0b10001000); }
	// Per lane: the low 32 bits from b, the high 32 bits from a.
	inline vi blend_lo32(vi a, vi b) { return _mm256_blend_epi32(a, b, 0b01010101); }

	// double lanes
	inline vd add_d(vd a, vd b) { return _mm256_add_pd(a, b); }
	inline vd sub_d(vd a, vd b) { return _mm256_sub_pd(a, b); }
	inline vd mul_d(vd a, vd b) { return _mm256_mul_pd(a, b); }
	inline vd cmplt_d(vd a, vd b) { return _mm256_cmp_pd(a, b, _CMP_LT_OS); } // all ones where a < b

	// float lanes
	inline vf mul_f(vf a, vf b) { return _mm_mul_ps(a, b); }
	inline vd cvt_f_to_d(vf a) { return _mm256_cvtps_pd(a); }
	inline vf cvt_d_to_f(vd a) { return _mm256_cvtpd_ps(a); }
}

namespace fx128::avx2
{
	// Interop with code that uses AVX2 intrinsics directly; only exists with the AVX2 backend.
	inline __m256d to_m256d(const double4& a) { return detail::load(a); }
	inline __m256i to_m256i(const long4& a) { return detail::load(a); }
	inline __m256i to_m256i(const ulong4& a) { return detail::load(a); }
	inline double4 to_double4(__m256d v) { return detail::to_double4(v); }
	inline long4 to_long4(__m256i v) { return detail::to_long4(v); }
	inline ulong4 to_ulong4(__m256i v) { return detail::to_ulong4(v); }
}
