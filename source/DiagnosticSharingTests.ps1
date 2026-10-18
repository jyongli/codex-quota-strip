param([Parameter(Mandatory)][string]$Executable)
$ErrorActionPreference = 'Stop'
if (-not ('QuotaHandleRelease' -as [type])) {
  Add-Type -TypeDefinition 'public static class QuotaHandleRelease { public static System.Threading.Tasks.Task CloseSoon(System.IDisposable handle) { return System.Threading.Tasks.Task.Run(async () => { await System.Threading.Tasks.Task.Delay(30); handle.Dispose(); }); } }'
}
$root = Join-Path ([IO.Path]::GetTempPath()) ('quota-status-sharing-'+[Guid]::NewGuid().ToString('N'))
$sessions = Join-Path $root 'sessions'
New-Item -ItemType Directory -Path $sessions -Force | Out-Null
$status = Join-Path $root 'status.json'
$log = Join-Path $sessions 'synthetic.jsonl'
[IO.File]::WriteAllText($log,'')
function Read-Shared([string]$Path) {
  $stream = [IO.FileStream]::new($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
  $reader = [IO.StreamReader]::new($stream)
  try { $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
}
function Wait-Value([string]$Path,[int]$Value) {
  $timer=[Diagnostics.Stopwatch]::StartNew()
  do { try { if ((Read-Shared $Path).weekly_remaining -eq $Value) { return } } catch {}; Start-Sleep -Milliseconds 20 } while ($timer.ElapsedMilliseconds -lt 5000)
  throw "Expected synthetic quota $Value at $Path"
}
function Append-Quota([int]$Used) {
  $event=@{timestamp=[DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ss.fffZ');type='event_msg';payload=@{type='token_count';rate_limits=@{primary=@{used_percent=$Used;window_minutes=10080;resets_at=0}}}} | ConvertTo-Json -Depth 8 -Compress
  [IO.File]::AppendAllText($log,$event+"`n",[Text.UTF8Encoding]::new($false))
}
$start=[Diagnostics.ProcessStartInfo]::new()
$start.FileName=$Executable;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
$start.Arguments='--headless --home "'+$root+'" --status "'+$status+'"'
$start.EnvironmentVariables['LOCALAPPDATA']=$root
$process=[Diagnostics.Process]::Start($start)
$held=$null
try {
  $timer=[Diagnostics.Stopwatch]::StartNew()
  do { try { if ((Read-Shared $status).watching) { break } } catch {}; Start-Sleep -Milliseconds 20 } while ($timer.ElapsedMilliseconds -lt 5000)
  Append-Quota 20; Wait-Value $status 80
  # 模拟 Get-Content 的共享方式：故意不共享删除权限，证明真实额度已处理而发布被锁挡住。
  $held=[IO.FileStream]::new($status,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
  Append-Quota 30; Wait-Value ($status+'.tmp') 70
  if ((Read-Shared $status).weekly_remaining -ne 80) { throw 'Non-delete-sharing reproduction did not retain the old state' }
  'PASS: a reader denying delete held status at 80 while completed new snapshot 70 existed in status.tmp.'
  $release=[QuotaHandleRelease]::CloseSoon($held)
  Wait-Value $status 70
  [void]$release.GetAwaiter().GetResult();$held=$null
  'PASS: once the brief external reader closes, the already processed snapshot is published without another quota event.'
} finally {
  if ($held) { $held.Dispose() }
  if (-not $process.HasExited) { $process.Kill();[void]$process.WaitForExit(5000) }
  $process.Dispose()
}
