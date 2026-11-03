# Runs the experiment set against a Release build and writes CSVs to results/.
#
# Prerequisite: a Release build, e.g. from a "Developer PowerShell for VS":
#   cmake --preset x64-release; cmake --build --preset x64-release
#
# Usage:
#   .\scripts\run_experiments.ps1                 # full run (over an hour)
#   .\scripts\run_experiments.ps1 -Quick          # small sizes, for checking the pipeline
#   .\scripts\run_experiments.ps1 -BinDir <dir>   # bench binaries somewhere else (e.g. a GCC build)
#   .\scripts\run_experiments.ps1 -Groups B,C1,D,E  # only these groups
#
# Groups (output files are prefixed by group, and plot_results.py reads them all):
#   A1  timing + per-op latency, uniform, all key types, n = 1e3 .. 1e6
#   A2  timing + per-op latency, uniform, all key types, n = 3e6, 1e7 (one seed pair)
#   B   timing + per-op latency, sorted/reverse/sliding_window/zipf/zipf_shuffled/steady, int keys
#   C1  counters, cost histograms and memory, all workloads, int keys
#   C2  counters, cost histograms and memory, uniform, string keys
#   C3  memory only, uniform, all key types, n = 3e6, 1e7
#   D   seed variance (H4): uniform int, n = 1e3 .. 3e5, 5 input seeds x 20 algorithm seeds
#   D2  seed variance with 200 algorithm seeds and 1 input seed, for bootstrap intervals on the spread
#   E   distinct keys, cache lines and pages read per operation (probe keys): int-like keys on
#       uniform/sorted/reverse/zipf/zipf_shuffled/steady, large-string keys on uniform/sorted/reverse
#   G   allocator alone (dict_alloc_bench), with block sizes modelled on each structure's nodes
#   G2  as G (10 reps), with 4 or 16 cache-evicting reads before each allocation and free
#   H   heap layout: page gaps between allocations (int keys) and whether each string buffer shares its
#       node's page (large-string probe keys), uniform/sorted/reverse
#
# Every instrumented group (C*, D, D2, E, H) also records heap allocations per operation, their sizes
# and the page gap between consecutive allocations.
# D2, G, G2 and H are not in the default set; name them with -Groups.

param([switch]$Quick, [string]$BinDir, [string[]]$Groups = @('A1', 'A2', 'B', 'C1', 'C2', 'C3', 'D', 'E'))

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = if ($BinDir) { $BinDir } else { Join-Path $root 'out\build\x64-release\bench' }
$results = Join-Path $root 'results'
New-Item -ItemType Directory -Force $results | Out-Null

