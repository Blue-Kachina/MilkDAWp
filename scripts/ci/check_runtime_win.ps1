param(
  [string]$BuildDir = "build",
  [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

Write-Host "[QA] Verifying runtime layout for Windows (config=$Config, buildDir=$BuildDir)"

# Locate the VST3 artefact directory produced by JUCE
$artefactsRoot = Join-Path $BuildDir "MilkDAWp_artefacts"
$vst3Root = Join-Path $artefactsRoot $Config
$vst3Root = Join-Path $vst3Root "VST3"

if (-not (Test-Path $vst3Root)) {
  throw "VST3 output directory not found: $vst3Root"
}

# Find MilkDAWp.vst3 (there may be nested Company/Product folders depending on JUCE version)
$vst3s = Get-ChildItem -Path $vst3Root -Recurse -Filter *.vst3 -ErrorAction SilentlyContinue
if ($vst3s.Count -eq 0) {
  throw "No .vst3 bundles found under $vst3Root"
}

$failures = @()
foreach ($bundle in $vst3s) {
  $bundleDir = Split-Path -Parent $bundle.FullName
  # On Windows, our Phase 3 step copies the projectM DLL next to the bundle in the same directory
  $dlls = Get-ChildItem -Path $bundleDir -Filter *.dll -ErrorAction SilentlyContinue
  $hasProjectMDll = $false
  foreach ($d in $dlls) {
    if ($d.Name -match "projectm" -or $d.Name -match "projectm.*\\.dll") {
      $hasProjectMDll = $true
      break
    }
  }
  if (-not $hasProjectMDll) {
    $failures += "Missing projectM DLL next to: $($bundle.FullName)"
  }
}

if ($failures.Count -gt 0) {
  Write-Error ("[QA] Runtime layout validation FAILED:`n" + ($failures -join "`n"))
  exit 2
}

Write-Host "[QA] Runtime layout validation PASSED for $($vst3s.Count) bundle(s)."