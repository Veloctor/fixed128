#pragma once
#include "config.hpp"
#include "types.hpp"

#include <climits>
#include <cstdint>

// AVX2 conversions between packed doubles and packed int64 / fixed128, 4 lanes at a time.
//
// Do not reorder the floating-point operations below and do not compile with fast-math:
// the order of additions, e.g. (v_hi - magic) + v_lo, is part of the correctness.

namespace fx128
{
	namespace detail
	{
		// Exact 2^e as a compile-time constant (same value as std::pow(2.0, e), including under/overflow).
		constexpr double pow2(int e)
		{
			double r = 1.0;
			for (; e > 0; --e) r *= 2.0;
			for (; e < 0; ++e) r *= 0.5;
			return r;
		}

		// Magic numbers for the double<>fixed128 conversions, depending on FIXED128_FRAC_BITS.
		constexpr int frac_bits = FIXED128_FRAC_BITS;
		constexpr double majik_d_hm = pow2(100 - frac_bits) + pow2(132 - frac_bits);
		constexpr double majik_d_lo = pow2(52 - frac_bits);
		constexpr double majik_d_mi = pow2(100 - frac_bits);
		constexpr double majik_d_hi = pow2(132 - frac_bits);
		constexpr double majik_d_hms = pow2(100 - frac_bits) + pow2(132 - frac_bits) + pow2(127 - frac_bits);
		constexpr double majik_d_his = pow2(132 - frac_bits) + pow2(127 - frac_bits);
	}

#pragma region Full-range int64_to_double conversions from wim on Stack Overflow
	// From wim's answer on Stack Overflow (CC BY-SA):
	// https://stackoverflow.com/questions/41144668/how-to-efficiently-perform-double-int64-conversions-with-sse-avx
	inline double4 int64_to_double_fast_precise(const long4 v)
	/* Optimized full range int64_t to double conversion           */
	/* Emulate _mm256_cvtepi64_pd()                                */
	{
		const ulong4 magic_i_lo = _mm256_set1_epi64x(0x4330000000000000);	//0_10000110011_0000000000000000000000000000000000000000000000000000	2^52
		const ulong4 magic_i_hi32 = _mm256_set1_epi64x(0x4530000080000000);	//0_10001010011_0000000000000000000010000000000000000000000000000000	2^84 + 2^63
		const ulong4 magic_i_all = _mm256_set1_epi64x(0x4530000080100000);	//0_10001010011_0000000000000000000010000000000100000000000000000000	2^84 + 2^63 + 2^52
		const double4 magic_d_all = _mm256_castsi256_pd(magic_i_all);

		__m256i v_lo = _mm256_blend_epi32(magic_i_lo, v, 0b01010101);         /* Blend the 32 lowest significant bits of v with magic_int_lo                                                   */
		__m256i v_hi = _mm256_srli_epi64(v, 32);                              /* Extract the 32 most significant bits of v                                                                     */
		v_hi = _mm256_xor_si256(v_hi, magic_i_hi32);                  /* Flip the msb of v_hi and blend with 0x45300000                                                                */
		double4 v_hi_dbl = _mm256_sub_pd(_mm256_castsi256_pd(v_hi), magic_d_all); /* Compute in double precision:                                                                                  */
		double4 result = _mm256_add_pd(v_hi_dbl, _mm256_castsi256_pd(v_lo));    /* (v_hi - magic_d_all) + v_lo  Do not assume associativity of floating point addition !!                        */
		return result;                                                        /* With gcc use -O3, then -fno-associative-math is default. Do not use -Ofast, which enables -fassociative-math! */
		/* With icc use -fp-model precise                                                                                */
	}

