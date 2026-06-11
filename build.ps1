# Script de build WarLand (Windows / MinGW + Ninja)
# Usage:  .\build.ps1            -> configure (si besoin) + compile
#         .\build.ps1 -Run       -> compile puis lance le jeu
#         .\build.ps1 -Clean     -> supprime le dossier build

param(
    [switch]$Run,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$buildDir = Join-Path $PSScriptRoot "build"

if ($Clean) {
    if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir }
    Write-Host "build/ supprime." -ForegroundColor Yellow
    return
}

# GLAD est genere par un script Python qui a besoin du module jinja2.
# On cherche un python qui possede jinja2 pour le passer a CMake, sinon la
# generation de GLAD echoue (le python du Windows Store n'a pas jinja2).
function Find-PythonWithJinja2 {
    foreach ($cmd in @("py -3.10", "py -3", "python", "python3")) {
        $parts = $cmd.Split(" ")
        $exe = $parts[0]
        $args = $parts[1..($parts.Length - 1)]
        try {
            $path = & $exe @args -c "import jinja2, sys; print(sys.executable)" 2>$null
            if ($LASTEXITCODE -eq 0 -and $path) { return $path.Trim() }
        } catch {}
    }
    return $null
}

# Configuration (genere build/ si absent)
if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
    Write-Host "== Configuration CMake ==" -ForegroundColor Cyan
    $pyArgs = @()
    $py = Find-PythonWithJinja2
    if ($py) {
        Write-Host "Python (GLAD) : $py" -ForegroundColor DarkGray
        $pyArgs = @("-DPython_EXECUTABLE=$py")
    } else {
        Write-Host "Attention: aucun python avec jinja2 trouve (pip install jinja2)" -ForegroundColor Yellow
    }
    cmake -S $PSScriptRoot -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release @pyArgs
    if ($LASTEXITCODE -ne 0) { throw "Echec de la configuration CMake" }
}

# Compilation
Write-Host "== Compilation ==" -ForegroundColor Cyan
cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "Echec de la compilation" }

Write-Host "OK -> $buildDir\bin\WarLand.exe" -ForegroundColor Green

if ($Run) {
    & (Join-Path $buildDir "bin\WarLand.exe")
}
