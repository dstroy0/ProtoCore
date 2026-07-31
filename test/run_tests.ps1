#Requires -Version 7
<#
.SYNOPSIS
    Build, run, and document all PlatformIO native test suites.

.DESCRIPTION
    Runs the native test suites, parses every test result, extracts per-test
    descriptions from source-file comments, and writes docs/TEST_REPORT.md.

.EXAMPLE
    # From project root:
    .\test\run_tests.ps1

    # From the test/ directory:
    .\run_tests.ps1
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ── Paths ─────────────────────────────────────────────────────────────────────

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = if ((Split-Path -Leaf $ScriptDir) -eq 'test') {
    Split-Path -Parent $ScriptDir
}
else { $ScriptDir }
$ReportPath = Join-Path (Join-Path $ProjectRoot 'docs') 'TEST_REPORT.md'

# ── Find pio ──────────────────────────────────────────────────────────────────

function Find-Pio {
    $inPath = Get-Command pio -ErrorAction SilentlyContinue
    if ($inPath) { return $inPath.Source }
    foreach ($pattern in @(
            "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe",
            "$env:LOCALAPPDATA\Programs\Python\Python*\Scripts\pio.exe"
        )) {
        $hit = Get-Item $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

$PioExe = Find-Pio
if (-not $PioExe) {
    Write-Error 'pio not found. Install PlatformIO Core or add it to PATH.'
    exit 1
}

Write-Host "pio     : $PioExe"
Write-Host "project : $ProjectRoot"
Write-Host "report  : $ReportPath"
Write-Host ''

# ── Run tests ─────────────────────────────────────────────────────────────────

Write-Host 'Running tests...'
Write-Host ''

Push-Location $ProjectRoot
$T0 = Get-Date
# Disable ErrorActionPreference locally so pio failure doesn't throw
$prev_ea = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$RawLines = & $PioExe test -e native -e native_app -e native_ssh -e native_ssh_hardened -e native_ssh_conn -e native_compliance 2>&1
$PioExit = $LASTEXITCODE
$ErrorActionPreference = $prev_ea
$T1 = Get-Date
Pop-Location

$WallSec = [Math]::Round(($T1 - $T0).TotalSeconds, 2)

# Format output lines
$Width = 80
$FormattedLines = $RawLines | ForEach-Object {
    $line = $_.ToString().TrimEnd()
    $cleanLine = $line -replace '\x1b\[[0-9;?]*[a-zA-Z]', ''

    # Test line: "test\test_X\test_X.cpp:829: test_name  [PASSED]"
    if ($cleanLine -match '(\S+\.cpp:\d+:\s+\S+)\s+\[(PASSED|FAILED)\]$') {
        $left = $Matches[1]
        $status = "[$($Matches[2])]"

        $cleanLeft = $left -replace '\x1b\[[0-9;?]*[a-zA-Z]', ''
        $pad = $Width - $cleanLeft.Length - $status.Length
        if ($pad -lt 1) { $pad = 1 }
        $spaces = ' ' * $pad

        $origLeft = $line -replace '\s+\[(PASSED|FAILED)\](\x1b\[[0-9;?]*[a-zA-Z])*$', ''
        $origRight = $line.Substring($origLeft.Length).Trim()

        $origLeft + $spaces + $origRight
    }
    # Banner line: "------------ native_ssh:test_ssh_crypto [PASSED] Took 6.70 seconds ------------"
    elseif ($cleanLine -match '^-+\s+(\S+.*\[(?:PASSED|FAILED)\]\s+Took\s+\S+\s+seconds)\s+-+$') {
        $msg = $Matches[1]
        $cleanMsg = $msg -replace '\x1b\[[0-9;?]*[a-zA-Z]', ''
        $padTotal = $Width - $cleanMsg.Length - 2
        if ($padTotal -ge 2) {
            $padLeft = [Math]::Floor($padTotal / 2)
            $padRight = $padTotal - $padLeft
            ('-' * $padLeft) + ' ' + $msg + ' ' + ('-' * $padRight)
        }
        else {
            $line
        }
    }
    # Section line: "--------------------------------------------------------------------------------"
    elseif ($cleanLine -match '^-{20,}$') {
        '-' * $Width
    }
    else {
        $line
    }
}

# Echo formatted output
$FormattedLines | Write-Host

# Strip ANSI escape sequences for parsing
$Lines = $FormattedLines | ForEach-Object {
    ($_ | Out-String).TrimEnd() -replace '\x1b\[[0-9;?]*[a-zA-Z]', ''
}

# ── Parse results ─────────────────────────────────────────────────────────────

$Tests = [System.Collections.Generic.List[hashtable]]::new()
$Suites = [ordered]@{}   # "env:suite" -> hashtable
$CurEnv = 'native'
$CurSuite = ''

foreach ($line in $Lines) {
    # "Processing test_X in native environment"
    if ($line -match '^Processing\s+(\S+)\s+in\s+(\S+)\s+environment') {
        $CurSuite = $Matches[1]; $CurEnv = $Matches[2]
    }

    # "test\test_X\test_X.cpp:829: test_name  [PASSED]"
    if ($line -match '(\S+\.cpp):(\d+):\s+(\S+)\s+\[(PASSED|FAILED)\]') {
        $rel = $Matches[1] -replace '\\', [IO.Path]::DirectorySeparatorChar
        $suite = [IO.Path]::GetFileNameWithoutExtension($rel)
        $Tests.Add(@{
                Env    = $CurEnv
                Suite  = $suite
                File   = $rel
                Line   = [int]$Matches[2]
                Name   = $Matches[3]
                Status = $Matches[4]
            })
    }

    # "native   test_http_parser   PASSED   00:00:02.07"
    if ($line -match '^\s*(native\S*)\s+(test_\S+)\s+(PASSED|FAILED)\s+(\S+)') {
        $key = "$($Matches[1]):$($Matches[2])"
        if (-not $Suites.Contains($key)) {
            $Suites[$key] = @{
                Env      = $Matches[1]
                Suite    = $Matches[2]
                Status   = $Matches[3]
                Duration = $Matches[4]
            }
        }
    }
}

# "222 test cases: 222 succeeded in ..."
$nTotal = 0; $nPassed = 0; $nFailed = 0
$totLine = $Lines | Where-Object { $_ -match '\d+ test cases' } | Select-Object -Last 1
if ($totLine -and ($totLine -match '(\d+) test cases.*?(\d+) succeeded')) {
    $nTotal = [int]$Matches[1]
    $nPassed = [int]$Matches[2]
    $nFailed = $nTotal - $nPassed
}

# ── Source-file helpers ───────────────────────────────────────────────────────

$_Cache = @{}

function Read-Source([string]$relPath) {
    $abs = Join-Path $ProjectRoot ($relPath -replace '/', [IO.Path]::DirectorySeparatorChar)
    if (-not $_Cache.ContainsKey($abs)) {
        $_Cache[$abs] = if (Test-Path $abs) { [IO.File]::ReadAllLines($abs) } else { @() }
    }
    return $_Cache[$abs]
}

# Returns the first // comment inside the named function body, or $null.
function Get-TestComment([string]$relFile, [string]$fnName) {
    $src = Read-Source $relFile
    # Find function definition
    $fi = -1
    for ($i = 0; $i -lt $src.Count; $i++) {
        if ($src[$i] -match "^\s*void\s+$([regex]::Escape($fnName))\s*\(") { $fi = $i; break }
    }
    if ($fi -lt 0) { return $null }

    # Find opening brace
    $bi = -1
    for ($i = $fi; $i -lt [Math]::Min($fi + 5, $src.Count); $i++) {
        if ($src[$i] -match '\{') { $bi = $i; break }
    }
    if ($bi -lt 0) { return $null }

    # First // comment in the next 12 lines of the function body
    for ($i = $bi + 1; $i -lt [Math]::Min($bi + 13, $src.Count); $i++) {
        $t = $src[$i].Trim()
        if ($t -match '^//\s*(.+)') { return $Matches[1].Trim() }
        # Stop at first assertion or declaration - the comment window has passed
        if ($t -match '^TEST_ASSERT|^http_|^feed_|^push|^arm_|^[a-z][a-zA-Z0-9_]+\s*[=(]') { break }
    }
    return $null
}

# Returns the first meaningful description line from the file-level comment block.
function Get-SuiteBrief([string]$suiteName) {
    $rel = "test/$suiteName/$suiteName.cpp"
    $src = Read-Source $rel
    $pastHeader = $false
    foreach ($line in $src) {
        $t = $line.Trim()
        # Skip copyright / SPDX / blank comment lines
        if ($t -match '^// (Copyright|SPDX)' -or $t -eq '//') {
            $pastHeader = $true; continue
        }
        # First substantive comment line after the header
        if ($pastHeader -and $t -match '^//\s*(.+)') { return $Matches[1].Trim() }
        # Stop at #include or non-comment
        if ($t -match '^#include|^[^/]') { break }
    }
    return ''
}

# Converts a snake_case test name to a readable sentence.
function Convert-Name([string]$name) {
    $prefix = ''
    if ($name -match '^(stress|race)_') {
        $prefix = "$(([cultureinfo]::InvariantCulture.TextInfo.ToTitleCase($Matches[1]))) - "
        $name = $name -replace '^(stress|race)_', ''
    }
    elseif ($name -match '^test_') {
        $name = $name -replace '^test_', ''
    }
    $words = $name -split '_'
    $words[0] = [cultureinfo]::InvariantCulture.TextInfo.ToTitleCase($words[0])
    return $prefix + ($words -join ' ')
}

# ── Build markdown ────────────────────────────────────────────────────────────

$ovIcon = if ($nFailed -eq 0) { '✅' } else { '❌' }
$dateStr = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
$sb = [System.Text.StringBuilder]::new(131072)

function Add([string]$s = '') { $null = $sb.AppendLine($s) }

Add "# Test Report"
Add ""
Add "**Generated:** $dateStr  "
Add "**Command:** ``pio test -e native -e native_app -e native_ssh -e native_ssh_hardened -e native_ssh_conn -e native_compliance``  "
$resStr = "$ovIcon $nPassed passed" + $(if ($nFailed) { ", $nFailed failed" } else { '' })
Add "**Result:** $resStr - ${WallSec}s  "
Add ""
Add "---"
Add ""
Add "## Summary"
Add ""
Add "| Suite | Environment | Tests | Status | Duration |"
Add "| :--- | :--- | ---: | :---: | ---: |"

foreach ($key in $Suites.Keys) {
    $s = $Suites[$key]
    $cnt = @($Tests | Where-Object { $_.Env -eq $s.Env -and $_.Suite -eq $s.Suite }).Count
    $icon = if ($s.Status -eq 'PASSED') { '✅' } else { '❌' }
    Add "| ``$($s.Suite)`` | ``$($s.Env)`` | $cnt | $icon | $($s.Duration) |"
}

Add ""
Add "---"
Add ""

# Per-suite detail sections, preserving suite order from $Suites
$done = [System.Collections.Generic.HashSet[string]]::new()

foreach ($key in $Suites.Keys) {
    $s = $Suites[$key]
    $suite = $s.Suite
    $env = $s.Env
    if (-not $done.Add("${env}:${suite}")) { continue }

    $group = @($Tests | Where-Object { $_.Suite -eq $suite -and $_.Env -eq $env })
    $pass = @($group | Where-Object { $_.Status -eq 'PASSED' }).Count
    $fail = @($group | Where-Object { $_.Status -eq 'FAILED' }).Count
    $gicon = if ($fail -eq 0) { '✅' } else { '❌' }
    $brief = Get-SuiteBrief $suite

    Add "## $suite - $gicon $pass passed$(if ($fail) { ", $fail failed" } else { '' })"
    Add ""
    Add "<details>"
    Add "<summary><b>Expand Suite Details</b></summary>"
    Add ""
    if ($brief) { Add "*${brief}*"; Add "" }

    Add "| # | Test | Status | Description |"
    Add "|---:|:---|:---:|:---|"

    $n = 1
    foreach ($t in $group) {
        $ticon = if ($t.Status -eq 'PASSED') { '✅' } else { '❌ **FAILED**' }
        $desc = Get-TestComment $t.File $t.Name
        if (-not $desc) { $desc = Convert-Name $t.Name }
        Add "| $n | ``$($t.Name)`` | $ticon | $desc |"
        $n++
    }
    Add ""

    if ($fail -gt 0) {
        Add "### Failure detail"
        Add ""
        Add '```'
        $inSect = $false
        foreach ($line in $Lines) {
            if ($line -match "Processing\s+$([regex]::Escape($suite))\s+in") { $inSect = $true }
            if ($inSect -and $line -match '\[FAILED\]') { Add $line }
            if ($inSect -and $line -match "---.*$([regex]::Escape($suite))") { $inSect = $false }
        }
        Add '```'
        Add ""
    }
    Add "</details>"
    Add ""
    Add "---"
    Add ""
}

# Collapsible raw output
Add "## Raw Output"
Add ""
Add "<details>"
Add "<summary>Expand full pio output</summary>"
Add ""
Add '```'
foreach ($line in $Lines) { Add $line }
Add '```'
Add ""
Add "</details>"

# ── Write ─────────────────────────────────────────────────────────────────────

[IO.File]::WriteAllText($ReportPath, $sb.ToString(), [Text.Encoding]::UTF8)
Write-Host ''
Write-Host "Report written: $ReportPath"
exit $PioExit
