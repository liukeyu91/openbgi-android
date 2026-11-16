# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only
# Read a Windows-user-protected credential into this build's environment only.
param(
    [Parameter(Mandatory=$true)][string]$WorkingDirectory,
    [Parameter(Mandatory=$true)][string]$PackageScript,
    [ValidateSet('main','ui-preview')][string]$Variant='main',
    [string]$SigningDirectory=(Join-Path $env:USERPROFILE '.openbgi\signing')
)
$ErrorActionPreference='Stop'
$names=@('OPENBGI_KEYSTORE','OPENBGI_KEY_ALIAS','OPENBGI_STORE_PASS','WSLENV')
$old=@{}
foreach($name in $names) {$old[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try {
    $filename=if($Variant -eq 'main') {'android.p12'} else {'android-ui-preview.p12'}
    $key=Join-Path $SigningDirectory $filename
    if(-not(Test-Path -LiteralPath $key)) {throw 'Persistent signing key is missing; refusing to create a replacement'}
    $credential=Import-Clixml -LiteralPath (Join-Path $SigningDirectory 'release.credential.xml')
    $translated=& wsl.exe -e wslpath -a $key
    if($LASTEXITCODE -ne 0 -or -not $translated) {throw 'Cannot translate the signing-key path for WSL'}
    $env:OPENBGI_KEYSTORE=$translated.Trim()
    $env:OPENBGI_KEY_ALIAS=$credential.UserName
    $env:OPENBGI_STORE_PASS=$credential.GetNetworkCredential().Password
    $env:WSLENV=(@($old['WSLENV'],'OPENBGI_KEYSTORE/u','OPENBGI_KEY_ALIAS/u','OPENBGI_STORE_PASS/u') | Where-Object {$_}) -join ':'
    Push-Location -LiteralPath $WorkingDirectory
    try {
        & wsl.exe -e sh $PackageScript
        if($LASTEXITCODE -ne 0) {throw "Android packaging failed with exit code $LASTEXITCODE"}
    } finally {Pop-Location}
} finally {
    foreach($name in $names) {[Environment]::SetEnvironmentVariable($name,$old[$name],'Process')}
    $credential=$null
}
