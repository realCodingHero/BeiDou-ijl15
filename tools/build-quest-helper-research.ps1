# Builds the quest helper UI polish, runs its native/GPU tests and deploys ONLY to the
# research client. BeiDou-Client is never touched (build.bat deploys there; not used).
param(
 [string]$Research='C:\Game\BeiDou-Client-research',
 [string]$ToolchainRoot='C:\Game\BeiDou-Server\tools\msvc'
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$outDir=Join-Path $repo 'out'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$log=Join-Path $outDir 'quest-helper-research-build.log'
Set-Content -Path $log -Value ("== Quest helper UI build "+(Get-Date -Format s)) -Encoding utf8
function Step([string]$name,[scriptblock]$block){
 ("`n== "+$name) | Tee-Object -FilePath $log -Append
 & $block *>&1 | ForEach-Object {"$_"} | Tee-Object -FilePath $log -Append
}
$status='FAILED'
try {
 Step 'Check research client is closed' {
  $running=@(Get-Process -ErrorAction SilentlyContinue | Where-Object { try { $_.Path -and $_.Path.StartsWith($Research,[StringComparison]::OrdinalIgnoreCase) } catch { $false } })
  if($running.Count){throw ('Close the research client first: '+(($running | ForEach-Object Name) -join ', '))}
  'research client not running'
 }
 Step 'Native UI tests (offline render + PCOM canvas + DirectX canvas)' { & (Join-Path $PSScriptRoot 'test-quest-helper-native.ps1') -Client $Research -ToolchainRoot $ToolchainRoot }
 Step 'Client DLL build' { & (Join-Path $PSScriptRoot 'build-window-scaling.ps1') -ToolchainRoot $ToolchainRoot }
 Step 'Deploy to research client only' {
  $dll=Join-Path $repo 'out\window-scaling\ijl15.dll'
  if(!(Test-Path $dll)){throw 'Build did not produce out\window-scaling\ijl15.dll'}
  $stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
  $targetDll=Join-Path $Research 'ijl15.dll';$targetIni=Join-Path $Research 'QuestHelperUI.ini'
  Copy-Item -LiteralPath $targetDll -Destination (Join-Path $Research "ijl15.before-quest-helper-ui-polish.$stamp.bak")
  if(Test-Path $targetIni){Copy-Item -LiteralPath $targetIni -Destination (Join-Path $Research "QuestHelperUI.before-ui-polish.$stamp.ini.bak")}
  Copy-Item -LiteralPath $dll -Destination $targetDll -Force
  Copy-Item -LiteralPath (Join-Path $repo 'resources\QuestHelperUI.ini') -Destination $targetIni -Force
  'backup: ijl15.before-quest-helper-ui-polish.'+$stamp+'.bak'
  'deployed sha256: '+(Get-FileHash -LiteralPath $targetDll -Algorithm SHA256).Hash
  'BeiDou-Client was not modified.'
 }
 $status='SUCCESS'
} catch {
 ('ERROR: '+$_.Exception.Message) | Tee-Object -FilePath $log -Append
 ($_.ScriptStackTrace) | Tee-Object -FilePath $log -Append
} finally {
 ("`nRESULT: "+$status) | Tee-Object -FilePath $log -Append
}
if($status -ne 'SUCCESS'){exit 1}
