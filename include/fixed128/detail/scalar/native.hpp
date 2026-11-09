#pragma once
#include "../../types.hpp"

#include <cstdint>
#include <cstring>

// Scalar backend: the same vector primitives as detail/avx2/native.hpp, emulated lane by lane in portable C++.
// Each primitive reproduces the exact result of the corresponding AVX2 instruction (including shift counts >= 64
// and byte-wise blends), so the conversions give bit-identical results on both backends.
// The lane loops are simple enough for compilers to auto-vectorize (SSE2, NEON).

namespace fx128::detail
{
	struct vi
	{
		uint64_t v[4];
	};

	struct vd
	{
		double v[4];
	};

	struct vf
	{
		float v[4];
	};

	inline vi load(const long4& a)
	{
		vi r;
		std::memcpy(r.v, &a, sizeof r.v);
		return r;
	}

	inline vi load(const ulong4& a)
	{
		vi r;
		std::memcpy(r.v, &a, sizeof r.v);
		return r;
	}

	inline vd load(const double4& a)
	{
		vd r;
		std::memcpy(r.v, &a, sizeof r.v);
		return r;
	}

	inline vf load(const float4& a)
	{
		vf r;
		std::memcpy(r.v, &a, sizeof r.v);
		return r;
	}

	inline long4 to_long4(vi v)
	{
		long4 r;
		std::memcpy(&r, v.v, sizeof v.v);
		return r;
	}

	inline ulong4 to_ulong4(vi v)
	{
		ulong4 r;
		std::memcpy(&r, v.v, sizeof v.v);
		return r;
	}

	inline double4 to_double4(vd v)
	{
		double4 r;
		std::memcpy(&r, v.v, sizeof v.v);
		return r;
	}

	inline float4 to_float4(vf v)
	{
		float4 r;
		std::memcpy(&r, v.v, sizeof v.v);
		return r;
	}

	inline uint64_t bits_of(double d)
	{
		uint64_t u;
		std::memcpy(&u, &d, sizeof u);
		return u;
	}

	inline double double_of(uint64_t u)
	{
		double d;
		std::memcpy(&d, &u, sizeof d);
		return d;
	}

	inline vi set1_i(int64_t a) { return {{static_cast<uint64_t>(a), static_cast<uint64_t>(a), static_cast<uint64_t>(a), static_cast<uint64_t>(a)}}; }
	inline vd set1_d(double a) { return {{a, a, a, a}}; }
	inline vf set1_f(float a) { return {{a, a, a, a}}; }
	inline vi zero_i() { return {{0, 0, 0, 0}}; }
	inline vd zero_d() { return {{0.0, 0.0, 0.0, 0.0}}; }

	inline vi as_i(vd a)
	{
		vi r;
		std::memcpy(r.v, a.v, sizeof r.v);
		return r;
	}

	inline vd as_d(vi a)
	{
		vd r;
		std::memcpy(r.v, a.v, sizeof r.v);
		return r;
	}

	// Applies a lane-wise function to 4 lanes.
	template <typename R, typename A, typename F>
	R map(const A& a, F f)
	{
		R r;
		for (int i = 0; i < 4; ++i) r.v[i] = f(a.v[i]);
		return r;
	}

	template <typename R, typename A, typename B, typename F>
	R map(const A& a, const B& b, F f)
	{
		R r;
		for (int i = 0; i < 4; ++i) r.v[i] = f(a.v[i], b.v[i]);
		return r;
	}

	// 64-bit integer lanes
	inline vi add_i(vi a, vi b) { return map<vi>(a, b, [](uint64_t x, uint64_t y) { return x + y; }); }
	inline vi sub_i(vi a, vi b) { return map<vi>(a, b, [](uint64_t x, uint64_t y) { return x - y; }); }
	inline vi and_i(vi a, vi b) { return map<vi>(a, b, [](uint64_t x, uint64_t y) { return x & y; }); }
	inline vi or_i(vi a, vi b) { return map<vi>(a, b, [](uint64_t x, uint64_t y) { return x | y; }); }
	inline vi xor_i(vi a, vi b) { return map<vi>(a, b, [](uint64_t x, uint64_t y) { return x ^ y; }); }
	inline vi slli(vi a, int n) { return map<vi>(a, [n](uint64_t x) { return n > 63 ? 0 : x << n; }); }
	inline vi srli(vi a, int n) { return map<vi>(a, [n](uint64_t x) { return n > 63 ? 0 : x >> n; }); }
	inline vi sllv(vi a, vi n) { return map<vi>(a, n, [](uint64_t x, uint64_t c) { return c > 63 ? 0 : x << c; }); }
	inline vi srlv(vi a, vi n) { return map<vi>(a, n, [](uint64_t x, uint64_t c) { return c > 63 ? 0 : x >> c; }); }
	inline vi cmpgt_i(vi a, vi b)
	{
		return map<vi>(a, b, [](uint64_t x, uint64_t y) { return static_cast<int64_t>(x) > static_cast<int64_t>(y) ? ~uint64_t{0} : 0; });
	}
	inline vi cmpeq_i(vi a, vi b) { return map<vi>(a, b, [](uint64_t x, uint64_t y) { return x == y ? ~uint64_t{0} : 0; }); }
	// Per byte: b where the top bit of the mask byte is set, otherwise a.
	inline vi blendv(vi a, vi b, vi mask)
	{
		vi r;
		for (int i = 0; i < 4; ++i) {
			const uint64_t sel = ((mask.v[i] >> 7) & 0x0101010101010101ull) * 0xFF;
			r.v[i] = (a.v[i] & ~sel) | (b.v[i] & sel);
		}
		return r;
	}
	// Per lane: the top 16 bits from b, the low 48 bits from a.
	inline vi blend_hi16(vi a, vi b)
	{
		return map<vi>(a, b, [](uint64_t x, uint64_t y) { return (x & 0x0000FFFFFFFFFFFFull) | (y & 0xFFFF000000000000ull); });
	}
	// Per lane: the low 32 bits from b, the high 32 bits from a.
	inline vi blend_lo32(vi a, vi b)
	{
		return map<vi>(a, b, [](uint64_t x, uint64_t y) { return (x & 0xFFFFFFFF00000000ull) | (y & 0x00000000FFFFFFFFull); });
	}

	// double lanes
	inline vd add_d(vd a, vd b) { return map<vd>(a, b, [](double x, double y) { return x + y; }); }
	inline vd sub_d(vd a, vd b) { return map<vd>(a, b, [](double x, double y) { return x - y; }); }
	inline vd mul_d(vd a, vd b) { return map<vd>(a, b, [](double x, double y) { return x * y; }); }
	inline vd cmplt_d(vd a, vd b) { return map<vd>(a, b, [](double x, double y) { return double_of(x < y ? ~uint64_t{0} : 0); }); }

	// float lanes
	inline vf mul_f(vf a, vf b) { return map<vf>(a, b, [](float x, float y) { return x * y; }); }
	inline vd cvt_f_to_d(vf a) { return map<vd>(a, [](float x) { return static_cast<double>(x); }); }
	inline vf cvt_d_to_f(vd a) { return map<vf>(a, [](double x) { return static_cast<float>(x); }); }
}
