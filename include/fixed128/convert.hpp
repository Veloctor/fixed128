#pragma once
#include "config.hpp"
#include "detail/native.hpp"
#include "types.hpp"

#include <cstdint>

// Conversions between packed doubles and packed int64 / fixed128, 4 lanes at a time.
// They are written in the backend's vector primitives (detail/native.hpp); with the AVX2 backend each primitive is
// one instruction, and the latency/throughput comments below refer to those instructions (Ice Lake).
// A fixed128 lane is passed as its two 64-bit halves, upper and lower, like the scalar fixed128.
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
	inline double4 int64_to_double_fast_precise(const long4& in)
	/* Optimized full range int64_t to double conversion           */
	/* Emulate _mm256_cvtepi64_pd()                                */
	{
		using namespace detail;
		const vi magic_i_lo = set1_i(0x4330000000000000);	//0_10000110011_0000000000000000000000000000000000000000000000000000	2^52
		const vi magic_i_hi32 = set1_i(0x4530000080000000);	//0_10001010011_0000000000000000000010000000000000000000000000000000	2^84 + 2^63
		const vi magic_i_all = set1_i(0x4530000080100000);	//0_10001010011_0000000000000000000010000000000100000000000000000000	2^84 + 2^63 + 2^52
		const vd magic_d_all = as_d(magic_i_all);
		const vi v = load(in);

		vi v_lo = blend_lo32(magic_i_lo, v);                 /* Blend the 32 lowest significant bits of v with magic_int_lo                                                   */
		vi v_hi = srli(v, 32);                               /* Extract the 32 most significant bits of v                                                                     */
		v_hi = xor_i(v_hi, magic_i_hi32);                    /* Flip the msb of v_hi and blend with 0x45300000                                                                */
		vd v_hi_dbl = sub_d(as_d(v_hi), magic_d_all);        /* Compute in double precision:                                                                                  */
		vd result = add_d(v_hi_dbl, as_d(v_lo));             /* (v_hi - magic_d_all) + v_lo  Do not assume associativity of floating point addition !!                        */
		return to_double4(result);                           /* With gcc use -O3, then -fno-associative-math is default. Do not use -Ofast, which enables -fassociative-math! */
		/* With icc use -fp-model precise                                                                                */
	}

	inline double4 uint64_to_double_fast_precise(const ulong4& in)
	/* Optimized full range uint64_t to double conversion          */
	/* This code is essentially identical to Mysticial's solution. */
	/* Emulate _mm256_cvtepu64_pd()                                */
	{
		using namespace detail;
		const vi magic_i_lo = set1_i(0x4330000000000000);		//0_10000110011_0000000000000000000000000000000000000000000000000000    //2^52
		const vi magic_i_hi32 = set1_i(0x4530000000000000);	//0_10001010011_0000000000000000000000000000000000000000000000000000    //2^84
		const vd magic_d_all = set1_d(1.9342813118337666E25);	//0_10001010011_0000000000000000000000000000000100000000000000000000    //2^84 + 2^52
		const vi v = load(in);

		vi v_lo = blend_lo32(magic_i_lo, v);			//Blend the 32 lowest significant bits of v with magic_int_lo
		vi v_hi = srli(v, 32);							//Extract the 32 most significant bits of v
		v_hi = xor_i(v_hi, magic_i_hi32);				//Blend v_hi with 0x45300000
		vd v_hi_fp = sub_d(as_d(v_hi), magic_d_all);	//compute in double precision:
		vd result = add_d(v_hi_fp, as_d(v_lo));		//(v_hi - magic_d_all) + v_lo  Do not assume associativity of floating point addition !!
		return to_double4(result);	//With gcc use -O3, then -fno-associative-math is default. Do not use -Ofast, which enables -fassociative-math!
	}								//With icc use -fp-model precise
#pragma endregion

