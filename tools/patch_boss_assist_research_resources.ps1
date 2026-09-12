param(
    [Parameter(Mandatory = $true)]
    [string]$ClientPath,

    [string]$MapleLibDirectory = 'C:\Game\BeiDou-Server\tools\WzBridge\bin\Release\net10.0-windows'
)

$ErrorActionPreference = 'Stop'
[Text.Encoding]::RegisterProvider([Text.CodePagesEncodingProvider]::Instance)

$resolvedClientPath = (Resolve-Path -LiteralPath $ClientPath).Path
if ((Split-Path -Leaf $resolvedClientPath) -ne 'BeiDou-Client-research') {
    throw "This patch is restricted to the BeiDou-Client-research directory: $resolvedClientPath"
}

$wzAesKey = [byte[]](
    0x13, 0, 0, 0, 0x08, 0, 0, 0, 0x06, 0, 0, 0, 0xB4, 0, 0, 0,
    0x1B, 0, 0, 0, 0x0F, 0, 0, 0, 0x33, 0, 0, 0, 0x52, 0, 0, 0
)
$gmsIvBlock = [byte[]](
    0x4D, 0x23, 0xC7, 0x2B, 0x4D, 0x23, 0xC7, 0x2B,
    0x4D, 0x23, 0xC7, 0x2B, 0x4D, 0x23, 0xC7, 0x2B
)
$codePage936 = [Text.Encoding]::GetEncoding(936)

function Get-WzKeyStream([int]$length) {
    if ($length -le 0) {
        return ,[byte[]]@()
    }

    $result = New-Object byte[] ([Math]::Ceiling($length / 16.0) * 16)
    $block = [byte[]]$gmsIvBlock.Clone()
    $aes = [Security.Cryptography.Aes]::Create()
    $aes.Mode = [Security.Cryptography.CipherMode]::ECB
    $aes.Padding = [Security.Cryptography.PaddingMode]::None
    $aes.Key = $wzAesKey
    $encryptor = $aes.CreateEncryptor()
    try {
        for ($offset = 0; $offset -lt $length; $offset += 16) {
            $nextBlock = New-Object byte[] 16
            [void]$encryptor.TransformBlock($block, 0, 16, $nextBlock, 0)
            [Array]::Copy($nextBlock, 0, $result, $offset, 16)
            $block = $nextBlock
        }
    } finally {
        $encryptor.Dispose()
        $aes.Dispose()
    }
    return ,$result
}

function Write-CompressedInt([IO.BinaryWriter]$writer, [int]$value) {
    if ($value -ge -127 -and $value -le 127) {
        $writer.Write([byte]($value -band 0xFF))
        return
    }
    $writer.Write([byte]0x80)
    $writer.Write($value)
}

function Write-WzAsciiString([IO.BinaryWriter]$writer, [string]$value) {
    $plain = $codePage936.GetBytes($value)
    Write-CompressedInt $writer (-$plain.Length)
    $keyStream = Get-WzKeyStream $plain.Length
    for ($i = 0; $i -lt $plain.Length; $i++) {
        $mask = (0xAA + $i) -band 0xFF
        $writer.Write([byte]($plain[$i] -bxor $keyStream[$i] -bxor $mask))
    }
}

function Write-WzUnicodeString([IO.BinaryWriter]$writer, [string]$value) {
    Write-CompressedInt $writer $value.Length
    $plain = [Text.Encoding]::Unicode.GetBytes($value)
    $keyStream = Get-WzKeyStream $plain.Length
    for ($i = 0; $i -lt $value.Length; $i++) {
        $mask = (0xAAAA + $i) -band 0xFFFF
        $writer.Write([byte]($plain[$i * 2] -bxor $keyStream[$i * 2] -bxor ($mask -band 0xFF)))
        $writer.Write([byte]($plain[$i * 2 + 1] -bxor $keyStream[$i * 2 + 1] -bxor (($mask -shr 8) -band 0xFF)))
    }
}

function Write-InlineWzString([IO.BinaryWriter]$writer, [string]$value) {
    $writer.Write([byte]0)
    Write-WzAsciiString $writer $value
}

function Write-InlineWzUnicodeString([IO.BinaryWriter]$writer, [string]$value) {
    $writer.Write([byte]0)
    Write-WzUnicodeString $writer $value
}

