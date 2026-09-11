param(
    [Parameter(Mandatory = $true)]
    [string]$ClientPath
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

function Update-ConsumeStrings([string]$path, [byte[]]$newRecord) {
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

$dataConsume = Join-Path $resolvedClientPath 'Data\String\Consume.img'
$englishConsume = Join-Path $resolvedClientPath 'EN\String\Consume.img'
$commodity = Join-Path $resolvedClientPath 'Data\Etc\Commodity.img'
$targets = @($dataConsume, $englishConsume, $commodity)
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
$dataResult = Update-ConsumeStrings $dataConsume $stringRecord
$englishResult = Update-ConsumeStrings $englishConsume $stringRecord
$commodityResult = Update-Commodity $commodity

[pscustomobject]@{
    DataConsume = $dataResult
    EnglishConsume = $englishResult
    Commodity = $commodityResult
    ItemName = '首领房辅助增益道具'
    DataConsumeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $dataConsume).Hash
    EnglishConsumeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $englishConsume).Hash
    CommodityHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $commodity).Hash
}
