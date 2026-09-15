param(
 [string]$Data='C:\Game\BeiDou-Client-research\Data',
 [string]$MapleLibPath='C:\Game\BeiDou-Server\tools\WzBridge\bin\Release\net10.0-windows\MapleLib.dll',
 [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\map-scenery')
)
# Read metadata only. Never save IMG/WZ or include moving actors in map bounds.
$ErrorActionPreference='Stop'
Add-Type -LiteralPath $MapleLibPath
$reader=[MapleLib.WzLib.Serializer.WzImgDeserializer]::new($false)
$iv=[MapleLib.WzLib.Util.WzTool]::GetIvByMapleVersion([MapleLib.WzLib.WzMapleVersion]::GMS)
$cache=@{};$hashes=@{};$results=[Collections.Generic.List[object]]::new()
function ReadImg($relative) {
 if(!$cache.ContainsKey($relative)) {
  $path=Join-Path $Data $relative;$ok=$false
  $img=$reader.WzImageFromIMGFile($path,$iv,[IO.Path]::GetFileName($relative),[ref]$ok)
  if(!$ok){throw "Cannot parse $relative"}
  $cache[$relative]=$img;$hashes[$relative]=(Get-FileHash -LiteralPath $path).Hash
 }
 return $cache[$relative]
}
function Value($node,$key,$default=0) {if(!$node -or !$node[$key]){return $default};return $node[$key].Value}
function Box($canvas) {
 for($i=0;$i -lt 16 -and $canvas -and $canvas.PropertyType.ToString() -eq 'UOL';$i++){$canvas=$canvas.LinkValue}
 if(!$canvas -or $canvas.PropertyType.ToString() -ne 'Canvas'){return $null}
 $x=0;$y=0;if($canvas['origin']){$x=[int]$canvas['origin'].X.Value;$y=[int]$canvas['origin'].Y.Value}
 return @(-$x,-$y,($canvas.PngProperty.Width-$x),($canvas.PngProperty.Height-$y))
}
try {
 $fixture=Join-Path $PSScriptRoot '..\tests\fixtures\map-scenery.txt'
 foreach($line in (Get-Content -LiteralPath $fixture)) {
  if(!$line.StartsWith('map ')){continue};$fields=$line.Split(' ');$id=$fields[1]
  $bounds=@($fields[2..5] | ForEach-Object {[int]$_})
  $img=ReadImg "Map\Map\Map$($id.Substring(0,1))\$id.img"
  $boxes=[Collections.Generic.List[object]]::new()
  foreach($level in 0..7) {
   $layer=$img[[string]$level];if(!$layer){continue}
   foreach($obj in $layer['obj'].WzProperties) {
    $set=Value $obj oS '';if(!$set){continue}
    $node=ReadImg "Map\Obj\$set.img"
    foreach($part in @('l0','l1','l2')){$node=$node[(Value $obj $part '')]}
    if((Value $obj moveType) -or (Value $node moveType)){continue}
    foreach($frame in $node.WzProperties) {
     if($frame.Name -notmatch '^\d+$'){continue};$b=Box $frame;if(!$b){continue}
     $x=[int](Value $obj x);$y=[int](Value $obj y)
     $left=$x+$b[0];$right=$x+$b[2];if(Value $obj f){$left=$x-$b[2];$right=$x-$b[0]}
     $boxes.Add(@($left,($y+$b[1]),$right,($y+$b[3])))
    }
   }
   $set=Value $layer['info'] tS '';if(!$set){continue};$tiles=ReadImg "Map\Tile\$set.img"
   foreach($tile in $layer['tile'].WzProperties) {
    $b=Box $tiles[(Value $tile u '')][([string](Value $tile no))];if(!$b){continue}
    $x=[int](Value $tile x);$y=[int](Value $tile y)
    $boxes.Add(@(($x+$b[0]),($y+$b[1]),($x+$b[2]),($y+$b[3])))
   }
  }
  $selected=@($boxes | Where-Object {$_[0] -lt $bounds[2] -and $_[2] -gt $bounds[0]})
  if(!$selected.Count){throw "No static scenery in $id"}
  $top=($selected | ForEach-Object {$_[1]} | Measure-Object -Minimum).Minimum
  $bottom=($selected | ForEach-Object {$_[3]} | Measure-Object -Maximum).Maximum
  $record=[ordered]@{id=$id;nativeBounds=$bounds;sceneryTop=$top;sceneryBottom=$bottom;boxes=@($boxes.ToArray())}
  $results.Add($record)
  [pscustomobject]@{id=$id;framesAndTiles=$boxes.Count;top=$top;bottom=$bottom}
 }
 $null=New-Item -ItemType Directory -Force -Path $OutputDirectory
 [ordered]@{maps=$results.ToArray();sourceSha256=$hashes} | ConvertTo-Json -Depth 8 |
  Set-Content -LiteralPath (Join-Path $OutputDirectory 'scenery.json') -Encoding utf8
} finally {foreach($img in $cache.Values){$img.Dispose()}}