function New-BossAssistStringRecord {
    $payloadStream = [IO.MemoryStream]::new()
    $payloadWriter = [IO.BinaryWriter]::new($payloadStream)
    try {
        # Reuse the root Property type name at offset 1.
        $payloadWriter.Write([byte]0x1B)
        $payloadWriter.Write([int]1)
        $payloadWriter.Write([uint16]0)
        Write-CompressedInt $payloadWriter 2

        Write-InlineWzString $payloadWriter 'name'
        $payloadWriter.Write([byte]8)
        Write-InlineWzUnicodeString $payloadWriter '首领房辅助增益道具'

        Write-InlineWzString $payloadWriter 'desc'
        $payloadWriter.Write([byte]8)
        Write-InlineWzUnicodeString $payloadWriter '双击后选择一个可用的辅助增益。\n仅可在单人专属首领房间内使用，离开房间后自动失效。'
        $payload = $payloadStream.ToArray()
    } finally {
        $payloadWriter.Dispose()
        $payloadStream.Dispose()
    }

    $recordStream = [IO.MemoryStream]::new()
    $recordWriter = [IO.BinaryWriter]::new($recordStream)
    try {
        Write-InlineWzString $recordWriter '2430034'
        $recordWriter.Write([byte]9)
        $recordWriter.Write([int]$payload.Length)
        $recordWriter.Write($payload)
        return ,$recordStream.ToArray()
    } finally {
        $recordWriter.Dispose()
        $recordStream.Dispose()
    }
}

function Find-ByteSequence([byte[]]$haystack, [byte[]]$needle) {
    if ($needle.Length -eq 0 -or $haystack.Length -lt $needle.Length) {
        return -1
    }
    for ($i = 0; $i -le $haystack.Length - $needle.Length; $i++) {
        $matched = $true
        for ($j = 0; $j -lt $needle.Length; $j++) {
            if ($haystack[$i + $j] -ne $needle[$j]) {
                $matched = $false
                break
            }
        }
        if ($matched) {
            return $i
        }
    }
    return -1
}

function Get-RootChildCount([byte[]]$bytes) {
    if ($bytes.Length -lt 17 -or $bytes[12] -ne 0x80) {
        throw 'Unexpected standalone IMG root header.'
    }
    return [BitConverter]::ToInt32($bytes, 13)
}

function Set-RootChildCount([byte[]]$bytes, [int]$count) {
    $encoded = [BitConverter]::GetBytes($count)
    [Array]::Copy($encoded, 0, $bytes, 13, 4)
}

function Join-ByteArrays([byte[]]$prefix, [byte[]]$suffix) {
    if ($null -eq $prefix) {
        $prefix = [byte[]]@()
    }
    if ($null -eq $suffix) {
        $suffix = [byte[]]@()
    }
    $joined = New-Object byte[] ($prefix.Length + $suffix.Length)
    [Array]::Copy($prefix, 0, $joined, 0, $prefix.Length)
    [Array]::Copy($suffix, 0, $joined, $prefix.Length, $suffix.Length)
    return ,$joined
}

function Update-StringTable([string]$path, [byte[]]$newRecord) {
    $bytes = [IO.File]::ReadAllBytes($path)
    if ((Find-ByteSequence $bytes $newRecord) -ge 0) {
        return 'already-current'
    }

    $outerPrefix = $newRecord[0..9]
    $recordOffset = Find-ByteSequence $bytes $outerPrefix
    if ($recordOffset -ge 0) {
        $payloadLength = [BitConverter]::ToInt32($bytes, $recordOffset + $outerPrefix.Length)
        $oldRecordLength = $outerPrefix.Length + 4 + $payloadLength
        $before = if ($recordOffset -eq 0) { [byte[]]@() } else { $bytes[0..($recordOffset - 1)] }
        $afterOffset = $recordOffset + $oldRecordLength
        $after = if ($afterOffset -ge $bytes.Length) { [byte[]]@() } else { $bytes[$afterOffset..($bytes.Length - 1)] }
        $updated = Join-ByteArrays (Join-ByteArrays $before $newRecord) $after
        [IO.File]::WriteAllBytes($path, $updated)
        return 'replaced-english-entry'
    }

    $oldCount = Get-RootChildCount $bytes
    Set-RootChildCount $bytes ($oldCount + 1)
    [IO.File]::WriteAllBytes($path, (Join-ByteArrays $bytes $newRecord))
    return 'added-chinese-entry'
}

