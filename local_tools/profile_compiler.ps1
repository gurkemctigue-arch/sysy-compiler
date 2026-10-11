param(
    [string]$Source = ".\compiler_main.cpp",
    [string]$Case = ".\ascii_cases\functional\86_long_code2.sy",
    [ValidateRange(1, 10000)]
    [int]$Repeat = 200,
    [ValidateRange(99, 4000)]
    [int]$Frequency = 999,
    [string]$OutputDir = ".\build\profile",
    [string]$Distro = "Ubuntu",
    [switch]$OpenReport
)

$ErrorActionPreference = "Stop"

function Get-FullPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function ConvertTo-WslPath([string]$Path) {
    $full = Get-FullPath $Path
    $drive = $full.Substring(0, 1).ToLowerInvariant()
    $rest = $full.Substring(2).Replace('\', '/')
    return "/mnt/$drive$rest"
}

function Quote-Bash([string]$Text) {
    return "'" + $Text.Replace("'", "'\''") + "'"
}

function Invoke-WslScript([string]$Script) {
    $encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Script))
    $oldErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & wsl.exe -d $Distro -u root -- bash -lc "printf %s $encoded | base64 -d | bash" 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldErrorAction
    }
    if ($exitCode -ne 0) {
        throw "WSL profiling failed (exit $exitCode):`n$($output -join "`n")"
    }
    return $output
}

$sourcePath = Get-FullPath $Source
$casePath = Get-FullPath $Case
$outputPath = Get-FullPath $OutputDir

if (!(Test-Path -LiteralPath $sourcePath)) {
    throw "Compiler source not found: $sourcePath"
}
if (!(Test-Path -LiteralPath $casePath)) {
    throw "SysY case not found: $casePath"
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$sourceWsl = Quote-Bash (ConvertTo-WslPath $sourcePath)
$caseWsl = Quote-Bash (ConvertTo-WslPath $casePath)
$outputWsl = Quote-Bash (ConvertTo-WslPath $outputPath)
$caseName = [System.IO.Path]::GetFileName($casePath)

$script = @"
set -euo pipefail

source_path=$sourceWsl
case_path=$caseWsl
output_dir=$outputWsl
binary="`$output_dir/compiler-perf"
perf_data="`$output_dir/perf.data"
perf_script="`$output_dir/perf.script"
folded="`$output_dir/stacks.folded"
flamegraph="`$output_dir/flamegraph.svg"
self_report="`$output_dir/hotspots-self.txt"
inclusive_report="`$output_dir/hotspots-inclusive.txt"

command -v g++ >/dev/null || { echo "g++ is not installed in WSL" >&2; exit 2; }
command -v perf >/dev/null || { echo "perf is not installed in WSL" >&2; exit 2; }
test -x /opt/FlameGraph/stackcollapse-perf.pl || { echo "/opt/FlameGraph is not installed" >&2; exit 2; }

if [[ ! -x "`$binary" || "`$source_path" -nt "`$binary" ]]; then
    echo "[1/5] Building optimized compiler with symbols..."
    g++ -std=c++17 -O2 -g -fno-omit-frame-pointer "`$source_path" -o "`$binary"
else
    echo "[1/5] Reusing the up-to-date profiling build."
fi

echo "[2/5] Sampling $caseName ($Repeat runs at $Frequency Hz)..."
rm -f "`$perf_data" "`$perf_script" "`$folded" "`$flamegraph" "`$self_report" "`$inclusive_report"
perf record -q -e task-clock:u -F $Frequency -g --call-graph dwarf,8192 --compression-level=1 -o "`$perf_data" -- \
    bash -c 'binary="`$1"; case_path="`$2"; repeat="`$3"; for ((i=0; i<repeat; i++)); do "`$binary" -S -o /tmp/bisheng-profile-output.s "`$case_path" -O1; done' \
    bash "`$binary" "`$case_path" $Repeat

echo "[3/5] Resolving sampled call stacks..."
perf script -i "`$perf_data" --no-inline --comms compiler-perf > "`$perf_script"
/opt/FlameGraph/stackcollapse-perf.pl "`$perf_script" > "`$folded"

echo "[4/5] Rendering SVG flame graph..."
/opt/FlameGraph/flamegraph.pl \
    --title "BiSheng Compiler CPU Flame Graph" \
    --subtitle "$caseName, $Repeat runs, $Frequency Hz" \
    --width 1600 --minwidth 0.5 \
    "`$folded" > "`$flamegraph"

echo "[5/5] Writing hotspot reports..."
{
    echo "BiSheng compiler hotspots by self CPU time"
    echo "Case: $caseName | Runs: $Repeat | Frequency: $Frequency Hz"
    echo
    perf report -i "`$perf_data" --stdio --no-children --call-graph none --no-inline \
        --percent-limit 0.1 --dsos compiler-perf --sort overhead,symbol
} > "`$self_report"
{
    echo "BiSheng compiler hotspots by inclusive CPU time"
    echo "Case: $caseName | Runs: $Repeat | Frequency: $Frequency Hz"
    echo
    perf report -i "`$perf_data" --stdio --call-graph none --no-inline \
        --percent-limit 0.1 --dsos compiler-perf --sort overhead,symbol
} > "`$inclusive_report"
"@

Invoke-WslScript $script | ForEach-Object { Write-Host $_ }

$flamegraphPath = Join-Path $outputPath "flamegraph.svg"
$selfReportPath = Join-Path $outputPath "hotspots-self.txt"
$inclusiveReportPath = Join-Path $outputPath "hotspots-inclusive.txt"

Write-Host ""
Write-Host "Top self-time hotspots:"
Get-Content -LiteralPath $selfReportPath | Select-Object -First 35
Write-Host ""
Write-Host "Flame graph:       $flamegraphPath"
Write-Host "Self-time report:  $selfReportPath"
Write-Host "Inclusive report:  $inclusiveReportPath"

if ($OpenReport) {
    $code = Get-Command code -ErrorAction SilentlyContinue
    if ($code) {
        & $code.Source --reuse-window $flamegraphPath | Out-Null
        & $code.Source --reuse-window $selfReportPath | Out-Null
    } else {
        Start-Process $flamegraphPath
    }
}
