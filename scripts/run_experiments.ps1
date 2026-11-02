# Runs the experiment set against a Release build and writes CSVs to results/.
#
# Prerequisite: a Release build, e.g. from a "Developer PowerShell for VS":
#   cmake --preset x64-release; cmake --build --preset x64-release
#
# Usage:
#   .\scripts\run_experiments.ps1                 # full run (over an hour)
#   .\scripts\run_experiments.ps1 -Quick          # small sizes, for checking the pipeline
#   .\scripts\run_experiments.ps1 -BinDir <dir>   # bench binaries somewhere else (e.g. a GCC build)
#
# Groups (output files are prefixed by group, and plot_results.py reads them all):
#   A1  timing + per-op latency, uniform, all key types, n = 1e3 .. 1e6
#   A2  timing + per-op latency, uniform, all key types, n = 3e6, 1e7 (one seed pair)
#   B   timing + per-op latency, sorted/reverse/sliding_window/zipf, int keys
#   C1  counters, cost histograms and memory, all workloads, int keys
#   C2  counters, cost histograms and memory, uniform, string keys
#   C3  memory only, uniform, all key types, n = 3e6, 1e7
#   D   seed variance (H4): uniform int n = 1e5, 5 input seeds x 20 algorithm seeds

param([switch]$Quick, [string]$BinDir)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = if ($BinDir) { $BinDir } else { Join-Path $root 'out\build\x64-release\bench' }
$results = Join-Path $root 'results'
New-Item -ItemType Directory -Force $results | Out-Null

if ($Quick) {
    $mid = '1000,10000'; $big = '30000'; $seedsIn = 2; $seedsAlgo = 2; $reps = 2
    $varN = '10000'; $varIn = 2; $varAlgo = 3
} else {
    $mid = '1000,3000,10000,30000,100000,300000,1000000'; $big = '3000000,10000000'
    $seedsIn = 3; $seedsAlgo = 2; $reps = 3
    $varN = '100000'; $varIn = 5; $varAlgo = 20
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

function Result-Path([string]$name) { "`"$(Join-Path $results $name)`"" }

function Invoke-Timing([string]$group, [string[]]$benchArgs) {
    Write-Host "== $group (timing + latency) =="
    Invoke-Bench 'dict_bench.exe' ($benchArgs + @('--out', (Result-Path "timing_$group.csv"),
        '--latency-out', (Result-Path "latency_$group.csv"), '--latency-hist-out', (Result-Path "latency_hist_$group.csv")))
}

# Structural counts are deterministic for a given seed pair, so one rep suffices.
function Invoke-Counts([string]$group, [string[]]$benchArgs) {
    Write-Host "== $group (counters, histograms, memory) =="
    Invoke-Bench 'dict_bench_instrumented.exe' ($benchArgs + @('--warmup', 0, '--out', (Result-Path "counts_$group.csv"),
        '--hist-out', (Result-Path "histograms_$group.csv"), '--mem-out', (Result-Path "memory_$group.csv")))
}

$started = Get-Date

Invoke-Timing 'A1' @('--key', 'all', '--workload', 'uniform', '--n', $mid,
    '--input-seeds', $seedsIn, '--algo-seeds', $seedsAlgo, '--reps', $reps, '--warmup', 1)
Invoke-Timing 'A2' @('--key', 'all', '--workload', 'uniform', '--n', $big,
    '--input-seeds', 1, '--algo-seeds', 1, '--reps', 2, '--warmup', 0)
Invoke-Timing 'B' @('--key', 'int', '--workload', 'sorted,reverse,sliding_window,zipf', '--n', $mid,
    '--input-seeds', 2, '--algo-seeds', 2, '--reps', $reps, '--warmup', 1)

Invoke-Counts 'C1' @('--key', 'int', '--workload', 'all', '--n', $mid,
    '--input-seeds', 2, '--algo-seeds', 2, '--reps', 1)
Invoke-Counts 'C2' @('--key', 'small_string,large_string', '--workload', 'uniform', '--n', $mid,
    '--input-seeds', 2, '--algo-seeds', 2, '--reps', 1)
# --reps 0: skip the counted runs and only take the memory snapshot.
Invoke-Counts 'C3' @('--key', 'all', '--workload', 'uniform', '--n', $big,
    '--input-seeds', 1, '--algo-seeds', 1, '--reps', 0)

Write-Host '== D (seed variance) =='
Invoke-Bench 'dict_bench.exe' @('--key', 'int', '--workload', 'uniform', '--n', $varN,
    '--input-seeds', $varIn, '--algo-seeds', $varAlgo, '--reps', 1, '--warmup', 1, '--out', (Result-Path 'seeds_timing.csv'))
Invoke-Bench 'dict_bench_instrumented.exe' @('--key', 'int', '--workload', 'uniform', '--n', $varN,
    '--input-seeds', $varIn, '--algo-seeds', $varAlgo, '--reps', 1, '--warmup', 0,
    '--out', (Result-Path 'seeds_counts.csv'), '--hist-out', (Result-Path 'seeds_histograms.csv'))

Write-Host ("Results written to $results in {0:N1} min" -f ((Get-Date) - $started).TotalMinutes)