	inline double4 uint64_to_double_fast_precise(const ulong4 v)
	/* Optimized full range uint64_t to double conversion          */
	/* This code is essentially identical to Mysticial's solution. */
	/* Emulate _mm256_cvtepu64_pd()                                */
	{
		const long4 magic_i_lo = _mm256_set1_epi64x(0x4330000000000000);		//0_10000110011_0000000000000000000000000000000000000000000000000000    //2^52
		const long4 magic_i_hi32 = _mm256_set1_epi64x(0x4530000000000000);	//0_10001010011_0000000000000000000000000000000000000000000000000000    //2^84
		const double4 magic_d_all = _mm256_set1_pd(1.9342813118337666E25);		//0_10001010011_0000000000000000000000000000000100000000000000000000    //2^84 + 2^52

		__m256i v_lo = _mm256_blend_epi32(magic_i_lo, v, 0b01010101);	//Blend the 32 lowest significant bits of v with magic_int_lo
		__m256i v_hi = _mm256_srli_epi64(v, 32);                                //Extract the 32 most significant bits of v
		v_hi = _mm256_xor_si256(v_hi, magic_i_hi32);                            //Blend v_hi with 0x45300000
		double4 v_hi_fp = _mm256_sub_pd(_mm256_castsi256_pd(v_hi), magic_d_all);//compute in double precision:
		double4 result = _mm256_add_pd(v_hi_fp, _mm256_castsi256_pd(v_lo));     //(v_hi - magic_d_all) + v_lo  Do not assume associativity of floating point addition !!
		return result;	//With gcc use -O3, then -fno-associative-math is default. Do not use -Ofast, which enables -fassociative-math!
	}					//With icc use -fp-model precise
#pragma endregion

#pragma region My(Velctor) conversions for double_to_int64
	//  Only works for inputs in full uint64 range, otherwise result undefined
	inline ulong4 double_to_uint64_fast(const double4 v) //9 instructions
	{
		//constants
		const __m256i mat_mask = _mm256_set1_epi64x(0x0FFFFFFFFFFFFF);	//0_00000000000_1111111111111111111111111111111111111111111111111111
		const __m256i hidden_1 = _mm256_set1_epi64x(0x10000000000000);	//0_00000000001_0000000000000000000000000000000000000000000000000000
		const __m256i exp_bias = _mm256_set1_epi64x(1023LL + 52);			//0_10001010011_0000000000000000000000000000000100000000000000000000    //2^84 + 2^52
		const __m256i zero256 = _mm256_setzero_si256();
		//majik operations
		__m256i bin = _mm256_castpd_si256(v);
		__m256i mat = _mm256_and_si256(bin, mat_mask);					//1,1/3
		mat = _mm256_or_si256(mat, hidden_1);							//1,1/3
		__m256i exp_enc = _mm256_srli_epi64(bin, 52);					//1,1/2
		__m256i exp_frac = _mm256_sub_epi64(exp_enc, exp_bias);			//1,1/3
		__m256i msl = _mm256_sllv_epi64(mat, exp_frac);					//1,1/2
		__m256i exp_frac_n = _mm256_sub_epi64(zero256, exp_frac);		//1,1/3
		__m256i msr = _mm256_srlv_epi64(mat, exp_frac_n);				//1,1/2
		__m256i exp_is_pos = _mm256_cmpgt_epi64(exp_frac, zero256);		//3,1
		__m256i result_abs = _mm256_blendv_epi8(msr, msl, exp_is_pos);	//2,1
		return result_abs;	//total latency: 12, total throughput CPI: 4.8
	}

