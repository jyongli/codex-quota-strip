param([string]$Executable = (Join-Path $PSScriptRoot 'CodexQuotaStrip.exe'))
# Public companion-start entry point. The Codex launcher invokes this once.
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { return }
try {
    # No -Wait and no extra resident PowerShell process. The native EXE handles single-instance reuse.
    # The saved surface preference controls taskbar-only versus taskbar plus Codex UI.
    Start-Process -FilePath $Executable -WindowStyle Hidden -ErrorAction Stop | Out-Null
} catch {
    # A companion failure must not prevent Codex from starting.
    Write-Warning ('Codex quota companion could not start: ' + $_.Exception.Message)
}
