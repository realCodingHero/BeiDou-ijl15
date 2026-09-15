param(
 [string]$Data='C:\Game\BeiDou-Client-research\Data',
 [string]$MapleLibPath='C:\Game\BeiDou-Server\tools\WzBridge\bin\Release\net10.0-windows\MapleLib.dll',
 [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\ground-camera')
)
# Read-only end-cap opacity/placement audit. Never saves IMG/WZ objects.
$ErrorActionPreference='Stop'
Add-Type -LiteralPath $MapleLibPath
$reader=[MapleLib.WzLib.Serializer.WzImgDeserializer]::new($false)
$iv=[MapleLib.WzLib.Util.WzTool]::GetIvByMapleVersion([MapleLib.WzLib.WzMapleVersion]::GMS)
function ReadImg($path){$ok=$false;$img=$reader.WzImageFromIMGFile($path,$iv,[IO.Path]::GetFileName($path),[ref]$ok);if(!$ok){throw "Parse failed: $path"};return $img}
function Val($n,$k,$d=0){if($null -eq $n -or $null -eq $n[$k]){return $d};return $n[$k].Value}
$caps=@{}
foreach($path in @(rg --files --no-ignore "$Data\Map\Tile" -g '*.img')) {
 $img=ReadImg $path
 try {foreach($entry in $img['enH1'].WzProperties) {
  $canvas=$entry
  for($i=0;$i -lt 16 -and $canvas -and $canvas.PropertyType.ToString() -eq 'UOL';$i++){$canvas=$canvas.LinkValue}
  if(!$canvas -or $canvas.PropertyType.ToString() -ne 'Canvas'){continue}
  $bitmap=$canvas.PngProperty.GetImage($false)
  try {
   if($bitmap.Width -gt 512 -or $bitmap.Height -gt 512){continue}
   $solid=0
   for($y=0;$y -lt $bitmap.Height;$y++) {
    $opaque=$true
    for($x=0;$x -lt $bitmap.Width;$x++){if($bitmap.GetPixel($x,$y).A -ne 255){$opaque=$false;break}}
    if(!$opaque){break};$solid++
   }
   $caps["$([IO.Path]::GetFileNameWithoutExtension($path))/$($entry.Name)"]=@($bitmap.Width,[int]$canvas['origin'].X.Value,[int]$canvas['origin'].Y.Value,$solid)
  } finally {$bitmap.Dispose()}
 }} finally {$img.Dispose()}
}
# Authoritative runtime fallback rectangles recorded from the supported EXE.
# Other maps below use explicit VR; no guessed native foothold bounds.
$runtime=@{'100000000'=@(-1018,-587,6328,770);'103000000'=@(-2500,-1443,2645,800)}
$lines=[Collections.Generic.List[string]]::new();$summary=[Collections.Generic.List[object]]::new()
foreach($path in @(rg --files --no-ignore "$Data\Map\Map" -g '*.img')) {
 $id=[IO.Path]::GetFileNameWithoutExtension($path);if($id -notmatch '^\d{9}$'){continue}
 $img=ReadImg $path
 try {
  $info=$img['info'];$vr=$runtime[$id]
  if(!$vr -and $info['VRLeft'] -and $info['VRTop'] -and $info['VRRight'] -and $info['VRBottom']){$vr=@((Val $info VRLeft),(Val $info VRTop),(Val $info VRRight),(Val $info VRBottom))}
  if(!$vr){continue}
  # Runtime already resolves linked-map contents. Skip incomplete offline links.
  if((Val $info link '') -ne ''){continue}
  $lines.Add("map $id $($vr -join ' ')");$count=0
  foreach($idx in 0..7) {
   $layer=$img[[string]$idx];$set=Val $layer['info'] tS ''
   foreach($tile in $layer['tile'].WzProperties) {
    if((Val $tile u '') -ne 'enH1'){continue}
    $meta=$caps["$set/$(Val $tile no)"];if(!$meta -or !$meta[3]){continue}
    $left=(Val $tile x)-$meta[1];$top=(Val $tile y)-$meta[2];$right=$left+$meta[0];$bottom=$top+$meta[3]
    $lines.Add("tile $idx $left $top $right $bottom");$count++
   }
  }
  $lines.Add('end');$summary.Add(@{map=$id;vr=$vr;endCaps=$count})
 } finally {$img.Dispose()}
}
$null=New-Item -ItemType Directory -Path $OutputDirectory -Force
$lines | Set-Content -LiteralPath (Join-Path $OutputDirectory 'fixtures.txt') -Encoding ascii
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'maps.json') -Encoding utf8
"Read-only ground audit: $($caps.Count) end-cap canvases, $($summary.Count) maps with resolved offline bounds."
