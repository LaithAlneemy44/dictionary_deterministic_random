# Runs the standard experiment set against the Release build and writes CSVs to results/.
#
# Prerequisite (from a "Developer PowerShell for VS"):
#   cmake --preset x64-release; cmake --build --preset x64-release
#
# Usage:
#   .\scripts\run_experiments.ps1            # full run
#   .\scripts\run_experiments.ps1 -Quick     # small sizes, for checking the pipeline

param([switch]$Quick)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'out\build\x64-release\bench'
$results = Join-Path $root 'results'
New-Item -ItemType Directory -Force $results | Out-Null

if ($Quick) {
    $sizes = '1000,10000'; $inputSeeds = 2; $algoSeeds = 3; $reps = 2
} else {
    $sizes = '1000,10000,100000,1000000,10000000'; $inputSeeds = 5; $algoSeeds = 20; $reps = 5
}

# Runs a benchmark binary at High priority pinned to core 2 (affinity mask 0x4)
# to reduce scheduler noise and core migration.
function Invoke-Bench([string]$exe, [string[]]$benchArgs) {
    $p = Start-Process -FilePath (Join-Path $bin $exe) -ArgumentList $benchArgs -NoNewWindow -PassThru
    $null = $p.Handle  # Windows PowerShell 5.1 only reports ExitCode if the handle was read
    $p.PriorityClass = 'High'
    $p.ProcessorAffinity = [IntPtr]0x4
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) { throw "$exe failed (exit $($p.ExitCode))" }
}

$common = @('--workload', 'all', '--n', $sizes, '--input-seeds', $inputSeeds, '--algo-seeds', $algoSeeds)

Write-Host '== timing build =='
Invoke-Bench 'dict_bench.exe' ($common + @('--reps', $reps, '--out', "`"$results\timing.csv`""))

Write-Host '== instrumented build =='
# Structural counts are deterministic for a given seed pair, so one rep suffices.
Invoke-Bench 'dict_bench_instrumented.exe' ($common + @('--reps', 1, '--warmup', 0,
    '--out', "`"$results\counts.csv`"", '--hist-out', "`"$results\histograms.csv`""))

Write-Host "Results written to $results"
