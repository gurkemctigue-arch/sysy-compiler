param(
    [string]$Compiler = ".\compiler_from_source.exe",
    [string]$CasesRoot = ".\perf_cases",
    [string]$BuildRoot = ".\local_expected_build",
    [string]$Distro = "Ubuntu",
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = "Stop"

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function ConvertTo-WslPath([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $drive = $full.Substring(0, 1).ToLowerInvariant()
    $rest = $full.Substring(2).Replace('\', '/')
    return "/mnt/$drive$rest"
}

function Quote-Bash([string]$Text) {
    return "'" + $Text.Replace("'", "'\''") + "'"
}

function Invoke-WslBash([string]$Script) {
    $b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Script))
    $oldErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & wsl -d $Distro -u root -- bash -lc "printf %s $b64 | base64 -d | bash" 2>&1
    } finally {
        $ErrorActionPreference = $oldErrorAction
    }
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output = ($output -join "`n")
    }
}

function Normalize-Text([string]$Text) {
    return $Text.Replace("`r`n", "`n").Replace("`r", "`n")
}

$compilerPath = Get-FullPath $Compiler
$casesRootPath = Get-FullPath $CasesRoot
$buildRootPath = Get-FullPath $BuildRoot
$runtimePath = Get-FullPath ".\local_tools\sysy_runtime.c"
$runtimeWsl = ConvertTo-WslPath $runtimePath

New-Item -ItemType Directory -Force -Path $buildRootPath | Out-Null

$cases = Get-ChildItem -Recurse -File -Path $casesRootPath -Filter "*.sy" | Sort-Object FullName
foreach ($case in $cases) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($case.Name)
    $asm = Join-Path $buildRootPath "$base.s"
    $exe = Join-Path $buildRootPath "$base.riscv"
    $stdoutFile = Join-Path $buildRootPath "$base.stdout"
    $outFile = [System.IO.Path]::ChangeExtension($case.FullName, ".out")
    $inputFile = [System.IO.Path]::ChangeExtension($case.FullName, ".in")

    & $compilerPath -S -o $asm $case.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "compiler failed for $($case.FullName)"
    }

    $linkScript = "set -e`nriscv64-linux-gnu-gcc -static -no-pie -O2 -march=rv64gc -mabi=lp64d -mcmodel=medany $(Quote-Bash (ConvertTo-WslPath $asm)) $(Quote-Bash $runtimeWsl) -o $(Quote-Bash (ConvertTo-WslPath $exe))"
    $link = Invoke-WslBash $linkScript
    if ($link.ExitCode -ne 0) {
        throw "link failed for $($case.FullName): $($link.Output)"
    }

    $exeQ = Quote-Bash (ConvertTo-WslPath $exe)
    $stdoutQ = Quote-Bash (ConvertTo-WslPath $stdoutFile)
    if (Test-Path -LiteralPath $inputFile) {
        $inputQ = Quote-Bash (ConvertTo-WslPath $inputFile)
        $runScript = "set +e`ntimeout ${TimeoutSeconds}s qemu-riscv64 $exeQ < $inputQ > $stdoutQ`nrc=`$?`necho `$rc"
    } else {
        $runScript = "set +e`ntimeout ${TimeoutSeconds}s qemu-riscv64 $exeQ > $stdoutQ`nrc=`$?`necho `$rc"
    }
    $run = Invoke-WslBash $runScript
    if ($run.ExitCode -ne 0) {
        throw "runner failed for $($case.FullName): $($run.Output)"
    }
    $rc = ($run.Output.Trim() -split "`n")[-1].Trim()
    if ($rc -eq "124") {
        throw "timeout for $($case.FullName)"
    }

    $stdout = ""
    if (Test-Path -LiteralPath $stdoutFile) {
        $stdout = Normalize-Text (Get-Content -LiteralPath $stdoutFile -Raw)
    }
    if ($stdout.Length -gt 0 -and !$stdout.EndsWith("`n")) {
        $stdout += "`n"
    }
    Set-Content -LiteralPath $outFile -NoNewline -Value ($stdout + $rc + "`n")
    "generated $outFile"
}
