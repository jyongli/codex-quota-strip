param([string]$Executable = (Join-Path $PSScriptRoot 'CodexQuotaStrip.exe'))
$ErrorActionPreference = 'Stop'
$fixture = Join-Path $PSScriptRoot ('fixtures\' + [guid]::NewGuid().ToString('N'))
$sessions = Join-Path $fixture 'sessions\2026\09\30'
[void](New-Item -ItemType Directory -Path $sessions -Force)
$status = Join-Path $fixture 'status.json'
$log = Join-Path $sessions 'rollout-test.jsonl'
$utf8 = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($log, '', $utf8)
$runner = Start-Process -FilePath $Executable -ArgumentList @('--headless','--home',('"' + $fixture + '"'),'--status',('"' + $status + '"')) -WindowStyle Hidden -PassThru
function Read-State { Get-Content -LiteralPath $status -Raw | ConvertFrom-Json }
function Wait-State([scriptblock]$condition, [string]$message) {
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    do { Start-Sleep -Milliseconds 20; try { $value = Read-State; if (& $condition $value) { return $value } } catch {} } while ($deadline.ElapsedMilliseconds -lt 10000)
    throw $message
}
function Make-Event([double]$weeklyUsed, $fiveUsed = $null, [long]$fiveReset = 0, [string]$time = '') {
    if (-not $time) { $time = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ss.fffZ') }
    $secondary = if ($null -eq $fiveUsed) { $null } else { @{used_percent=$fiveUsed;window_minutes=300;resets_at=$fiveReset} }
    @{timestamp=$time;type='event_msg';payload=@{type='token_count';rate_limits=@{limit_id='codex';primary=@{used_percent=$weeklyUsed;window_minutes=10080;resets_at=([DateTimeOffset]::UtcNow.ToUnixTimeSeconds()+7*86400)};secondary=$secondary}}} | ConvertTo-Json -Depth 8 -Compress
}
try {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while (-not (Test-Path -LiteralPath $status) -and $clock.ElapsedMilliseconds -lt 4000) { Start-Sleep -Milliseconds 30 }
    if (-not (Test-Path -LiteralPath $status)) { throw 'Test helper did not expose initial diagnostic status.' }
    # 诊断文件先于工作线程创建；等待监听器就绪，避免把测试启动竞争当作刷新失败。
    $null = Wait-State {param($s) $s.watching} 'Native watcher must become ready before appending fixture data.'
    $event = Make-Event 88
    $clock.Restart()
    [System.IO.File]::AppendAllText($log, $event + "`n", $utf8)
    do {
        Start-Sleep -Milliseconds 30
        try { $actual = Get-Content -LiteralPath $status -Raw | ConvertFrom-Json } catch { continue }
    } while ($actual.weekly_remaining -ne 12 -and $clock.ElapsedMilliseconds -lt 10000)
    if ($actual.weekly_remaining -ne 12) { throw 'An appended real quota event must refresh the display to 12% without polling.' }
    if ($null -ne $actual.five_hour_remaining) { throw 'A missing five-hour window must remain unavailable.' }
    "PASS: native filesystem event updated quota in $($clock.ElapsedMilliseconds) ms"

    $twoWindows = Make-Event 89 20 ([DateTimeOffset]::UtcNow.ToUnixTimeSeconds()+3600)
    [System.IO.File]::AppendAllText($log, $twoWindows.Substring(0, 70), $utf8)
    Start-Sleep -Milliseconds 180
    if ((Read-State).weekly_remaining -ne 12) { throw 'Partial JSON must not change displayed quotas.' }
    [System.IO.File]::AppendAllText($log, $twoWindows.Substring(70) + "`n", $utf8)
    $actual = Wait-State {param($s) $s.weekly_remaining -eq 11 -and $s.five_hour_remaining -eq 80} 'Completing an appended two-window event must refresh both numbers.'
    'PASS: real watcher retained fragmented JSON and updated both windows'

    $before = $actual.observed_ms
    $old = Make-Event 1 1 0 ([DateTime]::UtcNow.AddHours(-1).ToString('yyyy-MM-ddTHH:mm:ss.fffZ'))
    [System.IO.File]::AppendAllText($log, $old + "`n" + '{"type":"response_item","payload":{"rate_limits":{"primary":{"used_percent":0,"window_minutes":300}}}}' + "`n", $utf8)
    Start-Sleep -Milliseconds 200
    $actual = Read-State
    if ($actual.weekly_remaining -ne 11 -or $actual.observed_ms -ne $before) { throw 'Older snapshots and unrelated conversation records must not roll back the display.' }
    'PASS: older concurrent events and unrelated records were ignored'

    $newDirectory = Join-Path $sessions 'new'
    [void](New-Item -ItemType Directory -Path $newDirectory -Force)
    $newLog = Join-Path $newDirectory 'new-session.jsonl'
    [System.IO.File]::WriteAllText($newLog, (Make-Event 90) + "`n", $utf8)
    $actual = Wait-State {param($s) $s.weekly_remaining -eq 10 -and $null -eq $s.five_hour_remaining} 'A newly created nested session must be detected.'
    'PASS: recursive watcher detected a new session and cleared missing fields'

    [System.IO.File]::WriteAllText((Join-Path $fixture 'auth.json'), '{}', $utf8)
    $actual = Wait-State {param($s) $null -eq $s.weekly_remaining -and $null -eq $s.five_hour_remaining} 'A login-file change must clear previous account snapshots.'
    Start-Sleep -Milliseconds 100
    [System.IO.File]::AppendAllText($newLog, (Make-Event 91 22 ([DateTimeOffset]::UtcNow.ToUnixTimeSeconds()+2)) + "`n", $utf8)
    $actual = Wait-State {param($s) $s.weekly_remaining -eq 9 -and $s.five_hour_remaining -eq 78} 'New events after an account change must resume quota display.'
    $actual = Wait-State {param($s) $s.weekly_remaining -eq 9 -and $null -eq $s.five_hour_remaining} 'At reset time, a window must become unavailable until confirmed by a new event.'
    'PASS: account change cleared old quotas; reset timer cleared an expired window'

    $runner.Refresh(); $startCPU = $runner.TotalProcessorTime.TotalSeconds; $idle = [Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Seconds 2
    $runner.Refresh()
    [ordered]@{test='idle native watcher';seconds=[math]::Round($idle.Elapsed.TotalSeconds,3);cpuSecondsAdded=($runner.TotalProcessorTime.TotalSeconds-$startCPU);privateMB=[math]::Round($runner.PrivateMemorySize64/1MB,2)} | ConvertTo-Json -Compress
} finally {
    if (-not $runner.HasExited) { Stop-Process -Id $runner.Id -Force }
}
