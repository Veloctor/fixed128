# Vendored test and benchmark dependencies

doctest and nanobench are committed as single headers in `third_party/` (with their licenses) instead of being fetched by CMake (FetchContent, vcpkg, Conan). The author wants a fresh clone to build its tests and benchmarks without network access. Both are single-header MIT libraries, so vendoring costs only a manual update when upgrading; the library itself has no dependencies and consumers never see these headers.
