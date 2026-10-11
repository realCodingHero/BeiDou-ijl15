param(
 [string]$Client='C:\Game\BeiDou-Client-research',
 [string]$ToolchainRoot='C:\Game\BeiDou-Server\tools\msvc',
 [string]$MapleLibDirectory='C:\Game\BeiDou-Server\tools\WzBridge\bin\Release\net10.0-windows',
 [string]$WzInclude='C:\Game\BeiDou-Server\tools\kaentake-src\external\WzLib\include'
)
$ErrorActionPreference='Stop'
# Verify the actual executable before compiling the native ABI fixture.
$executable=[IO.File]::ReadAllBytes((Join-Path $Client 'BeiDou.exe'))
$nt=[BitConverter]::ToInt32($executable,0x3c)
$sectionCount=[BitConverter]::ToUInt16($executable,$nt+6)
$sections=$nt+24+[BitConverter]::ToUInt16($executable,$nt+20)
function Read-ExecutableBytes([uint32]$address,[int]$count){
 $rva=$address-0x400000
 for($i=0;$i -lt $sectionCount;$i++){
  $s=$sections+40*$i;$start=[BitConverter]::ToUInt32($executable,$s+12);$size=[BitConverter]::ToUInt32($executable,$s+16)
  if($rva -ge $start -and $rva+$count -le $start+$size){$offset=[BitConverter]::ToUInt32($executable,$s+20)+$rva-$start;return ,$executable[$offset..($offset+$count-1)]}
 }
 throw ('Unmapped executable address '+$address.ToString('X'))
}
foreach($entry in @(@(0x9de383,'B84A68AE00'),@(0x9de4d2,'B8D868AE00'),@(0x9e00af,'B8486CAE00'),@(0x9e0502,'B8946CAE00'),@(0x9e03c5,'B85C6CAE00'),@(0x9e0447,'B8706CAE00'))){
 if([Convert]::ToHexString([byte[]](Read-ExecutableBytes $entry[0] 5)) -ne $entry[1]){throw 'Unsupported native quest window ABI'}
}
if([BitConverter]::ToUInt32([byte[]](Read-ExecutableBytes (0xb3f078+11*4) 4),0) -ne 0x9e0502){throw 'Incorrect native Draw virtual slot'}
if([Convert]::ToHexString([byte[]](Read-ExecutableBytes 0x9e067b 3)) -ne 'C20400'){throw 'Incorrect native Draw parameter count'}
if([Convert]::ToHexString([byte[]](Read-ExecutableBytes 0x9e04c9 6)) -ne '33C039442404'){throw 'Incorrect native InvalidateRect address'}
if([Convert]::ToHexString([byte[]](Read-ExecutableBytes 0x9e04ff 3)) -ne 'C20400'){throw 'Incorrect native InvalidateRect parameter count'}
if([Convert]::ToHexString([byte[]](Read-ExecutableBytes 0x424717 4)) -ne '0FB7710C'){throw 'Incorrect native packet length field'}
if([Convert]::ToHexString([byte[]](Read-ExecutableBytes 0x424710 3)) -ne '8B5114'){throw 'Incorrect native packet offset field'}
Write-Output 'PASS actual v83 executable: constructor/create/destroy/draw signatures, Draw slot 11 and one-argument ABI'
$repo=Split-Path -Parent $PSScriptRoot
$out=Join-Path $repo 'out\quest-helper-native'
[IO.Directory]::CreateDirectory($out) | Out-Null
Add-Type -AssemblyName System.Drawing
foreach($a in Get-ChildItem $MapleLibDirectory -Filter *.dll){try{[void][Reflection.Assembly]::LoadFrom($a.FullName)}catch{}}
[MapleLib.MapleCryptoLib.MapleCryptoConstants]::UserKey_WzLib=[MapleLib.MapleCryptoLib.MapleCryptoConstants]::MAPLESTORY_USERKEY_DEFAULT.Clone()
$r=[MapleLib.WzLib.Serializer.WzImgDeserializer]::new($false)
$paths=@('UIWindow.img/Quest/backgrnd','UIWindow.img/Quest/backgrnd2','UIWindow.img/QuestAlarm/backgrndmax','UIWindow.img/QuestAlarm/backgrndcenter','UIWindow.img/QuestAlarm/backgrndbottom','Basic.img/BtOK/normal/0','Basic.img/BtOK/mouseOver/0','Basic.img/BtOK/disabled/0','UIWindow.img/Quest/BtDetail/normal/0','UIWindow.img/Quest/BtDetail/mouseOver/0','UIWindow.img/Quest/BtDetail/pressed/0','UIWindow.img/Quest/BtDetail/disabled/0','Basic.img/BtClose/normal/0','Basic.img/BtClose/mouseOver/0','Basic.img/BtClose/pressed/0','Basic.img/BtClose/disabled/0')
$paths+=@('Basic.img/Tab2/left0','Basic.img/Tab2/left1','Basic.img/Tab2/right0','Basic.img/Tab2/right1','Basic.img/Tab2/fill0','Basic.img/Tab2/fill1','Basic.img/VScr/enabled/prev0','Basic.img/VScr/enabled/prev1','Basic.img/VScr/enabled/next0','Basic.img/VScr/enabled/next1','Basic.img/VScr/enabled/base','Basic.img/VScr/enabled/thumb0','Basic.img/VScr/enabled/thumb1','Basic.img/VScr/disabled/prev','Basic.img/VScr/disabled/next','Basic.img/VScr/disabled/base')
[IO.File]::WriteAllLines((Join-Path $out 'skin-paths.txt'),$paths,[Text.UTF8Encoding]::new($false))
$images=@{}
for($i=0;$i -lt $paths.Count;$i++){
 $parts=$paths[$i].Split('/');$imgName=$parts[0]
 if(!$images.ContainsKey($imgName)){$ok=$false;$images[$imgName]=$r.WzImageFromIMGFile((Join-Path $Client "Data\UI\$imgName"),[MapleLib.WzLib.WzAESConstant]::WZ_GMSIV,$imgName,[ref]$ok);if(!$ok){throw 'Cannot parse native UI'}}
 $property=$images[$imgName];foreach($part in $parts[1..($parts.Length-1)]){$property=$property[$part]}
 $property.PngProperty.GetImage($false).Save((Join-Path $out "$i.bmp"),[Drawing.Imaging.ImageFormat]::Bmp)
}
# Extra native skins for the preview fixture: gauges, quest icons, 12x12 buttons and sample item/mob art.
Remove-Item (Join-Path $out 'extra-*.bmp') -ErrorAction SilentlyContinue
$extra=@(Get-Content (Join-Path $repo 'resources\skin-paths-extra.txt') | Where-Object {$_.Trim() -ne ''})
[IO.File]::WriteAllLines((Join-Path $out 'skin-paths-extra.txt'),[string[]]$extra,[Text.UTF8Encoding]::new($false))
for($i=0;$i -lt $extra.Count;$i++){
 try {
  $parts=$extra[$i].Trim().Split('/');$k=0;while(-not $parts[$k].EndsWith('.img')){$k++}
  $file=Join-Path $Client ('Data\'+(($parts[0..$k]) -join '\'))
  if(!$images.ContainsKey($file)){$ok=$false;$images[$file]=$r.WzImageFromIMGFile($file,[MapleLib.WzLib.WzAESConstant]::WZ_GMSIV,$parts[$k],[ref]$ok);if(!$ok){throw "Cannot parse $file"}}
  $property=$images[$file];foreach($part in $parts[($k+1)..($parts.Length-1)]){$property=$property[$part]}
  if($property -is [MapleLib.WzLib.WzProperties.WzUOLProperty]){$property=$property.LinkValue}
  $property.PngProperty.GetImage($false).Save((Join-Path $out "extra-$i.bmp"),[Drawing.Imaging.ImageFormat]::Bmp)
 } catch {Write-Warning ('Optional preview skin skipped: '+$extra[$i]+' '+$_.Exception.Message)}
}
$ok=$false
$quest=$r.WzImageFromIMGFile((Join-Path $Client 'Data\Quest\QuestInfo.img'),[MapleLib.WzLib.WzAESConstant]::WZ_GMSIV,'QuestInfo.img',[ref]$ok)
if(!$ok){throw 'Cannot parse original quest description'}
[IO.File]::WriteAllText((Join-Path $out 'quest-description.txt'),$quest['2000']['1'].Value,[Text.UTF8Encoding]::new($false))
foreach($questId in @('8048','2089','28269')){[IO.File]::WriteAllText((Join-Path $out "quest-$questId.txt"),$quest[$questId]['1'].Value,[Text.UTF8Encoding]::new($false))}
$resourceLines=[Collections.Generic.List[string]]::new()
foreach($file in @('Etc.img','Npc.img','Mob.img')){
 $strings=$r.WzImageFromIMGFile((Join-Path $Client "Data\String\$file"),[MapleLib.WzLib.WzAESConstant]::WZ_GMSIV,$file,[ref]$ok)
 if(!$ok){throw 'Cannot parse original quest token names'}
 $ids=if($file -eq 'Etc.img'){@('4000003','4000004','4000010')}elseif($file -eq 'Npc.img'){@('1012109','1032100','1032106')}else{@('1110100')}
 foreach($id in $ids){$node=if($file -eq 'Etc.img'){$strings['Etc'][$id]}else{$strings[$id]};$path=if($file -eq 'Etc.img'){"String/$file/Etc/$id/name"}else{"String/$file/$id/name"};$resourceLines.Add($path+"`t"+$node['name'].Value)}
}
$checks=$r.WzImageFromIMGFile((Join-Path $Client 'Data\Quest\Check.img'),[MapleLib.WzLib.WzAESConstant]::WZ_GMSIV,'Check.img',[ref]$ok)
if(!$ok){throw 'Cannot parse original quest counter reference'}
$resourceLines.Add("Quest/Check.img/8048/1/mob/0/id`t"+$checks['8048']['1']['mob']['0']['id'].Value)
[IO.File]::WriteAllLines((Join-Path $out 'token-resources.tsv'),$resourceLines,[Text.UTF8Encoding]::new($false))
$npc=$r.WzImageFromIMGFile((Join-Path $Client 'Data\Npc\1032001.img'),[MapleLib.WzLib.WzAESConstant]::WZ_GMSIV,'1032001.img',[ref]$ok)
if(!$ok){throw 'Cannot parse original NPC portrait'}
$portrait=$npc['stand']['0'].PngProperty.GetImage($false)
$data=$portrait.LockBits([Drawing.Rectangle]::new(0,0,$portrait.Width,$portrait.Height),[Drawing.Imaging.ImageLockMode]::ReadOnly,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
try {
 $pixels=[byte[]]::new($portrait.Width*$portrait.Height*4)
 for($y=0;$y -lt $portrait.Height;$y++){[Runtime.InteropServices.Marshal]::Copy([IntPtr]::Add($data.Scan0,$y*$data.Stride),$pixels,$y*$portrait.Width*4,$portrait.Width*4)}
 $stream=[IO.BinaryWriter]::new([IO.File]::Create((Join-Path $out 'npc-portrait.rgba')))
 try{$stream.Write([int]$portrait.Width);$stream.Write([int]$portrait.Height);$stream.Write($pixels)}finally{$stream.Dispose()}
}finally{$portrait.UnlockBits($data);$portrait.Dispose()}
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
 & "$vc\bin\Hostx64\x86\cl.exe" @common "$repo\tests\QuestHelperPacketHookTests.cpp" /Fo"$out\" /link /MACHINE:X86 /OUT:"$out\QuestHelperPacketHookTests.exe"
 if($LASTEXITCODE -ne 0){throw 'Native receive hook fixture build failed'}
 & "$out\QuestHelperPacketHookTests.exe"
 if($LASTEXITCODE -ne 0){throw 'Native receive hook tests failed'}
 & "$vc\bin\Hostx64\x86\cl.exe" @common "$repo\tests\QuestHelperNativeTests.cpp" /Fo"$out\" /link /MACHINE:X86 user32.lib gdi32.lib oleaut32.lib /OUT:"$out\QuestHelperNativeTests.exe"
 if($LASTEXITCODE -ne 0){throw 'Native fixture build failed'}
 & "$out\QuestHelperNativeTests.exe" $Client $out "$repo\resources\QuestHelperUI.ini"
 if($LASTEXITCODE -ne 0){throw "Native UI tests failed: $LASTEXITCODE"}
 & "$vc\bin\Hostx64\x86\cl.exe" @common /DNOMINMAX /DWORLD_VIEWPORT_TESTING /D_CRT_SECURE_NO_WARNINGS /I"$repo\third_party\d3d8to9\source" /I"$WzInclude" "$repo\tests\QuestHelperGraphicsTests.cpp" "$repo\ezorsia\WorldViewport.cpp" "$repo\ezorsia\UpscaleLoader.cpp" /Fo"$out\" /link /MACHINE:X86 "$repo\detours\detours.lib" user32.lib gdi32.lib oleaut32.lib ole32.lib comsuppw.lib /OUT:"$out\QuestHelperGraphicsTests.exe"
 if($LASTEXITCODE -ne 0){throw 'Graphics fixture build failed'}
 Copy-Item -LiteralPath (Join-Path $Client 'BeiDouUpscale.dll') -Destination (Join-Path $out 'BeiDouUpscale.dll') -Force
 & "$out\QuestHelperGraphicsTests.exe" $Client $out
 if($LASTEXITCODE -ne 0){throw "Graphics tests failed: $LASTEXITCODE"}
}finally{$env:PATH,$env:INCLUDE,$env:LIB=$savedPath,$savedInclude,$savedLib}
