# SPDX-License-Identifier: GPL-3.0-or-later
# Internal evaluation packaging only. NOT a complete public-release license/source bundle.
param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$client=Split-Path $PSScriptRoot -Parent
$project=Split-Path $client -Parent
if(Test-Path $OutputDirectory){throw 'Refusing existing package directory'}
$record=Get-Content "$BuildRoot\build-manifest.json" -Raw | ConvertFrom-Json
$exe="$BuildRoot\app\release\rkmoon-client.exe"
if((Get-FileHash $exe -Algorithm SHA256).Hash.ToLower() -ne $record.binary_sha256){throw 'Binary differs from build manifest'}
$dir=Join-Path $OutputDirectory 'RKMoon'
New-Item -ItemType Directory -Path $dir -Force | Out-Null
Copy-Item $exe $dir
& "$QtRoot\bin\windeployqt.exe" --release --no-plugins --no-translations --no-quick-import --no-ffmpeg --no-opengl-sw --no-compiler-runtime --dir $dir "$dir\rkmoon-client.exe" *> "$OutputDirectory\deployment.log"
if($LASTEXITCODE -ne 0){throw 'Qt deployment failed'}
foreach($item in @('platforms\qwindows.dll','tls\qschannelbackend.dll')){
    $target=Join-Path $dir $item
    New-Item -ItemType Directory -Force (Split-Path $target -Parent) | Out-Null
    Copy-Item "$QtRoot\plugins\$item" $target
}
$libs="$project\vendor\moonlight-qt\libs\windows\lib\x64"
foreach($dll in @('SDL2.dll','SDL2_ttf.dll','avcodec-61.dll','avutil-59.dll','dav1d.dll','swscale-8.dll','opus.dll','libcrypto-3-x64.dll','libssl-3-x64.dll')){Copy-Item "$libs\$dll" $dir}
$crt=Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT'
if(!(Test-Path "$crt\vcruntime140.dll")){throw 'VS2022 CRT redistributable required; run in its build environment'}
Copy-Item "$crt\*.dll" $dir
Copy-Item "$client\COPYING" "$dir\COPYING.txt"
Copy-Item "$client\licenses" "$dir\licenses" -Recurse
Copy-Item "$client\docs\LICENSES.md" "$dir\LICENSES.md"
Copy-Item "$client\README.md" "$dir\README.md"
Copy-Item "$client\sources.lock.json" "$dir\sources.lock.json"
Copy-Item "$BuildRoot\build-manifest.json" "$dir\build-manifest.json"
Copy-Item "$BuildRoot\overlay.patch" "$dir\overlay.patch"
$files=@{}
Get-ChildItem $dir -Recurse -File | ForEach-Object { $files[$_.FullName.Substring($dir.Length+1).Replace('\','/')]=@{size=$_.Length;sha256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()} }
$files | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 "$dir\package-files.json"
$tag=$record.project_commit.Substring(0,12)
$zip=Join-Path $OutputDirectory "RKMoon-Windows-x64-reviewed-$tag.zip"
Compress-Archive -Path "$dir\*" -DestinationPath $zip -CompressionLevel Optimal
$hash=(Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $(Split-Path $zip -Leaf)" | Set-Content -Encoding ascii "$zip.sha256"
Write-Output "Internal package: $zip size=$((Get-Item $zip).Length) SHA256=$hash"
