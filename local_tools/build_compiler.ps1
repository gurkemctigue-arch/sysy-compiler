param(
    [string]$Source = ".\compiler_main.cpp",
    [string]$Output = ".\compiler_from_source.exe",
    [string]$TempDir = "D:\work\SysYToolchain\tmp"
)

$ErrorActionPreference = "Stop"

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

$sourcePath = Get-FullPath $Source
$outputPath = Get-FullPath $Output

if (!(Test-Path -LiteralPath $sourcePath)) {
    throw "source not found: $sourcePath"
}

New-Item -ItemType Directory -Force -Path $TempDir | Out-Null
$env:TEMP = $TempDir
$env:TMP = $TempDir

g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ $Source -o $Output
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Get-Item -LiteralPath $outputPath | Select-Object FullName,Length,LastWriteTime
