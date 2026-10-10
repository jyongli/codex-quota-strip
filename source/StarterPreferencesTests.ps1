param(
    [string]$Starter = (Join-Path $PSScriptRoot 'Start-With-Codex.ps1'),
    [string]$Executable = (Join-Path $PSScriptRoot 'CodexQuotaStrip.exe')
)
$ErrorActionPreference = 'Stop'
$fixture = Join-Path $PSScriptRoot ('preference-fixtures\' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path (Join-Path $fixture 'sessions'),(Join-Path $fixture 'CodexQuotaStrip') -Force)
[IO.File]::WriteAllText((Join-Path $fixture 'CodexQuotaStrip\settings.ini'), "[display]`r`nposition=top`r`nsurface=taskbar-and-codex`r`n",[Text.UTF8Encoding]::new($false))
$previousHome = $env:CODEX_HOME; $previousLocal = $env:LOCALAPPDATA
$env:CODEX_HOME = $fixture; $env:LOCALAPPDATA = $fixture
$expected = [IO.Path]::GetFullPath($Executable); $started = @()
try {
    & $Starter -Executable $expected
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do { Start-Sleep -Milliseconds 40; $started = @(Get-Process -Name CodexQuotaStrip -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $expected}) } while (-not $started -and $timer.ElapsedMilliseconds -lt 2500)
    if($started.Count -ne 1){throw 'The preference regression requires one real companion process.'}
    $command = (Get-CimInstance Win32_Process -Filter ('ProcessId='+$started[0].Id)).CommandLine
    if($command -match '(?i)\s--taskbar(?:\s|$)'){throw 'Startup must preserve the saved in-Codex display choice instead of forcing taskbar-only mode.'}
    'PASS: startup preserves saved display preferences without a command-line override.'
} finally {
    if($started){& $expected --quit; foreach($p in $started){[void]$p.WaitForExit(3000)}}
    $env:CODEX_HOME=$previousHome; $env:LOCALAPPDATA=$previousLocal
}
