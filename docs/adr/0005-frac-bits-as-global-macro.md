# Fractional bits stay a global macro for now

**Status: superseded by [ADR 0008](0008-frac-bits-as-template-parameter.md)** (the number of fractional bits is now a template parameter without default; the macro is gone).

The number of fractional bits is the macro `FIXED128_FRAC_BITS` (default 36) rather than a template parameter such as `fixed128<36>`, to keep the restructuring free of API changes. The cost is that one program can use only one scale, and translation units compiled with different values silently violate the ODR; the CMake target therefore propagates the value to every consumer, and the tests build one executable per configuration. Turning the scale into a template parameter is the intended future direction; the magic constants are already `constexpr` functions of the scale, which makes that change mechanical.
