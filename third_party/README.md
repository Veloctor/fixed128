# Vendored dependencies

Used only by the tests and benchmarks, never by the library headers. Committed so that a fresh clone builds offline (see docs/adr/0002).

| Library | Version | Source | License |
|---|---|---|---|
| doctest | 2.5.3 | https://github.com/doctest/doctest/blob/v2.5.3/doctest/doctest.h | MIT (`doctest/LICENSE.txt`) |
| nanobench | 4.6.0 | https://github.com/martinus/nanobench/blob/v4.6.0/src/include/nanobench.h | MIT (`nanobench/LICENSE`) |

To upgrade, replace the header and license file with the new release's and update this table.
