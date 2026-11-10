<#
.SYNOPSIS
  Looks up latency / throughput / ports of x86 instructions in the uops.info table (data/uops.tsv).

.EXAMPLE
  ./tools/uops/Find-Uops.ps1 VPSRLVQ                       # all operand forms, default microarchitectures
  ./tools/uops/Find-Uops.ps1 'VPBLENDVB \(YMM' -Arch ICL,ZEN4
  ./tools/uops/Find-Uops.ps1 VPSRLVQ,VPSLLVQ,VPBLENDVB -Ymm  # several mnemonics, 256-bit register forms only
  ./tools/uops/Find-Uops.ps1 VCVTQQ2PD -Arch all

.NOTES
  -Pattern is a regex matched against the start of the instruction string, e.g. "VPSRLVQ (YMM, YMM, YMM)";
  a bare mnemonic matches all its operand forms. Run Convert-UopsXml.ps1 once to create data/uops.tsv.
  Columns: tp = reciprocal throughput (cycles), lat = latency (cycles, min..max over operand pairs).
#>
param(
	[Parameter(Mandatory, Position = 0)][string[]]$Pattern,
	# uops.info abbreviations; "all" for every microarchitecture
	[string[]]$Arch = @('SKL', 'ICL', 'ADL-P', 'ZEN3', 'ZEN4', 'ZEN5'),
	# only register forms with 256-bit operands (no memory operands)
	[switch]$Ymm,
	[string]$Table = "$PSScriptRoot/data/uops.tsv"
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Table)) { throw "Not found: $Table. Run Convert-UopsXml.ps1 first." }

$regex = '^(' + (($Pattern | ForEach-Object { if ($_ -match '^[A-Z0-9]+$') { "$_ " } else { $_ } }) -join '|') + ')'
$allArch = $Arch -contains 'all'

Get-Content $Table | Select-Object -Skip 1 | Where-Object { $_ -match $regex } | ForEach-Object {
	$c = $_ -split "`t"
	[pscustomobject]@{ instr = $c[0]; arch = $c[1]; uops = $c[2]; ports = $c[3]; tp = $c[4]; lat = $c[5]; lat_detail = $c[6] }
} | Where-Object {
	($allArch -or $Arch -contains $_.arch) -and (-not $Ymm -or ($_.instr -match 'YMM' -and $_.instr -notmatch '\bM\d+'))
} | Format-Table -AutoSize instr, arch, uops, ports, tp, lat, lat_detail
