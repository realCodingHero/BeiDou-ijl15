param(
    [Parameter(Mandatory = $true)][string]$SourceClientPath,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\out\help-menu-resources'),
    [string]$MapleLibDirectory = 'C:\Game\BeiDou-Server\tools\WzBridge\bin\Release\net10.0-windows'
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
foreach ($assembly in Get-ChildItem -LiteralPath $MapleLibDirectory -Filter '*.dll' -File) {
    try { [void][Reflection.Assembly]::LoadFrom($assembly.FullName) } catch { }
}
[MapleLib.MapleCryptoLib.MapleCryptoConstants]::UserKey_WzLib =
    [MapleLib.MapleCryptoLib.MapleCryptoConstants]::MAPLESTORY_USERKEY_DEFAULT.Clone()
$entries = @(
    @{ Name = 'BtHelper'; Chinese = '枫叶助手'; English = 'Helper'; Caption = 'HELPER' }
    @{ Name = 'BtAuction'; Chinese = '拍卖行'; English = 'Auction'; Caption = 'AUCTION' }
    @{ Name = 'BtQuestHelper'; Chinese = '任务辅助'; English = 'Quests'; Caption = 'QUESTS' }
    @{ Name = 'BtTeleport'; Chinese = '超级传送'; English = 'Teleport'; Caption = 'TELEPORT' }
)
$backgroundHeight = 32 + 26 * $entries.Count

function Read-Img([string]$path) {
    $parsed = $false
    $reader = [MapleLib.WzLib.Serializer.WzImgDeserializer]::new($false)
    $img = $reader.WzImageFromIMGFile($path, [MapleLib.WzLib.WzAESConstant]::WZ_GMSIV,
        [IO.Path]::GetFileName($path), [ref]$parsed)
    if (-not $parsed) { throw "Cannot parse $path" }
    return $img
}

function Set-V83Canvas($canvas, [Drawing.Bitmap]$bitmap) {
    # MapleLib's PNG setter auto-selects Format257 for these opaque buttons.
    # The v83 renderer misreads that format. Match the original ShortCut's
    # Format2 (BGRA8888) explicitly, including tightly packed scanlines.
    $encoded = [MapleLib.Helpers.PngUtility]::CompressImageToPngFormat(
        $bitmap, [Microsoft.Xna.Framework.Graphics.SurfaceFormat]::Bgra32)
    if ([int]$encoded.Item1 -ne 2 -or $encoded.Item2.Length -ne $bitmap.Width * $bitmap.Height * 4) {
        throw 'Invalid v83 BGRA8888 payload'
    }
    $stream = [IO.MemoryStream]::new()
    $zip = [IO.Compression.ZLibStream]::new($stream, [IO.Compression.CompressionLevel]::Optimal, $true)
    try { $zip.Write($encoded.Item2, 0, $encoded.Item2.Length) } finally { $zip.Dispose() }
    $png = [MapleLib.WzLib.WzProperties.WzPngProperty]::new()
    $png.Width = $bitmap.Width; $png.Height = $bitmap.Height
    $png.Format = $encoded.Item1
    $png.SetValue($stream.ToArray())
    $stream.Dispose()
    $canvas.PngProperty = $png
    $bitmap.Dispose()
}

function Set-ButtonText($button, [string]$label, [string]$caption) {
    foreach ($state in $button.WzProperties) {
        $canvas = $state['0']
        $bitmap = [Drawing.Bitmap]$canvas.PngProperty.GetImage($false).Clone()
        # Keep the native border, gradient, state colors and marker. Remove
        # both old text rows using an unlettered column of the same scanline.
        for ($y = 2; $y -lt 23; $y++) {
            $color = $bitmap.GetPixel(72, $y)
            for ($x = 12; $x -lt 78; $x++) { $bitmap.SetPixel($x, $y, $color) }
        }
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
        $font = [Drawing.Font]::new('SimSun', 12, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
        $small = [Drawing.Font]::new('Arial', 7, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
        $brush = [Drawing.SolidBrush]::new([Drawing.Color]::White)
        if ($state.Name -eq 'disabled') { $brush.Color = [Drawing.Color]::FromArgb(230,230,230) }
        $format = [Drawing.StringFormat]::GenericTypographic.Clone()
        try {
            $graphics.DrawString($label, $font, $brush, 13, 2, $format)
            $graphics.DrawString($caption, $small, $brush, 13, 14, $format)
        } finally { $graphics.Dispose(); $font.Dispose(); $small.Dispose(); $brush.Dispose(); $format.Dispose() }
        Set-V83Canvas $canvas $bitmap
    }
}

foreach ($language in @('Data', 'EN')) {
    # Both locales use the same native skin/geometry; only the labels differ.
    $source = Join-Path $SourceClientPath 'Data\UI\UIWindow.img'
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing $source" }
    $original = Read-Img $source
    $result = [MapleLib.WzLib.WzImage]::new('HelpMenu.img')
    try {
        $shortcut = $original['ShortCut']
        $background = $shortcut['backgrnd'].DeepClone()
        $nativeBitmap = $background.PngProperty.GetImage($false)
        if ($nativeBitmap.Width -ne 93 -or $nativeBitmap.Height -ne 245) { throw 'Unexpected ShortCut background size' }
        $bitmap = [Drawing.Bitmap]::new(93, $backgroundHeight, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        for ($y = 0; $y -lt $backgroundHeight; $y++) {
            $sourceY = if ($y -lt $backgroundHeight - 8) { $y } else { $y + 245 - $backgroundHeight }
            for ($x = 0; $x -lt 93; $x++) { $bitmap.SetPixel($x, $y, $nativeBitmap.GetPixel($x, $sourceY)) }
        }
        for ($y = 4; $y -lt 18; $y++) {
            $color = $bitmap.GetPixel(75, $y)
            for ($x = 6; $x -lt 86; $x++) { $bitmap.SetPixel($x, $y, $color) }
        }
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
        $font = [Drawing.Font]::new('SimSun', 12, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
        $format = [Drawing.StringFormat]::GenericTypographic.Clone()
        $heading = if ($language -eq 'Data') { '帮助' } else { 'Help' }
        $graphics.DrawString($heading, $font, [Drawing.Brushes]::Black, 8, 4, $format)
        $graphics.Dispose(); $font.Dispose(); $format.Dispose()
        Set-V83Canvas $background $bitmap
        $result.AddProperty($background)
        foreach ($entry in $entries) {
            $button = $shortcut['BtItem'].DeepClone(); $button.Name = $entry.Name
            $label = if ($language -eq 'Data') { $entry.Chinese } else { $entry.English }
            Set-ButtonText $button $label $entry.Caption
            $result.AddProperty($button)
        }
        $target = Join-Path $OutputDirectory "$language\UI"
        [void][IO.Directory]::CreateDirectory($target)
        $path = Join-Path $target 'HelpMenu.img'
        $result.Changed = $true
        $serializer = [MapleLib.WzLib.Serializer.WzImgSerializer]::new([MapleLib.WzLib.WzAESConstant]::WZ_GMSIV)
        $serializer.SerializeImage($result, $path)
        # Verify the serialized IMG, and render a preview from its decoded pixels.
        $verify = Read-Img $path
        try {
            if ($verify.WzProperties.Count -ne $entries.Count + 1 -or $verify['backgrnd'].PngProperty.Height -ne $backgroundHeight) { throw 'Invalid help menu structure' }
            if ([int]$verify['backgrnd'].PngProperty.Format -ne 2) { throw 'Background must use v83 Format2' }
            $preview = [Drawing.Bitmap]$verify['backgrnd'].PngProperty.GetImage($false).Clone()
            $g = [Drawing.Graphics]::FromImage($preview)
            $y = 24
            foreach ($entry in $entries) {
                $name = $entry.Name
                foreach ($state in @('normal','pressed','disabled','mouseOver')) {
                    $png = $verify[$name][$state]['0'].PngProperty
                    if ($png.Width -ne 81 -or $png.Height -ne 25) { throw "Invalid $name/$state" }
                    if ([int]$png.Format -ne 2) { throw "Unsupported v83 texture format: $name/$state" }
                }
                $g.DrawImageUnscaled($verify[$name]['normal']['0'].PngProperty.GetImage($false), 6, $y)
                $y += 26
            }
            $g.Dispose()
            $preview.Save((Join-Path $OutputDirectory "$language-preview.png"))
            $preview.Dispose()
        } finally { $verify.Dispose() }
        Write-Output "Verified: $path"
    } finally { $original.Dispose(); $result.Dispose() }
}