#pragma region My(Velctor) conversions for double_to_int64
	//  Only works for inputs in full uint64 range, otherwise result undefined
	inline ulong4 double_to_uint64_fast(const double4& in) //9 instructions
	{
		using namespace detail;
		//constants
		const vi mat_mask = set1_i(0x0FFFFFFFFFFFFF);	//0_00000000000_1111111111111111111111111111111111111111111111111111
		const vi hidden_1 = set1_i(0x10000000000000);	//0_00000000001_0000000000000000000000000000000000000000000000000000
		const vi exp_bias = set1_i(1023LL + 52);		//0_10001010011_0000000000000000000000000000000100000000000000000000    //2^84 + 2^52
		const vi zero256 = zero_i();
		//majik operations
		vi bin = as_i(load(in));
		vi mat = and_i(bin, mat_mask);					//1,1/3
		mat = or_i(mat, hidden_1);						//1,1/3
		vi exp_enc = srli(bin, 52);						//1,1/2
		vi exp_frac = sub_i(exp_enc, exp_bias);			//1,1/3
		vi msl = sllv(mat, exp_frac);					//1,1/2
		vi exp_frac_n = sub_i(zero256, exp_frac);		//1,1/3
		vi msr = srlv(mat, exp_frac_n);					//1,1/2
		vi exp_is_pos = cmpgt_i(exp_frac, zero256);		//3,1
		vi result_abs = blendv(msr, msl, exp_is_pos);	//2,1
		return to_ulong4(result_abs);	//total latency: 12, total throughput CPI: 4.8
	}

	inline long4 double_to_int64_fast(const double4& in) //13 instructions
	{
		using namespace detail;
		//constants
		const vi mat_mask = set1_i(0x0FFFFFFFFFFFFF);	//0_00000000000_1111111111111111111111111111111111111111111111111111
		const vi hidden_1 = set1_i(0x10000000000000);	//0_00000000001_0000000000000000000000000000000000000000000000000000
		const vi exp_bias = set1_i(1023LL + 52);		//0_10001010011_0000000000000000000000000000000100000000000000000000    //2^84 + 2^52
		const vi zero256 = zero_i();
		//majik operations								//Latency, Throughput(references IceLake)
		vi bin = as_i(load(in));
		vi negative = cmpgt_i(zero256, bin);			//3,1
		vi mat = and_i(bin, mat_mask);					//1,1/3
		mat = or_i(mat, hidden_1);						//1,1/3
		vi exp_enc = slli(bin, 1);						//1,1/2
		exp_enc = srli(exp_enc, 53);					//1,1/2
		vi exp_frac = sub_i(exp_enc, exp_bias);			//1,1/3
		vi msl = sllv(mat, exp_frac);					//1,1/2
		vi exp_frac_n = sub_i(zero256, exp_frac);		//1,1/3
		vi msr = srlv(mat, exp_frac_n);					//1,1/2
		vi exp_is_pos = cmpgt_i(exp_frac, zero256);		//3,1
		vi result_abs = blendv(msr, msl, exp_is_pos);	//2,1
		vi result = xor_i(result_abs, negative);		//1,1/3
		result = sub_i(result, negative);				//1,1/3
		return to_long4(result);	//total latency: 18, total throughput CPI: 7
	}
#pragma endregion

