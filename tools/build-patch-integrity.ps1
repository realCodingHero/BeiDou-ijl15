param(
    [string]$ToolchainRoot = 'C:\Game\BeiDou-Server\tools\msvc',
    [string]$ClientExe = 'C:\Game\BeiDou-Client\BeiDou.exe'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vcRoot = (Get-ChildItem -LiteralPath (Join-Path $ToolchainRoot 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$sdkRoot = Join-Path $ToolchainRoot 'Windows Kits\10'
$sdkVersion = (Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$out = Join-Path $repoRoot 'out\patch-integrity'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$savedPath, $savedInclude, $savedLib = $env:PATH, $env:INCLUDE, $env:LIB
try {
    $env:PATH = (Join-Path $vcRoot 'bin\Hostx64\x86') + ';' + $env:PATH
    $env:INCLUDE = (@((Join-Path $vcRoot 'include')) + @('ucrt','shared','um','winrt' | ForEach-Object { Join-Path $sdkRoot "Include\$sdkVersion\$_" })) -join ';'
    $env:LIB = @((Join-Path $vcRoot 'lib\x86'), (Join-Path $sdkRoot "Lib\$sdkVersion\ucrt\x86"), (Join-Path $sdkRoot "Lib\$sdkVersion\um\x86")) -join ';'
    $bytes = [IO.File]::ReadAllBytes((Join-Path $repoRoot 'ezorsia\Client.cpp'))
    $encoded = Join-Path $out 'Client.cpp'
    [IO.File]::WriteAllBytes($encoded, [byte[]](@(0xEF,0xBB,0xBF)+$bytes))
    $compiler = Join-Path $vcRoot 'bin\Hostx64\x86\cl.exe'
    $patchCompilerArgs = @('/nologo','/O2','/Gy','/Gw','/MD','/EHsc','/std:c++17','/W3','/source-charset:gbk','/execution-charset:gbk','/DWIN32','/D_WINDOWS','/DNOMINMAX','/D_CRT_SECURE_NO_WARNINGS','/DRESOLUTION_PATCH_TESTING',('/I'+(Join-Path $repoRoot 'ezorsia')))
    & $compiler @patchCompilerArgs (Join-Path $repoRoot 'tests\ResolutionPatchTests.cpp') $encoded (Join-Path $repoRoot 'ezorsia\ResolutionPatch.cpp') (Join-Path $repoRoot 'ezorsia\Memory.cpp') (Join-Path $repoRoot 'ezorsia\WindowScaling.cpp') ('/Fo'+$out+'\') /link /MACHINE:X86 /OPT:REF /OPT:ICF user32.lib ws2_32.lib imm32.lib (Join-Path $repoRoot 'detours\detours.lib') ('/OUT:'+(Join-Path $out 'ResolutionPatchTests.exe'))
    if ($LASTEXITCODE -ne 0) { throw 'Patch integrity fixture build failed' }
    & (Join-Path $out 'ResolutionPatchTests.exe') --exe $ClientExe
    if ($LASTEXITCODE -ne 0) { throw 'Patch integrity tests failed' }
} finally {
    $env:PATH, $env:INCLUDE, $env:LIB = $savedPath, $savedInclude, $savedLib
}
