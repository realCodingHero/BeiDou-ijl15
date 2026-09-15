param(
 [string]$Client='C:\Game\BeiDou-Client-research',
 [string]$ToolchainRoot='C:\Game\BeiDou-Server\tools\msvc',
 [string]$WzInclude='C:\Game\BeiDou-Server\tools\kaentake-src\external\WzLib\include'
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$toolchain=$ToolchainRoot
$vc=(Get-ChildItem -LiteralPath "$toolchain\VC\Tools\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$sdk="$toolchain\Windows Kits\10";$version=(Get-ChildItem -LiteralPath "$sdk\Include" -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$out=Join-Path $repoRoot 'out\world-native';New-Item -ItemType Directory -Force $out | Out-Null
$savedPath,$savedInclude,$savedLib=$env:PATH,$env:INCLUDE,$env:LIB
try {
 $env:PATH="$vc\bin\Hostx64\x86;"+$env:PATH
 $env:INCLUDE=(@("$vc\include")+@('ucrt','shared','um','winrt' | ForEach-Object {"$sdk\Include\$version\$_"})) -join ';'
 $env:LIB=@("$vc\lib\x86","$sdk\Lib\$version\ucrt\x86","$sdk\Lib\$version\um\x86") -join ';'
 & "$vc\bin\Hostx64\x86\cl.exe" /nologo /O2 /MD /EHsc /std:c++17 /DNOMINMAX /DWORLD_VIEWPORT_TESTING /D_CRT_SECURE_NO_WARNINGS /I"$repoRoot\ezorsia" /I"$repoRoot\third_party\d3d8to9\source" /I"$WzInclude" "$repoRoot\tests\WorldViewportNativeTests.cpp" "$repoRoot\ezorsia\WorldViewport.cpp" "$repoRoot\ezorsia\UpscaleLoader.cpp" /Fo"$out\" /link /MACHINE:X86 "$repoRoot\detours\detours.lib" user32.lib oleaut32.lib ole32.lib comsuppw.lib /OUT:"$out\WorldViewportNativeTests.exe"
 if($LASTEXITCODE -ne 0){throw 'Native viewport fixture build failed'}
 Copy-Item -LiteralPath (Join-Path $Client 'BeiDouUpscale.dll') -Destination (Join-Path $out 'BeiDouUpscale.dll') -Force
 & "$out\WorldViewportNativeTests.exe" $Client
 if($LASTEXITCODE -ne 0){throw "Native viewport fixture failed: $LASTEXITCODE"}
}finally{$env:PATH,$env:INCLUDE,$env:LIB=$savedPath,$savedInclude,$savedLib}
