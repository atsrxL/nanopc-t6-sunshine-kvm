# SPDX-License-Identifier: GPL-3.0-or-later
# Run inside VS2022 x64 environment (or the build-node Invoke-InBuildEnvironment wrapper).
# Does not install tools, fetch sources, pair, access devices, or provision a VM.
param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [Parameter(Mandatory=$true)][string]$BuildRoot
)
$ErrorActionPreference='Stop'
$client=Split-Path $PSScriptRoot -Parent
$project=Split-Path $client -Parent
$vendor=Join-Path $project 'vendor\moonlight-qt'
if(!(Test-Path "$QtRoot\bin\qmake.exe")){throw 'Qt MSVC x64 qmake required'}
if(Test-Path $BuildRoot){throw 'Use a NEW shadow build root; do not overwrite prior evidence'}
New-Item -ItemType Directory -Path $BuildRoot | Out-Null
$log=Join-Path $BuildRoot 'logs'
New-Item -ItemType Directory -Path $log | Out-Null
$rev=(& git -C $project rev-parse HEAD)
if($LASTEXITCODE -ne 0){throw 'Build requires committed project checkout'}
$dirty=& git -C $project status --porcelain -- client
if($dirty){throw 'Refusing dirty client sources'}
& python "$client\tools\apply_moonlight.py" $vendor --patch-output "$BuildRoot\overlay.patch" *> "$log\overlay.log"
if($LASTEXITCODE -ne 0){throw 'Pinned overlay verification failed'}
& python "$client\tools\test_client.py" *> "$log\source-tests.log"
if($LASTEXITCODE -ne 0){throw 'Sources must be fetched and the pinned overlay applied before build'}
Push-Location $BuildRoot
try {
    & "$QtRoot\bin\qmake.exe" "$client\rkmoon-client.pro" 'CONFIG+=release' *> "$log\qmake.log"
    if($LASTEXITCODE -ne 0){throw 'qmake failed; inspect log'}
    & nmake /NOLOGO *> "$log\build.log"
    if($LASTEXITCODE -ne 0){throw 'nmake failed; inspect log'}
    $binary=Join-Path $BuildRoot 'app\release\rkmoon-client.exe'
    if(!(Test-Path $binary)){throw 'No binary after nmake'}
    $inputs=@{}
    Get-ChildItem "$client\app" -Recurse -File | ForEach-Object {
        $inputs[$_.FullName.Substring($project.Length+1).Replace('\','/')]=(Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()
    }
    $record=@{schema=1;project_commit=$rev;qt=(& "$QtRoot\bin\qmake.exe" -query QT_VERSION);configuration='Windows x64 MSVC2022 release';overlay_sha256=(Get-FileHash "$BuildRoot\overlay.patch" -Algorithm SHA256).Hash.ToLower();binary_sha256=(Get-FileHash $binary -Algorithm SHA256).Hash.ToLower();binary_size=(Get-Item $binary).Length;inputs=$inputs;live_stream_tested=$false}
    $record | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 "$BuildRoot\build-manifest.json"
    Write-Output "Built revision $rev; manifest: $BuildRoot\build-manifest.json"
}finally{Pop-Location}
