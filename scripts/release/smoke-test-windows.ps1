#Requires -Version 7
<#
.SYNOPSIS
  Installs, checks and uninstalls the Windows release (6.2).
.DESCRIPTION
  For the release workflow's throwaway runner only: the installer uses the
  real identity (D1), so on a developer's machine it would replace v1 in
  every host. Steps: silent install (with the .milk association), check
  every file and registry entry, pluginval (strictness 5) on the installed
  VST3 with projectM required, start the installed app, silent uninstall,
  check that nothing is left. Exits non-zero on the first failure.
.EXAMPLE
  pwsh scripts/release/smoke-test-windows.ps1 -Dist dist -Version 1.0.0-beta.1
#>
param(
  [Parameter(Mandatory = $true)][string]$Dist,
  [Parameter(Mandatory = $true)][string]$Version
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
function Fail([string]$message) { Write-Host "smoke-test-windows: FAIL: $message"; exit 1 }
function Pass([string]$message) { Write-Host "smoke-test-windows: ok: $message" }

$setup = Join-Path (Resolve-Path $Dist) "MilkDAWp-$Version-windows-x64-setup.exe"
if (-not (Test-Path $setup)) { Fail "missing $setup" }
$log = Join-Path ([System.IO.Path]::GetTempPath()) "milkdawp-install.log"

$app = Join-Path $env:ProgramFiles "MilkDAWp"
$vst3 = Join-Path $env:CommonProgramFiles "VST3\MilkDAWp.vst3"
$content = Join-Path $env:ProgramData "MilkDAWp"

# 1. Install.
$proc = Start-Process $setup -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/TASKS=associate", "/LOG=`"$log`"" -Wait -PassThru
if ($proc.ExitCode -ne 0) { Get-Content $log -Tail 40; Fail "installer exited with $($proc.ExitCode)" }
Pass "installed"

# 2. Files and registry.
$expected = @(
  "$app\MilkDAWp.exe", "$app\projectM-4.dll", "$app\unins000.exe", "$app\licenses\LICENSE.txt",
  "$app\licenses\projectM-COPYRIGHT.txt", "$app\README.txt",
  "$vst3\Contents\x86_64-win\MilkDAWp.vst3", "$vst3\Contents\x86_64-win\projectM-4.dll",
  "$content\Presets\Cream of the Crop\LICENSE.md", "$content\Textures\worms.jpg"
)
foreach ($path in $expected) { if (-not (Test-Path $path)) { Fail "not installed: $path" } }
$presets = (Get-ChildItem "$content\Presets" -Recurse -Filter *.milk).Count
if ($presets -lt 9000) { Fail "only $presets presets installed" }
$extra = Get-ChildItem "$vst3\Contents\x86_64-win" | Where-Object { $_.Name -notin @("MilkDAWp.vst3", "projectM-4.dll") }
if ($extra) { Fail "unexpected files next to the plugin: $($extra.Name -join ', ')" }
Pass "files in place ($presets presets)"

if ((Get-ItemProperty "HKLM:\Software\Classes\.milk" -ErrorAction SilentlyContinue).'(default)' -ne "MilkDAWp.Preset") { Fail ".milk isn't associated" }
$command = (Get-ItemProperty "HKLM:\Software\Classes\MilkDAWp.Preset\shell\open\command").'(default)'
if ($command -notlike "*MilkDAWp.exe*%1*") { Fail "odd open command: $command" }
$vc = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" -ErrorAction SilentlyContinue
if (-not $vc -or $vc.Installed -ne 1) { Fail "the VC++ runtime isn't installed" }
Pass ".milk association and VC++ runtime $($vc.Major).$($vc.Minor)"

# 3. pluginval on the installed plugin.
$env:MILKDAWP_REQUIRE_PROJECTM = "1"
& (Join-Path $RepoRoot "scripts\pluginval.ps1") -Plugin $vst3 -Strictness 5 -SkipGui
if ($LASTEXITCODE -ne 0) { Fail "pluginval on the installed VST3" }
Remove-Item Env:MILKDAWP_REQUIRE_PROJECTM
Pass "pluginval (strictness 5) on the installed VST3"

# 4. The installed app starts and keeps running. (The runner has no GPU, so
# its engine reports unavailable; this checks it loads and doesn't crash.)
$running = Start-Process "$app\MilkDAWp.exe" -PassThru
Start-Sleep -Seconds 8
if ($running.HasExited) { Fail "the installed app exited with $($running.ExitCode)" }
Stop-Process -Id $running.Id -Force
Pass "the installed app starts"

# 5. Uninstall. The uninstaller re-launches itself from %TEMP% and returns at
# once, so wait for the files to go.
Start-Process "$app\unins000.exe" -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART" -Wait
$deadline = (Get-Date).AddSeconds(120)
while ((Test-Path "$app\MilkDAWp.exe") -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
Start-Sleep -Seconds 3
foreach ($path in @("$app\MilkDAWp.exe", $vst3, "$content\Presets", "$content\Textures")) {
  if (Test-Path $path) { Fail "uninstall left $path" }
}
if (Test-Path "HKLM:\Software\Classes\MilkDAWp.Preset") { Fail "uninstall left the .milk file type" }
Pass "uninstalled cleanly"
Write-Host "smoke-test-windows: all passed"
