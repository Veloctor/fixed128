#pragma once
#include <fixed128/fixed128.hpp>

namespace candidates
{
	// The library's own conversions (the configured backend and rounding).
	struct library
	{
		static constexpr const char* name = "library";
		static constexpr bool round_half_away = FIXED128_ROUND_HALF_AWAY_FROM_ZERO != 0;
		static constexpr unsigned max_ulp = 1;

		template <int F>
		static void double_to_fixed128(const fx128::double4& in, fx128::ulong4& upper, fx128::ulong4& lower)
		{
			fx128::double_to_fixed128_full<F>(in, upper, lower);
		}

		template <int F>
		static void double_to_ufixed128(const fx128::double4& in, fx128::ulong4& upper, fx128::ulong4& lower)
		{
			fx128::double_to_ufixed128_full<F>(in, upper, lower);
		}

		template <int F>
		static fx128::double4 fixed128_to_double(const fx128::ulong4& upper, const fx128::ulong4& lower)
		{
			return fx128::fixed128_to_double<F>(upper, lower);
		}

		template <int F>
		static fx128::double4 ufixed128_to_double(const fx128::ulong4& upper, const fx128::ulong4& lower)
		{
			return fx128::ufixed128_to_double<F>(upper, lower);
		}
	};
}
