param(
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$IdfArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Quote-CmdArg {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    if ($Value -match '[\s"]') {
        return '"' + ($Value -replace '"', '""') + '"'
    }

    return $Value
}

$scriptDir = $PSScriptRoot
$projectDir = Split-Path -Parent $scriptDir

if (-not $env:IDF_PATH) {
    $env:IDF_PATH = Join-Path $env:USERPROFILE ".espressif\frameworks\esp-idf-v5.4.2"
}

$exportBat = Join-Path $env:IDF_PATH "export.bat"
if (-not (Test-Path $exportBat)) {
    throw "ESP-IDF not found: $exportBat"
}

$cmdParts = @(
    (Quote-CmdArg -Value $exportBat),
    "&&",
    "idf.py"
) + ($IdfArgs | ForEach-Object { Quote-CmdArg -Value $_ })

$commandLine = [string]::Join(" ", $cmdParts)

Push-Location $projectDir
try {
    & cmd.exe /c $commandLine
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}
