# Adds Visual Studio's bundled CMake bin directory to the *user* PATH if missing.
# Run from any PowerShell:  .\scripts\Ensure-CMakeOnPath.ps1
# Then open a new terminal (or log off/on) so `cmake` resolves everywhere.

$ErrorActionPreference = 'Stop'

$candidates = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\BuildTools",
    "${env:ProgramFiles}\Microsoft Visual Studio\18\BuildTools",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools"
)

$cmakeBin = $null
foreach ($root in $candidates) {
    $exe = Join-Path $root "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path -LiteralPath $exe) {
        $cmakeBin = Split-Path -LiteralPath $exe
        break
    }
}

if (-not $cmakeBin) {
    Write-Error "Could not find cmake.exe under Visual Studio. Install workload 'Desktop development with C++' and 'CMake tools for Windows', or install CMake from https://cmake.org/download/"
    exit 1
}

$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if ($null -eq $userPath) { $userPath = '' }
$segments = $userPath.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries)
if ($segments -contains $cmakeBin) {
    Write-Host "CMake bin already on user PATH: $cmakeBin"
    exit 0
}

$trimmed = $userPath.TrimEnd(';').Trim()
$newPath = if ($trimmed.Length -gt 0) { "$trimmed;$cmakeBin" } else { $cmakeBin }
[Environment]::SetEnvironmentVariable('Path', $newPath, 'User')
Write-Host "Added to user PATH: $cmakeBin"
Write-Host "Open a new PowerShell window, then run: cmake --version"
