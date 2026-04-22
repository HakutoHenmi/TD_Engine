param (
    [ValidateSet("Debug", "Development", "Release")]
    [string]$Configuration = "Debug",
    [switch]$BuildOnly
)

$ErrorActionPreference = "Stop"

# Get absolute paths
$ScriptDir = $PSScriptRoot
$ProjectRoot = Split-Path -Parent $ScriptDir
$LogDir = Join-Path $ProjectRoot "logs"
$BuildLogDir = Join-Path $LogDir "build"
$AppLogDir = Join-Path $LogDir "app"

# Ensure directories exist
if (-not (Test-Path $BuildLogDir)) {
    New-Item -ItemType Directory -Path $BuildLogDir -Force | Out-Null
}
if (-not (Test-Path $AppLogDir)) {
    New-Item -ItemType Directory -Path $AppLogDir -Force | Out-Null
}

# Find MSBuild using vswhere
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found. Is Visual Studio installed?"
    exit 1
}

$vsPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
if (-not $vsPath -or -not (Test-Path $vsPath)) {
    Write-Error "MSBuild.exe not found."
    exit 1
}

# Prepare build log file
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$buildLogFile = Join-Path $BuildLogDir "build_$timestamp.log"

Write-Host "Building project in $Configuration mode..." -ForegroundColor Cyan
Write-Host "Build log will be saved to: $buildLogFile" -ForegroundColor DarkGray

# Build
$slnPath = Join-Path $ProjectRoot "DirectXGame_New.sln"
$buildArgs = @(
    $slnPath,
    "/t:Build",
    "/p:Configuration=$Configuration",
    "/v:m",
    "/fl",
    "/flp:logfile=""$buildLogFile"";Verbosity=normal"
)

$process = Start-Process -FilePath $vsPath -ArgumentList $buildArgs -NoNewWindow -Wait -PassThru

if ($process.ExitCode -ne 0) {
    Write-Host "Build failed! Check $buildLogFile for details." -ForegroundColor Red
    exit $process.ExitCode
}

Write-Host "Build succeeded!" -ForegroundColor Green

if ($BuildOnly) {
    Write-Host "BuildOnly switch provided, skipping execution." -ForegroundColor Yellow
    exit 0
}

# Find executable
$ParentDir = Split-Path -Parent $ProjectRoot
$exePath = Join-Path $ParentDir "Generated\Outputs\$Configuration\DirectXGameApp.exe"
if (-not (Test-Path $exePath)) {
    Write-Error "Executable not found at expected path: $exePath"
    exit 1
}

Write-Host "Running application..." -ForegroundColor Cyan

# Run application
# Set working directory to project root as the C++ code expects to transition there anyway
Set-Location $ProjectRoot
Start-Process -FilePath $exePath -NoNewWindow -Wait
