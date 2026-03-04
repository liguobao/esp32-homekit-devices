param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet("outlet", "light", "dashboard")]
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

function Get-ConfigTarget {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ConfigPath
    )

    if (-not (Test-Path $ConfigPath)) {
        return $null
    }

    $match = Select-String -Path $ConfigPath -Pattern '^CONFIG_IDF_TARGET="([^"]+)"$' | Select-Object -First 1
    if (-not $match) {
        return $null
    }

    return $match.Matches[0].Groups[1].Value
}

if (-not $IdfArgs -or $IdfArgs.Count -eq 0) {
    $IdfArgs = @("reconfigure", "flash")
}

$idfTarget = Get-ConfigTarget -ConfigPath (Join-Path $projectDir "sdkconfig")
if (-not $idfTarget) {
    $idfTarget = Get-ConfigTarget -ConfigPath (Join-Path $projectDir "sdkconfig.defaults")
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

$forwardArgs = @()
if ($idfTarget) {
    $forwardArgs += "-DIDF_TARGET=$idfTarget"
}
$forwardArgs += "-DHOMEKIT_DEVICE_TYPE=$DeviceType"
$forwardArgs += $IdfArgs

& $idfRun @forwardArgs
