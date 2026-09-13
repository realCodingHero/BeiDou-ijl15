param(
    [Parameter(Mandatory = $true)][string]$ToolchainRoot,
    [switch]$TestsOnly
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vcRoot = (Get-ChildItem -LiteralPath (Join-Path $ToolchainRoot 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$sdkRoot = Join-Path $ToolchainRoot 'Windows Kits\10'
$sdkVersion = (Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$compiler = Join-Path $vcRoot 'bin\Hostx64\x86\cl.exe'
$outDir = Join-Path $repoRoot 'out\window-scaling'
$testDir = Join-Path $outDir 'tests'
$objDir = Join-Path $outDir 'obj'
New-Item -ItemType Directory -Force -Path $outDir, $testDir, $objDir | Out-Null
$savedPath = $env:PATH
$savedInclude = $env:INCLUDE
$savedLib = $env:LIB
try {
    $env:PATH = (Join-Path $vcRoot 'bin\Hostx64\x86') + ';' + $env:PATH
    $env:INCLUDE = (@((Join-Path $vcRoot 'include')) + @('ucrt','shared','um','winrt' | ForEach-Object { Join-Path $sdkRoot "Include\$sdkVersion\$_" })) -join ';'
    $env:LIB = (@((Join-Path $vcRoot 'lib\x86'), (Join-Path $sdkRoot "Lib\$sdkVersion\ucrt\x86"), (Join-Path $sdkRoot "Lib\$sdkVersion\um\x86")) -join ';')
    $common = @('/nologo','/O2','/MD','/EHsc','/std:c++17','/W3','/source-charset:gbk','/execution-charset:gbk','/DWIN32','/D_WINDOWS','/D_CRT_SECURE_NO_WARNINGS',('/I' + (Join-Path $repoRoot 'ezorsia')))
    $detours = Join-Path $repoRoot 'detours\detours.lib'
    & $compiler @common /LD (Join-Path $repoRoot 'tests\RendererResizeFixture.cpp') ('/Fo' + $testDir + '\') /link /MACHINE:X86 user32.lib ('/IMPLIB:' + (Join-Path $testDir 'RendererResizeFixture.lib')) ('/OUT:' + (Join-Path $testDir 'Gr2D_DX8.dll'))
    if ($LASTEXITCODE -ne 0) { throw 'Renderer fixture build failed' }
    & $compiler @common (Join-Path $repoRoot 'tests\WindowScalingTests.cpp') (Join-Path $repoRoot 'ezorsia\WindowScaling.cpp') ('/Fo' + $testDir + '\') /link /MACHINE:X86 $detours user32.lib ('/OUT:' + (Join-Path $testDir 'WindowScalingTests.exe'))
    if ($LASTEXITCODE -ne 0) { throw 'Window scaling test build failed' }
    & (Join-Path $testDir 'WindowScalingTests.exe') (Join-Path $testDir 'Gr2D_DX8.dll')
    if ($LASTEXITCODE -ne 0) { throw 'Window scaling tests failed' }
    if (!$TestsOnly) {
        # Client.cpp is UTF-8, while its legacy headers are GBK. A BOM tells MSVC
        # to decode this translation unit correctly without re-encoding the headers.
        $clientSource = Join-Path $repoRoot 'ezorsia\Client.cpp'
        $clientBytes = [System.IO.File]::ReadAllBytes($clientSource)
        $strictUtf8 = [System.Text.UTF8Encoding]::new($false, $true)
        $null = $strictUtf8.GetString($clientBytes)
        $encodedDir = Join-Path $outDir 'encoded-source'
        New-Item -ItemType Directory -Force -Path $encodedDir | Out-Null
        $encodedClient = Join-Path $encodedDir 'Client.cpp'
        if ($clientBytes.Length -ge 3 -and $clientBytes[0] -eq 0xEF -and $clientBytes[1] -eq 0xBB -and $clientBytes[2] -eq 0xBF) {
            [System.IO.File]::WriteAllBytes($encodedClient, $clientBytes)
        } else {
            [System.IO.File]::WriteAllBytes($encodedClient, [byte[]](@(0xEF, 0xBB, 0xBF) + $clientBytes))
        }
        $sources = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'ezorsia') -Filter '*.cpp' | ForEach-Object {
            if ($_.Name -eq 'Client.cpp') { $encodedClient } else { $_.FullName }
        })
        & $compiler @common /DNDEBUG /D_USRDLL /DEZORSIA_EXPORTS /LD @sources ('/Fo' + $objDir + '\') /link /MACHINE:X86 $detours imm32.lib ws2_32.lib user32.lib gdi32.lib advapi32.lib ole32.lib oleaut32.lib ('/IMPLIB:' + (Join-Path $outDir 'ijl15.lib')) ('/OUT:' + (Join-Path $outDir 'ijl15.dll'))
        if ($LASTEXITCODE -ne 0) { throw 'Client DLL build failed' }
        Get-FileHash -LiteralPath (Join-Path $outDir 'ijl15.dll') -Algorithm SHA256
    }
} finally {
    $env:PATH = $savedPath
    $env:INCLUDE = $savedInclude
    $env:LIB = $savedLib
}
