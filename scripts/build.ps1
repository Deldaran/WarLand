param(
  [switch]$Configure,
  [string]$BuildType = "Debug"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ws = Split-Path -Parent $root
$buildDir = Join-Path $ws "build"

if (!(Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }

if ($Configure) {
  cmake -S $ws -B $buildDir -DCMAKE_BUILD_TYPE=$BuildType
}

cmake --build $buildDir --config $BuildType --parallel
