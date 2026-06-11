param(
    [switch]$Rebuild
)

$ErrorActionPreference = "Stop"
$exe = Join-Path $PSScriptRoot "build\bin\WarLand.exe"

if ($Rebuild -or -not (Test-Path $exe)) {
    & (Join-Path $PSScriptRoot "build.ps1")
}

Write-Host "Lancement de WarLand..." -ForegroundColor Green
& $exe