if ($Quick) {
    $mid = '1000,10000'; $big = '30000'; $seedsIn = 2; $seedsAlgo = 2; $reps = 2
    $varN = '1000,10000'; $varIn = 2; $varAlgo = 3; $manyAlgo = 12
} else {
    $mid = '1000,3000,10000,30000,100000,300000,1000000'; $big = '3000000,10000000'
    $seedsIn = 3; $seedsAlgo = 2; $reps = 3
    $varN = '1000,10000,100000,300000'; $varIn = 5; $varAlgo = 20; $manyAlgo = 200
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

if ($Groups -contains 'A1') {
    Invoke-Timing 'A1' @('--key', 'all', '--workload', 'uniform', '--n', $mid,
        '--input-seeds', $seedsIn, '--algo-seeds', $seedsAlgo, '--reps', $reps, '--warmup', 1)
}
if ($Groups -contains 'A2') {
    Invoke-Timing 'A2' @('--key', 'all', '--workload', 'uniform', '--n', $big,
        '--input-seeds', 1, '--algo-seeds', 1, '--reps', 2, '--warmup', 0)
}
if ($Groups -contains 'B') {
    Invoke-Timing 'B' @('--key', 'int', '--workload', 'sorted,reverse,sliding_window,zipf,zipf_shuffled,steady',
        '--n', $mid, '--input-seeds', 2, '--algo-seeds', 2, '--reps', $reps, '--warmup', 1)
}

if ($Groups -contains 'C1') {
    Invoke-Counts 'C1' @('--key', 'int', '--workload', 'all', '--n', $mid,
        '--input-seeds', 2, '--algo-seeds', 2, '--reps', 1)
}
if ($Groups -contains 'C2') {
    Invoke-Counts 'C2' @('--key', 'small_string,large_string', '--workload', 'uniform', '--n', $mid,
        '--input-seeds', 2, '--algo-seeds', 2, '--reps', 1)
}
if ($Groups -contains 'C3') {
    # --reps 0: skip the counted runs and only take the memory snapshot.
    Invoke-Counts 'C3' @('--key', 'all', '--workload', 'uniform', '--n', $big,
        '--input-seeds', 1, '--algo-seeds', 1, '--reps', 0)
}

if ($Groups -contains 'D') {
    Write-Host '== D (seed variance) =='
    Invoke-Bench 'dict_bench.exe' @('--key', 'int', '--workload', 'uniform', '--n', $varN,
        '--input-seeds', $varIn, '--algo-seeds', $varAlgo, '--reps', 1, '--warmup', 1, '--out', (Result-Path 'seeds_timing.csv'))
    Invoke-Bench 'dict_bench_instrumented.exe' @('--key', 'int', '--workload', 'uniform', '--n', $varN,
        '--input-seeds', $varIn, '--algo-seeds', $varAlgo, '--reps', 1, '--warmup', 0,
        '--out', (Result-Path 'seeds_counts.csv'), '--hist-out', (Result-Path 'seeds_histograms.csv'))
}

if ($Groups -contains 'D2') {
    # One input seed is enough: this measures the spread from the skip list's own seed. The red-black
    # tree is left out because it ignores the algorithm seed.
    Write-Host '== D2 (seed variance, many algorithm seeds) =='
    Invoke-Bench 'dict_bench.exe' @('--impl', 'skip_list', '--key', 'int', '--workload', 'uniform', '--n', $varN,
        '--input-seeds', 1, '--algo-seeds', $manyAlgo, '--reps', 1, '--warmup', 1,
        '--out', (Result-Path 'seeds200_timing.csv'))
    Invoke-Bench 'dict_bench_instrumented.exe' @('--impl', 'skip_list', '--key', 'int', '--workload', 'uniform',
        '--n', $varN, '--input-seeds', 1, '--algo-seeds', $manyAlgo, '--reps', 1, '--warmup', 0,
        '--out', (Result-Path 'seeds200_counts.csv'), '--hist-out', (Result-Path 'seeds200_histograms.csv'))
}

if ($Groups -contains 'E') {
    # Probe keys order like int (or large_string) keys, so the structures have the same shapes as in C1 (C2).
    Write-Host '== E (distinct keys, cache lines and pages read, probe keys) =='
    Invoke-Bench 'dict_bench_instrumented.exe' @('--key', 'probe',
        '--workload', 'uniform,sorted,reverse,zipf,zipf_shuffled,steady',
        '--n', $mid, '--input-seeds', 2, '--algo-seeds', 2, '--reps', 1, '--warmup', 0,
        '--out', (Result-Path 'counts_E.csv'), '--hist-out', (Result-Path 'histograms_E.csv'))
    Invoke-Bench 'dict_bench_instrumented.exe' @('--key', 'probe_large_string', '--workload', 'uniform,sorted,reverse',
        '--n', $mid, '--input-seeds', 2, '--algo-seeds', 2, '--reps', 1, '--warmup', 0,
        '--out', (Result-Path 'counts_E2.csv'), '--hist-out', (Result-Path 'histograms_E2.csv'))
}

if ($Groups -contains 'G') {
    Write-Host '== G (allocator alone) =='
    Invoke-Bench 'dict_alloc_bench.exe' @('--n', $mid, '--model', 'fixed:48,tower:32:8', '--reps', 5,
        '--out', (Result-Path 'alloc_G.csv'))
}

if ($Groups -contains 'G2') {
    Write-Host '== G2 (allocator after cache-evicting reads) =='
    foreach ($evict in 4, 16) {
        Invoke-Bench 'dict_alloc_bench.exe' @('--n', $mid, '--model', 'fixed:48,tower:32:8', '--reps', 10,
            '--evict', $evict, '--out', (Result-Path "alloc_G2_e$evict.csv"))
    }
}

if ($Groups -contains 'H') {
    Write-Host '== H (heap layout) =='
    Invoke-Bench 'dict_bench_instrumented.exe' @('--impl', 'red_black_tree,skip_list', '--key', 'int',
        '--workload', 'uniform,sorted,reverse', '--n', $mid, '--input-seeds', 1, '--algo-seeds', 1,
        '--reps', 1, '--warmup', 0, '--out', (Result-Path 'layout_counts_H.csv'), '--hist-out', (Result-Path 'layout_hist_H.csv'))
    Invoke-Bench 'dict_bench_instrumented.exe' @('--impl', 'red_black_tree,skip_list', '--key', 'probe_large_string',
        '--workload', 'uniform,sorted,reverse', '--n', $mid, '--input-seeds', 1, '--algo-seeds', 1,
        '--reps', 1, '--warmup', 0, '--out', (Result-Path 'layout_counts_H2.csv'), '--hist-out', (Result-Path 'layout_hist_H2.csv'))
}

Write-Host ("Results written to $results in {0:N1} min" -f ((Get-Date) - $started).TotalMinutes)