	inline long4 double_to_int64_fast(const double4 v) //13 instructions
	{
		//constants
		const __m256i mat_mask = _mm256_set1_epi64x(0x0FFFFFFFFFFFFF);	//0_00000000000_1111111111111111111111111111111111111111111111111111
		const __m256i hidden_1 = _mm256_set1_epi64x(0x10000000000000);	//0_00000000001_0000000000000000000000000000000000000000000000000000
		const __m256i exp_bias = _mm256_set1_epi64x(1023LL + 52);			//0_10001010011_0000000000000000000000000000000100000000000000000000    //2^84 + 2^52
		const __m256i zero256 = _mm256_setzero_si256();
		//majik operations										  //Latency, Throughput(references IceLake)
		__m256i bin = _mm256_castpd_si256(v);
		__m256i negative = _mm256_cmpgt_epi64(zero256, bin);			//3,1
		__m256i mat = _mm256_and_si256(bin, mat_mask);					//1,1/3
		mat = _mm256_or_si256(mat, hidden_1);							//1,1/3
		__m256i exp_enc = _mm256_slli_epi64(bin, 1);					//1,1/2
		exp_enc = _mm256_srli_epi64(exp_enc, 53);						//1,1/2
		__m256i exp_frac = _mm256_sub_epi64(exp_enc, exp_bias);			//1,1/3
		__m256i msl = _mm256_sllv_epi64(mat, exp_frac);					//1,1/2
		__m256i exp_frac_n = _mm256_sub_epi64(zero256, exp_frac);		//1,1/3
		__m256i msr = _mm256_srlv_epi64(mat, exp_frac_n);				//1,1/2
		__m256i exp_is_pos = _mm256_cmpgt_epi64(exp_frac, zero256);		//3,1
		__m256i result_abs = _mm256_blendv_epi8(msr, msl, exp_is_pos);	//2,1
		__m256i result = _mm256_xor_si256(result_abs, negative);		//1,1/3
		result = _mm256_sub_epi64(result, negative);					//1,1/3
		return result;	//total latency: 18, total throughput CPI: 7
	}
#pragma endregion

#pragma region My(Velctor) Majikal conversions for double<>fixed128
	// The 128 bits are split into 48 + 48 + 32 bit pieces (v_lo / v_mi / v_hi); each piece gets the exponent bits of
	// a magic number and the magic numbers cancel out in floating point.

	//works for full unsigned fixed128 range, otherwise result undefined
	//11 instructions, total latency: 23, total throughput CPI: 4.7
	inline double4 ufixed128_to_double(const ulong4& ihigh, const ulong4& ilow)//maybe low 48bit cannot round off with 0.5ULP, 1ULP error
	{
		const double4 majik_d_lo = _mm256_set1_pd(detail::majik_d_lo);
		const double4 majik_d_hm = _mm256_set1_pd(detail::majik_d_hm);
		const __m256i majik_i_mi = _mm256_castpd_si256(_mm256_set1_pd(detail::majik_d_mi));
		const __m256i majik_i_hi = _mm256_castpd_si256(_mm256_set1_pd(detail::majik_d_hi));
		//majik operations													//Latency, Throughput(references IceLake)
		__m256i v_lo = _mm256_blend_epi16(ilow, _mm256_castpd_si256(majik_d_lo), 0b10001000);		//L1, T1/2
		__m256i v_mi = _mm256_slli_epi64(ihigh, 16);							//L1, T1/2
		__m256i losr48 = _mm256_srli_epi64(ilow, 48);							//L1, T1/2
		v_mi = _mm256_xor_si256(v_mi, losr48);									//L1, T1/3
		v_mi = _mm256_blend_epi32(majik_i_mi, v_mi, 0b01010101);				//L1, T1/3
		__m256i v_hi = _mm256_srli_epi64(ihigh, 16);							//L1, T1/2
		v_hi = _mm256_xor_si256(v_hi, majik_i_hi);								//L1, T1/3
		double4 loresult = _mm256_sub_pd(_mm256_castsi256_pd(v_lo), majik_d_lo);//L4, T1/2
		double4 result = _mm256_sub_pd(_mm256_castsi256_pd(v_hi), majik_d_hm);	//L4, T1/2
		result = _mm256_add_pd(result, _mm256_castsi256_pd(v_mi));				//L4, T1/2
		result = _mm256_add_pd(result, loresult);								//L4, T1/2
		return result;
	}

