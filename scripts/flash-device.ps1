param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet("outlet", "light", "dashboard", "epaper")]
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

function Get-DefaultTargetForDevice {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DeviceTypeToCheck
    )

    switch ($DeviceTypeToCheck) {
        "epaper" {
            return "esp32s3"
        }
        default {
            return Get-ConfigTarget -ConfigPath (Join-Path $projectDir "sdkconfig.defaults")
        }
    }
}

function Get-CachedBuildTarget {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CachePath
    )

    if (-not (Test-Path $CachePath)) {
        return $null
    }

    $match = Select-String -Path $CachePath -Pattern '^IDF_TARGET:STRING=(.+)$' | Select-Object -First 1
    if (-not $match) {
        return $null
    }

    return $match.Matches[0].Groups[1].Value
}

function Get-CachedToolchainFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CachePath
    )

    if (-not (Test-Path $CachePath)) {
        return $null
    }

    $match = Select-String -Path $CachePath -Pattern '^CMAKE_TOOLCHAIN_FILE:FILEPATH=(.+)$' | Select-Object -First 1
    if (-not $match) {
        return $null
    }

    return $match.Matches[0].Groups[1].Value
}

function Test-HasIdfAction {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$ArgsToCheck
    )

    $idfActions = @(
        "all",
        "app",
        "app-flash",
        "bootloader",
        "bootloader-flash",
        "build",
        "clean",
        "dfu",
        "docs",
        "efuse-common-table",
        "erase-flash",
        "encrypted-app-flash",
        "encrypted-flash",
        "encrypted-ota-data-initial",
        "flash",
        "fullclean",
        "menuconfig",
        "monitor",
        "partition-table",
        "partition-table-flash",
        "python-clean",
        "reconfigure",
        "set-target",
        "show-efuse-table",
        "size",
        "size-components",
        "size-files",
        "uf2",
        "uf2-app"
    )

    foreach ($arg in $ArgsToCheck) {
        if ($idfActions -contains $arg) {
            return $true
        }
    }

    return $false
}

if (-not $IdfArgs -or $IdfArgs.Count -eq 0) {
    $IdfArgs = @("reconfigure", "flash")
} elseif (-not (Test-HasIdfAction -ArgsToCheck $IdfArgs)) {
    $IdfArgs += @("reconfigure", "flash")
}

$idfTarget = Get-ConfigTarget -ConfigPath (Join-Path $projectDir "sdkconfig.defaults.local")
if (-not $idfTarget) {
    $idfTarget = Get-DefaultTargetForDevice -DeviceTypeToCheck $DeviceType
}
if (-not $idfTarget) {
    $idfTarget = Get-ConfigTarget -ConfigPath (Join-Path $projectDir "sdkconfig")
}

$buildTarget = Get-CachedBuildTarget -CachePath (Join-Path $projectDir "build\\CMakeCache.txt")
$bootloaderToolchain = Get-CachedToolchainFile -CachePath (Join-Path $projectDir "build\\bootloader\\CMakeCache.txt")
$expectedToolchainSuffix = if ($idfTarget) { "toolchain-$idfTarget.cmake" } else { $null }

if (($idfTarget -and $buildTarget -and $idfTarget -ne $buildTarget) -or
        ($expectedToolchainSuffix -and $bootloaderToolchain -and -not $bootloaderToolchain.EndsWith($expectedToolchainSuffix))) {
    Remove-Item -Recurse -Force (Join-Path $projectDir "build")
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
