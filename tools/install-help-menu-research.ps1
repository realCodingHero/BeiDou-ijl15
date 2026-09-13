param(
    [string]$ClientPath = 'C:\Game\BeiDou-Client-research',
    [string]$DllPath = (Join-Path $PSScriptRoot '..\out\window-scaling\ijl15.dll'),
    [string]$ResourcesPath = (Join-Path $PSScriptRoot '..\out\help-menu-resources')
)
$ErrorActionPreference = 'Stop'
$client = (Resolve-Path -LiteralPath $ClientPath).Path
if ($client -ne 'C:\Game\BeiDou-Client-research') { throw 'Only the Research client may be installed by this script.' }
$running = Get-Process -Name BeiDou,MoonKidsMS -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and ([IO.Path]::GetDirectoryName($_.Path) -eq $client) }
if ($running) { throw 'Close the Research client before installing.' }
if ((Get-FileHash -LiteralPath (Join-Path $client 'BeiDou.exe')).Hash -ne
    '1198FA57CA5A7C489BAE43EC13C69681D9CABE0F96762F3DC0357FACF2E7D4DF') { throw 'Unsupported client executable.' }
$files = @{
    'ijl15.dll' = (Resolve-Path -LiteralPath $DllPath).Path
    'Data\UI\HelpMenu.img' = (Resolve-Path -LiteralPath (Join-Path $ResourcesPath 'Data\UI\HelpMenu.img')).Path
    'EN\UI\HelpMenu.img' = (Resolve-Path -LiteralPath (Join-Path $ResourcesPath 'EN\UI\HelpMenu.img')).Path
}
# Validate and prepare config before replacing any client files. Latin1 is a
# reversible byte mapping, preserving the existing INI encoding and settings.
$config = Join-Path $client 'config.ini'
$encoding = [Text.Encoding]::Latin1
$text = $encoding.GetString([IO.File]::ReadAllBytes($config))
$pattern = '(?mi)^\[optional\][^\r\n]*(?:\r?\n)(?:(?!^\[).*(?:\r?\n|$))*'
$section = [regex]::Match($text, $pattern)
if (-not $section.Success) { throw 'Missing optional section; client files were not changed.' }
$replacement = $section.Value
if ($replacement -match '(?mi)^native_help_menu\s*=') {
    $replacement = [regex]::Replace($replacement, '(?mi)^native_help_menu\s*=[^\r\n]*', 'native_help_menu=true')
} else {
    $replacement = $replacement.Insert($replacement.IndexOf("`n") + 1, "native_help_menu=true`r`n")
}
$text = $text.Substring(0, $section.Index) + $replacement + $text.Substring($section.Index + $section.Length)
$backup = Join-Path $client ('.deployment-backups\help-menu-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
[void][IO.Directory]::CreateDirectory($backup)
$manifest = @()
foreach ($relative in @('config.ini') + @($files.Keys)) {
    $path = Join-Path $client $relative
    $existed = Test-Path -LiteralPath $path
    if ($existed) {
        $saved = Join-Path $backup $relative
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($saved))
        Copy-Item -LiteralPath $path -Destination $saved
    }
    $manifest += [pscustomobject]@{ path = $relative; existed = $existed }
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding utf8
foreach ($relative in $files.Keys) {
    $target = Join-Path $client $relative
    Copy-Item -LiteralPath $files[$relative] -Destination $target -Force
    if ((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $files[$relative]).Hash) {
        throw "Copy verification failed: $relative; backup at $backup"
    }
}
[IO.File]::WriteAllBytes($config, $encoding.GetBytes($text))
Write-Output "Research help menu installed. Backup: $backup"
