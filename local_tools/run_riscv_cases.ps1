param(
    [string]$Compiler = ".\compiler.exe",
    [string]$CasesRoot = ".\ascii_cases",
    [string]$OutRoot = ".\local_riscv_out",
    [string]$Distro = "Ubuntu",
    [int]$TimeoutSeconds = 20,
    [switch]$O1
)

$ErrorActionPreference = "Stop"

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Get-RelativePathCompat([string]$BasePath, [string]$Path) {
    $baseFull = [System.IO.Path]::GetFullPath($BasePath).TrimEnd([char[]]@('\', '/'))
    $pathFull = [System.IO.Path]::GetFullPath($Path)
    if ($pathFull.StartsWith($baseFull, [StringComparison]::OrdinalIgnoreCase)) {
        return $pathFull.Substring($baseFull.Length).TrimStart([char[]]@('\', '/'))
    }
    return [System.IO.Path]::GetFileName($pathFull)
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
$outRootPath = Get-FullPath $OutRoot
$runtimePath = Get-FullPath ".\local_tools\sysy_runtime.c"

if (!(Test-Path -LiteralPath $compilerPath)) {
    throw "compiler not found: $compilerPath"
}
if (!(Test-Path -LiteralPath $runtimePath)) {
    throw "runtime not found: $runtimePath"
}

New-Item -ItemType Directory -Force -Path $outRootPath | Out-Null

$cases = Get-ChildItem -Recurse -File -Path $casesRootPath -Filter "*.sy" | Sort-Object FullName
$results = New-Object System.Collections.Generic.List[object]
$runtimeWsl = ConvertTo-WslPath $runtimePath

foreach ($case in $cases) {
    $rel = Get-RelativePathCompat $casesRootPath $case.FullName
    $relDir = [System.IO.Path]::GetDirectoryName($rel)
    if ($null -eq $relDir) { $relDir = "" }
    $base = [System.IO.Path]::GetFileNameWithoutExtension($case.Name)
    $caseOutDir = Join-Path $outRootPath $relDir
    New-Item -ItemType Directory -Force -Path $caseOutDir | Out-Null

    $asm = Join-Path $caseOutDir "$base.s"
    $exe = Join-Path $caseOutDir "$base.riscv"
    $stdoutFile = Join-Path $caseOutDir "$base.stdout"
    $actualFile = Join-Path $caseOutDir "$base.actual"
    $errFile = Join-Path $caseOutDir "$base.err"
    $expectedFile = [System.IO.Path]::ChangeExtension($case.FullName, ".out")
    $inputFile = [System.IO.Path]::ChangeExtension($case.FullName, ".in")

    $compileArgs = @("-S", "-o", $asm, $case.FullName)
    if ($O1) {
        $compileArgs += "-O1"
    }

    $status = "AC"
    $elapsed = 0.0
    $message = ""
    $watch = [System.Diagnostics.Stopwatch]::StartNew()

    $compileOutput = & $compilerPath @compileArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        $status = "CE"
        $message = ($compileOutput -join "`n")
        Set-Content -LiteralPath $errFile -Value $message
    } else {
        $asmWsl = ConvertTo-WslPath $asm
        $exeWsl = ConvertTo-WslPath $exe
        $linkScript = "set -e`nriscv64-linux-gnu-gcc -static -no-pie -O2 -march=rv64gc -mabi=lp64d -mcmodel=medany $(Quote-Bash $asmWsl) $(Quote-Bash $runtimeWsl) -o $(Quote-Bash $exeWsl)"
        $link = Invoke-WslBash $linkScript
        if ($link.ExitCode -ne 0) {
            $status = "LINK"
            $message = $link.Output
            Set-Content -LiteralPath $errFile -Value $message
        } else {
            $stdoutWsl = ConvertTo-WslPath $stdoutFile
            $exeWslQ = Quote-Bash $exeWsl
            $stdoutWslQ = Quote-Bash $stdoutWsl
            if (Test-Path -LiteralPath $inputFile) {
                $inputWslQ = Quote-Bash (ConvertTo-WslPath $inputFile)
                $runScript = "set +e`ntimeout ${TimeoutSeconds}s qemu-riscv64 $exeWslQ < $inputWslQ > $stdoutWslQ`nrc=`$?`necho `$rc"
            } else {
                $runScript = "set +e`ntimeout ${TimeoutSeconds}s qemu-riscv64 $exeWslQ > $stdoutWslQ`nrc=`$?`necho `$rc"
            }
            $run = Invoke-WslBash $runScript
            if ($run.ExitCode -ne 0) {
                $status = "RUNNER"
                $message = $run.Output
                Set-Content -LiteralPath $errFile -Value $message
            } else {
                $rcText = ($run.Output.Trim() -split "`n")[-1].Trim()
                if ($rcText -eq "124") {
                    $status = "TLE"
                    $message = "timeout after ${TimeoutSeconds}s"
                } else {
                    $stdout = ""
                    if (Test-Path -LiteralPath $stdoutFile) {
                        $stdout = Get-Content -LiteralPath $stdoutFile -Raw
                    }
                    $stdout = Normalize-Text $stdout
                    if ($stdout.Length -gt 0 -and !$stdout.EndsWith("`n")) {
                        $stdout += "`n"
                    }
                    $actual = $stdout + $rcText + "`n"
                    Set-Content -LiteralPath $actualFile -NoNewline -Value $actual
                    $expected = Normalize-Text (Get-Content -LiteralPath $expectedFile -Raw)
                    if ($actual -ne $expected) {
                        $status = "WA"
                        $message = "output differs"
                    }
                }
            }
        }
    }

    $watch.Stop()
    $elapsed = [Math]::Round($watch.Elapsed.TotalSeconds, 3)
    $results.Add([pscustomobject]@{
        Case = $rel
        Status = $status
        Seconds = $elapsed
        Message = $message
    })
    "{0,-45} {1,-6} {2,8:N3}s {3}" -f $rel, $status, $elapsed, $message
}

$csvPath = Join-Path $outRootPath "summary.csv"
$results | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csvPath
$grouped = $results | Group-Object Status | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Count)" }
"summary: " + ($grouped -join ", ")
"csv: $csvPath"

$failed = @($results | Where-Object { $_.Status -ne "AC" })
if ($failed.Count -gt 0) {
    exit 1
}
