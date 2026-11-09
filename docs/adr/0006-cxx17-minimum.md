# C++17 is the minimum standard

The library requires only C++17, although the author's toolchains support C++20, because the main planned consumer is a fork of Jolt Physics, which is built as C++17. Requiring C++20 would force the consumer to change its standard just to include these headers. Tests, benchmarks and examples may use newer features if needed; the headers under `include/` must stay C++17.
