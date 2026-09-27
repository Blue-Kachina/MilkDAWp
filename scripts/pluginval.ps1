#Requires -Version 5.1
<#
.SYNOPSIS
  Runs pluginval against the MilkDAWp VST3 (Phase 3.9).
.DESCRIPTION
  Downloads the pluginval version pinned in toolchain.json into build-tools/
  (checked against its SHA-256), then validates the plugin. Exits with
  pluginval's exit code, so a failed validation fails the calling script or
  CI step. The full log is written to build-tools/pluginval-logs/.
.PARAMETER Plugin
  Path to a .vst3 bundle. Default: the most recently built bundle under
  build-*/plugin/milkdawp_plugin_artefacts/.
.PARAMETER Strictness
  pluginval strictness level, 1-10. Default 10; the release gate is 5.
.PARAMETER Repeat
  Run the whole suite this many times with a randomised test order.
.PARAMETER SkipGui
  Skip the tests that open the editor.
.EXAMPLE
  pwsh scripts/pluginval.ps1 -Repeat 5
#>
param(
  [string]$Plugin,
  [ValidateRange(1, 10)][int]$Strictness = 10,
  [ValidateRange(1, 1000)][int]$Repeat = 1,
  [switch]$SkipGui
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Toolchain = Get-Content (Join-Path $RepoRoot "toolchain.json") -Raw | ConvertFrom-Json
$Version = $Toolchain.pluginval.version
$ToolsDir = Join-Path $RepoRoot "build-tools"
$PluginvalDir = Join-Path $ToolsDir "pluginval-$Version"
$Exe = Join-Path $PluginvalDir "pluginval.exe"

if (-not (Test-Path $Exe)) {
  New-Item -ItemType Directory -Force $PluginvalDir | Out-Null
  $zip = Join-Path $PluginvalDir "pluginval_Windows.zip"
  $url = "https://github.com/Tracktion/pluginval/releases/download/v$Version/pluginval_Windows.zip"
  Write-Host "Downloading pluginval $Version"
  Invoke-WebRequest $url -OutFile $zip -UseBasicParsing
  $hash = (Get-FileHash $zip -Algorithm SHA256).Hash
  if ($hash -ne $Toolchain.pluginval.sha256Windows) {
    Remove-Item $zip
    throw "pluginval download hash mismatch: got $hash, toolchain.json pins $($Toolchain.pluginval.sha256Windows)"
  }
  Expand-Archive $zip $PluginvalDir -Force
  Remove-Item $zip
}

if (-not $Plugin) {
  # The bundle is a folder; sort by the binary inside it, which is what a rebuild touches.
  $Plugin = Resolve-Path (Join-Path $RepoRoot "build-*/plugin/milkdawp_plugin_artefacts/*/VST3/*.vst3") `
      -ErrorAction SilentlyContinue |
    Sort-Object { (Get-ChildItem (Join-Path $_.Path "Contents/x86_64-win") -Filter *.vst3 -File).LastWriteTime } |
    Select-Object -Last 1 -ExpandProperty Path
  if (-not $Plugin) { throw "No VST3 bundle found under build-*/. Build milkdawp_plugin_VST3 first, or pass -Plugin." }
}
if (-not (Test-Path $Plugin)) { throw "Plugin not found: $Plugin" }

$LogDir = Join-Path $ToolsDir "pluginval-logs"
New-Item -ItemType Directory -Force $LogDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$log = Join-Path $LogDir "pluginval-$stamp.log"

$argList = @("--strictness-level", $Strictness, "--timeout-ms", 900000)
if ($Repeat -gt 1) { $argList += @("--repeat", $Repeat, "--randomise") }
if ($SkipGui) { $argList += "--skip-gui-tests" }
$argList += @("--validate", "`"$Plugin`"")

Write-Host "pluginval $Version, strictness $Strictness, repeat $Repeat$(if ($SkipGui) { ', no GUI' })"
Write-Host "Plugin: $Plugin"
# pluginval is a GUI-subsystem exe: calling it with & returns at once without its output, so wait
# on the process and redirect its stdout to the log.
$proc = Start-Process -FilePath $Exe -ArgumentList $argList -NoNewWindow -Wait -PassThru `
  -RedirectStandardOutput $log -RedirectStandardError "$log.err"
Get-Content $log, "$log.err" | Write-Host
Write-Host "Log: $log"
if ($proc.ExitCode -ne 0) { Write-Host "pluginval FAILED (exit $($proc.ExitCode))" }
exit $proc.ExitCode
