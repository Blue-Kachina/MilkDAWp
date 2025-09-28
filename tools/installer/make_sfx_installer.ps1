<#!
.SYNOPSIS
  Creates a self-extracting (SFX) installer EXE or a simple ZIP+install.bat for MilkDAWp.

.DESCRIPTION
  - Packages a built MilkDAWp.vst3 bundle into a .zip.
  - If 7-Zip is available (7z.exe and 7z.sfx), builds an SFX that auto-extracts to
    %LOCALAPPDATA%\Programs\Common\VST3 (per-user VST3 folder).
  - If 7-Zip is not available, emits the .zip and a portable install.bat that uses
    PowerShell Expand-Archive to extract to the same folder.

.PARAMETER Vst3Path
  Full path to MilkDAWp.vst3 (the bundle directory) to package.

.PARAMETER OutputDir
  Output directory to place the generated artifacts. Defaults to the current directory.

.EXAMPLE
  .\tools\installer\make_sfx_installer.ps1 -Vst3Path "D:\\Dev\\Hobby\\MilkDAWp\\cmake-build-release\\MilkDAWp_VST3\\MilkDAWp.vst3" -OutputDir "D:\\temp"

.NOTES
  - Requires PowerShell 5+.
  - For SFX build: requires 7-Zip installed and accessible. The script attempts typical locations.
#>
param(
  [Parameter(Mandatory=$true)] [string]$Vst3Path,
  [Parameter(Mandatory=$false)] [string]$OutputDir = (Get-Location).Path
)

$ErrorActionPreference = 'Stop'

