param(
    [string]$ToolchainRoot = 'C:\Game\BeiDou-Server\tools\msvc',
    [string]$Python = 'C:\Users\trans\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',
    [switch]$SkipGpuTests
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vcRoot = (Get-ChildItem -LiteralPath (Join-Path $ToolchainRoot 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$sdkRoot = Join-Path $ToolchainRoot 'Windows Kits\10'
$sdkVersion = (Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$compiler = Join-Path $vcRoot 'bin\Hostx64\x86\cl.exe'
$outDir = Join-Path $repoRoot 'out\neural'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$savedPath, $savedInclude, $savedLib = $env:PATH, $env:INCLUDE, $env:LIB
try {
    $env:PATH = (Join-Path $vcRoot 'bin\Hostx64\x86') + ';' + $env:PATH
    $env:INCLUDE = (@((Join-Path $vcRoot 'include')) + @('ucrt','shared','um','winrt' | ForEach-Object { Join-Path $sdkRoot "Include\$sdkVersion\$_" })) -join ';'
    $env:LIB = @((Join-Path $vcRoot 'lib\x86'), (Join-Path $sdkRoot "Lib\$sdkVersion\ucrt\x86"), (Join-Path $sdkRoot "Lib\$sdkVersion\um\x86")) -join ';'
    $common = @('/nologo','/O2','/MD','/EHsc','/std:c++17','/W3','/utf-8','/DWIN32','/D_WINDOWS','/DNOMINMAX','/D_CRT_SECURE_NO_WARNINGS')
    & $Python -B (Join-Path $repoRoot 'tests\upscaling\DeployConfigTests.py')
    if ($LASTEXITCODE -ne 0) { throw 'Deployment config tests failed' }
    & $Python -B (Join-Path $repoRoot 'tools\generate-cunny.py') --out (Join-Path $outDir 'shaders')
    if ($LASTEXITCODE -ne 0) { throw 'Shader generation failed' }
    Copy-Item -Path (Join-Path $repoRoot 'upscaling\*.hlsl') -Destination (Join-Path $outDir 'shaders')
    & $compiler @common (Join-Path $repoRoot 'tests\upscaling\CompileShaders.cpp') ('/Fo' + $outDir + '\') /link /MACHINE:X86 d3dcompiler.lib ('/OUT:' + (Join-Path $outDir 'CompileShaders.exe'))
    if ($LASTEXITCODE -ne 0) { throw 'Shader compiler build failed' }
    & (Join-Path $outDir 'CompileShaders.exe') (Join-Path $outDir 'shaders')
    if ($LASTEXITCODE -ne 0) { throw 'Shader compilation failed' }
    & $Python -B (Join-Path $repoRoot 'tools\embed-upscaling-shaders.py') (Join-Path $outDir 'shaders') (Join-Path $outDir 'NeuralShaders.h')
    if ($LASTEXITCODE -ne 0) { throw 'Shader embedding failed' }
    $common += @('/DD3D8TO9NOLOG',('/I' + (Join-Path $repoRoot 'upscaling')),('/I' + $outDir),('/I' + (Join-Path $repoRoot 'ezorsia')),('/I' + (Join-Path $repoRoot 'third_party\d3d8to9\source')))
    $sources = @(Get-ChildItem -Path (Join-Path $repoRoot 'third_party\d3d8to9\source\*.cpp'),(Join-Path $repoRoot 'upscaling\*.cpp') | ForEach-Object FullName)
    & $compiler @common /LD @sources ('/Fo' + $outDir + '\') /link /MACHINE:X86 d3d9.lib user32.lib gdi32.lib advapi32.lib ('/DEF:' + (Join-Path $repoRoot 'third_party\d3d8to9\res\d3d8.def')) ('/IMPLIB:' + (Join-Path $outDir 'd3d8.lib')) ('/OUT:' + (Join-Path $outDir 'BeiDouUpscale.dll'))
    if ($LASTEXITCODE -ne 0) { throw 'Upscaling DLL build failed' }
    & $compiler @common (Join-Path $repoRoot 'tests\upscaling\RendererTests.cpp') (Join-Path $outDir 'UpscaleRenderer.obj') (Join-Path $outDir 'UpscaleConfig.obj') ('/Fo' + $outDir + '\') /link /MACHINE:X86 d3d9.lib user32.lib ('/OUT:' + (Join-Path $outDir 'RendererTests.exe'))
    if ($LASTEXITCODE -ne 0) { throw 'Renderer fixture build failed' }
    New-Item -ItemType Directory -Force -Path (Join-Path $outDir 'fixture') | Out-Null
    & $compiler @common (Join-Path $repoRoot 'tests\upscaling\ProxyTests.cpp') ('/Fo' + $outDir + '\') /link /MACHINE:X86 user32.lib ('/OUT:' + (Join-Path $outDir 'fixture\ProxyTests.exe'))
    if ($LASTEXITCODE -ne 0) { throw 'Proxy fixture build failed' }
    & $compiler @common /LD (Join-Path $repoRoot 'tests\upscaling\LoaderCaller.cpp') ('/Fo' + $outDir + '\') /link /MACHINE:X86 user32.lib ('/IMPLIB:' + (Join-Path $outDir 'fixture\LoaderCaller.lib')) ('/OUT:' + (Join-Path $outDir 'fixture\Gr2D_DX8.dll'))
    if ($LASTEXITCODE -ne 0) { throw 'Loader caller build failed' }
    & $compiler @common (Join-Path $repoRoot 'tests\upscaling\LoaderTests.cpp') (Join-Path $repoRoot 'ezorsia\UpscaleLoader.cpp') ('/Fo' + $outDir + '\') /link /MACHINE:X86 (Join-Path $repoRoot 'detours\detours.lib') user32.lib ('/OUT:' + (Join-Path $outDir 'fixture\LoaderTests.exe'))
    if ($LASTEXITCODE -ne 0) { throw 'Loader fixture build failed' }
    & (Join-Path $outDir 'fixture\LoaderTests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Scoped loader tests failed' }

    & $compiler @common (Join-Path $repoRoot 'tests\upscaling\Benchmark.cpp') (Join-Path $outDir 'UpscaleRenderer.obj') (Join-Path $outDir 'UpscaleConfig.obj') ('/Fo' + $outDir + '\') /link /MACHINE:X86 d3d9.lib user32.lib ('/OUT:' + (Join-Path $outDir 'Benchmark.exe'))
    if ($LASTEXITCODE -ne 0) { throw 'Benchmark build failed' }

    & $compiler @common (Join-Path $repoRoot 'tests\upscaling\FramePacerTests.cpp') ('/Fo' + $outDir + '\') /link /MACHINE:X86 ('/OUT:' + (Join-Path $outDir 'FramePacerTests.exe'))
    if ($LASTEXITCODE -ne 0) { throw 'Frame pacer fixture build failed' }
    & (Join-Path $outDir 'FramePacerTests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Frame pacing tests failed' }
    & $Python -B (Join-Path $repoRoot 'tests\upscaling\reference.py') (Join-Path $outDir 'reference') prepare
    if ($LASTEXITCODE -ne 0) { throw 'Reference fixture creation failed' }
    if (!$SkipGpuTests) {
        & (Join-Path $outDir 'RendererTests.exe') (Join-Path $outDir 'reference')
        if ($LASTEXITCODE -ne 0) { throw 'GPU renderer tests failed' }
        & $Python -B (Join-Path $repoRoot 'tests\upscaling\reference.py') (Join-Path $outDir 'reference') compare
        if ($LASTEXITCODE -ne 0) { throw 'Independent reference comparison failed' }
        foreach ($mode in @('enabled','linear','disabled')) {
            & (Join-Path $outDir 'fixture\ProxyTests.exe') (Join-Path $outDir 'BeiDouUpscale.dll') $mode
            if ($LASTEXITCODE -ne 0) { throw "Proxy tests failed: $mode" }
        }
    }
} finally {
    $env:PATH, $env:INCLUDE, $env:LIB = $savedPath, $savedInclude, $savedLib
}