#pragma region My(Velctor) Majikal conversions for double<>fixed128
	// The 128 bits are split into 48 + 48 + 32 bit pieces (v_lo / v_mi / v_hi); each piece gets the exponent bits of
	// a magic number and the magic numbers cancel out in floating point.

	//works for full unsigned fixed128 range, otherwise result undefined
	//11 instructions, total latency: 23, total throughput CPI: 4.7
	inline double4 ufixed128_to_double(const ulong4& upper, const ulong4& lower)//maybe low 48bit cannot round off with 0.5ULP, 1ULP error
	{
		using namespace detail;
		const vd majik_lo = set1_d(detail::majik_d_lo);
		const vd majik_hm = set1_d(detail::majik_d_hm);
		const vi majik_i_mi = as_i(set1_d(detail::majik_d_mi));
		const vi majik_i_hi = as_i(set1_d(detail::majik_d_hi));
		const vi ihigh = load(upper), ilow = load(lower);
		//majik operations											//Latency, Throughput(references IceLake)
		vi v_lo = blend_hi16(ilow, as_i(majik_lo));					//L1, T1/2
		vi v_mi = slli(ihigh, 16);									//L1, T1/2
		vi losr48 = srli(ilow, 48);									//L1, T1/2
		v_mi = xor_i(v_mi, losr48);									//L1, T1/3
		v_mi = blend_lo32(majik_i_mi, v_mi);						//L1, T1/3
		vi v_hi = srli(ihigh, 16);									//L1, T1/2
		v_hi = xor_i(v_hi, majik_i_hi);								//L1, T1/3
		vd loresult = sub_d(as_d(v_lo), majik_lo);					//L4, T1/2
		vd result = sub_d(as_d(v_hi), majik_hm);					//L4, T1/2
		result = add_d(result, as_d(v_mi));							//L4, T1/2
		result = add_d(result, loresult);							//L4, T1/2
		return to_double4(result);
	}

	//works for full fixed128 range, otherwise result undefined
	//11 instructions, total latency: 23, total throughput CPI: 4.7
	inline double4 fixed128_to_double(const ulong4& upper, const ulong4& lower)//maybe low 48bit cannot round off with 0.5ULP, 1ULP error
	{
		using namespace detail;
		const vd majik_lo = set1_d(detail::majik_d_lo);
		const vd majik_hms = set1_d(detail::majik_d_hms);
		const vi majik_i_mi = as_i(set1_d(detail::majik_d_mi));
		const vi majik_i_his = as_i(set1_d(detail::majik_d_his));
		const vi ihigh = load(upper), ilow = load(lower);
		//majik operations											//Latency, Throughput(references IceLake)
		vi v_lo = blend_hi16(ilow, as_i(majik_lo));					//L1, T1/2
		vi v_mi = slli(ihigh, 16);									//L1, T1/2
		vi losr48 = srli(ilow, 48);									//L1, T1/2
		v_mi = xor_i(v_mi, losr48);									//L1, T1/3
		v_mi = blend_lo32(majik_i_mi, v_mi);						//L1, T1/3
		vi v_hi = srli(ihigh, 16);									//L1, T1/2
		v_hi = xor_i(v_hi, majik_i_his);							//L1, T1/3
		vd loresult = sub_d(as_d(v_lo), majik_lo);					//L4, T1/2
		vd result = sub_d(as_d(v_hi), majik_hms);					//L4, T1/2
		result = add_d(result, as_d(v_mi));							//L4, T1/2
		result = add_d(result, loresult);							//L4, T1/2
		return to_double4(result);
	}

	// Rounding (FIXED128_ROUND_HALF_AWAY_FROM_ZERO, see config.hpp) is exact and done on the integer mantissa:
	// only the low half can be a right shift by n >= 1 bits, and then the magnitude is < 2^52, so
	// round_half_up(mat / 2^n) == ((mat >> (n - 1)) + 1) >> 1 never carries into the high half.
	// The right-shift path is selected for n >= 1 only (a shift by 0 takes the left-shift path), because
	// mat >> (n - 1) needs n >= 1; srlv by a "negative" (huge unsigned) count gives 0, which also handles n - 1 >= 64.

	//works for full unsigned fixed128 range, otherwise result undefined
	//instruction total latency: 30(trunc), total throughput CPI: 10.8(trunc) (references IceLake); round half away: +2 instructions (add, srli) on the low path
	inline void double_to_ufixed128_full(const double4& in, ulong4& outUpper, ulong4& outLower)
	{
		using namespace detail;
		//constants
		const vi mat_mask = set1_i(0x0FFFFFFFFFFFFF);	//0_00000000000_1111111111111111111111111111111111111111111111111111
		const vi hidden_1 = set1_i(0x10000000000000);	//0_00000000001_0000000000000000000000000000000000000000000000000000//you can reduce this constant by add 1 to mat_mask, inc more 1/3 clock throughput
		const vi exp_lbias = set1_i(1023LL + 52 - FIXED128_FRAC_BITS);
		const vi exp_hbias = set1_i(1023LL + 52 + 64 - FIXED128_FRAC_BITS);
		const vi zero256 = zero_i();
		//majik operations								//Latency, Throughput
		vi bin = as_i(load(in));
		vi exp_enc = srli(bin, 52);						//1,1/2
		vi mat = and_i(bin, mat_mask);					//1,1/3
		mat = or_i(mat, hidden_1);						//1,1/3
		vi exp_hfrac = sub_i(exp_enc, exp_hbias);		//1,1/3
		vi hmsl = sllv(mat, exp_hfrac);					//1,1/2
		vi exp_hfrac_n = sub_i(zero256, exp_hfrac);		//1,1/3
		vi hmsr = srlv(mat, exp_hfrac_n);				//1,1/2
		vi hexp_is_pos = cmpgt_i(exp_hfrac, zero256);	//3,1
		outUpper = to_ulong4(blendv(hmsr, hmsl, hexp_is_pos));	//2,1
		vi exp_lfrac = sub_i(exp_enc, exp_lbias);		//1,1/3
		vi lmsl = sllv(mat, exp_lfrac);					//1,1/2
#if FIXED128_ROUND_HALF_AWAY_FROM_ZERO
		const vi exp_lbias_m1 = set1_i(1023LL + 52 - FIXED128_FRAC_BITS - 1);
		const vi one256 = set1_i(1);
		const vi minus_one256 = set1_i(-1);
		vi exp_lfrac_n1 = sub_i(exp_lbias_m1, exp_enc);	//1,1/3		n - 1
		vi lmsr = srlv(mat, exp_lfrac_n1);				//1,1/2		keeps the rounding bit
		lmsr = add_i(lmsr, one256);						//1,1/3
		lmsr = srli(lmsr, 1);							//1,1/2
		vi lexp_is_pos = cmpgt_i(exp_lfrac, minus_one256);	//3,1		left shift, or shift by 0
#else
		vi exp_lfrac_n = sub_i(zero256, exp_lfrac);		//1,1/3
		vi lmsr = srlv(mat, exp_lfrac_n);				//1,1/2
		vi lexp_is_pos = cmpgt_i(exp_lfrac, zero256);	//3,1
#endif
		outLower = to_ulong4(blendv(lmsr, lmsl, lexp_is_pos));	//2,1
	}

	//works for full fixed128 range, otherwise result undefined
	//instruction total latency: 36(trunc), total throughput CPI: 12.2(trunc) (references IceLake); round half away: +2 instructions (add, srli) on the low path
	inline void double_to_fixed128_full(const double4& in, ulong4& outUpper, ulong4& outLower)
	{
		using namespace detail;
		//constants
		const vi mat_mask = set1_i(0x0FFFFFFFFFFFFF);	//0_00000000000_1111111111111111111111111111111111111111111111111111
		const vi hidden_1 = set1_i(0x10000000000000);	//0_00000000001_0000000000000000000000000000000000000000000000000000
		const vi exp_lbias = set1_i(1023LL + 52 - FIXED128_FRAC_BITS);
		const vi exp_hbias = set1_i(1023LL + 52 + 64 - FIXED128_FRAC_BITS);
		const vi zero256 = zero_i();
		//majik operations								//Latency, Throughput
		const vd v = load(in);
		vi bin = as_i(v);
		vi exp_enc = slli(bin, 1);						//1,1/2
		exp_enc = srli(exp_enc, 53);					//1,1/2
		vd neg = cmplt_d(v, zero_d());					//4,1/2
		vi mat = and_i(bin, mat_mask);					//1,1/3
		mat = or_i(mat, hidden_1);						//1,1/3
		vi exp_lfrac = sub_i(exp_enc, exp_lbias);		//1,1/3
		vi lmsl = sllv(mat, exp_lfrac);					//1,1/2
#if FIXED128_ROUND_HALF_AWAY_FROM_ZERO
		// rounds the magnitude, see the comment above double_to_ufixed128_full
		const vi exp_lbias_m1 = set1_i(1023LL + 52 - FIXED128_FRAC_BITS - 1);
		const vi one256 = set1_i(1);
		const vi minus_one256 = set1_i(-1);
		vi exp_lfrac_n1 = sub_i(exp_lbias_m1, exp_enc);	//1,1/3		n - 1
		vi lmsr = srlv(mat, exp_lfrac_n1);				//1,1/2		keeps the rounding bit
		lmsr = add_i(lmsr, one256);						//1,1/3
		lmsr = srli(lmsr, 1);							//1,1/2
		vi lexp_is_pos = cmpgt_i(exp_lfrac, minus_one256);	//3,1		left shift, or shift by 0
#else
		vi exp_lfrac_n = sub_i(zero256, exp_lfrac);		//1,1/3
		vi lmsr = srlv(mat, exp_lfrac_n);				//1,1/2
		vi lexp_is_pos = cmpgt_i(exp_lfrac, zero256);	//3,1
#endif
		vi lresult_abs = blendv(lmsr, lmsl, lexp_is_pos);	//2,1
		vi negative = as_i(neg);
		vi ilow = xor_i(lresult_abs, negative);			//1,1/3
		ilow = sub_i(ilow, negative);					//1,1/3
		outLower = to_ulong4(ilow);
		vi exp_hfrac = sub_i(exp_enc, exp_hbias);		//1,1/3
		vi hmsl = sllv(mat, exp_hfrac);					//1,1/2
		vi exp_hfrac_n = sub_i(zero256, exp_hfrac);		//1,1/3
		vi hmsr = srlv(mat, exp_hfrac_n);				//1,1/2
		vi hexp_is_pos = cmpgt_i(exp_hfrac, zero256);	//3,1
		vi hresult_abs = blendv(hmsr, hmsl, hexp_is_pos);	//2,1
		vi ihigh = xor_i(hresult_abs, negative);		//1,1/3
		vi borrow = cmpeq_i(lresult_abs, zero256);		//1,1/2
		borrow = and_i(borrow, negative);				//1,1/3
		ihigh = sub_i(ihigh, borrow);					//1,1/3
		outUpper = to_ulong4(ihigh);
	}
#pragma endregion
}