	//works for full fixed128 range, otherwise result undefined
	//11 instructions, total latency: 23, total throughput CPI: 4.7
	inline double4 fixed128_to_double(const ulong4& ihigh, const ulong4& ilow)//maybe low 48bit cannot round off with 0.5ULP, 1ULP error
	{
		const double4 majik_d_lo = _mm256_set1_pd(detail::majik_d_lo);
		const double4 majik_d_hms = _mm256_set1_pd(detail::majik_d_hms);
		const __m256i majik_i_mi = _mm256_castpd_si256(_mm256_set1_pd(detail::majik_d_mi));
		const __m256i majik_i_his = _mm256_castpd_si256(_mm256_set1_pd(detail::majik_d_his));
		//majik operations													//Latency, Throughput(references IceLake)
		__m256i v_lo = _mm256_blend_epi16(ilow, _mm256_castpd_si256(majik_d_lo), 0b10001000);		//L1, T1/2
		__m256i v_mi = _mm256_slli_epi64(ihigh, 16);							//L1, T1/2
		__m256i losr48 = _mm256_srli_epi64(ilow, 48);							//L1, T1/2
		v_mi = _mm256_xor_si256(v_mi, losr48);									//L1, T1/3
		v_mi = _mm256_blend_epi32(majik_i_mi, v_mi, 0b01010101);				//L1, T1/3
		__m256i v_hi = _mm256_srli_epi64(ihigh, 16);							//L1, T1/2
		v_hi = _mm256_xor_si256(v_hi, majik_i_his);								//L1, T1/3
		double4 loresult = _mm256_sub_pd(_mm256_castsi256_pd(v_lo), majik_d_lo);//L4, T1/2
		double4 result = _mm256_sub_pd(_mm256_castsi256_pd(v_hi), majik_d_hms);	//L4, T1/2
		result = _mm256_add_pd(result, _mm256_castsi256_pd(v_mi));				//L4, T1/2
		result = _mm256_add_pd(result, loresult);								//L4, T1/2
		return result;
	}

	//works for full unsigned fixed128 range, otherwise result undefined
	//instruction total latency: 34(round half away)_30(trunc), total throughput CPI: 11.2(round half away)_10.8(trunc) (references IceLake)
	inline void double_to_ufixed128_full(double4 v, ulong4& outIhigh, ulong4& outIlow)
	{//rounding is selected by FIXED128_ROUND_HALF_AWAY_FROM_ZERO (see config.hpp)
		//constants
		const __m256i mat_mask = _mm256_set1_epi64x(0x0FFFFFFFFFFFFF);	//0_00000000000_1111111111111111111111111111111111111111111111111111
		const __m256i hidden_1 = _mm256_set1_epi64x(0x10000000000000);	//0_00000000001_0000000000000000000000000000000000000000000000000000//you can reduce this constant by add 1 to mat_mask, inc more 1/3 clock throughput
		const __m256i exp_lbias = _mm256_set1_epi64x(1023LL + 52 - FIXED128_FRAC_BITS);
		const __m256i exp_hbias = _mm256_set1_epi64x(1023LL + 52 + 64 - FIXED128_FRAC_BITS);
		const __m256i zero256 = _mm256_setzero_si256();
#if FIXED128_ROUND_HALF_AWAY_FROM_ZERO
		//majik operations											  //Latency, Throughput
		v = _mm256_add_pd(v, _mm256_set1_pd(0.5));							//4,1/2
#endif
		__m256i bin = _mm256_castpd_si256(v);
		__m256i exp_enc = _mm256_srli_epi64(bin, 52);						//1,1/2
		__m256i mat = _mm256_and_si256(bin, mat_mask);						//1,1/3
		mat = _mm256_or_si256(mat, hidden_1);								//1,1/3
		__m256i exp_hfrac = _mm256_sub_epi64(exp_enc, exp_hbias);			//1,1/3
		__m256i hmsl = _mm256_sllv_epi64(mat, exp_hfrac);					//1,1/2
		__m256i exp_hfrac_n = _mm256_sub_epi64(zero256, exp_hfrac);			//1,1/3
		__m256i hmsr = _mm256_srlv_epi64(mat, exp_hfrac_n);					//1,1/2
		__m256i hexp_is_pos = _mm256_cmpgt_epi64(exp_hfrac, zero256);		//3,1
		outIhigh = _mm256_blendv_epi8(hmsr, hmsl, hexp_is_pos);				//2,1
		__m256i exp_lfrac = _mm256_sub_epi64(exp_enc, exp_lbias);			//1,1/3
		__m256i lmsl = _mm256_sllv_epi64(mat, exp_lfrac);					//1,1/2
		__m256i exp_lfrac_n = _mm256_sub_epi64(zero256, exp_lfrac);			//1,1/3
		__m256i lmsr = _mm256_srlv_epi64(mat, exp_lfrac_n);					//1,1/2
		__m256i lexp_is_pos = _mm256_cmpgt_epi64(exp_lfrac, zero256);		//3,1
		outIlow = _mm256_blendv_epi8(lmsr, lmsl, lexp_is_pos);					//2,1
	}

