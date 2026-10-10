$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BuildSdk.ps1')
$fixture = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ('quota-sdk-' + [guid]::NewGuid().ToString('N'))))
[void](New-Item -ItemType Directory -Path $fixture -Force)
function Add-SdkFixture([string]$Version, [switch]$Complete) {
    [void](New-Item -ItemType Directory -Path (Join-Path $fixture ('Lib/' + $Version)) -Force)
    if ($Complete) {
        # 真实临时目录模拟安装完整性；不修改本机 SDK 或编译器。
        foreach ($relative in @('Include/um/Windows.h', 'Include/ucrt/stdio.h', 'Include/shared/windef.h', 'Lib/um/x64/kernel32.lib', 'Lib/ucrt/x64/libucrt.lib')) {
            $parts = $relative.Split('/')
            $target = Join-Path $fixture ($parts[0] + '/' + $Version + '/' + ($parts[1..($parts.Length - 1)] -join '/'))
            [void](New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force)
            [IO.File]::WriteAllText($target, 'fixture')
        }
    }
}
try {
    Add-SdkFixture '10.0.22621.0' -Complete
    Add-SdkFixture '10.0.26100.0' -Complete
    Add-SdkFixture '10.0.99999.0'
    Add-SdkFixture 'ucrt'
    $selected = Find-QuotaWindowsSdk -KitsPath $fixture
    if ($selected.Name -ne '10.0.26100.0') { throw ('A newer incomplete SDK must not hide a complete SDK; selected ' + $selected.Name) }
    [IO.File]::Delete((Join-Path $fixture 'Include/10.0.26100.0/um/Windows.h'))
    $selected = Find-QuotaWindowsSdk -KitsPath $fixture
    if ($selected.Name -ne '10.0.22621.0') { throw 'Removing an SDK header must select the next complete version.' }
    [IO.File]::Delete((Join-Path $fixture 'Lib/10.0.22621.0/ucrt/x64/libucrt.lib'))
    $rejected = $false
    try { $null = Find-QuotaWindowsSdk -KitsPath $fixture } catch { $rejected = $true }
    if (-not $rejected) { throw 'A machine with no complete SDK must report an actionable build error.' }
    'PASS: SDK discovery skips partial installations and rejects missing headers/libraries.'
} finally {
    # 只删除本测试创建的 GUID 目录，先确认其绝对路径仍处于临时目录内。
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $fixture.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $fixture) -notmatch '^quota-sdk-[a-f0-9]{32}$') { throw 'Unsafe fixture cleanup path.' }
    [IO.Directory]::Delete($fixture, $true)
}
