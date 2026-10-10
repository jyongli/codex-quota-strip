param(
    [switch]$Integration,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build')
)
$ErrorActionPreference = 'Stop'
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
& (Join-Path $PSScriptRoot 'source\BuildSdkTests.ps1')
$targets = @('QuotaTests','SettingsTests','TrayIconTests')
if ($Integration) { $targets += @('LifecycleTests','ConfigUiTests','WeeklyDisplayTests') }
foreach ($target in $targets) {
    & (Join-Path $PSScriptRoot 'Build.ps1') -Target $target -OutputDirectory $OutputDirectory
}
if ($Integration) { & (Join-Path $PSScriptRoot 'Build.ps1') -OutputDirectory $OutputDirectory }
# 每个集成测试使用独立的数据目录，不修改用户的 Codex 数据。
foreach ($target in $targets) {
    $testExecutable = Join-Path $OutputDirectory ($target + '.exe')
    # 单个路径显式传参，避免 PowerShell 把标量字符串按字符展开。
    if ($target -in @('LifecycleTests','ConfigUiTests','WeeklyDisplayTests')) {
        & $testExecutable (Join-Path $OutputDirectory 'CodexQuotaStrip.exe')
    } else {
        & $testExecutable
    }
    if ($LASTEXITCODE -ne 0) { throw "$target failed with exit code $LASTEXITCODE" }
}
if ($Integration) {
    & (Join-Path $PSScriptRoot 'source\IntegrationTests.ps1') -Executable (Join-Path $OutputDirectory 'CodexQuotaStrip.exe')
}
Write-Output 'PASS: all selected tests completed.'