	//works for full fixed128 range, otherwise result undefined
	//instruction total latency: 40(round half away)_36(trunc), total throughput CPI: 12.6(round half away)_12.2(trunc) (references IceLake)
	inline void double_to_fixed128_full(const double4 v, long4& outIHigh, ulong4& outIlow)
	{
		//constants
		const __m256i mat_mask = _mm256_set1_epi64x(0x0FFFFFFFFFFFFF);	//0_00000000000_1111111111111111111111111111111111111111111111111111
		const __m256i hidden_1 = _mm256_set1_epi64x(0x10000000000000);	//0_00000000001_0000000000000000000000000000000000000000000000000000
		const __m256i exp_lbias = _mm256_set1_epi64x(1023LL + 52 - FIXED128_FRAC_BITS);
		const __m256i exp_hbias = _mm256_set1_epi64x(1023LL + 52 + 64 - FIXED128_FRAC_BITS);
		const __m256i zero256 = _mm256_setzero_si256();
		//majik operations											  //Latency, Throughput
#if FIXED128_ROUND_HALF_AWAY_FROM_ZERO
		double4 abs = _mm256_and_pd(v, _mm256_castsi256_pd(_mm256_set1_epi64x(LLONG_MAX)));							//1,1/3
		abs = _mm256_add_pd(abs, _mm256_set1_pd(0.5));						//4,1/2
		__m256i bin = _mm256_castpd_si256(abs);
		__m256i exp_enc = _mm256_srli_epi64(bin, 52);						//1,1/2
#else
		__m256i bin = _mm256_castpd_si256(v);
		__m256i exp_enc = _mm256_slli_epi64(bin, 1);						//1,1/2
		exp_enc = _mm256_srli_epi64(exp_enc, 53);							//1,1/2
#endif
		double4 neg = _mm256_cmp_pd(v, _mm256_setzero_pd(), _CMP_LT_OS);	//4,1/2
		__m256i mat = _mm256_and_si256(bin, mat_mask);						//1,1/3
		mat = _mm256_or_si256(mat, hidden_1);								//1,1/3
		__m256i exp_lfrac = _mm256_sub_epi64(exp_enc, exp_lbias);			//1,1/3
		__m256i lmsl = _mm256_sllv_epi64(mat, exp_lfrac);					//1,1/2
		__m256i exp_lfrac_n = _mm256_sub_epi64(zero256, exp_lfrac);			//1,1/3
		__m256i lmsr = _mm256_srlv_epi64(mat, exp_lfrac_n);					//1,1/2
		__m256i lexp_is_pos = _mm256_cmpgt_epi64(exp_lfrac, zero256);		//3,1
		__m256i lresult_abs = _mm256_blendv_epi8(lmsr, lmsl, lexp_is_pos);	//2,1
		__m256i negative = _mm256_castpd_si256(neg);
		outIlow = _mm256_xor_si256(lresult_abs, negative);						//1,1/3
		outIlow = _mm256_sub_epi64(outIlow, negative);							//1,1/3
		__m256i exp_hfrac = _mm256_sub_epi64(exp_enc, exp_hbias);			//1,1/3
		__m256i hmsl = _mm256_sllv_epi64(mat, exp_hfrac);					//1,1/2
		__m256i exp_hfrac_n = _mm256_sub_epi64(zero256, exp_hfrac);			//1,1/3
		__m256i hmsr = _mm256_srlv_epi64(mat, exp_hfrac_n);					//1,1/2
		__m256i hexp_is_pos = _mm256_cmpgt_epi64(exp_hfrac, zero256);		//3,1
		__m256i hresult_abs = _mm256_blendv_epi8(hmsr, hmsl, hexp_is_pos);	//2,1
		outIHigh = _mm256_xor_si256(hresult_abs, negative);					//1,1/3
		__m256i borrow = _mm256_cmpeq_epi64(lresult_abs, zero256);			//1,1/2
		borrow = _mm256_and_si256(borrow, negative);						//1,1/3
		outIHigh = _mm256_sub_epi64(outIHigh, borrow);							//1,1/3
	}
#pragma endregion
}
