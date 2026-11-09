#pragma once
#include <cstdint>

// Public 4-lane value types: plain structs with the same memory layout on every backend (x is lane 0).
// The backends load them into SIMD registers internally; when inlined, the compilers remove the copies.

namespace fx128
{
	struct alignas(32) double4
	{
		double x, y, z, w;
	};

	struct alignas(32) long4
	{
		int64_t x, y, z, w;
	};

	struct alignas(32) ulong4
	{
		uint64_t x, y, z, w;
	};

	struct alignas(16) float4
	{
		float x, y, z, w;
	};
}
