param(
    [Parameter(Mandatory)][string]$ArtifactRoot,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$Commit,
    [Parameter(Mandatory)][string]$RunUrl,
    [string]$Version = 'v3.0.0-preview.5'
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$ArtifactRoot=[IO.Path]::GetFullPath($ArtifactRoot)
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if ($Commit -notmatch '^[0-9a-f]{40}$') { throw 'Commit must be an exact Git SHA' }
foreach ($name in @('codex-quota-strip-windows-x64','codex-quota-strip-Linux-native','codex-quota-strip-macOS-native')) {
    $directory=Join-Path $ArtifactRoot $name
    $info=[IO.File]::ReadAllText((Join-Path $directory 'BUILD-INFO.txt'))
    if ($info -notmatch ('(?m)^Commit: '+[regex]::Escape($Commit)+'\r?$')) { throw "Wrong artifact commit: $name" }
    if ($info -notmatch '(?m)^Repository: jyongli/codex-quota-strip\r?$') { throw "Wrong artifact repository: $name" }
    foreach ($line in Get-Content -LiteralPath (Join-Path $directory 'SHA256SUMS.txt')) {
        if ($line -notmatch '^([0-9a-fA-F]{64}) [ *](.+)$') { throw 'Invalid artifact checksum manifest' }
        $expected=$Matches[1];$file=$Matches[2]
        if ([IO.Path]::GetFileName($file) -ne $file) { throw 'Unexpected checksum path' }
        if ((Get-FileHash -LiteralPath (Join-Path $directory $file)).Hash -ne $expected) { throw "Artifact checksum mismatch: $file" }
    }
    Write-Output "PASS: $name source provenance and checksums"
}
if (Test-Path -LiteralPath $OutputDirectory) {
    if (@(Get-ChildItem -LiteralPath $OutputDirectory -Force).Count) { throw 'Release output must be empty to avoid mixing versions' }
} else { New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null }
function Write-Checksums([string]$Directory) {
    $lines=foreach ($file in Get-ChildItem -LiteralPath $Directory -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object Name) {
        (Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant()+'  '+$file.Name
    }
    [IO.File]::WriteAllLines((Join-Path $Directory 'SHA256SUMS.txt'),[string[]]$lines,[Text.UTF8Encoding]::new($false))
}
# 便携 ZIP 的校验清单仅覆盖 ZIP 内实际文件，不能引用单独发布的 setup.exe。
$portable=Join-Path ([IO.Path]::GetTempPath()) ('quota-release-portable-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $portable | Out-Null
try {
    $windows=Join-Path $ArtifactRoot 'codex-quota-strip-windows-x64'
    foreach ($name in @('CodexQuotaStrip.exe','Start-With-Codex.ps1','LICENSE','THIRD_PARTY_NOTICES.md','BUILD-INFO.txt')) {
        Copy-Item -LiteralPath (Join-Path $windows $name) -Destination $portable
    }
    Write-Checksums $portable
    Compress-Archive -LiteralPath @(Get-ChildItem -LiteralPath $portable -File | ForEach-Object FullName) -DestinationPath (Join-Path $OutputDirectory 'CodexQuotaStrip-windows-x64.zip')
} finally {
    # 只清理本次创建并核实位于 TEMP 的 staging 目录。
    $tempBase=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if ($portable.StartsWith($tempBase,[StringComparison]::OrdinalIgnoreCase) -and (Split-Path $portable -Leaf) -like 'quota-release-portable-*') {
        Remove-Item -LiteralPath $portable -Recurse -Force
    }
}
$packages=@{
    'codex-quota-strip-windows-x64'=@('CodexQuotaStrip-windows-x64-setup.exe')
    'codex-quota-strip-Linux-native'=@('CodexQuotaStrip-linux-x86_64.deb','CodexQuotaStrip-linux-x86_64.rpm','CodexQuotaStrip-linux-x86_64.tar.gz')
    'codex-quota-strip-macOS-native'=@('CodexQuotaStrip-macos-universal.zip','CodexQuotaStrip-macos-universal.dmg','CodexQuotaStrip-macos-universal.pkg')
}
foreach ($directory in $packages.Keys) { foreach ($name in $packages[$directory]) { Copy-Item -LiteralPath (Join-Path (Join-Path $ArtifactRoot $directory) $name) -Destination $OutputDirectory } }
foreach ($name in @('LICENSE','THIRD_PARTY_NOTICES.md')) { Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $OutputDirectory }
$info="Repository: jyongli/codex-quota-strip`nCommit: $Commit`nVersion: $Version`nBuild run: $RunUrl`nTargets: Windows x64 setup/ZIP; Linux x64 DEB/RPM/TGZ; macOS 14+ universal DMG/PKG/ZIP`n"
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'BUILD-INFO.txt'),$info,[Text.UTF8Encoding]::new($false))
Write-Checksums $OutputDirectory
if (@(Get-ChildItem -LiteralPath $OutputDirectory -File).Count -ne 12) { throw 'Unexpected release asset set' }
Get-ChildItem -LiteralPath $OutputDirectory -File | Select-Object Name,Length
