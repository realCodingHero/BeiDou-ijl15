param([string]$Data='C:\Game\BeiDou-Client-research\Data',
 [string]$MapleLibPath='C:\Game\BeiDou-Server\tools\WzBridge\bin\Release\net10.0-windows\MapleLib.dll',
 [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\world-native'))
# Test fixture only: read the actual Temple Keeper sprite, never save WZ/IMG.
$ErrorActionPreference='Stop';Add-Type -LiteralPath $MapleLibPath
$reader=[MapleLib.WzLib.Serializer.WzImgDeserializer]::new($false)
$iv=[MapleLib.WzLib.Util.WzTool]::GetIvByMapleVersion([MapleLib.WzLib.WzMapleVersion]::GMS)
$path=Join-Path $Data 'Npc\2140000.img';$ok=$false
$img=$reader.WzImageFromIMGFile($path,$iv,'2140000.img',[ref]$ok);if(!$ok){throw 'NPC parse failed'}
try {
 $node=$img['stand']['0'];for($i=0;$i -lt 16 -and $node.PropertyType.ToString() -eq 'UOL';$i++){$node=$node.LinkValue}
 $bitmap=$node.PngProperty.GetImage($false)
 try {
  $runs=[Collections.Generic.List[object]]::new();$left=$bitmap.Width;$top=$bitmap.Height;$right=0;$bottom=0
  for($y=0;$y -lt $bitmap.Height;$y++){
   for($x=0;$x -lt $bitmap.Width;){
    $color=$bitmap.GetPixel($x,$y);$argb=$color.ToArgb();$end=$x+1
    while($end -lt $bitmap.Width -and $bitmap.GetPixel($end,$y).ToArgb() -eq $argb){$end++}
    if($color.A -gt 0){$runs.Add(@($x,$y,($end-$x),$argb))}
    if($color.A -gt 127){$left=[Math]::Min($left,$x);$top=[Math]::Min($top,$y);$right=[Math]::Max($right,$end);$bottom=[Math]::Max($bottom,$y+1)}
    $x=$end
   }
  }
  $null=New-Item -ItemType Directory -Force -Path $OutputDirectory
  $writer=[IO.BinaryWriter]::new([IO.File]::Create((Join-Path $OutputDirectory 'temple-keeper.runs')))
  try{
   foreach($n in @($bitmap.Width,$bitmap.Height,$left,$top,$right,$bottom,$runs.Count)){$writer.Write([int]$n)}
   foreach($run in $runs){foreach($n in $run){$writer.Write([int]$n)}}
  }finally{$writer.Dispose()}
  [pscustomobject]@{asset=$path;sha256=(Get-FileHash -LiteralPath $path).Hash;width=$bitmap.Width;height=$bitmap.Height;opaqueBounds=@($left,$top,$right,$bottom);runs=$runs.Count} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'temple-keeper.json') -Encoding utf8
  "Exported actual NPC 2140000: $($bitmap.Width)x$($bitmap.Height), $($runs.Count) color runs."
 }finally{$bitmap.Dispose()}
}finally{$img.Dispose()}
