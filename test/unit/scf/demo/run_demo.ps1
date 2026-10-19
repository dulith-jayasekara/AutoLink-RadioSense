[CmdletBinding()]
param(
    [string]$Compiler = "C:\msys64\ucrt64\bin\g++.exe"
)

$ErrorActionPreference = "Stop"
$demoDirectory = $PSScriptRoot
$repositoryRoot = (Resolve-Path (Join-Path $demoDirectory "../../../..")).Path
$sourceRoot = Join-Path $repositoryRoot "src\Store Carry Forward"
$simulationRoot = Join-Path $demoDirectory ".."
$outputDirectory = Join-Path $demoDirectory "output"
$executable = Join-Path $env:TEMP "autolink_scf_demo.exe"

$env:MSYSTEM = "UCRT64"
$env:MSYS2_PATH_TYPE = "inherit"
$env:CHERE_INVOKING = "1"
$env:Path = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:Path

if (-not (Test-Path $Compiler))
{
    throw "Compiler not found: $Compiler"
}

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$sources = @(
    (Join-Path $sourceRoot "SCF_Message.cpp"),
    (Join-Path $sourceRoot "SCF_Buffer.cpp"),
    (Join-Path $sourceRoot "SCF_DuplicateTracker.cpp"),
    (Join-Path $sourceRoot "SCF_Reliability.cpp"),
    (Join-Path $sourceRoot "SCF_Codec.cpp"),
    (Join-Path $sourceRoot "SCF_Scheduler.cpp"),
    (Join-Path $sourceRoot "SCF_Radio_Adapter.cpp"),
    (Join-Path $simulationRoot "SCF_Simulation.cpp"),
    (Join-Path $demoDirectory "demo_runner.cpp")
)

& $Compiler -std=c++11 -Wall -Wextra -pedantic `
    -I $sourceRoot `
    @sources `
    -o $executable

if ($LASTEXITCODE -ne 0)
{
    throw "Demo compilation failed with exit code $LASTEXITCODE"
}

& $executable $outputDirectory
$runExit = $LASTEXITCODE
Remove-Item $executable -Force -ErrorAction SilentlyContinue

if ($runExit -ne 0)
{
    throw "Demo execution failed with exit code $runExit"
}

Write-Host "Demo completed successfully."
Write-Host "CSV output directory: $outputDirectory"
