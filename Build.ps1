param(
    [ValidateSet('App','QuotaTests','SettingsTests','TrayIconTests','LifecycleTests','ConfigUiTests','WeeklyDisplayTests','UiProbe','LayerTests','VisibilityTests','ProviderProbe','ProviderEnvironmentTests','ProxyConfigTests')]
    [string]$Target = 'App',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build')
)
$ErrorActionPreference = 'Stop'
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
# 公开入口沿用经过验证的 MSVC 构建脚本，生成文件集中放在 build 目录。
$selection = @{}
switch ($Target) {
    'QuotaTests' { $selection.Tests = $true }
    'TrayIconTests' { $selection.TrayIconTests = $true }
    'UiProbe' { $selection.UiProbe = $true }
    'LayerTests' { $selection.LayerTests = $true }
    'VisibilityTests' { $selection.VisibilityTests = $true }
    'App' { }
    default { $selection.Extra = $Target }
}
& (Join-Path $PSScriptRoot 'source\Build.ps1') @selection -OutputDirectory $OutputDirectory
if ($Target -eq 'App') {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'source\Start-With-Codex.ps1') -Destination $OutputDirectory
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'THIRD_PARTY_NOTICES.md') -Destination $OutputDirectory
}
