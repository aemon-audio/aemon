# Builds Aemon-Suite-Setup.exe, Aemon-Suite-VST3.zip and their checksums. AGPL-3.0-or-later.
# Usage: build-suite.ps1 -BuildDir build -OutDir dist -Version 0.1.0
param([string]$BuildDir = "build", [string]$OutDir = "dist", [string]$Version = "0.0.0")
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $here "..\..")).Path

$iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") |
  Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 not found" }

$bundles = @(Get-ChildItem (Join-Path $BuildDir "plugins\*\*_artefacts\Release\VST3\*.vst3") -Directory)
if ($bundles.Count -eq 0) { throw "No VST3 bundles in $BuildDir" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$stage = Join-Path ([IO.Path]::GetTempPath()) ("aemon-suite-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $stage | Out-Null
try {
  foreach ($b in $bundles) { Copy-Item $b.FullName $stage -Recurse }
  Get-ChildItem $stage -Recurse -Include *.pdb, *.lib, *.exp | Remove-Item -Force
  # collected first, so that a caller cutting the output short cannot stop the script while the compiler runs
  $log = & $iscc /Qp "/DAppVer=$Version" "/DVst3Root=$stage" "/DOutDir=$OutDir" (Join-Path $here "AemonSuite.iss") 2>&1 | Out-String
  if ($LASTEXITCODE -ne 0) { Write-Host $log; throw "ISCC failed ($LASTEXITCODE)" }

  Copy-Item (Join-Path $repo "LICENSE") (Join-Path $stage "LICENSE.txt")
  Copy-Item (Join-Path $here "THIRD-PARTY.txt") $stage
  Set-Content (Join-Path $stage "READ ME.txt") ("Aemon Suite $Version`r`n`r`nCopy the .vst3 folders to " +
    "C:\Program Files\Common Files\VST3, then scan for new plugins in your DAW.`r`n`r`n" +
    "Windows may ask you to confirm, because Aemon is not signed yet. Signing is coming soon.`r`n")
  $zip = Join-Path $OutDir "Aemon-Suite-VST3.zip"
  if (Test-Path $zip) { Remove-Item $zip -Force }
  Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
}
finally { Remove-Item $stage -Recurse -Force }

$lines = foreach ($f in "Aemon-Suite-Setup.exe", "Aemon-Suite-VST3.zip") {
  "{0}  {1}" -f (Get-FileHash (Join-Path $OutDir $f) -Algorithm SHA256).Hash.ToLower(), $f
}
Set-Content (Join-Path $OutDir "SHA256SUMS.txt") $lines
Write-Host ("Built Aemon Suite {0} with {1}" -f $Version, (($bundles | ForEach-Object { $_.BaseName }) -join ", "))
Get-ChildItem $OutDir | ForEach-Object { Write-Host ("  {0}  {1:n1} MB" -f $_.Name, ($_.Length / 1MB)) }
