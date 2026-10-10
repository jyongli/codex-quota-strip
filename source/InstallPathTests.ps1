param([string]$Executable = (Join-Path $PSScriptRoot 'CodexQuotaStrip.exe'))
$ErrorActionPreference = 'Stop'
$fixture = Join-Path $PSScriptRoot ('install-fixtures\' + [guid]::NewGuid().ToString('N'))
$dataRoot = Join-Path $fixture 'data'
[void](New-Item -ItemType Directory -Path (Join-Path $dataRoot 'sessions') -Force)
$first = Join-Path $fixture 'portable\CodexQuotaStrip.exe'
$second = Join-Path $fixture 'installed\CodexQuotaStrip.exe'
[void](New-Item -ItemType Directory -Path (Split-Path $first),(Split-Path $second) -Force)
Copy-Item -LiteralPath $Executable -Destination $first
Copy-Item -LiteralPath $Executable -Destination $second
$process = $null
try {
    foreach ($file in @($first,$second)) {
        $status = Join-Path (Split-Path $file) 'status.json'
        $process = Start-Process -FilePath $file -ArgumentList @('--taskbar','--home',('"'+$dataRoot+'"'),'--status',('"'+$status+'"')) -WindowStyle Hidden -PassThru
        $timer = [Diagnostics.Stopwatch]::StartNew(); $state = $null
        do {
            Start-Sleep -Milliseconds 40
            if (Test-Path -LiteralPath $status) { try { $state = Get-Content -LiteralPath $status -Raw | ConvertFrom-Json } catch {} }
        } while ($state.tray_icons -ne 2 -and $timer.ElapsedMilliseconds -lt 2500)
        if ($state.tray_icons -ne 2) { throw ('Both taskbar icons must register at each install path: ' + $file) }
        & $file --home $dataRoot --quit
        if (-not $process.WaitForExit(3000)) { throw 'Relocation fixture did not exit.' }
        $process = $null
    }
    'PASS: portable and installed binaries register both icons with the same Codex data directory.'
} finally {
    if ($process -and -not $process.HasExited) { & $process.Path --home $dataRoot --quit; [void]$process.WaitForExit(3000) }
}
