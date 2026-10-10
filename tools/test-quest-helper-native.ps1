param(
 [string]$Client='C:\Game\BeiDou-Client-research',
 [string]$ToolchainRoot='C:\Game\BeiDou-Server\tools\msvc',
 [string]$MapleLibDirectory='C:\Game\BeiDou-Server\tools\WzBridge\bin\Release\net10.0-windows'
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$out=Join-Path $repo 'out\quest-helper-native'
[IO.Directory]::CreateDirectory($out) | Out-Null
Add-Type -AssemblyName System.Drawing
foreach($a in Get-ChildItem $MapleLibDirectory -Filter *.dll){try{[void][Reflection.Assembly]::LoadFrom($a.FullName)}catch{}}
[MapleLib.MapleCryptoLib.MapleCryptoConstants]::UserKey_WzLib=[MapleLib.MapleCryptoLib.MapleCryptoConstants]::MAPLESTORY_USERKEY_DEFAULT.Clone()
$r=[MapleLib.WzLib.Serializer.WzImgDeserializer]::new($false)
$paths=@('UIWindow.img/Quest/backgrnd','UIWindow.img/Quest/backgrnd2','UIWindow.img/QuestAlarm/backgrndmax','UIWindow.img/QuestAlarm/backgrndcenter','UIWindow.img/QuestAlarm/backgrndbottom','Basic.img/BtOK/normal/0','Basic.img/BtOK/mouseOver/0','Basic.img/BtOK/disabled/0')
$images=@{}
for($i=0;$i -lt $paths.Count;$i++){
 $parts=$paths[$i].Split('/');$imgName=$parts[0]
 if(!$images.ContainsKey($imgName)){$ok=$false;$images[$imgName]=$r.WzImageFromIMGFile((Join-Path $Client "Data\UI\$imgName"),[MapleLib.WzLib.WzAESConstant]::WZ_GMSIV,$imgName,[ref]$ok);if(!$ok){throw 'Cannot parse native UI'}}
 $property=$images[$imgName];foreach($part in $parts[1..($parts.Length-1)]){$property=$property[$part]}
 $property.PngProperty.GetImage($false).Save((Join-Path $out "$i.bmp"),[Drawing.Imaging.ImageFormat]::Bmp)
}
$vc=(Get-ChildItem "$ToolchainRoot\VC\Tools\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$sdk="$ToolchainRoot\Windows Kits\10";$v=(Get-ChildItem "$sdk\Include" -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$savedPath,$savedInclude,$savedLib=$env:PATH,$env:INCLUDE,$env:LIB
try {
 $env:PATH="$vc\bin\Hostx64\x86;"+$env:PATH
 $env:INCLUDE=(@("$vc\include")+@('ucrt','shared','um','winrt' | ForEach-Object {"$sdk\Include\$v\$_"})) -join ';'
 $env:LIB=@("$vc\lib\x86","$sdk\Lib\$v\ucrt\x86","$sdk\Lib\$v\um\x86") -join ';'
 $common=@('/nologo','/O2','/MD','/EHsc','/std:c++17',"/I$repo\ezorsia")
 & "$vc\bin\Hostx64\x86\cl.exe" @common "$repo\tests\QuestHelperProtocolTests.cpp" /Fo"$out\" /link /MACHINE:X86 /OUT:"$out\QuestHelperProtocolTests.exe"
 if($LASTEXITCODE -ne 0){throw 'Protocol fixture build failed'}
 & "$out\QuestHelperProtocolTests.exe"
 if($LASTEXITCODE -ne 0){throw 'Protocol tests failed'}
 & "$vc\bin\Hostx64\x86\cl.exe" @common "$repo\tests\QuestHelperNativeTests.cpp" /Fo"$out\" /link /MACHINE:X86 user32.lib gdi32.lib oleaut32.lib /OUT:"$out\QuestHelperNativeTests.exe"
 if($LASTEXITCODE -ne 0){throw 'Native fixture build failed'}
 & "$out\QuestHelperNativeTests.exe" $Client $out "$repo\resources\QuestHelperUI.ini"
 if($LASTEXITCODE -ne 0){throw "Native UI tests failed: $LASTEXITCODE"}
}finally{$env:PATH,$env:INCLUDE,$env:LIB=$savedPath,$savedInclude,$savedLib}
