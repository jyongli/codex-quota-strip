# 部分机器只有某版本的运行库目录；必须验证对应头文件和 x64 链接库齐全。
function Find-QuotaWindowsSdk {
    param([Parameter(Mandatory = $true)][string]$KitsPath)
    $libraryRoot = Join-Path $KitsPath 'Lib'
    if (Test-Path -LiteralPath $libraryRoot -PathType Container) {
        $candidates = Get-ChildItem -LiteralPath $libraryRoot -Directory |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending
        foreach ($candidate in $candidates) {
            $includeRoot = Join-Path $KitsPath ('Include/' + $candidate.Name)
            $required = @(
                (Join-Path $includeRoot 'um/Windows.h'),
                (Join-Path $includeRoot 'ucrt/stdio.h'),
                (Join-Path $includeRoot 'shared/windef.h'),
                (Join-Path $candidate.FullName 'um/x64/kernel32.lib'),
                (Join-Path $candidate.FullName 'ucrt/x64/libucrt.lib')
            )
            if (@($required | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) }).Count -eq 0) { return $candidate }
        }
    }
    throw 'No complete Windows SDK for x64 was found. Install Windows SDK headers and libraries through Visual Studio Installer.'
}
