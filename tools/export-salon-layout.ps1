# Generates resources/SalonLayout.txt from a client's Data folder (read-only on the client).
# The native salon draws avatars from WZ bitmaps but cannot read canvas origins or anchor vectors,
# so their geometry ships in this table. Lines (UTF-8, space separated):
#   B <skin> <layers>      body, arm (Character/0000200s.img stand1/0) and head (0001200s.img stand1/0)
#   H <style> <layers>     a hair style's default layers (shared by its colours)
#   H! <id> <layers>       a hair colour whose layers differ from its style
#   F <style> / F! <id>    the same for faces
# A layer is "path,z,originX,originY[,anchor,x,y]..."; layers are joined with ';'. Hair and face
# paths are relative to the item's own img ("default/hair"); a path starting with '/' is relative
# to Character.wz (a UOL into another img).
param(
    [string]$Client = 'C:\Game\MoonkidsMS\BeiDou-Client-research',
    [string]$Out = (Join-Path (Split-Path -Parent $PSScriptRoot) 'resources\SalonLayout.txt'),
    [string]$MapleLibDirectory = 'C:\Game\MoonkidsMS\BeiDou-Server\tools\WzBridge\bin\Release\net10.0-windows'
)
$ErrorActionPreference = 'Stop'
foreach ($x in Get-ChildItem $MapleLibDirectory -Filter *.dll) { try { [void][Reflection.Assembly]::LoadFrom($x.FullName) } catch {} }
function Open-Img([string]$path) {
    $p = $false
    $img = [MapleLib.WzLib.Serializer.WzImgDeserializer]::new($false).WzImageFromIMGFile($path, [MapleLib.WzLib.WzAESConstant]::WZ_GMSIV, [IO.Path]::GetFileName($path), [ref]$p)
    if (-not $p) { return $null }
    return $img
}
# Path of a property inside its img ("default/hair"), from MapleLib's "<img>\a\b" full path.
function Relative($prop) {
    $parts = $prop.FullPath -split '\\'
    $i = [Array]::FindIndex($parts, [Predicate[string]] { param($s) $s.EndsWith('.img') })
    return ($parts[($i + 1)..($parts.Length - 1)] -join '/')
}
function Layer($slot, [string]$imgName, [string]$folder) {
    $canvas = $slot
    if ($canvas -is [MapleLib.WzLib.WzProperties.WzUOLProperty]) { $canvas = $canvas.LinkValue }
    if ($canvas -isnot [MapleLib.WzLib.WzProperties.WzCanvasProperty]) { return $null }
    $path = Relative $canvas
    if ($canvas.ParentImage.Name -ne $imgName) { $path = '/' + $folder + $canvas.ParentImage.Name + '/' + $path }
    $z = if ($canvas['z']) { [string]$canvas['z'].Value } else { $slot.Name }
    $o = $canvas['origin']
    $fields = @($path, $z, $o.X.Value, $o.Y.Value)
    if ($canvas['map']) { foreach ($m in $canvas['map'].WzProperties) { $fields += @($m.Name, $m.X.Value, $m.Y.Value) } }
    return ($fields -join ',')
}
function Layers($node, [string]$imgName, [string]$folder) {
    $list = @()
    foreach ($slot in $node.WzProperties) { $l = Layer $slot $imgName $folder; if ($l) { $list += $l } }
    return ($list -join ';')
}
$lines = [Collections.Generic.List[string]]::new()
$character = Join-Path $Client 'Data\Character'
foreach ($bodyFile in Get-ChildItem $character -Filter '0000200*.img' | Sort-Object Name) {
    $skin = [int]$bodyFile.BaseName - 2000
    $headFile = Join-Path $character ('{0:D8}.img' -f (12000 + $skin))
    $body = Open-Img $bodyFile.FullName; $head = if (Test-Path $headFile) { Open-Img $headFile } else { $null }
    if (-not $body -or -not $head) { continue }
    $parts = @()
    foreach ($n in 'body', 'arm') { $l = Layer $body['stand1']['0'][$n] $body.Name ''; if ($l) { $parts += ('/' + $body.Name + '/' + ($l -replace '^/[^/]+/', '')) } }
    $l = Layer $head['stand1']['0']['head'] $head.Name ''; if ($l) { $parts += ('/' + $head.Name + '/' + $l) }
    $lines.Add("B $skin " + ($parts -join ';'))
    $body.Dispose(); $head.Dispose()
}
$counts = [ordered]@{}
foreach ($kind in @(@('Hair', 'H'), @('Face', 'F'))) {
    $folder = $kind[0] + '/'
    $styles = @{}
    foreach ($f in Get-ChildItem (Join-Path $character $kind[0]) -Filter '*.img') {
        $id = [int]$f.BaseName
        $style = if ($kind[1] -eq 'H') { if ($id % 10 -le 7) { $id - $id % 10 } else { $id } } else { $c = [math]::Floor($id / 100) % 10; if ($c -le 8) { $id - $c * 100 } else { $id } }
        $img = Open-Img $f.FullName
        if (-not $img) { continue }
        try {
            if ($null -eq $img['default']) { continue }
            if (-not $styles.ContainsKey($style)) { $styles[$style] = [Collections.Generic.SortedDictionary[int,string]]::new() }
            $styles[$style][$id] = Layers $img['default'] $img.Name $folder
        } finally { $img.Dispose() }
    }
    $overrides = 0
    foreach ($style in ($styles.Keys | Sort-Object)) {
        $variants = $styles[$style]; $first = @($variants.Keys)[0]; $shared = $variants[$first]
        $lines.Add("$($kind[1]) $style $shared")
        foreach ($id in @($variants.Keys)) {
            if ($variants[$id] -ne $shared) { $lines.Add("$($kind[1])! $id $($variants[$id])"); $overrides++ }
        }
    }
    $counts[$kind[0]] = "styles=$($styles.Count) overrides=$overrides"
}
[IO.File]::WriteAllLines($Out, $lines, [Text.UTF8Encoding]::new($false))
$counts.GetEnumerator() | % { "$($_.Key): $($_.Value)" }
'lines=' + $lines.Count + ' bytes=' + (Get-Item $Out).Length
