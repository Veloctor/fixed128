<#
.SYNOPSIS
  Compares two benchmark result files written by `fixed128_bench --json FILE`.

.DESCRIPTION
  For every benchmark present in both files (matched by title and name): time per unit before and after, and the
  ratio new / old. Rows whose change exceeds -Threshold (default 5 %) are marked; also printed when the two runs come
  from different CPUs, compilers or flags, because then the comparison measures the environment, not the code.
  nanobench's "median(elapsed)" is per iteration of the benchmarked code, i.e. per batch; it is divided by "batch" here.

.EXAMPLE
  ./tools/bench/Compare-Bench.ps1 before.json after.json
#>
param(
	[Parameter(Mandatory, Position = 0)][string]$Old,
	[Parameter(Mandatory, Position = 1)][string]$New,
	[double]$Threshold = 0.05
)

$ErrorActionPreference = 'Stop'
$a = Get-Content $Old -Raw | ConvertFrom-Json
$b = Get-Content $New -Raw | ConvertFrom-Json

foreach ($key in 'cpu', 'compiler', 'flags', 'backend') {
	if ($a.environment.$key -ne $b.environment.$key) {
		Write-Warning "$key differs: '$($a.environment.$key)' vs '$($b.environment.$key)'"
	}
}
"old: $($a.environment.commit)   new: $($b.environment.commit)"

function Get-Rows($file) {
	$rows = @{}
	foreach ($bench in $file.benches) {
		foreach ($r in $bench.results) {
			$rows["$($r.title) | $($r.name)"] = [pscustomobject]@{
				unit = $r.unit
				ns   = $r.'median(elapsed)' * 1e9 / $r.batch
				err  = $r.'medianAbsolutePercentError(elapsed)'
			}
		}
	}
	$rows
}

$ra = Get-Rows $a
$rb = Get-Rows $b
$ra.Keys | Where-Object { $rb.ContainsKey($_) } | Sort-Object | ForEach-Object {
	$o = $ra[$_]; $n = $rb[$_]
	$ratio = $n.ns / $o.ns
	[pscustomobject]@{
		benchmark = $_
		unit      = $o.unit
		'old ns'  = [math]::Round($o.ns, 3)
		'new ns'  = [math]::Round($n.ns, 3)
		'new/old' = [math]::Round($ratio, 3)
		change    = if ($ratio -gt 1 + $Threshold) { 'SLOWER' } elseif ($ratio -lt 1 - $Threshold) { 'faster' } else { '' }
	}
} | Format-Table -AutoSize -Wrap
