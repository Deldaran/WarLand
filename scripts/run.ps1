param(
  [string]$BuildType = "Debug"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ws = Split-Path -Parent $root

$exeSingle = Join-Path $ws "build/Warland.exe"
$exeMulti  = Join-Path $ws "build/$BuildType/Warland.exe"

$exe = $null
if (Test-Path $exeSingle) { $exe = $exeSingle }
elseif (Test-Path $exeMulti) { $exe = $exeMulti }
else { throw "Executable not found. Build the project first (looked for $exeSingle and $exeMulti)." }

& $exe
