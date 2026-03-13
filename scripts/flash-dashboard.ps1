param(
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$IdfArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = $PSScriptRoot
$flashDevice = Join-Path $scriptDir "flash-device.ps1"
if (-not (Test-Path $flashDevice)) {
    throw "Shared PowerShell flash entry point not found: $flashDevice"
}

$hadMSYSTEM = Test-Path Env:MSYSTEM
if ($hadMSYSTEM) {
    $originalMSYSTEM = $env:MSYSTEM
    Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
}

try {
    & $flashDevice dashboard @IdfArgs
} finally {
    if ($hadMSYSTEM) {
        $env:MSYSTEM = $originalMSYSTEM
    }
}
