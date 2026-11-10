#pragma once

namespace fx128::detail
{
	// Exact 2^e as a compile-time constant (same value as std::pow(2.0, e), including under/overflow).
	constexpr double pow2(int e)
	{
		double r = 1.0;
		for (; e > 0; --e) r *= 2.0;
		for (; e < 0; ++e) r *= 0.5;
		return r;
	}
}
