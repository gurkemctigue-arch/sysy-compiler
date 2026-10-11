param(
    [string]$Source = ".\compiler_main.cpp",
    [string]$Case = ".\ascii_cases\functional\86_long_code2.sy",
    [string]$OutputDir = ".\build\gprof",
    [string]$TempDir = "D:\work\SysYToolchain\tmp",
    [switch]$OpenReport
)

$ErrorActionPreference = "Stop"

function Get-FullPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

$sourcePath = Get-FullPath $Source
$casePath = Get-FullPath $Case
$outputPath = Get-FullPath $OutputDir
$reportPath = Join-Path $outputPath "hotspots-gprof.txt"
$gxx = "D:\mingw-64\mingw64\bin\g++.exe"
$gprof = "D:\mingw-64\mingw64\bin\gprof.exe"

if (!(Test-Path -LiteralPath $sourcePath)) {
    throw "Compiler source not found: $sourcePath"
}
if (!(Test-Path -LiteralPath $casePath)) {
    throw "SysY case not found: $casePath"
}
if (!(Test-Path -LiteralPath $gxx) -or !(Test-Path -LiteralPath $gprof)) {
    throw "MinGW g++/gprof not found under D:\mingw-64\mingw64\bin"
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
New-Item -ItemType Directory -Force -Path $TempDir | Out-Null
$env:TEMP = $TempDir
$env:TMP = $TempDir

Write-Host "[1/3] Building instrumented Windows compiler..."
Push-Location $outputPath
try {
    & $gxx -std=c++17 -O2 -g -pg -fno-omit-frame-pointer -static -static-libgcc -static-libstdc++ `
        "-Wl,--disable-dynamicbase" "-Wl,--disable-high-entropy-va" `
        $sourcePath -o ".\compiler-gprof.exe"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "[2/3] Running $([System.IO.Path]::GetFileName($casePath))..."
    Remove-Item -LiteralPath ".\gmon.out" -ErrorAction SilentlyContinue
    & ".\compiler-gprof.exe" -S -o ".\profile-output.s" $casePath -O1
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "[3/3] Writing gprof hotspot report..."
    $gprofOutput = & $gprof -b ".\compiler-gprof.exe" ".\gmon.out"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    @(
        "BiSheng compiler gprof call-count report"
        "Note: MinGW gprof uses a 10 ms timer; use WSL perf reports for CPU-time ranking."
        ""
    ) + $gprofOutput | Set-Content -LiteralPath $reportPath -Encoding UTF8
} finally {
    Pop-Location
}

Write-Host "Hotspot report: $reportPath"
Get-Content -LiteralPath $reportPath | Select-Object -First 35

if ($OpenReport) {
    $code = Get-Command code -ErrorAction SilentlyContinue
    if ($code) {
        & $code.Source --reuse-window $reportPath | Out-Null
    } else {
        Start-Process $reportPath
    }
}
