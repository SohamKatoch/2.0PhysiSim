# Run physisim from the CMake build output (not on PATH by default).
# From repo root:
#   .\scripts\Run-PhysiSim.ps1 --ipc-port 17500
#   .\scripts\Run-PhysiSim.ps1 -Client --port 17500

param(
    [switch]$Client,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Passthrough
)

$root = Split-Path $PSScriptRoot -Parent
if (-not (Test-Path (Join-Path $root "CMakeLists.txt"))) {
    Write-Error "Could not find repo root (CMakeLists.txt). Run script from the PhysiSim clone."
    exit 1
}

$candidates = if ($Client) {
    @(
        Join-Path $root "build\Release\physisim_client.exe"
        Join-Path $root "build\Debug\physisim_client.exe"
        Join-Path $root "build\physisim_client.exe"
    )
} else {
    @(
        Join-Path $root "build\Release\physisim.exe"
        Join-Path $root "build\Debug\physisim.exe"
        Join-Path $root "build\physisim.exe"
    )
}

$exe = $null
foreach ($c in $candidates) {
    if (Test-Path $c) { $exe = $c; break }
}

if (-not $exe) {
    $name = if ($Client) { "physisim_client.exe" } else { "physisim.exe" }
    Write-Error @"
$name not found under build\. Build from repo root first:

  cd `"$root`"
  cmake -B build -G `"Visual Studio 17 2022`" -A x64
  cmake --build build --config Release

Or open the .sln in Visual Studio and build the physisim target.
"@
    exit 1
}

Write-Host "Running: $exe" -ForegroundColor Cyan
Push-Location $root
try {
    & $exe @Passthrough
} finally {
    Pop-Location
}
