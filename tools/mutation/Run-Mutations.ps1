<#
.SYNOPSIS
  Mutation testing: checks that the tests notice deliberate mistakes in the implementation.

.DESCRIPTION
  Creates a temporary git worktree of HEAD (the working copy is never touched), then for every mutation below:
  applies it, rebuilds the test executables, runs them, and restores the file. A mutant is
    killed     some test failed: good, the tests cover this mistake;
    SURVIVED   all tests passed: the tests do not notice this mistake (a coverage gap, or an equivalent mutant);
    no build   the mutated code does not compile (the mutation itself must be fixed).
  Every mutation must leave the code compiling and change behavior somewhere in the tested domain (a mutation that
  changes nothing observable, e.g. a result the library discards, is an "equivalent mutant" and survives by design).

  Needs cmake and ninja on PATH (e.g. a Visual Studio developer shell). Takes several minutes per mutation.
  Only committed code is mutated (the worktree is created from HEAD).

.EXAMPLE
  ./tools/mutation/Run-Mutations.ps1               # all mutations
  ./tools/mutation/Run-Mutations.ps1 -Only 1,7     # selected mutations (1-based)
#>
param(
	[int[]]$Only,
	[string]$WorkDir = (Join-Path ([IO.Path]::GetTempPath()) 'fixed128-mutation')
)

$ErrorActionPreference = 'Stop'

# file, occurrence (1-based) of the text in the file, text, replacement, description
$mutations = @(
	@('include/fixed128/convert.hpp', 1, 'ihigh = sub_i(ihigh, borrow);', 'ihigh = sub_i(ihigh, and_i(borrow, zero256));', 'double -> fixed128: no borrow from the lower into the upper half'),
	@('include/fixed128/convert.hpp', 1, 'lmsr = add_i(lmsr, one256);', 'lmsr = add_i(lmsr, zero256);', 'double -> ufixed128 rounding: no rounding increment'),
	@('include/fixed128/convert.hpp', 2, 'lmsr = add_i(lmsr, one256);', 'lmsr = add_i(lmsr, zero256);', 'double -> fixed128 rounding: no rounding increment'),
	@('include/fixed128/convert.hpp', 1, 'cmpgt_i(exp_lfrac, minus_one256)', 'cmpgt_i(exp_lfrac, zero256)', 'double -> ufixed128 rounding: shift by 0 takes the right-shift path'),
	@('include/fixed128/convert.hpp', 2, 'vi lexp_is_pos = cmpgt_i(exp_lfrac, zero256);', 'vi lexp_is_pos = cmpeq_i(exp_lfrac, zero256);', 'double -> fixed128 truncation: wrong shift direction select'),
	@('include/fixed128/convert.hpp', 1, 'constexpr double majik_d_lo = pow2(52 - FracBits);', 'constexpr double majik_d_lo = pow2(52 - FracBits) * 1.0000000000000002;', 'magic number of the low piece off by one ULP'),
	@('include/fixed128/convert.hpp', 1, 'v_hi = xor_i(v_hi, majik_i_his);', 'v_hi = or_i(v_hi, majik_i_his);', 'fixed128 -> double: OR instead of XOR on the high piece'),
	@('include/fixed128/convert.hpp', 1, 'vi losr48 = srli(ilow, 48);', 'vi losr48 = srli(ilow, 47);', 'ufixed128 -> double: wrong split of the middle piece'),
	@('include/fixed128/convert.hpp', 1, 'exp_enc = srli(exp_enc, 53);', 'exp_enc = srli(exp_enc, 52);', 'double -> int64: exponent extracted with the wrong shift'),
	@('include/fixed128/convert.hpp', 1, 'set1_i(0x4530000080100000)', 'set1_i(0x4530000080100001)', 'int64 -> double: magic number off by one'),
	@('include/fixed128/simd.hpp', 1, 'auto retupper = sub_i(add_i(load(upper), load(value.upper)), carry);', 'auto retupper = add_i(add_i(load(upper), load(value.upper)), carry);', 'fixed128x4 +: carry with the wrong sign'),
	@('include/fixed128/check.hpp', 1, 'else if (!(v >= lo && v < hi))', 'else if (!(v >= lo && v <= hi))', 'input check: upper limit accepted'),
	@('include/fixed128/scalar.hpp', 1, 'const uint64_t r = d - borrow;', 'const uint64_t r = d;', 'portable subborrow: borrow-in not subtracted'),
	@('include/fixed128/scalar.hpp', 1, 'c1 -= c0 > 0;', 'c1 -= 0;', 'scalar fixed128(double): no borrow when negating'),
	@('include/fixed128/detail/scalar/native.hpp', 1, 'return c > 63 ? 0 : x >> c;', 'return c > 64 ? 0 : x >> (c & 63);', 'scalar backend srlv: shift count 64 not zero'),
	@('include/fixed128/simd.hpp', 1, 'return to_float4(cvt_d_to_f(load(subtract_to_double(a, b))));', 'return to_float4(cvt_d_to_f(load(subtract_to_double(b, a))));', 'subtract_to_float: operands swapped'),
	@('include/fixed128/simd.hpp', 1, 'fixed128x4 operator-(const double4& value) const { return *this - fixed128x4{value}; }', 'fixed128x4 operator-(const double4& value) const { return *this + fixed128x4{value}; }', 'fixed128x4 - double4: adds instead')
)

