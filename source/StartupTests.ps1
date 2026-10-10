param(
    [string]$Starter = (Join-Path $PSScriptRoot 'Start-With-Codex.ps1'),
    [string]$Executable = (Join-Path $PSScriptRoot 'CodexQuotaStrip.exe')
)
$ErrorActionPreference = 'Stop'
$expected = [IO.Path]::GetFullPath($Executable)
$existing = @(Get-Process -Name CodexQuotaStrip -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $expected })
if ($existing) { throw 'Run the startup regression with this companion instance stopped.' }
$previousCodeHome = $env:CODEX_HOME
$fixture = Join-Path $PSScriptRoot ('startup-fixtures\' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path (Join-Path $fixture 'sessions') -Force)
$env:CODEX_HOME = $fixture
try {
    & $Starter -Executable $expected
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        Start-Sleep -Milliseconds 40
        $started = @(Get-Process -Name CodexQuotaStrip -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $expected })
    } while (-not $started -and $timer.ElapsedMilliseconds -lt 2500)
    if ($started.Count -ne 1) { throw 'The Codex companion-start entry must launch one real quota process.' }
    $firstPid = $started[0].Id
    & $Starter -Executable $expected
    Start-Sleep -Milliseconds 500
    $again = @(Get-Process -Name CodexQuotaStrip -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $expected })
    if ($again.Count -ne 1 -or $again[0].Id -ne $firstPid) { throw 'Repeated Codex activation must keep the same single quota process.' }
    'PASS: companion startup launches the real EXE and repeated activation stays single-instance.'
} finally {
    $owned = @(Get-Process -Name CodexQuotaStrip -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $expected })
    if ($owned) { & $expected --quit; foreach ($p in $owned) { [void]$p.WaitForExit(3000) } }
    $env:CODEX_HOME = $previousCodeHome
}
