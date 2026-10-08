# Installs Atmospheric on Windows. Right-click > Run with PowerShell.
# VST3 goes to C:\Program Files\Common Files\VST3 (needs admin), the app to %LOCALAPPDATA%\Programs.
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
  Start-Process powershell -Verb RunAs -ArgumentList "-ExecutionPolicy Bypass -File `"$($MyInvocation.MyCommand.Path)`""
  exit
}

$vst3 = Join-Path $env:CommonProgramFiles "VST3"
New-Item -ItemType Directory -Force -Path $vst3 | Out-Null
Remove-Item -Recurse -Force (Join-Path $vst3 "Atmospheric.vst3") -ErrorAction SilentlyContinue
Copy-Item -Recurse (Join-Path $here "Atmospheric.vst3") $vst3

$app = Join-Path $env:LOCALAPPDATA "Programs\Atmospheric"
New-Item -ItemType Directory -Force -Path $app | Out-Null
Copy-Item -Force (Join-Path $here "Atmospheric.exe") $app
Get-ChildItem -Recurse $vst3\Atmospheric.vst3, $app | Unblock-File

Write-Host "Installed. VST3: $vst3\Atmospheric.vst3   App: $app\Atmospheric.exe"
Write-Host "Rescan plugins in your DAW."
Read-Host "Press Enter to close"
