param([switch]$Tests, [switch]$UiProbe, [switch]$VisibilityTests, [switch]$TrayIconTests, [switch]$LayerTests, [ValidateSet('SettingsTests','ConfigUiTests','LifecycleTests','WeeklyDisplayTests','ProviderProbe','ProviderEnvironmentTests','ProxyConfigTests')][string]$Extra, [string]$OutputDirectory = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'Visual Studio Installer / vswhere was not found. Install Visual Studio 2022 C++ Build Tools and a Windows SDK.' }
# 在切换工作目录之前解析相对输出路径，支持从任意目录构建。
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Microsoft C++ Build Tools are required to rebuild; the delivered EXE needs no compiler.' }
$vc = Get-ChildItem -LiteralPath (Join-Path $installation 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
. (Join-Path $PSScriptRoot 'BuildSdk.ps1')
$sdk = Find-QuotaWindowsSdk -KitsPath $kits
$sdkIncludes = Join-Path $kits ('Include\' + $sdk.Name)
Write-Output ('Windows SDK: ' + $sdk.Name)
$env:INCLUDE = (@((Join-Path $vc.FullName 'include')) + @('ucrt','um','shared','winrt').ForEach({ Join-Path $sdkIncludes $_ })) -join ';'
$env:LIB = @((Join-Path $vc.FullName 'lib\x64'),(Join-Path $sdk.FullName 'ucrt\x64'),(Join-Path $sdk.FullName 'um\x64')) -join ';'
$compiler = Join-Path $vc.FullName 'bin\Hostx64\x64\cl.exe'
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
Push-Location $OutputDirectory
try {
    $source = if ($Tests) { 'QuotaTests.cpp' } elseif ($UiProbe) { 'UiProbe.cpp' } elseif ($VisibilityTests) { 'VisibilityTests.cpp' } elseif ($TrayIconTests) { 'TrayIconTests.cpp' } elseif ($LayerTests) { 'LayerTests.cpp' } else { 'QuotaStrip.cpp' }
    $target = if ($Tests) { 'QuotaTests.exe' } elseif ($UiProbe) { 'UiProbe.exe' } elseif ($VisibilityTests) { 'VisibilityTests.exe' } elseif ($TrayIconTests) { 'TrayIconTests.exe' } elseif ($LayerTests) { 'LayerTests.exe' } else { 'CodexQuotaStrip.exe' }
    if ($Extra) { $source = $Extra + '.cpp'; $target = $Extra + '.exe' }
    $arguments = @('/nologo','/std:c++17','/EHsc','/W4','/utf-8','/O2','/MT','/DUNICODE','/D_UNICODE','/D_CRT_SECURE_NO_WARNINGS',('/Fe:' + (Join-Path $OutputDirectory $target)),(Join-Path $PSScriptRoot $source))
    if($source -in @('QuotaStrip.cpp','ProviderProbe.cpp','ProviderEnvironmentTests.cpp')){$arguments+=(Join-Path $PSScriptRoot 'QuotaProvider.cpp')}
    if ($UiProbe -or $VisibilityTests -or $TrayIconTests -or $LayerTests -or $Extra) { $arguments += @('/link','user32.lib','gdi32.lib','shell32.lib','dwmapi.lib') }
    elseif (-not $Tests) { $arguments += @('/link','/SUBSYSTEM:WINDOWS','user32.lib','gdi32.lib','comctl32.lib','shell32.lib','advapi32.lib','dwmapi.lib','imm32.lib','uxtheme.lib') }
    & $compiler @arguments
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }
