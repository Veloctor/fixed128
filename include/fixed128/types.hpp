#pragma once
#include <immintrin.h>

namespace fx128
{
	using double4 = __m256d;
	using float4 = __m128;
	using long4 = __m256i;
	using ulong4 = __m256i;
	using int4 = __m128i;
	using uint4 = __m128i;
	using int8 = __m256i;
	using uint8 = __m256i;
}
