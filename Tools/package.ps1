# Builds the release zips from a finished Release build:
#   dist\OpenMasteringStudio-<ver>-win64.zip    plugin + standalone app + docs + license
#   dist\OpenMasteringStudio-<ver>-source.zip   complete corresponding source (GNU AGPLv3), incl. JUCE and the ASIO SDK
# Usage (from the project folder, after "cmake --build build --config Release"):
#   pwsh -File Tools\package.ps1   (PowerShell 7: its zips use "/" paths, which every unzip tool reads correctly)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$ver = [regex]::Match((Get-Content "$root\CMakeLists.txt" -Raw), 'project\(MasteringStudio VERSION ([0-9.]+)').Groups[1].Value
$art = Join-Path $root 'build\MasteringStudio_artefacts\Release'
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null

# ---- Version check: exe, vst3 binary and moduleinfo must all report the build version ----
$exeV = (Get-Item "$art\Standalone\Open Mastering Studio.exe").VersionInfo.FileVersion
$vstV = (Get-Item "$art\VST3\Open Mastering Studio.vst3\Contents\x86_64-win\Open Mastering Studio.vst3").VersionInfo.FileVersion
$miV = [regex]::Match((Get-Content "$art\VST3\Open Mastering Studio.vst3\Contents\Resources\moduleinfo.json" -Raw), '"Version"\s*:\s*"([^"]+)"').Groups[1].Value   # (JSON5: Windows PowerShell's ConvertFrom-Json rejects it)
foreach ($v in @($exeV, $vstV, $miV)) { if (-not ($v -like "$ver*")) { throw "Version mismatch: exe $exeV, vst3 $vstV, moduleinfo $miV (want $ver)" } }
"versions: exe $exeV, vst3 $vstV, moduleinfo $miV"

# ---- Binary package ----
$stage = Join-Path $dist "Open Mastering Studio $ver"
$zip = Join-Path $dist "OpenMasteringStudio-$ver-win64.zip"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force "$stage\Standalone", "$stage\VST3" | Out-Null
Copy-Item "$art\Standalone\Open Mastering Studio.exe" "$stage\Standalone\"
Copy-Item -Recurse "$art\VST3\Open Mastering Studio.vst3" "$stage\VST3\"
Copy-Item "$root\Resources\desktop.ini", "$root\Resources\Plugin.ico" "$stage\VST3\Open Mastering Studio.vst3\"
Copy-Item "$root\Tools\Install VST3.bat", "$root\LICENSE.txt", "$root\THIRD-PARTY-NOTICES.txt", "$root\README.md" $stage
Copy-Item "$root\Reports\measurements.md" "$stage\MEASUREMENTS.md"
$install = (Get-Content "$root\Tools\INSTALL-template.txt" -Raw).Replace('{VERSION}', $ver) -replace "(?<!`r)`n", "`r`n"
Set-Content -Path "$stage\INSTALL.txt" -Value $install -NoNewline
attrib +s "$stage\VST3\Open Mastering Studio.vst3"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip
"binary zip: $zip $([math]::Round((Get-Item $zip).Length / 1MB, 2)) MB"

# ---- Source package (complete corresponding source) ----
$src = Join-Path $dist "OpenMasteringStudio-$ver-source"
$szip = Join-Path $dist "OpenMasteringStudio-$ver-source.zip"
if (Test-Path $src) { Remove-Item -Recurse -Force $src }
New-Item -ItemType Directory -Force $src | Out-Null
foreach ($f in 'CMakeLists.txt', 'README.md', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt', '.gitignore') { if (Test-Path "$root\$f") { Copy-Item "$root\$f" $src } }
foreach ($d in 'Source', 'Tests', 'Resources', 'Tools', 'Reports') { Copy-Item -Recurse "$root\$d" "$src\$d" }
New-Item -ItemType Directory -Force "$src\external" | Out-Null
Copy-Item -Recurse "$root\external\asiosdk" "$src\external\asiosdk"
# JUCE without its examples and docs (not needed to build; JUCE's own license files are kept)
New-Item -ItemType Directory -Force "$src\external\JUCE" | Out-Null
Get-ChildItem -Force "$root\external\JUCE" | Where-Object { $_.Name -notin @('examples', 'docs', '.github', '.gitlab-ci.yml', '.clang-tidy') } |
    ForEach-Object { Copy-Item -Recurse $_.FullName "$src\external\JUCE\$($_.Name)" }
if (Test-Path $szip) { Remove-Item -Force $szip }
Compress-Archive -Path $src -DestinationPath $szip
Remove-Item -Recurse -Force $src
"source zip: $szip $([math]::Round((Get-Item $szip).Length / 1MB, 2)) MB"

# ---- Verify the binary zip matches the build ----
Add-Type -AssemblyName System.IO.Compression.FileSystem
$tmp = Join-Path $env:TEMP "ms_zipcheck_$ver"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
[IO.Compression.ZipFile]::ExtractToDirectory($zip, $tmp)
$ok1 = (Get-FileHash "$art\Standalone\Open Mastering Studio.exe").Hash -eq (Get-FileHash "$tmp\Open Mastering Studio $ver\Standalone\Open Mastering Studio.exe").Hash
$ok2 = (Get-FileHash "$art\VST3\Open Mastering Studio.vst3\Contents\x86_64-win\Open Mastering Studio.vst3").Hash -eq (Get-FileHash "$tmp\Open Mastering Studio $ver\VST3\Open Mastering Studio.vst3\Contents\x86_64-win\Open Mastering Studio.vst3").Hash
$ok3 = Test-Path "$tmp\Open Mastering Studio $ver\LICENSE.txt"
"exe hash match: $ok1; vst3 hash match: $ok2; LICENSE.txt included: $ok3"
Remove-Item -Recurse -Force $tmp
