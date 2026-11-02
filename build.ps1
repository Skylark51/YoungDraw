param([string]$BuildDirectory = 'build/release-v04-final')
$ErrorActionPreference = 'Stop'
if ($BuildDirectory -notmatch '^[A-Za-z0-9_/-]+$' -or $BuildDirectory.Contains('..')) { throw 'Use a relative build folder inside the project.' }
$env:VSLANG = '1033'
$projectDir = $PSScriptRoot
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsDir = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsDir) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
$devShell = Join-Path $vsDir 'Common7/Tools/Launch-VsDevShell.ps1'
& $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:VSLANG = '1033'
$ninjaPath = Join-Path $vsDir 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
# MSVC/CMake subprocesses can fail on Korean paths. A temporary drive alias
# gives all build tools an ASCII path without copying or moving project files.
$driveLetter = @('Z','Y','X','W','V') | Where-Object { !(Test-Path "${_}:/") } | Select-Object -First 1
if (!$driveLetter) { throw 'No free drive letter for the temporary build alias.' }
subst "${driveLetter}:" $projectDir
if ($LASTEXITCODE) { throw 'Cannot create temporary build alias' }
try {
    $buildRoot = "${driveLetter}:/"
    $outputDir = "${buildRoot}$BuildDirectory"
    cmake --fresh -S $buildRoot -B $outputDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninjaPath" -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE) { throw 'CMake configure failed' }
    cmake --build $outputDir
    if ($LASTEXITCODE) { throw 'Build failed' }
    ctest --test-dir $outputDir --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed' }
} finally {
    subst "${driveLetter}:" /D
}
$taskBuiltExe = Join-Path $projectDir "$BuildDirectory/YoungDraw.exe"
$taskRootExe = Join-Path $projectDir 'YoungDraw.exe'
try {
    Copy-Item -LiteralPath $taskBuiltExe -Destination $taskRootExe -Force -ErrorAction Stop
} catch {
    # Windows permits a running executable to be renamed. Preserve its process
    # and open document while publishing the replacement at the usual path.
    $taskPreviousExe = [IO.Path]::GetFullPath((Join-Path $projectDir ("build/YoungDraw-previous-" + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.exe')))
    $taskBackupRoot = [IO.Path]::GetFullPath((Join-Path $projectDir 'build')) + [IO.Path]::DirectorySeparatorChar
    if (!$taskPreviousExe.StartsWith($taskBackupRoot, [StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $taskRootExe -PathType Leaf)) { throw }
    Move-Item -LiteralPath $taskRootExe -Destination $taskPreviousExe -ErrorAction Stop
    Copy-Item -LiteralPath $taskBuiltExe -Destination $taskRootExe -ErrorAction Stop
}
Write-Host "Run: $projectDir\YoungDraw.exe"
