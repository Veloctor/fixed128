#pragma once
#include <fixed128/fixed128.hpp>

namespace candidates
{
	// The scalar fixed128 class, one lane at a time: the plain-C++ baseline the SIMD conversions are measured against.
	// Its conversion from double always truncates; there is no unsigned scalar type.
	struct scalar_class
	{
		static constexpr const char* name = "scalar fixed128";
		static constexpr bool round_half_away = false;
		static constexpr unsigned max_ulp = 1;

		template <int F>
		static void double_to_fixed128(const fx128::double4& in, fx128::ulong4& upper, fx128::ulong4& lower)
		{
			const fx128::fixed128<F> x{in.x}, y{in.y}, z{in.z}, w{in.w};
			upper = {x.upper, y.upper, z.upper, w.upper};
			lower = {x.lower, y.lower, z.lower, w.lower};
		}

		template <int F>
		static fx128::double4 fixed128_to_double(const fx128::ulong4& upper, const fx128::ulong4& lower)
		{
			auto lane = [](uint64_t u, uint64_t l) {
				return static_cast<double>(fx128::fixed128<F>{static_cast<int64_t>(u), static_cast<int64_t>(l)});
			};
			return {lane(upper.x, lower.x), lane(upper.y, lower.y), lane(upper.z, lower.z), lane(upper.w, lower.w)};
		}
	};
}
