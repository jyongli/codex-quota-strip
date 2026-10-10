param(
    [string]$SourceDirectory = $PSScriptRoot,
    [switch]$NoStart,
    [string]$ReceiptPath
)
$ErrorActionPreference = 'Stop'
function Get-StartupBinaryHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path); $algorithm = [Security.Cryptography.SHA256]::Create()
    try { ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '') }
    finally { $stream.Dispose(); $algorithm.Dispose() }
}
try {
$installRoot = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'CodexQuotaStrip'))
$launcher = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'OpenAI\Codex\launch-with-clash.ps1'))
$sourceExe = Join-Path $SourceDirectory 'CodexQuotaStrip.exe'
$sourceStarter = Join-Path $SourceDirectory 'Start-With-Codex.ps1'
foreach ($file in @($sourceExe, $sourceStarter, $launcher)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw ('Required file was not found: ' + $file) }
}
$original = [IO.File]::ReadAllText($launcher, [Text.Encoding]::UTF8)
$tokens = $null; $parseErrors = $null
[void][Management.Automation.Language.Parser]::ParseInput($original, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'The current Codex launcher has syntax errors; nothing was changed.' }
$marker = '# CodexQuotaStrip startup hook (managed).'
$updated = $original
if (-not $original.Contains($marker)) {
    $anchor = "            -PreventBreakaway`r`n        exit 0"
    if (-not $original.Contains($anchor)) { $anchor = "            -PreventBreakaway`n        exit 0" }
    if (-not $original.Contains($anchor)) { throw 'The expected unpackaged launcher exit was not found; nothing was changed.' }
    $hook = @'
            -PreventBreakaway
        # CodexQuotaStrip startup hook (managed).
        $quotaStarter = Join-Path $env:LOCALAPPDATA 'CodexQuotaStrip\Start-With-Codex.ps1'
        if (Test-Path -LiteralPath $quotaStarter -PathType Leaf) {
            try { & $quotaStarter } catch { Write-Warning ('Quota companion startup failed: ' + $_.Exception.Message) }
        }
        exit 0
'@
    $updated = $original.Replace($anchor, $hook)
    [void][Management.Automation.Language.Parser]::ParseInput($updated, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count) { throw 'The proposed startup integration did not parse; nothing was changed.' }
}
[void](New-Item -ItemType Directory -Path $installRoot -Force)
$installedExe = Join-Path $installRoot 'CodexQuotaStrip.exe'
if (-not (Test-Path -LiteralPath $installedExe) -or (Get-StartupBinaryHash $sourceExe) -ne (Get-StartupBinaryHash $installedExe)) {
    if (Test-Path -LiteralPath $installedExe) {
        $old = @(Get-Process -Name CodexQuotaStrip -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $installedExe })
        if ($old) { & $installedExe --quit; foreach ($process in $old) { if (-not $process.WaitForExit(3000)) { throw 'Previous quota companion did not stop; replacement was cancelled.' } } }
    }
    Copy-Item -LiteralPath $sourceExe -Destination $installedExe
}
Copy-Item -LiteralPath $sourceStarter -Destination (Join-Path $installRoot 'Start-With-Codex.ps1')
$backup = $null
if ($updated -ne $original) {
    $backup = $launcher + '.before-quota-startup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.bak'
    Copy-Item -LiteralPath $launcher -Destination $backup
    # The shortcuts use Windows PowerShell 5.1; keep Chinese messages decodable there.
    [IO.File]::WriteAllText($launcher, $updated, [Text.UTF8Encoding]::new($true))
}
if (-not $NoStart) { & (Join-Path $installRoot 'Start-With-Codex.ps1') }
$packageIdentity = $null
try { $packageIdentity = [Windows.ApplicationModel.Package,Windows.ApplicationModel,ContentType=WindowsRuntime]::Current.Id.FamilyName } catch {}
$receipt = [pscustomobject]@{ installedExe=$installedExe; launcher=$launcher; launcherBackup=$backup; linked=$true; binarySHA256=(Get-StartupBinaryHash $installedExe); launcherSHA256=(Get-StartupBinaryHash $launcher); hookCount=([regex]::Matches([IO.File]::ReadAllText($launcher),'CodexQuotaStrip startup hook \(managed\)\.')).Count; packageIdentity=$packageIdentity }
if ($ReceiptPath) { [IO.File]::WriteAllText($ReceiptPath, ($receipt | ConvertTo-Json -Depth 3), [Text.UTF8Encoding]::new($false)) }
$receipt
} catch {
    if ($ReceiptPath) { [IO.File]::WriteAllText($ReceiptPath, (@{linked=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace} | ConvertTo-Json), [Text.UTF8Encoding]::new($false)) }
    throw
}
