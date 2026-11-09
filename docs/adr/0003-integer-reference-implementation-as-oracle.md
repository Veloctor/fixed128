# Exact integer reference implementation as the test oracle

Correctness is judged only against `tests/reference.hpp`, a pure-integer implementation that decomposes doubles into mantissa and exponent and computes exact results (double → fixed128 must match bit for bit; fixed128 → double must be within the documented 1 ULP of the correctly rounded value). The scalar `fixed128` type is *not* the oracle: it uses floating-point tricks (`fma` with 2^64) itself and is tested against the reference like everything else. Do not "simplify" the tests into comparing SIMD results with the scalar types.
