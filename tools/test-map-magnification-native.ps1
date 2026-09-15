param([string]$Client='C:\Game\BeiDou-Client-research',
 [string]$ToolchainRoot='C:\Game\BeiDou-Server\tools\msvc',
 [string]$WzInclude='C:\Game\BeiDou-Server\tools\kaentake-src\external\WzLib\include',
 [string]$ViewportSource='', [switch]$Baseline, [switch]$BuffRegression, [switch]$ExpectBuffBug, [string]$FixturePath='')
$ErrorActionPreference='Stop';$repoRoot=Split-Path -Parent $PSScriptRoot
if(($Baseline -and $BuffRegression) -or ($ExpectBuffBug -and !$BuffRegression)){throw 'Buff regression uses current scenery metadata; ExpectBuffBug requires BuffRegression'}
if(!$ViewportSource){$ViewportSource=Join-Path $repoRoot 'ezorsia\WorldViewport.cpp'}
$vc=(Get-ChildItem -LiteralPath "$ToolchainRoot\VC\Tools\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$sdk="$ToolchainRoot\Windows Kits\10";$version=(Get-ChildItem -LiteralPath "$sdk\Include" -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$out=Join-Path $repoRoot 'out\world-native';$null=New-Item -ItemType Directory -Force $out
$savedPath,$savedInclude,$savedLib=$env:PATH,$env:INCLUDE,$env:LIB
try{
 $env:PATH="$vc\bin\Hostx64\x86;"+$env:PATH
 $env:INCLUDE=(@("$vc\include")+@('ucrt','shared','um','winrt' | ForEach-Object {"$sdk\Include\$version\$_"})) -join ';'
 $env:LIB=@("$vc\lib\x86","$sdk\Lib\$version\ucrt\x86","$sdk\Lib\$version\um\x86") -join ';'
 [string[]]$extra=@(if(!$Baseline){'/DVIEWPORT_SCENERY_FIXED'})
 & "$vc\bin\Hostx64\x86\cl.exe" /nologo /O2 /MD /EHsc /std:c++17 /DNOMINMAX /DWORLD_VIEWPORT_TESTING /D_CRT_SECURE_NO_WARNINGS @extra /I"$repoRoot\ezorsia" /I"$repoRoot\third_party\d3d8to9\source" /I"$WzInclude" "$repoRoot\tests\MapMagnificationNativeTests.cpp" $ViewportSource "$repoRoot\ezorsia\UpscaleLoader.cpp" /Fo"$out\" /link /MACHINE:X86 "$repoRoot\detours\detours.lib" user32.lib oleaut32.lib ole32.lib comsuppw.lib /OUT:"$out\MapMagnificationNativeTests.exe"
 if($LASTEXITCODE -ne 0){throw 'Map sample fixture build failed'}
 Copy-Item -LiteralPath (Join-Path $Client 'BeiDouUpscale.dll') -Destination (Join-Path $out 'BeiDouUpscale.dll') -Force
 $mode=if($BuffRegression){if($ExpectBuffBug){'buff-before'}else{'buff'}}elseif($Baseline){'baseline'}else{'fixed'}
 $fixture=if($BuffRegression){'buff-hud.txt'}elseif($Baseline){'map-magnification.txt'}else{'map-scenery.txt'}
 if(!$FixturePath){$FixturePath="$repoRoot\tests\fixtures\$fixture"}
 & "$out\MapMagnificationNativeTests.exe" $Client $FixturePath $mode
 if($LASTEXITCODE -ne 0){throw "Map sample fixture failed: $LASTEXITCODE"}
}finally{$env:PATH,$env:INCLUDE,$env:LIB=$savedPath,$savedInclude,$savedLib}
