param(
 [string]$Data='C:\Game\BeiDou-Client-research\Data',
 [string]$MapleLibPath='C:\Game\BeiDou-Server\tools\WzBridge\bin\Release\net10.0-windows\MapleLib.dll',
 [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\scene-audit')
)
# Read-only source IMG inventory. Outputs are reports only; never save WZ data.
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$ErrorActionPreference='Stop'
Add-Type -LiteralPath $MapleLibPath
$r=[MapleLib.WzLib.Serializer.WzImgDeserializer]::new($false)
$iv=[MapleLib.WzLib.Util.WzTool]::GetIvByMapleVersion([MapleLib.WzLib.WzMapleVersion]::GMS)
function ReadImg($path){$ok=$false;$v=$r.WzImageFromIMGFile($path,$iv,[IO.Path]::GetFileName($path),[ref]$ok);if(!$ok){throw "parse $path"};return $v}
function Val($node,$key,$default=0){if($null -eq $node -or $null -eq $node[$key]){return $default};return $node[$key].Value}
$counts=[ordered]@{maps=0;objects=0;tiles=0;background=0;foreground=0;life=0;mobs=0;npcs=0;portals=0;reactors=0;footholds=0;ladders=0;hasExplicitVR=0;narrower1920=0;shorter1080=0;bothTooSmall=0;linkedMaps=0;limitedViewMaps=0}
$backTypes=@{};$uses=[Collections.Generic.List[object]]::new();$maps=[Collections.Generic.List[object]]::new();$errors=[Collections.Generic.List[object]]::new();$refs=[Collections.Generic.HashSet[string]]::new();$objects=[Collections.Generic.HashSet[string]]::new()
foreach($path in @(rg --files --no-ignore (Join-Path $Data 'Map\Map') -g '*.img')){
 if([IO.Path]::GetFileNameWithoutExtension($path) -notmatch '^\d{9}$'){continue}
 $img=$null
 try {
  $img=ReadImg $path;$id=[IO.Path]::GetFileNameWithoutExtension($path);$info=$img['info'];$counts.maps++
  $vr=$null
  if($info['VRLeft'] -and $info['VRRight'] -and $info['VRTop'] -and $info['VRBottom']){
   $vr=@([int](Val $info VRLeft),[int](Val $info VRTop),[int](Val $info VRRight),[int](Val $info VRBottom));$w=$vr[2]-$vr[0];$h=$vr[3]-$vr[1];$counts.hasExplicitVR++
   if($w -lt 1920){$counts.narrower1920++};if($h -lt 1080){$counts.shorter1080++};if($w -lt 1920 -and $h -lt 1080){$counts.bothTooSmall++}
  }
  if($info['link']){$counts.linkedMaps++};if((Val $info fieldType) -eq 9){$counts.limitedViewMaps++}
  $maps.Add(@{id=$id;vr=$vr;link=(Val $info link '');fieldType=(Val $info fieldType);fly=(Val $info fly);swim=(Val $info swim)})
  foreach($b in $img['back'].WzProperties){if(Val $b front){$counts.foreground++}else{$counts.background++};$key=[string](Val $b type);$backTypes[$key]++}
  foreach($l in 0..7){$layer=$img["$l"];if(!$layer){continue};$counts.tiles+=$layer['tile'].WzProperties.Count
   foreach($o in $layer['obj'].WzProperties){$counts.objects++;$key="$(Val $o oS '')/$(Val $o l0 '')/$(Val $o l1 '')/$(Val $o l2 '')";$null=$objects.Add($key)
    if($vr){$uses.Add(@{map=$id;key=$key;x=[int](Val $o x);y=[int](Val $o y);flip=[int](Val $o f);vr=$vr;layer=$l})}
   }
  }
  foreach($life in $img['life'].WzProperties){$counts.life++;$t=Val $life type '';$kind=if($t -eq 'm'){'Mob'}else{'Npc'};if($t -eq 'm'){$counts.mobs++}else{$counts.npcs++};$null=$refs.Add("$kind/$(([int](Val $life id)).ToString('D7')).img")}
  $counts.portals+=$img['portal'].WzProperties.Count;$counts.reactors+=$img['reactor'].WzProperties.Count;$counts.ladders+=$img['ladderRope'].WzProperties.Count
  foreach($re in $img['reactor'].WzProperties){$null=$refs.Add("Reactor/$(([int](Val $re id)).ToString('D7')).img")}
  foreach($fl in $img['foothold'].WzProperties){foreach($group in $fl.WzProperties){$counts.footholds+=$group.WzProperties.Count}}
 }catch{$errors.Add(@{file=$path;error=$_.Exception.Message})}finally{if($img){$img.Dispose()}}
 if($counts.maps%1000 -eq 0){Write-Output "Maps read $($counts.maps)"}
}
$bounds=@{};$assetProblems=[Collections.Generic.List[object]]::new();$frames=0
foreach($group in ($objects | Group-Object {($_ -split '/')[0]})){
 $img=$null
 try{
  $img=ReadImg (Join-Path $Data "Map\Obj\$($group.Name).img")
  foreach($key in $group.Group){
   $parts=$key -split '/';$node=$img;foreach($p in $parts[1..3]){if($node){$node=$node[$p]}}
   if(!$node){$assetProblems.Add(@{key=$key;reason='unresolved object path'});continue}
   $boxes=@();foreach($f in $node.WzProperties){if($f.Name -notmatch '^\d+$'){continue};$canvas=$f;for($depth=0;$depth -lt 16 -and $canvas -and $canvas.PropertyType.ToString() -eq 'UOL';$depth++){$canvas=$canvas.LinkValue}
    if($canvas -and $canvas.PropertyType.ToString() -eq 'Canvas'){
     $ox=if($canvas['origin']){[int]$canvas['origin'].X.Value}else{0};$oy=if($canvas['origin']){[int]$canvas['origin'].Y.Value}else{0}
     $boxes+=,@(-$ox,-$oy,([int]$canvas.PngProperty.Width-$ox),([int]$canvas.PngProperty.Height-$oy));$frames++
    }
   }
   if(!$boxes.Count){$assetProblems.Add(@{key=$key;reason='no resolved direct canvas frames'});continue}
   $bounds[$key]=@(($boxes|ForEach-Object {$_[0]}|Measure-Object -Minimum).Minimum,($boxes|ForEach-Object {$_[1]}|Measure-Object -Minimum).Minimum,($boxes|ForEach-Object {$_[2]}|Measure-Object -Maximum).Maximum,($boxes|ForEach-Object {$_[3]}|Measure-Object -Maximum).Maximum)
  }
 }catch{$assetProblems.Add(@{key=$group.Name;reason=$_.Exception.Message})}finally{if($img){$img.Dispose()}}
}
$edge=[Collections.Generic.List[object]]::new()
foreach($u in $uses){$b=$bounds[$u.key];if(!$b){continue};$left=$u.x+$b[0];$right=$u.x+$b[2];if($u.flip){$left=$u.x-$b[2];$right=$u.x-$b[0]};$top=$u.y+$b[1];$bottom=$u.y+$b[3];$sides=@();$vr=$u.vr
 if(($vr[2]-$vr[0]) -lt 1920){if([Math]::Abs($left-$vr[0]) -le 8){$sides+='left'};if([Math]::Abs($right-$vr[2]) -le 8){$sides+='right'}}
 if(($vr[3]-$vr[1]) -lt 1080){if([Math]::Abs($top-$vr[1]) -le 8){$sides+='top'};if([Math]::Abs($bottom-$vr[3]) -le 8){$sides+='bottom'}}
 if($sides.Count){$edge.Add(@{map=$u.map;key=$u.key;layer=$u.layer;bounds=@($left,$top,$right,$bottom);vr=$vr;edges=$sides})}
}
$missing=@($refs|Where-Object {!(Test-Path -LiteralPath (Join-Path $Data $_))})
$filesByCategory=[ordered]@{};foreach($category in @('Character','Mob','Npc','Reactor','Morph','TamingMob','Skill','Effect','Map\Back','Map\Obj','Map\Tile','UI')){$filesByCategory[$category]=@(rg --files --no-ignore (Join-Path $Data $category) -g '*.img').Count}
$report=[ordered]@{counts=$counts;backTypes=$backTypes;uniqueObjectPaths=$objects.Count;resolvedObjectPaths=$bounds.Count;objectAnimationFrames=$frames;edgeCandidates=$edge.Count;mapsWithEdgeCandidates=@($edge.map|Sort-Object -Unique).Count;uniqueInteractiveAssetFiles=$refs.Count;missingInteractiveAssetFiles=$missing;fileInventory=$filesByCategory;mapErrors=$errors;objectProblems=$assetProblems;candidateMethod='Animation canvas bounding edges within 8px of explicit VR; review candidates, not proven asset defects. Moving offsets, links to other IMG files, pixel opacity and composite tile occlusion not simulated.'}
$report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
$edge|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $OutputDirectory 'edge-candidates.json') -Encoding utf8
$maps|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputDirectory 'maps.json') -Encoding utf8
$report|ConvertTo-Json -Depth 5

$maps | Where-Object {$_.vr} | Sort-Object id | ForEach-Object {"$($_.id) $($_.vr -join ' ')"} | Set-Content -LiteralPath (Join-Path $OutputDirectory 'bounds.txt') -Encoding ascii
