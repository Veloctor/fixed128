# AVX2 only, no runtime dispatch

**Status: superseded by [ADR 0007](0007-portable-backends-behind-pod-types.md)** (a portable scalar backend now exists; the "no runtime dispatch" part still holds).

The library has a single backend, AVX2, selected at compile time: the whole program is built with `/arch:AVX2` / `-mavx2` and `config.hpp` refuses to compile otherwise. There is no runtime CPU dispatch and no fallback for other architectures (ARM / Apple Silicon), because the purpose of the library is the fastest possible conversion on the main target, and dispatch would add a branch or indirect call around functions that take only a few nanoseconds. AVX2 code lives behind the x4 types so that a portable scalar or NEON backend can be added later with the same lane semantics; until then non-AVX2 platforms are unsupported.