function Update-Commodity([string]$path) {
    $bytes = [IO.File]::ReadAllBytes($path)
    $newSn = [byte[]](0x80, 0xED, 0x91, 0x02, 0x03)
    if ((Find-ByteSequence $bytes $newSn) -ge 0) {
        return 'already-current'
    }

    # Exact v83 Commodity node 8158 (SN 50500076), up to but not including SN.
    $sourcePrefix = [byte[]](
        0x00, 0xFC, 0x04, 0x34, 0xA6, 0x31, 0x09, 0x4B, 0, 0, 0,
        0x1B, 0x01, 0, 0, 0, 0, 0, 0x08, 0x00, 0xFE, 0x6F, 0x4B, 0x03
    )
    $sourceOffset = Find-ByteSequence $bytes $sourcePrefix
    if ($sourceOffset -lt 0) {
        throw 'Could not locate the expected Commodity template node.'
    }

    $oldCount = Get-RootChildCount $bytes
    $payloadLength = [BitConverter]::ToInt32($bytes, $sourceOffset + 7)
    $payload = New-Object byte[] $payloadLength
    [Array]::Copy($bytes, $sourceOffset + 11, $payload, 0, $payloadLength)

    # Commodity's top-level children are a zero-based numeric sequence. The
    # original client converts every child name to an integer while entering
    # the Cash Shop, so a descriptive name such as "boss" crashes that parse.
    # Use the next numeric index and let its variable-length WZ string encoding
    # grow naturally instead of overwriting a four-character template name.
    [Array]::Copy($newSn, 0, $payload, 13, 5)
    [Array]::Copy([byte[]](0x80, 0x52, 0x14, 0x25, 0x00), 0, $payload, 24, 5) # ItemId 2430034
    [Array]::Copy([byte[]](0x80, 0x6C, 0x07, 0x00, 0x00), 0, $payload, 42, 5) # Price 1900
    $payload[60] = 1 # Priority

    $recordStream = [IO.MemoryStream]::new()
    $recordWriter = [IO.BinaryWriter]::new($recordStream)
    try {
        Write-InlineWzString $recordWriter $oldCount.ToString([Globalization.CultureInfo]::InvariantCulture)
        $recordWriter.Write([byte]9)
        $recordWriter.Write([int]$payloadLength)
        $recordWriter.Write($payload)
        $record = $recordStream.ToArray()
    } finally {
        $recordWriter.Dispose()
        $recordStream.Dispose()
    }

    Set-RootChildCount $bytes ($oldCount + 1)
    [IO.File]::WriteAllBytes($path, (Join-ByteArrays $bytes $record))
    return 'added-purchasable-commodity'
}

function Import-MapleLib([string]$directory) {
    $mapleLibPath = Join-Path $directory 'MapleLib.dll'
    if (-not (Test-Path -LiteralPath $mapleLibPath -PathType Leaf)) {
        throw "MapleLib.dll was not found in: $directory"
    }

    # MapleLib references several sibling assemblies. Load every managed DLL
    # from the WzBridge output directory before resolving MapleLib's types.
    foreach ($assemblyPath in Get-ChildItem -LiteralPath $directory -Filter '*.dll' -File) {
        try {
            [void][Reflection.Assembly]::LoadFrom($assemblyPath.FullName)
        } catch {
            # Native and optional DLLs are expected to fail managed loading.
        }
    }

    try {
        $mapleAssembly = [Reflection.Assembly]::LoadFrom($mapleLibPath)
    } catch {
        throw "Unable to load MapleLib.dll from ${directory}: $($_.Exception.Message)"
    }
    if ($null -eq $mapleAssembly.GetType('MapleLib.WzLib.Serializer.WzImgDeserializer')) {
        throw "Loaded MapleLib.dll does not expose WzImgDeserializer: $mapleLibPath"
    }
}

