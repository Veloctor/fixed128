// fixed128 / fixed128x4 addition and subtraction against the reference implementation.
#include "mismatch_log.hpp"
#include "test_helpers.hpp"

using namespace fx128;

namespace
{
	fixed128 to_fixed128(ref::u128 x) { return fixed128{static_cast<int64_t>(x.hi), static_cast<int64_t>(x.lo)}; }
	ref::u128 to_ref(const fixed128& x) { return {x.upper, x.lower}; }

	// Operands that exercise carries/borrows between the two halves.
	ref::u128 random_operand(std::mt19937_64& rng)
	{
		switch (rng() % 4) {
		case 0: return {rng(), rng()};
		case 1: return {rng(), ~0ull - (rng() % 4)};
		case 2: return {rng(), rng() % 4};
		default: return {rng() % 2 ? 0 : ~0ull, rng()};
		}
	}

	void log_mismatch(mismatch_log& log, const char* op, ref::u128 a, ref::u128 b, ref::u128 got, ref::u128 expected)
	{
		log.add("%s: a=%016llx_%016llx b=%016llx_%016llx got=%016llx_%016llx expected=%016llx_%016llx", op,
			(unsigned long long)a.hi, (unsigned long long)a.lo, (unsigned long long)b.hi, (unsigned long long)b.lo,
			(unsigned long long)got.hi, (unsigned long long)got.lo, (unsigned long long)expected.hi, (unsigned long long)expected.lo);
	}
}

TEST_SUITE("arithmetic")
{
	TEST_CASE("fixed128 + - += -= wrap modulo 2^128")
	{
		mismatch_log log;
		std::mt19937_64 rng(kSeed);
		for (int i = 0; i < kRandomCount; ++i) {
			const ref::u128 a = random_operand(rng), b = random_operand(rng);
			const ref::u128 sum = ref::add(a, b), diff = ref::sub(a, b);
			if (to_ref(to_fixed128(a) + to_fixed128(b)) != sum) log_mismatch(log, "+", a, b, to_ref(to_fixed128(a) + to_fixed128(b)), sum);
			if (to_ref(to_fixed128(a) - to_fixed128(b)) != diff) log_mismatch(log, "-", a, b, to_ref(to_fixed128(a) - to_fixed128(b)), diff);
			fixed128 x = to_fixed128(a);
			x += to_fixed128(b);
			if (to_ref(x) != sum) log_mismatch(log, "+=", a, b, to_ref(x), sum);
			x = to_fixed128(a);
			x -= to_fixed128(b);
			if (to_ref(x) != diff) log_mismatch(log, "-=", a, b, to_ref(x), diff);
		}
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("fixed128 += fixed64 adds the raw bits to the lower half with carry")
	{
		mismatch_log log;
		std::mt19937_64 rng(kSeed);
		for (int i = 0; i < kRandomCount; ++i) {
			const ref::u128 a = random_operand(rng);
			fixed64 f{0.0};
			f.ubits = rng();
			fixed128 x = to_fixed128(a);
			x += f;
			const ref::u128 expected = ref::add(a, {0, f.ubits});
			if (to_ref(x) != expected) log_mismatch(log, "+=fixed64", a, {0, f.ubits}, to_ref(x), expected);
		}
		CHECK_NO_MISMATCH(log);
	}

	TEST_CASE("fixed128x4 operators match lane-wise 128-bit arithmetic")
	{
		mismatch_log log;
		std::mt19937_64 rng(kSeed);
		for (int i = 0; i < kRandomCount; i += 4) {
			ref::u128 a[4], b[4];
			lanes_i64 ah, al, bh, bl;
			for (int l = 0; l < 4; ++l) {
				a[l] = random_operand(rng);
				b[l] = random_operand(rng);
				ah[l] = a[l].hi, al[l] = a[l].lo, bh[l] = b[l].hi, bl[l] = b[l].lo;
			}
			const fixed128x4 va{from_lanes(ah), from_lanes(al)}, vb{from_lanes(bh), from_lanes(bl)};
			const fixed128x4 sum = va + vb, diff = va - vb;
			const fixed128x4 sum_lo = va + from_lanes(bl), diff_lo = va - from_lanes(bl);
			fixed128x4 acc = va;
			acc += vb;
			acc -= from_lanes(bl);
			for (int l = 0; l < 4; ++l) {
				const ref::u128 lo_only{0, bl[l]};
				struct { const char* op; const fixed128x4& v; ref::u128 expected; ref::u128 b; } cases[] = {
					{"x4 +", sum, ref::add(a[l], b[l]), b[l]},
					{"x4 -", diff, ref::sub(a[l], b[l]), b[l]},
					{"x4 + long4", sum_lo, ref::add(a[l], lo_only), lo_only},
					{"x4 - long4", diff_lo, ref::sub(a[l], lo_only), lo_only},
					{"x4 += -=", acc, ref::sub(ref::add(a[l], b[l]), lo_only), b[l]},
				};
				for (const auto& c : cases) {
					const ref::u128 got{to_lanes(c.v.upper)[l], to_lanes(c.v.lower)[l]};
					if (got != c.expected) log_mismatch(log, c.op, a[l], c.b, got, c.expected);
				}
			}
		}
		CHECK_NO_MISMATCH(log);
	}
}
