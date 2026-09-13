param(
    [string]$ToolchainRoot = 'C:\Game\BeiDou-Server\tools\msvc',
    [string]$KaentakeRoot = 'C:\Game\BeiDou-Server\tools\kaentake-src',
    [string]$CMake = 'C:\Game\BeiDou-Server\tools\pybuild\cmake\data\bin\cmake.exe',
    [string]$Ninja = 'C:\Game\BeiDou-Server\tools\pybuild\bin\ninja.exe'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vcRoot = (Get-ChildItem -LiteralPath (Join-Path $ToolchainRoot 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$sdkRoot = Join-Path $ToolchainRoot 'Windows Kits\10'
$sdkVersion = (Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$out = Join-Path $repoRoot 'out\itemeff'
$savedPath, $savedInclude, $savedLib = $env:PATH, $env:INCLUDE, $env:LIB
try {
    $env:PATH = (Join-Path $vcRoot 'bin\Hostx64\x86') + ';' + (Join-Path $sdkRoot "bin\$sdkVersion\x64") + ';' + $env:PATH
    $env:INCLUDE = (@((Join-Path $vcRoot 'include')) + @('ucrt','shared','um','winrt' | ForEach-Object { Join-Path $sdkRoot "Include\$sdkVersion\$_" })) -join ';'
    $env:LIB = @((Join-Path $vcRoot 'lib\x86'), (Join-Path $sdkRoot "Lib\$sdkVersion\ucrt\x86"), (Join-Path $sdkRoot "Lib\$sdkVersion\um\x86")) -join ';'
    & $CMake -S (Join-Path $repoRoot 'itemeff') -B $out -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_CXX_COMPILER=$(Join-Path $vcRoot 'bin\Hostx64\x86\cl.exe')" -DCMAKE_BUILD_TYPE=Release "-DKAENTAKE_ROOT=$KaentakeRoot"
    if ($LASTEXITCODE -ne 0) { throw 'ItemEff configuration failed' }
    & $CMake --build $out
    if ($LASTEXITCODE -ne 0) { throw 'ItemEff build failed' }
    & (Join-Path $out 'ItemEffLifetimeTests.exe') --dll (Join-Path $out 'ItemEffLifetimeFixture.dll')
    if ($LASTEXITCODE -ne 0) { throw 'ItemEff lifetime regression failed' }
} finally {
    $env:PATH, $env:INCLUDE, $env:LIB = $savedPath, $savedInclude, $savedLib
}