function Resolve-7Zip {
  $candidates = @(
    (Get-Command 7z.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Path,
    "$Env:ProgramFiles\\7-Zip\\7z.exe",
    "$Env:ProgramFiles(x86)\\7-Zip\\7z.exe"
  ) | Where-Object { $_ -and (Test-Path $_) }
  if ($candidates.Count -gt 0) { return $candidates[0] }
  return $null
}

function Resolve-7ZipSfxModule {
  $possibleDirs = @(
    Split-Path (Resolve-7Zip) -Parent,
    "$Env:ProgramFiles\\7-Zip",
    "$Env:ProgramFiles(x86)\\7-Zip"
  ) | Where-Object { $_ -and (Test-Path $_) }
  foreach ($d in $possibleDirs) {
    $sfx = Join-Path $d '7z.sfx'
    if (Test-Path $sfx) { return $sfx }
  }
  return $null
}

function Resolve-WinRAR {
  $candidates = @(
    (Get-Command rar.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Path,
    (Get-Command WinRAR.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Path,
    "$Env:ProgramFiles\\WinRAR\\rar.exe",
    "$Env:ProgramFiles\\WinRAR\\WinRAR.exe",
    "$Env:ProgramFiles(x86)\\WinRAR\\rar.exe",
    "$Env:ProgramFiles(x86)\\WinRAR\\WinRAR.exe"
  ) | Where-Object { $_ -and (Test-Path $_) }
  if ($candidates.Count -gt 0) { return $candidates[0] }
  return $null
}

# Validate inputs
if (-not (Test-Path $Vst3Path)) {
  throw "VST3 path not found: $Vst3Path"
}
if (-not (Test-Path $OutputDir)) {
  New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

$bundleName = Split-Path $Vst3Path -Leaf
if ($bundleName -ne 'MilkDAWp.vst3') {
  Write-Host "Warning: Expected bundle directory named 'MilkDAWp.vst3' but found '$bundleName'. Proceeding anyway..." -ForegroundColor Yellow
}

$zipPath = Join-Path $OutputDir 'MilkDAWp_vst3.zip'
$sfxConfig = Join-Path (Split-Path $PSCommandPath -Parent) 'sfx-config.txt'
$sfxOut = Join-Path $OutputDir 'MilkDAWp_VST3_Installer.exe'
$installBat = Join-Path $OutputDir 'Install_MilkDAWp_per_user.bat'

# Create a staging directory to assemble the archive contents
$stage = Join-Path $OutputDir ('_stage_' + [System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null

try {
  # Copy the bundle folder into stage so it ends up at the archive root
  Copy-Item -Path $Vst3Path -Destination (Join-Path $stage 'MilkDAWp.vst3') -Recurse -Force

  # Create zip: ensure any previous file is removed
  if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  [System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $zipPath)
  Write-Host "Created: $zipPath"

  # Attempt SFX build (prefer WinRAR if available, then 7-Zip; else fallback to ZIP+BAT)
  $winrar = Resolve-WinRAR
  $winrarComment = Join-Path (Split-Path $PSCommandPath -Parent) 'winrar-sfx-comment.txt'
  $sevenZip = Resolve-7Zip
  $sfxModule = Resolve-7ZipSfxModule

  if ($winrar -and (Test-Path $winrarComment)) {
    Write-Host "WinRAR detected at: $winrar"
    # Build SFX using WinRAR
    if (Test-Path $sfxOut) { Remove-Item $sfxOut -Force }
    Push-Location $stage
    # Package everything in stage (which contains MilkDAWp.vst3) into SFX EXE
    & $winrar a -sfx -r -ep1 -z"$winrarComment" "$sfxOut" * | Out-Null
    Pop-Location
    if (Test-Path $sfxOut) {
      Write-Host "Created SFX installer (WinRAR): $sfxOut"
    } else {
      Write-Host "WinRAR packaging failed; will try 7-Zip..." -ForegroundColor Yellow
      goto :Try7Zip
    }
  }
  elseif ($sevenZip -and $sfxModule -and (Test-Path $sfxConfig)) {
    :Try7Zip
    Write-Host "7-Zip detected at: $sevenZip"
    $tmpDir = Join-Path $OutputDir ('_sfx_' + [System.Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmpDir | Out-Null

    # Repack with 7z for maximum SFX compatibility (optional; we can also use our zip directly)
    $archive7z = Join-Path $tmpDir 'payload.7z'
    Push-Location $stage
    & $sevenZip a -t7z -mx=9 $archive7z * | Out-Null
    Pop-Location

    # Copy config to tmp
    $configTmp = Join-Path $tmpDir 'config.txt'
    Copy-Item $sfxConfig $configTmp -Force

    # Build the SFX by concatenation: 7z.sfx + config + archive.7z
    if (Test-Path $sfxOut) { Remove-Item $sfxOut -Force }
    $outStream = [System.IO.File]::OpenWrite($sfxOut)
    try {
      foreach ($part in @($sfxModule, $configTmp, $archive7z)) {
        $bytes = [System.IO.File]::ReadAllBytes($part)
        $outStream.Write($bytes, 0, $bytes.Length)
      }
    }
    finally { $outStream.Close() }

    Write-Host "Created SFX installer (7-Zip): $sfxOut"
  }
  else {
    # Fallback: generate an install.bat that extracts the zip to the per-user VST3 folder
    $bat = "@echo off\r\nsetlocal enableextensions\r\nset DEST=%LOCALAPPDATA%\\Programs\\Common\\VST3\r\nif not exist \"%DEST%\" mkdir \"%DEST%\"\r\n\r\nset SCRIPT=%~f0\r\nset SCRIPT_DIR=%~dp0\r\nset ZIP=%SCRIPT_DIR%MilkDAWp_vst3.zip\r\n\r\npowershell -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -Path \"\"%ZIP%\"\" -DestinationPath \"\"%DEST%\"\" -Force\"\r\nif errorlevel 1 (\r\n  echo Failed to extract archive. Ensure PowerShell is available.\r\n  exit /b 1\r\n) else (\r\n  echo Installed MilkDAWp.vst3 to %DEST%\r\n)\r\npause\r\n"
    Set-Content -LiteralPath $installBat -Value $bat -Encoding ASCII
    Write-Host "Neither WinRAR nor 7-Zip SFX build available. Emitted ZIP + installer BAT:" -ForegroundColor Yellow
    Write-Host "  ZIP: $zipPath"
    Write-Host "  BAT: $installBat"
  }
}
finally {
  if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
}
