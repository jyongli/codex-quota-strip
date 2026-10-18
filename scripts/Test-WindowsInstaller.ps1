[CmdletBinding()]
param(
    [string]$Version = '3.0.0-preview.6',
    [string]$PayloadDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build'),
    [string]$CompilerPath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$testRoot = $null
$registryPath = $null
$exitCode = 0
$uninstalled = $false
$steps = [Collections.Generic.List[string]]::new()
$fixtures = [Collections.Generic.List[object]]::new()

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

# 外部程序返回值原样传播；参数逐个传递，不依赖 shell 拼接或引用转义。
function Invoke-CheckedProcess([string]$FilePath, [string[]]$Arguments, [string]$Label) {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) { [void]$startInfo.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($startInfo)
    try {
        # 两条管道同时异步排空；先等退出再读取同步管道会因缓冲区写满而死锁。
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $outputText = $outputTask.GetAwaiter().GetResult()
        $errorText = $errorTask.GetAwaiter().GetResult()
        if ($outputText) { Write-Output $outputText.TrimEnd() }
        if ($errorText) { [Console]::Error.WriteLine($errorText.TrimEnd()) }
        # 每次子进程输出同时写入隔离目录，让 CI 可上传完整编译及测试诊断。
        $logName = ($Label -replace '[^A-Za-z0-9.-]', '-') + '.process.log'
        [IO.File]::WriteAllText((Join-Path $testRoot $logName),
            "ExitCode=$($process.ExitCode)`r`nSTDOUT:`r`n$outputText`r`nSTDERR:`r`n$errorText")
        if ($process.ExitCode -ne 0) {
            $failure = [Exception]::new("$Label exited with code $($process.ExitCode). Logs: $testRoot")
            $failure.Data['ExitCode'] = $process.ExitCode
            throw $failure
        }
    } finally { $process.Dispose() }
}

function Assert-Payload([string]$InstallPath, [string]$PayloadPath, [string]$LicensePath) {
    foreach ($name in @('CodexQuotaStrip.exe', 'Start-With-Codex.ps1', 'THIRD_PARTY_NOTICES.md', 'LICENSE')) {
        $expected = if ($name -eq 'LICENSE') { $LicensePath } else { Join-Path $PayloadPath $name }
        $actual = Join-Path $InstallPath $name
        Assert-Condition (Test-Path -LiteralPath $actual -PathType Leaf) "Installed payload missing: $name"
        Assert-Condition ((Get-FileHash -LiteralPath $expected).Hash -eq (Get-FileHash -LiteralPath $actual).Hash) "Installed payload hash mismatch: $name"
    }
}

# 不只判断非零退出码；错误日志必须明确说明目标程序仍在运行，避免其他失败蒙混过关。
function Invoke-ExpectedRunningFailure([string]$FilePath, [string[]]$Arguments, [string]$LogPath, [string]$Label) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FilePath
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { [void]$info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($info)
    try {
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $outputText = $outputTask.GetAwaiter().GetResult()
        $errorText = $errorTask.GetAwaiter().GetResult()
        if ($outputText) { Write-Output $outputText.TrimEnd() }
        if ($errorText) { [Console]::Error.WriteLine($errorText.TrimEnd()) }
        Assert-Condition ($process.ExitCode -ne 0) "$Label unexpectedly succeeded while the installed application was running."
        Assert-Condition (Test-Path -LiteralPath $LogPath) "$Label did not produce its diagnostic log."
        Assert-Condition ((Get-Content -LiteralPath $LogPath -Raw) -match 'still running|仍在运行') "$Label failed without reporting the expected running application."
        Write-Output "PASS: $Label refused the running fixture (exit $($process.ExitCode))."
    } finally { $process.Dispose() }
}

function Start-HeadlessFixture([string]$Executable, [string]$FixtureName) {
    $fixtureHome = Join-Path $testRoot $FixtureName
    [void](New-Item -ItemType Directory -Path (Join-Path $fixtureHome 'sessions') -Force)
    $status = Join-Path $fixtureHome 'status.json'
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Executable
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    # 环境覆盖仅属于这一个子进程，不改变真实 CODEX_HOME、LOCALAPPDATA 或主会话。
    $info.Environment['CODEX_HOME'] = $fixtureHome
    $info.Environment['LOCALAPPDATA'] = $fixtureHome
    foreach ($argument in @('--headless', '--local-only', '--home', $fixtureHome, '--status', $status)) {
        [void]$info.ArgumentList.Add($argument)
    }
    $owned = [pscustomobject]@{ Process = [Diagnostics.Process]::Start($info); Executable = $Executable; Home = $fixtureHome }
    $fixtures.Add($owned)
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (-not (Test-Path -LiteralPath $status) -and -not $owned.Process.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
    }
    Assert-Condition (-not $owned.Process.HasExited -and (Test-Path -LiteralPath $status)) "Headless fixture failed to initialize: $FixtureName"
    return $owned
}

function Stop-HeadlessFixture($Fixture) {
    if ($Fixture.Process.HasExited) { return }
    # 只向已保存句柄且路径匹配的本次 PID 发退出命令；必要时仅终止这个进程对象。
    Assert-Condition ($Fixture.Process.Path -eq $Fixture.Executable) 'Refusing to stop a fixture whose executable path changed.'
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Fixture.Executable
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.Environment['CODEX_HOME'] = $Fixture.Home
    $info.Environment['LOCALAPPDATA'] = $Fixture.Home
    foreach ($argument in @('--headless', '--local-only', '--home', $Fixture.Home, '--quit')) {
        [void]$info.ArgumentList.Add($argument)
    }
    $quitProcess = [Diagnostics.Process]::Start($info)
    try { $quitProcess.WaitForExit() } finally { $quitProcess.Dispose() }
    if (-not $Fixture.Process.WaitForExit(5000)) {
        $Fixture.Process.Kill()
        Assert-Condition ($Fixture.Process.WaitForExit(5000)) 'Owned headless fixture did not exit.'
    }
}

function Assert-TestRegistration([string]$ExpectedVersion) {
    Assert-Condition (Test-Path -LiteralPath $registryPath) 'Test uninstall registration missing in HKCU.'
    $registration = Get-ItemProperty -LiteralPath $registryPath
    Assert-Condition ($registration.QuotaStripInstallerTestRoot -eq $testRoot) 'Test registration does not belong to this isolated run.'
    Assert-Condition ($registration.DisplayVersion -eq $ExpectedVersion) 'Registered version does not match the installed build.'
    Assert-Condition ($registration.InstallLocation.TrimEnd('\') -eq $installPath) 'Registered installation path escaped the fixture.'
    Assert-Condition ($registration.UninstallString -eq ('"' + (Join-Path $installPath 'unins000.exe') + '"')) 'Uninstall command escaped the fixture.'
}

try {
    Assert-Condition ($PSVersionTable.PSVersion.Major -ge 7) 'Run this test with PowerShell 7.'
    Assert-Condition ([OperatingSystem]::IsWindows()) 'Windows installer tests require Windows.'
    # 安装器写入 64 位卸载视图，使用同一视图验证，避免 x86 PowerShell 的注册表重定向。
    Assert-Condition ([Environment]::Is64BitProcess) 'Run this test with 64-bit PowerShell 7 to verify the 64-bit HKCU uninstall registry view.'
    $Version = $Version.TrimStart('v')
    $repoRoot = Split-Path $PSScriptRoot -Parent
    $PayloadDirectory = [IO.Path]::GetFullPath($PayloadDirectory)
    $testId = [Guid]::NewGuid().ToString('N')
    $testRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) "CodexQuotaStrip-installer-$testId"))
    $tempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    Assert-Condition ($testRoot.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) 'Fixture must be inside TEMP.'
    $installPath = Join-Path $testRoot 'app'
    $payloadPath = Join-Path $testRoot 'payload'
    $testAppId = "CodexQuotaStrip.Test.$testId"
    $registryPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\${testAppId}_is1"
    Assert-Condition (-not (Test-Path -LiteralPath $registryPath)) 'Refusing to reuse an existing test registration.'
    [void](New-Item -ItemType Directory -Path $payloadPath -Force)
    foreach ($name in @('CodexQuotaStrip.exe', 'Start-With-Codex.ps1', 'THIRD_PARTY_NOTICES.md')) {
        Copy-Item -LiteralPath (Join-Path $PayloadDirectory $name) -Destination $payloadPath
    }
    $licensePath = Join-Path $repoRoot 'LICENSE'
    $pwsh = (Get-Process -Id $PID).Path
    $buildScript = Join-Path $PSScriptRoot 'Build-WindowsInstaller.ps1'
    $buildArguments = @('-NoProfile', '-File', $buildScript, '-PayloadDirectory', $payloadPath,
        '-TestBuild', '-TestRoot', $testRoot, '-TestAppId', $testAppId)
    if ($CompilerPath) { $buildArguments += @('-CompilerPath', $CompilerPath) }
    # 先装低版本，再用发布版本覆盖；两个包使用相同测试 AppId，仅此随机项可被卸载。
    $baseVersion = '0.0.0-preview.1'
    $baseOutput = Join-Path $testRoot 'base'
    $upgradeOutput = Join-Path $testRoot 'upgrade'
    Invoke-CheckedProcess $pwsh ($buildArguments + @('-Version', $baseVersion, '-OutputDirectory', $baseOutput)) 'Base installer compilation'
    Invoke-CheckedProcess $pwsh ($buildArguments + @('-Version', $Version, '-OutputDirectory', $upgradeOutput)) 'Upgrade installer compilation'
    $steps.Add('Compiled two independently isolated test installers.')
    $setupName = 'CodexQuotaStrip-windows-x64-test-setup.exe'
    $silentArguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/SP-', '/NORESTART', '/RESTARTEXITCODE=3010',
        '/NOCLOSEAPPLICATIONS', '/NORESTARTAPPLICATIONS', "/DIR=$installPath")
    Invoke-CheckedProcess (Join-Path $baseOutput $setupName) ($silentArguments + @('/LANG=english', '/TASKS=', "/LOG=$(Join-Path $testRoot 'install.log')")) 'Silent install'
    Assert-Payload $installPath $payloadPath $licensePath
    Assert-TestRegistration $baseVersion
    Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $testRoot 'desktop\CodexQuotaStrip.lnk'))) 'Desktop shortcut must be opt-in.'
    # skipifsilent 禁用结束启动；不启动真实程序，也不访问账号或执行 Windows UI 自动化。
    $running = @(Get-CimInstance Win32_Process -Filter "Name='CodexQuotaStrip.exe'" | Where-Object { $_.ExecutablePath -eq (Join-Path $installPath 'CodexQuotaStrip.exe') })
    Assert-Condition ($running.Count -eq 0) 'Silent setup unexpectedly launched the installed application.'
    $shell = New-Object -ComObject WScript.Shell
    $programLink = $shell.CreateShortcut((Join-Path $testRoot 'start-menu\CodexQuotaStrip.lnk'))
    Assert-Condition ($programLink.TargetPath -eq (Join-Path $installPath 'CodexQuotaStrip.exe')) 'Program shortcut target mismatch.'
    $settingsLink = $shell.CreateShortcut((Join-Path $testRoot 'start-menu\CodexQuotaStrip Settings.lnk'))
    Assert-Condition ($settingsLink.Arguments -eq '--settings') 'Settings shortcut must open --settings.'
    $removeLink = $shell.CreateShortcut((Join-Path $testRoot 'start-menu\Uninstall CodexQuotaStrip.lnk'))
    Assert-Condition ($removeLink.TargetPath -eq (Join-Path $installPath 'unins000.exe')) 'Uninstall shortcut target mismatch.'
    $steps.Add('Installed payload SHA256, HKCU registration, shortcuts and disabled silent launch verified.')

    # 两个独立 home 防止单实例复用；另一路径实例在升级/卸载全流程中必须保持存活。
    $otherDirectory = Join-Path $testRoot 'other-installation'
    [void](New-Item -ItemType Directory -Path $otherDirectory -Force)
    $otherExecutable = Join-Path $otherDirectory 'CodexQuotaStrip.exe'
    Copy-Item -LiteralPath (Join-Path $payloadPath 'CodexQuotaStrip.exe') -Destination $otherExecutable
    $otherFixture = Start-HeadlessFixture $otherExecutable 'other-home'
    $installedFixture = Start-HeadlessFixture (Join-Path $installPath 'CodexQuotaStrip.exe') 'installed-home'
    $blockedUpgradeLog = Join-Path $testRoot 'blocked-upgrade.log'
    Invoke-ExpectedRunningFailure (Join-Path $upgradeOutput $setupName) ($silentArguments + @('/LANG=english', '/TASKS=', "/LOG=$blockedUpgradeLog")) $blockedUpgradeLog 'Upgrade'
    Assert-Payload $installPath $payloadPath $licensePath
    Assert-TestRegistration $baseVersion
    Assert-Condition (-not $installedFixture.Process.HasExited -and -not $otherFixture.Process.HasExited) 'Refused upgrade stopped a fixture.'
    $blockedUninstallLog = Join-Path $testRoot 'blocked-uninstall.log'
    Invoke-ExpectedRunningFailure (Join-Path $installPath 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$blockedUninstallLog") $blockedUninstallLog 'Uninstall'
    Assert-Payload $installPath $payloadPath $licensePath
    Assert-TestRegistration $baseVersion
    Assert-Condition (-not $installedFixture.Process.HasExited -and -not $otherFixture.Process.HasExited) 'Refused uninstall stopped a fixture.'
    Stop-HeadlessFixture $installedFixture
    $steps.Add('Same-path running process blocked upgrade/uninstall with intact payload; neither owned process was stopped.')

    # 合成用户设置与额外文件均只写入临时目录，覆盖卸载不可递归删目录的边界。
    $settingsPath = Join-Path $installPath 'settings.ini'
    [IO.File]::WriteAllText($settingsPath, "[refresh]`r`nonline=0`r`nseconds=97`r`n[display]`r`nlanguage=en`r`n")
    [void](New-Item -ItemType Directory -Path (Join-Path $installPath 'custom') -Force)
    $sentinels = @($settingsPath, (Join-Path $installPath 'custom\startup-hook.ps1'),
        (Join-Path $testRoot 'synthetic-codex-account.json'), (Join-Path $installPath 'user-file.txt'))
    [IO.File]::WriteAllText($sentinels[1], '# Synthetic custom hook: preserve')
    [IO.File]::WriteAllText($sentinels[2], '{"synthetic":true}')
    [IO.File]::WriteAllText($sentinels[3], 'Synthetic arbitrary user file: preserve')
    $sentinelHashes = @{}
    foreach ($file in $sentinels) { $sentinelHashes[$file] = (Get-FileHash -LiteralPath $file).Hash }
    [IO.File]::WriteAllText((Join-Path $installPath 'THIRD_PARTY_NOTICES.md'), 'Old installed payload to be replaced during upgrade')
    Invoke-CheckedProcess (Join-Path $upgradeOutput $setupName) ($silentArguments + @('/LANG=chinesesimplified', '/TASKS=desktopicon', "/LOG=$(Join-Path $testRoot 'upgrade.log')")) 'Silent upgrade'
    Assert-Payload $installPath $payloadPath $licensePath
    Assert-TestRegistration $Version
    Assert-Condition (-not $otherFixture.Process.HasExited) 'Upgrade stopped the application from another installation folder.'
    foreach ($file in $sentinels) {
        Assert-Condition ((Get-FileHash -LiteralPath $file).Hash -eq $sentinelHashes[$file]) "Upgrade changed user file: $file"
    }
    Assert-Condition (Test-Path -LiteralPath (Join-Path $testRoot 'desktop\CodexQuotaStrip.lnk')) 'Explicit desktop shortcut task was not honored.'
    $steps.Add('Upgrade replaced payload/version and preserved synthetic settings, hook, account and arbitrary user file.')

    # 只执行当前随机 AppId 下、安装目录内的卸载程序；从不删除真实 HKCU 安装项。
    Assert-TestRegistration $Version
    Invoke-CheckedProcess (Join-Path $installPath 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$(Join-Path $testRoot 'uninstall.log')") 'Silent uninstall'
    $uninstalled = $true
    foreach ($name in @('CodexQuotaStrip.exe', 'Start-With-Codex.ps1', 'THIRD_PARTY_NOTICES.md', 'LICENSE')) {
        Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $installPath $name))) "Uninstall left installed payload: $name"
    }
    Assert-Condition (-not (Test-Path -LiteralPath $registryPath)) 'Test uninstall registration remained.'
    Assert-Condition (-not $otherFixture.Process.HasExited) 'Uninstall stopped the application from another installation folder.'
    foreach ($file in $sentinels) {
        Assert-Condition ((Get-FileHash -LiteralPath $file).Hash -eq $sentinelHashes[$file]) "Uninstall changed user file: $file"
    }
    $remainingLinks = @(Get-ChildItem -LiteralPath $testRoot -Filter '*.lnk' -File -Recurse)
    Assert-Condition ($remainingLinks.Count -eq 0) 'Uninstall left installed shortcuts.'
    $steps.Add('Uninstall removed payload, shortcuts and its registration; preserved all synthetic user files.')
    Write-Output "PASS: Windows installer install/upgrade/uninstall. Evidence: $testRoot"
} catch {
    if ($_.Exception.Data.Contains('ExitCode')) { $exitCode = [int]$_.Exception.Data['ExitCode'] } else { $exitCode = 1 }
    Write-Error $_ -ErrorAction Continue
} finally {
    foreach ($fixture in $fixtures) {
        try { Stop-HeadlessFixture $fixture } catch {
            Write-Warning "Owned fixture cleanup failed: $($_.Exception.Message)"
            if ($exitCode -eq 0) { $exitCode = 1 }
        } finally { $fixture.Process.Dispose() }
    }
    # 失败时仅清理可证明属于本次运行的安装；保留测试目录和日志供 CI 上传。
    if (-not $uninstalled -and $registryPath -and (Test-Path -LiteralPath $registryPath)) {
        try {
            $entry = Get-ItemProperty -LiteralPath $registryPath
            if ($entry.QuotaStripInstallerTestRoot -eq $testRoot -and
                $entry.InstallLocation.TrimEnd('\') -eq $installPath -and
                (Test-Path -LiteralPath (Join-Path $installPath 'unins000.exe'))) {
                Invoke-CheckedProcess (Join-Path $installPath 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$(Join-Path $testRoot 'cleanup.log')") 'Fixture cleanup'
            }
        } catch { Write-Warning "Isolated cleanup failed; fixture retained at $testRoot. $($_.Exception.Message)" }
    }
    if ($testRoot -and (Test-Path -LiteralPath $testRoot)) {
        @{ version = $Version; exitCode = $exitCode; testRoot = $testRoot; steps = @($steps) } |
            ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $testRoot 'test-results.json') -Encoding utf8
    }
}
exit $exitCode
