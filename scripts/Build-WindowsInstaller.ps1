[CmdletBinding()]
param(
    [string]$Version = '3.0.0-preview.5',
    [string]$PayloadDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build'),
    [string]$OutputDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'dist'),
    [string]$CompilerPath,
    [switch]$TestBuild,
    [string]$TestRoot,
    [string]$TestAppId
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
try {
    $repoRoot = Split-Path $PSScriptRoot -Parent
    $PayloadDirectory = [IO.Path]::GetFullPath($PayloadDirectory)
    $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
    $Version = $Version.TrimStart('v')
    if ($Version -notmatch '^(\d+)\.(\d+)\.(\d+)(?:-preview\.(\d+))?$') {
        throw 'Version must be major.minor.patch or major.minor.patch-preview.number (optional v prefix).'
    }
    $parts = @($Matches[1], $Matches[2], $Matches[3], $(if ($Matches[4]) { $Matches[4] } else { '0' }))
    if (@($parts | Where-Object { [long]$_ -gt 65535 }).Count) { throw 'Numeric version components must not exceed 65535.' }
    $numericVersion = $parts -join '.'
    foreach ($file in @('CodexQuotaStrip.exe', 'Start-With-Codex.ps1', 'THIRD_PARTY_NOTICES.md')) {
        if (-not (Test-Path -LiteralPath (Join-Path $PayloadDirectory $file) -PathType Leaf)) {
            throw "Missing payload $file. Run ./Build.ps1 -Target App first."
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'LICENSE') -PathType Leaf)) { throw 'Repository LICENSE is missing.' }
    # 安装器声明 x64；拒绝误装其他架构的二进制，也不在打包阶段重建程序。
    $binary = [IO.File]::ReadAllBytes((Join-Path $PayloadDirectory 'CodexQuotaStrip.exe'))
    if ($binary.Length -lt 64 -or $binary[0] -ne 0x4d -or $binary[1] -ne 0x5a) { throw 'Payload is not a PE executable.' }
    $peOffset = [BitConverter]::ToInt32($binary, 0x3c)
    if ($peOffset -lt 0 -or $peOffset + 6 -gt $binary.Length -or
        [BitConverter]::ToUInt32($binary, $peOffset) -ne 0x4550 -or
        [BitConverter]::ToUInt16($binary, $peOffset + 4) -ne 0x8664) { throw 'Installer payload must be an x64 PE executable.' }

    # 仅发现已安装的编译器；CI windows-2022 预装 Inno Setup，不全局安装工具。
    if (-not $CompilerPath) {
        $candidate = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($candidate) { $CompilerPath = $candidate.Source }
        else {
            foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
                if ($base) {
                    $path = Join-Path $base 'Inno Setup 6\ISCC.exe'
                    if (Test-Path -LiteralPath $path -PathType Leaf) { $CompilerPath = $path; break }
                }
            }
        }
    }
    if (-not $CompilerPath -or -not (Test-Path -LiteralPath $CompilerPath -PathType Leaf)) {
        throw 'Inno Setup 6.3+ ISCC.exe is required. Use the preinstalled CI compiler or pass -CompilerPath; no tools are installed automatically.'
    }
    $CompilerPath = [IO.Path]::GetFullPath($CompilerPath)
    $compilerMetadata = (Get-Item -LiteralPath $CompilerPath).VersionInfo
    Write-Output "ISCC: $CompilerPath; FileVersion=$($compilerMetadata.FileVersion); ProductVersion=$($compilerMetadata.ProductVersion)"
    # ISCC 的文件版本可能固定为 6.0；用无输出的 stdin 编译读取实际 ISCmplr 引擎版本。
    $probeInfo = [Diagnostics.ProcessStartInfo]::new()
    $probeInfo.FileName = $CompilerPath
    $probeInfo.UseShellExecute = $false
    $probeInfo.CreateNoWindow = $true
    $probeInfo.RedirectStandardInput = $true
    $probeInfo.RedirectStandardOutput = $true
    $probeInfo.RedirectStandardError = $true
    [void]$probeInfo.ArgumentList.Add('-')
    $probe = [Diagnostics.Process]::Start($probeInfo)
    try {
        $probeOutputTask = $probe.StandardOutput.ReadToEndAsync()
        $probeErrorTask = $probe.StandardError.ReadToEndAsync()
        $probe.StandardInput.WriteLine("[Setup]`nAppName=Compiler version probe`nAppVersion=1`nOutput=no`nCreateAppDir=no`nUninstallable=no")
        $probe.StandardInput.Close()
        $probe.WaitForExit()
        $probeOutput = $probeOutputTask.GetAwaiter().GetResult()
        $probeError = $probeErrorTask.GetAwaiter().GetResult()
        if ($probe.ExitCode -ne 0) {
            Write-Output $probeOutput
            Write-Error $probeError -ErrorAction Continue
            exit $probe.ExitCode
        }
    } finally { $probe.Dispose() }
    if ($probeOutput -notmatch '(?im)^Compiler engine version:[^\r\n]*?(\d+\.\d+\.\d+(?:\.\d+)?)') {
        throw 'ISCC did not report its compiler engine version.'
    }
    $compilerVersion = [version]$Matches[1]
    Write-Output "Compiler engine version: $compilerVersion"
    if ($compilerVersion.Major -ne 6 -or $compilerVersion.Minor -lt 3) {
        throw 'Use Inno Setup 6.3 or later in the 6.x series (x64compatible requires 6.3+).'
    }
    [void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
    $arguments = @('/Qp', "/DRepoRoot=$repoRoot", "/DPayloadRoot=$PayloadDirectory", "/DOutputRoot=$OutputDirectory",
        "/DAppVersion=$Version", "/DNumericVersion=$numericVersion")
    $outputName = 'CodexQuotaStrip-windows-x64-setup.exe'
    if ($TestBuild) {
        if (-not $TestRoot -or $TestAppId -notmatch '^CodexQuotaStrip\.Test\.[a-f0-9]{32}$') {
            throw 'Test builds require -TestRoot and a unique CodexQuotaStrip.Test.<32 lowercase hex> -TestAppId.'
        }
        $TestRoot = [IO.Path]::GetFullPath($TestRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)
        if (-not $TestRoot.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()), [StringComparison]::OrdinalIgnoreCase)) {
            throw 'TestRoot must be inside the temporary directory.'
        }
        $arguments += @('/DTestBuild=1', "/DTestRoot=$TestRoot", "/DTestAppId=$TestAppId")
        $outputName = 'CodexQuotaStrip-windows-x64-test-setup.exe'
    }
    $arguments += (Join-Path $repoRoot 'packaging\windows\CodexQuotaStrip.iss')
    & $CompilerPath @arguments
    $compilerExitCode = $LASTEXITCODE
    if ($compilerExitCode -ne 0) { exit $compilerExitCode }
    $output = Join-Path $OutputDirectory $outputName
    if (-not (Test-Path -LiteralPath $output -PathType Leaf)) { throw 'ISCC returned success without the expected installer.' }
    Write-Output $output
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
