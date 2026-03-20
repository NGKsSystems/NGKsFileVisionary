param(
  [string]$ProofDir = ""
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if ([string]::IsNullOrWhiteSpace($ProofDir)) {
  $ProofDir = (Get-ChildItem "_proof" -Directory |
    Where-Object { $_.Name -like 'closeout_regression_lock_*' } |
    Sort-Object Name |
    Select-Object -Last 1).FullName
}

if (-not (Test-Path $ProofDir)) {
  Write-Error "proof_dir_not_found=$ProofDir"
}

$assertPath = Join-Path $ProofDir '10_assertions_runtime.txt'
$extractPath = Join-Path $ProofDir '09_runtime_extract.txt'

if (-not (Test-Path $assertPath)) {
  Write-Error "missing_assertions_file=$assertPath"
}
if (-not (Test-Path $extractPath)) {
  Write-Error "missing_runtime_extract=$extractPath"
}

$assert = @{}
Get-Content $assertPath | ForEach-Object {
  if ($_ -match '^([^=]+)=(.*)$') {
    $assert[$matches[1].Trim()] = $matches[2].Trim()
  }
}

$lines = Get-Content $extractPath

$childSignal = [bool]($lines | Select-String -Pattern 'fs_watcher_directory_changed path=.*/child_path_probe_direct' -Quiet)
$childEmit = [bool]($lines | Select-String -Pattern 'fs_watcher_refresh_triggered .*changed_norm=.*/child_path_probe_direct' -Quiet)
$mainReach = (($assert['main_event'] -eq 'True') -or ($assert['main_event'] -eq 'true'))
$requery = (($assert['requery'] -eq 'True') -or ($assert['requery'] -eq 'true'))
$proxy = (($assert['proxy_sync'] -eq 'True') -or ($assert['proxy_sync'] -eq 'true'))
$childMs = -1
if ($assert.ContainsKey('child_delete_model_sync_ms')) {
  [void][int]::TryParse($assert['child_delete_model_sync_ms'], [ref]$childMs)
}
$latencyOk = ($childMs -ge 0 -and $childMs -le 1000)

$overall = ($childSignal -and $childEmit -and $mainReach -and $requery -and $proxy -and $latencyOk)

$out = @(
  "proof_dir=$ProofDir",
  "child_signal_source=$childSignal",
  "child_signal_emitted=$childEmit",
  "child_event_reaches_mainwindow=$mainReach",
  "visible_root_requery_triggered=$requery",
  "proxy_model_sync_present=$proxy",
  "child_delete_model_sync_ms=$childMs",
  "child_delete_within_1s=$latencyOk",
  "watcher_chain_regression_guard=$([string]$overall)"
)

$outPath = Join-Path $ProofDir '15_watcher_chain_guard_result.txt'
$out | Set-Content $outPath
$out | ForEach-Object { Write-Output $_ }

if (-not $overall) {
  exit 1
}
