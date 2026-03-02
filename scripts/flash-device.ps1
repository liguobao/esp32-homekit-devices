param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet("outlet", "light")]
    [string]$DeviceType,

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$IdfArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = $PSScriptRoot
$projectDir = Split-Path -Parent $scriptDir
$idfRun = Join-Path $scriptDir "idf-run.ps1"
if (-not (Test-Path $idfRun)) {
    throw "Shared PowerShell entry point not found: $idfRun"
}

if (-not $IdfArgs -or $IdfArgs.Count -eq 0) {
    $IdfArgs = @("reconfigure", "flash")
}

$sdkconfigFiles = @(
    (Join-Path $projectDir "sdkconfig"),
    (Join-Path $projectDir "sdkconfig.old")
)
foreach ($sdkconfigFile in $sdkconfigFiles) {
    if (Test-Path $sdkconfigFile) {
        Remove-Item -Force $sdkconfigFile
    }
}

& $idfRun "-DHOMEKIT_DEVICE_TYPE=$DeviceType" @IdfArgs
