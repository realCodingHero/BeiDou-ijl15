param(
    [string]$MapDirectory = 'C:\Game\BeiDou-Client-research\Data\Map\Map',
    [string]$MapleLibPath = 'C:\Game\BeiDou-Server\tools\WzBridge\bin\Release\net10.0-windows\MapleLib.dll',
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\out\map-viewports.json')
)
# Read-only asset inventory. Never saves WzImage objects or changes map files.
$ErrorActionPreference = 'Stop'
Add-Type -LiteralPath $MapleLibPath
$reader = [MapleLib.WzLib.Serializer.WzImgDeserializer]::new($false)
$iv = [MapleLib.WzLib.Util.WzTool]::GetIvByMapleVersion([MapleLib.WzLib.WzMapleVersion]::GMS)
$rows = [Collections.Generic.List[object]]::new()
$files = @(rg --files --no-ignore $MapDirectory -g '*.img')
foreach ($path in $files) {
    $img = $null
    try {
        $ok = $false
        $img = $reader.WzImageFromIMGFile($path, $iv, [IO.Path]::GetFileName($path), [ref]$ok)
        if (!$ok) { throw 'IMG parse failed' }
        $info = $img['info']
        $row = [ordered]@{map=[IO.Path]::GetFileNameWithoutExtension($path); link=$null; status='explicit'; top=$null; bottom=$null; height=$null; extraAbove1080=$null}
        if ($null -ne $info -and $null -ne $info['link']) { $row.link = [string]$info['link'].Value }
        if ($null -ne $info -and $null -ne $info['VRTop'] -and $null -ne $info['VRBottom']) {
            $row.top = [int]$info['VRTop'].Value
            $row.bottom = [int]$info['VRBottom'].Value
            if ($row.bottom -ge $row.top) {
                $row.height = $row.bottom - $row.top
                $row.extraAbove1080 = [Math]::Max(0,1080-$row.height)
            } else { $row.status = 'invalid-range' }
        } else {
            # Native RestoreViewRange owns fallback to foothold bounds and map
            # links. Do not pretend an offline partial rectangle is authoritative.
            $row.status = 'native-fallback-or-link'
        }
        $rows.Add([pscustomobject]$row)
    } catch {
        $rows.Add([pscustomobject]@{map=[IO.Path]::GetFileNameWithoutExtension($path); status='parse-error'; error=$_.Exception.Message})
    } finally {
        if ($null -ne $img) { $img.Dispose() }
    }
    if ($rows.Count % 500 -eq 0) { Write-Output "Read $($rows.Count)/$($files.Count) maps" }
}
$summary = [ordered]@{
    total=$rows.Count
    explicit=@($rows | Where-Object status -eq 'explicit').Count
    shorterThan1080=@($rows | Where-Object {$_.status -eq 'explicit' -and $_.height -lt 1080}).Count
    nativeFallbackOrLink=@($rows | Where-Object status -eq 'native-fallback-or-link').Count
    invalidRange=@($rows | Where-Object status -eq 'invalid-range').Count
    parseErrors=@($rows | Where-Object status -eq 'parse-error').Count
}
$parent = Split-Path -Parent ([IO.Path]::GetFullPath($OutputPath))
[IO.Directory]::CreateDirectory($parent) | Out-Null
@{summary=$summary;maps=$rows} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding utf8
$summary | ConvertTo-Json
if ($summary.parseErrors) { throw 'Some map files could not be read; see the inventory.' }
