# Runs the red-black tree vs skip list comparison (bench/compare_main.cpp)
# against the Release build and writes CSVs to results/.
#
# Prerequisite (from a "Developer PowerShell for VS"):
#   cmake --preset x64-release; cmake --build --preset x64-release
#
# For stable numbers: plug the laptop in, use the "Best performance" power mode,
# and close other programs. The largest sizes need roughly 3 GB of free RAM.
#
# Usage:
#   .\scripts\run_comparison.ps1            # full run: n = 2^10..2^23, all key types
#   .\scripts\run_comparison.ps1 -Quick     # n = 2^10..2^14, one seed, for checking the pipeline

param([switch]$Quick)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'out\build\x64-release\bench\dict_compare.exe'
$results = Join-Path $root 'results'
New-Item -ItemType Directory -Force $results | Out-Null

if ($Quick) {
    $benchArgs = @('--max-exp', 14, '--seeds', 1, '--reps', 2)
} else {
    $benchArgs = @('--min-exp', 10, '--max-exp', 23, '--seeds', 3, '--reps', 3, '--latency-reps', 1)
}
$benchArgs += @('--out', "`"$results\compare.csv`"",
                '--lat-out', "`"$results\compare_latency.csv`"",
                '--hist-out', "`"$results\compare_hist.csv`"")

# High priority, pinned to core 2 (affinity mask 0x4) to reduce scheduler noise
# and core migration.
$p = Start-Process -FilePath $exe -ArgumentList $benchArgs -NoNewWindow -PassThru
$null = $p.Handle  # Windows PowerShell 5.1 only reports ExitCode if the handle was read
$p.PriorityClass = 'High'
$p.ProcessorAffinity = [IntPtr]0x4
$p.WaitForExit()
if ($p.ExitCode -ne 0) { throw "dict_compare failed (exit $($p.ExitCode))" }

Write-Host "Results written to $results"