function Invoke-Checked([scriptblock]$Command) {
	$output = & $Command 2>&1
	return @{ ok = ($LASTEXITCODE -eq 0); output = $output }
}

$repo = (git rev-parse --show-toplevel).Trim()
if (Test-Path $WorkDir) { git -C $repo worktree remove --force $WorkDir 2>$null; Remove-Item -Recurse -Force $WorkDir -ErrorAction SilentlyContinue }
git -C $repo worktree add --detach $WorkDir HEAD | Out-Null
try {
	$build = Join-Path $WorkDir 'build'
	$configure = Invoke-Checked { cmake -S $WorkDir -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFIXED128_BUILD_BENCHMARKS=OFF -DFIXED128_BUILD_EXAMPLES=OFF -DFIXED128_BUILD_VALIDATION=OFF }
	if (-not $configure.ok) { $configure.output; throw 'configure failed' }
	$baseline = Invoke-Checked { cmake --build $build }
	if (-not $baseline.ok) { $baseline.output | Select-Object -Last 20; throw 'baseline build failed' }
	$baselineTest = Invoke-Checked { ctest --test-dir $build }
	if (-not $baselineTest.ok) { $baselineTest.output | Select-Object -Last 20; throw 'baseline tests fail: fix them before mutation testing' }

	$results = for ($i = 0; $i -lt $mutations.Count; $i++) {
		if ($Only -and -not ($Only -contains ($i + 1))) { continue }
		$file, $occurrence, $text, $replacement, $description = $mutations[$i]
		$path = Join-Path $WorkDir $file
		$content = [IO.File]::ReadAllText($path)
		$index = -1
		for ($k = 0; $k -lt $occurrence; $k++) {
			$index = $content.IndexOf($text, $index + 1, [StringComparison]::Ordinal)
			if ($index -lt 0) { break }
		}
		if ($index -lt 0) {
			$status = 'not found'
		}
		else {
			[IO.File]::WriteAllText($path, $content.Substring(0, $index) + $replacement + $content.Substring($index + $text.Length))
			Write-Host "[$($i + 1)/$($mutations.Count)] $description ..."
			$b = Invoke-Checked { cmake --build $build }
			if (-not $b.ok) { $status = 'no build' }
			else {
				$t = Invoke-Checked { ctest --test-dir $build }
				$status = if ($t.ok) { 'SURVIVED' } else { 'killed' }
			}
			[IO.File]::WriteAllText($path, $content)
		}
		[pscustomobject]@{ '#' = $i + 1; status = $status; mutation = $description; file = $file }
	}
	$results | Format-Table -AutoSize -Wrap
	$survived = @($results | Where-Object { $_.status -ne 'killed' })
	if ($survived.Count) { Write-Warning "$($survived.Count) mutant(s) not killed" } else { 'all mutants killed' }
}
finally {
	git -C $repo worktree remove --force $WorkDir | Out-Null
}
