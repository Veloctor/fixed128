#pragma once
#include "convert.hpp"
#include "detail/native.hpp"
#include "scalar.hpp"
#include "types.hpp"

#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>

// 4-lane fixed-point types: fixed64x4<FracBits> and fixed128x4<FracBits>, structure of arrays of the public 4-lane types.
// Note: the constructors taking 4 scalars take them in lane order, like the members x, y, z, w: the first argument is lane 0.

namespace fx128
{
	template<int FracBits>
	struct fixed64x4
	{
		static constexpr int frac_bits = FracBits;

		long4 bits;

		explicit fixed64x4(double fp)
		{
			fp *= std::pow(2.0, FracBits);
			bits = double_to_int64_fast(double4{fp, fp, fp, fp});
		}

		explicit fixed64x4(int64_t i)
		{
			i <<= FracBits;
			bits = long4{i, i, i, i};
		}

		explicit fixed64x4(const long4& i)
		{
			bits = detail::to_long4(detail::slli(detail::load(i), FracBits));
		}

		explicit fixed64x4(const double4& fp)
		{
			using namespace detail;
			const vd vfactor = set1_d(std::pow(2.0, FracBits));
			const vd vfp = mul_d(load(fp), vfactor);
			bits = double_to_int64_fast(to_double4(vfp));
		}

		explicit fixed64x4(const float4& fp32)
		{
			using namespace detail;
			const vf vfactor = set1_f(static_cast<float>(std::pow(2.0f, FracBits)));
			const vf vfp32 = mul_f(load(fp32), vfactor);
			const vd vfp = cvt_f_to_d(vfp32);
			bits = double_to_int64_fast(to_double4(vfp));
		}

		explicit operator double4() const
		{
			using namespace detail;
			const vd vfactor = set1_d(std::pow(2.0, -FracBits));
			const vd vfp = load(int64_to_double_fast_precise(bits));
			return to_double4(mul_d(vfp, vfactor));
		}

		explicit operator float4() const
		{
			using namespace detail;
			const vf vfactor = set1_f(static_cast<float>(std::pow(2.0f, -FracBits)));
			const vd vfp = load(int64_to_double_fast_precise(bits));
			const vf vfp32 = cvt_d_to_f(vfp);
			return to_float4(mul_f(vfp32, vfactor));
		}
	};

	template<int FracBits>
	struct fixed128x4
	{
		static constexpr int frac_bits = FracBits;

		using scalar64 = fixed64<FracBits>;
		using scalar128 = fixed128<FracBits>;

		ulong4 upper, lower;

		explicit fixed128x4(const double4& x)
		{
			double_to_fixed128_full<FracBits>(x, upper, lower);
		}

		explicit fixed128x4(double x)
		{
			*this = fixed128x4{double4{x, x, x, x}};
		}

		fixed128x4()
		{
			upper = ulong4{0, 0, 0, 0};
			lower = ulong4{0, 0, 0, 0};
		}

		// lanes 0, 1, 2, 3
		fixed128x4(scalar64 x, scalar64 y, scalar64 z, scalar64 w)
		{
			upper = ulong4{0, 0, 0, 0};
			lower = ulong4{x.ubits, y.ubits, z.ubits, w.ubits};
		}

		fixed128x4(scalar64 x)
		{
			upper = ulong4{0, 0, 0, 0};
			lower = ulong4{x.ubits, x.ubits, x.ubits, x.ubits};
		}

		fixed128x4(scalar128 x)
		{
			upper = ulong4{x.upper, x.upper, x.upper, x.upper};
			lower = ulong4{x.lower, x.lower, x.lower, x.lower};
		}

		// lanes 0, 1, 2, 3
		fixed128x4(scalar128 x, scalar128 y, scalar128 z, scalar128 w)
		{
			upper = ulong4{x.upper, y.upper, z.upper, w.upper};
			lower = ulong4{x.lower, y.lower, z.lower, w.lower};
		}

		fixed128x4(const ulong4& upper, const ulong4& lower)
		{
			this->upper = upper;
			this->lower = lower;
		}

		//索引器
		scalar128 operator[](int i) const
		{
			assert(i >= 0 && i < 4);
			const uint64_t u[4] = {upper.x, upper.y, upper.z, upper.w};
			const uint64_t l[4] = {lower.x, lower.y, lower.z, lower.w};
			return scalar128{static_cast<int64_t>(u[i]), static_cast<int64_t>(l[i])};
		}

		explicit operator double4() const
		{
			return fixed128_to_double<FracBits>(upper, lower);
		}

		fixed128x4 operator+(const fixed128x4& value) const
		{
			using namespace detail;
			const vi sign_mask = set1_i(LLONG_MIN);
			const vi vlower = load(value.lower);
			auto retlower = add_i(load(lower), vlower);
			vi carry = cmpgt_i(add_i(vlower, sign_mask), add_i(retlower, sign_mask));
			auto retupper = sub_i(add_i(load(upper), load(value.upper)), carry);
			return {to_ulong4(retupper), to_ulong4(retlower)};
		}

		fixed128x4 operator+(const ulong4& rlower) const
		{
			using namespace detail;
			const vi sign_mask = set1_i(LLONG_MIN);
			const vi vrlower = load(rlower);
			auto retlower = add_i(load(lower), vrlower);
			vi carry = cmpgt_i(add_i(vrlower, sign_mask), add_i(retlower, sign_mask));
			auto retupper = sub_i(load(upper), carry);
			return {to_ulong4(retupper), to_ulong4(retlower)};
		}

		fixed128x4 operator-(const fixed128x4& value) const
		{
			using namespace detail;
			const vi sign_mask = set1_i(LLONG_MIN);
			const vi vlower = load(lower), value_lower = load(value.lower);
			vi up = sub_i(load(upper), load(value.upper));
			vi carry = cmpgt_i(add_i(value_lower, sign_mask), add_i(vlower, sign_mask));
			auto retlower = sub_i(vlower, value_lower);
			auto retupper = add_i(up, carry);
			return {to_ulong4(retupper), to_ulong4(retlower)};
		}

		fixed128x4 operator-(const ulong4& rlower) const
		{
			using namespace detail;
			const vi sign_mask = set1_i(LLONG_MIN);
			const vi vlower = load(lower), vrlower = load(rlower);
			vi carry = cmpgt_i(add_i(vrlower, sign_mask), add_i(vlower, sign_mask));
			auto retlower = sub_i(vlower, vrlower);
			auto retupper = add_i(load(upper), carry);
			return {to_ulong4(retupper), to_ulong4(retlower)};
		}
		fixed128x4& operator+=(const ulong4& value) { return *this = *this + value; }
		fixed128x4& operator+=(const fixed128x4& value) { return *this = *this + value; }
		fixed128x4& operator-=(const ulong4& value) { return *this = *this - value; }
		fixed128x4& operator-=(const fixed128x4& value) { return *this = *this - value; }
	};
}