function Update-BossAssistItemCashFlag([string]$path, [string]$mapleLibDirectory) {
    Import-MapleLib $mapleLibDirectory

    $parsed = $false
    $deserializer = [MapleLib.WzLib.Serializer.WzImgDeserializer]::new($false)
    $image = $deserializer.WzImageFromIMGFile(
        $path,
        [MapleLib.WzLib.WzAESConstant]::WZ_GMSIV,
        [IO.Path]::GetFileName($path),
        [ref]$parsed
    )
    if (-not $parsed) {
        $image.Dispose()
        throw "Unable to parse item IMG: $path"
    }
    $originalPropertyCount = $image.WzProperties.Count

    $temporaryPath = "$path.boss-assist.tmp.img"
    try {
        $item = $image['02430034']
        if ($null -eq $item -or $null -eq $item['info']) {
            throw 'Boss assist item 02430034 or its info node is missing.'
        }

        $info = $item['info']
        $cash = $info['cash']
        if ($null -ne $cash -and $cash.Value -eq 1) {
            return 'already-current'
        }

        if ($null -eq $cash) {
            $info.AddProperty([MapleLib.WzLib.WzProperties.WzIntProperty]::new('cash', 1))
        } else {
            $cash.Value = 1
        }

        if (Test-Path -LiteralPath $temporaryPath) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
        [MapleLib.MapleCryptoLib.MapleCryptoConstants]::UserKey_WzLib =
            [MapleLib.MapleCryptoLib.MapleCryptoConstants]::MAPLESTORY_USERKEY_DEFAULT.Clone()
        $image.Changed = $true
        $serializer = [MapleLib.WzLib.Serializer.WzImgSerializer]::new(
            [MapleLib.WzLib.WzAESConstant]::WZ_GMSIV
        )
        $serializer.SerializeImage($image, $temporaryPath)
    } finally {
        $image.Dispose()
    }

    $verifyParsed = $false
    $verifyDeserializer = [MapleLib.WzLib.Serializer.WzImgDeserializer]::new($true)
    $verifyImage = $verifyDeserializer.WzImageFromIMGFile(
        $temporaryPath,
        [MapleLib.WzLib.WzAESConstant]::WZ_GMSIV,
        [IO.Path]::GetFileName($temporaryPath),
        [ref]$verifyParsed
    )
    try {
        $verifyItem = if ($verifyParsed) { $verifyImage['02430034'] } else { $null }
        $verifyInfo = if ($null -ne $verifyItem) { $verifyItem['info'] } else { $null }
        if ((-not $verifyParsed) -or
            ($verifyImage.WzProperties.Count -ne $originalPropertyCount) -or
            ($null -eq $verifyInfo) -or
            ($null -eq $verifyInfo['cash']) -or
            ($verifyInfo['cash'].Value -ne 1) -or
            ($null -eq $verifyInfo['icon']) -or
            ($null -eq $verifyInfo['iconRaw'])) {
            throw 'Patched item IMG failed cash-flag or icon verification.'
        }
    } finally {
        if ($null -ne $verifyImage) {
            $verifyImage.Dispose()
        }
    }

    Move-Item -LiteralPath $temporaryPath -Destination $path -Force
    return 'added-cash-flag'
}

$dataConsume = Join-Path $resolvedClientPath 'Data\String\Consume.img'
$englishConsume = Join-Path $resolvedClientPath 'EN\String\Consume.img'
$dataCash = Join-Path $resolvedClientPath 'Data\String\Cash.img'
$englishCash = Join-Path $resolvedClientPath 'EN\String\Cash.img'
$commodity = Join-Path $resolvedClientPath 'Data\Etc\Commodity.img'
$itemConsume = Join-Path $resolvedClientPath 'Data\Item\Consume\0243.img'
$targets = @($dataConsume, $englishConsume, $dataCash, $englishCash, $commodity, $itemConsume)
foreach ($target in $targets) {
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        throw "Required Research client resource is missing: $target"
    }
    $backup = "$target.before-boss-assist-cn-shop-fix-20260911"
    if (-not (Test-Path -LiteralPath $backup)) {
        Copy-Item -LiteralPath $target -Destination $backup
    }
}

$stringRecord = New-BossAssistStringRecord
$dataResult = Update-StringTable $dataConsume $stringRecord
$englishResult = Update-StringTable $englishConsume $stringRecord
$dataCashResult = Update-StringTable $dataCash $stringRecord
$englishCashResult = Update-StringTable $englishCash $stringRecord
$commodityResult = Update-Commodity $commodity
$itemCashResult = Update-BossAssistItemCashFlag $itemConsume $MapleLibDirectory

[pscustomobject]@{
    DataConsume = $dataResult
    EnglishConsume = $englishResult
    DataCash = $dataCashResult
    EnglishCash = $englishCashResult
    Commodity = $commodityResult
    ItemConsumeCashFlag = $itemCashResult
    ItemName = '首领房辅助增益道具'
    DataConsumeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $dataConsume).Hash
    EnglishConsumeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $englishConsume).Hash
    DataCashHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $dataCash).Hash
    EnglishCashHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $englishCash).Hash
    CommodityHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $commodity).Hash
    ItemConsumeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $itemConsume).Hash
}
