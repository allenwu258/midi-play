#Requires -Version 5.1
[CmdletBinding()]
param(
    [string] $EnvironmentFile = '',
    [ValidatePattern('^[a-zA-Z0-9][a-zA-Z0-9_-]*$')]
    [string] $BuildName = 'windows-release',
    [ValidatePattern('^[a-zA-Z0-9][a-zA-Z0-9_-]*$')]
    [string] $PackageName = 'midi-play-windows-x64',
    [ValidateRange(1, 128)]
    [int] $Jobs = [Math]::Max(1, [Math]::Min(8, [Environment]::ProcessorCount)),
    [switch] $Traditional,
    [switch] $CheckEnvironment,
    [switch] $AudioSmoke,
    [string] $SoundFontPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $EnvironmentFile) { $EnvironmentFile = Join-Path $repo 'build.env.psd1' }

function Require-File([string] $Path, [string] $Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description not found: $Path. Check $EnvironmentFile."
    }
    return [IO.Path]::GetFullPath($Path)
}

function Invoke-Checked([string] $Program, [string[]] $Arguments) {
    & $Program @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$([IO.Path]::GetFileName($Program)) failed with exit code $LASTEXITCODE."
    }
}

function Assert-OutputPath([string] $Path, [string] $Root) {
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($Root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Output must be inside ${Root}: $full"
    }
    $ancestor = $full
    while ($ancestor -and $ancestor -ne [IO.Path]::GetPathRoot($ancestor)) {
        if (Test-Path -LiteralPath $ancestor) {
            if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Output paths cannot traverse junctions or symbolic links: $ancestor"
            }
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}

function Remove-OwnedPackage([string] $Path) {
    Assert-OutputPath $Path (Join-Path $repo 'dist')
    if (-not (Test-Path -LiteralPath (Join-Path $Path '.midi-play-package') -PathType Leaf)) {
        throw "Refusing to remove a directory not created by this build script: $Path"
    }
    $links = @(Get-ChildItem -LiteralPath $Path -Recurse -Force |
        Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
    if ($links.Count -gt 0) { throw "Refusing to remove a package containing symbolic links: $Path" }
    Remove-Item -LiteralPath $Path -Recurse -Force
}

$savedEnvironment = [Environment]::GetEnvironmentVariables('Process')
$buildLock = $null
$stage = $null
try {
    if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitProcess) {
        throw 'Run this script with 64-bit PowerShell on Windows.'
    }
    if (-not (Test-Path -LiteralPath $EnvironmentFile -PathType Leaf)) {
        throw "Build environment is not configured. Copy build.env.sample.psd1 to build.env.psd1, fill in your SDK paths, then rerun. Requested file: $EnvironmentFile"
    }
    $EnvironmentFile = (Resolve-Path -LiteralPath $EnvironmentFile).Path
    $settings = Import-PowerShellDataFile -LiteralPath $EnvironmentFile
    $allowedKeys = @('VisualStudioRoot', 'QtRoot', 'VcpkgRoot', 'VulkanSdk', 'CMakeExe', 'EnableVulkan')
    foreach ($key in $settings.Keys) {
        if ($key -notin $allowedKeys) { throw "Unknown environment setting: $key" }
    }
    foreach ($key in @('VisualStudioRoot', 'QtRoot', 'VcpkgRoot')) {
        if (-not ($settings[$key] -is [string]) -or [string]::IsNullOrWhiteSpace($settings[$key])) {
            throw "Configure $key in $EnvironmentFile."
        }
    }
    if (-not ($settings['EnableVulkan'] -is [bool])) { throw 'EnableVulkan must be $true or $false.' }
    foreach ($key in @('VulkanSdk', 'CMakeExe')) {
        if (-not $settings.ContainsKey($key)) { $settings[$key] = '' }
        if (-not ($settings[$key] -is [string])) { throw "$key must be a string." }
    }
    $enableVulkan = $settings.EnableVulkan -and -not $Traditional
    foreach ($key in @('VisualStudioRoot', 'QtRoot', 'VcpkgRoot', 'VulkanSdk', 'CMakeExe')) {
        if ($settings[$key]) {
            $pathRoot = [IO.Path]::GetPathRoot($settings[$key])
            if (-not $pathRoot -or $pathRoot -eq '\' -or $pathRoot.EndsWith(':')) {
                throw "$key must be an absolute path."
            }
            if ($settings[$key].Contains(';')) { throw "$key cannot contain a semicolon (PATH/CMake separator)." }
            $settings[$key] = [IO.Path]::GetFullPath($settings[$key])
        }
    }
    $vsRoot = $settings.VisualStudioRoot.TrimEnd('\', '/')
    $qtRoot = $settings.QtRoot.TrimEnd('\', '/')
    $vcpkgRoot = $settings.VcpkgRoot.TrimEnd('\', '/')
    $devShell = Require-File "$vsRoot\Common7\Tools\Launch-VsDevShell.ps1" 'VS 2022 developer shell'
    $qtPaths = Require-File "$qtRoot\bin\qtpaths.exe" 'Qt tools'
    $deployQt = Require-File "$qtRoot\bin\windeployqt.exe" 'Qt deployment tool'
    $null = Require-File "$qtRoot\lib\Qt6Core.lib" 'MSVC Qt library'
    $null = Require-File "$qtRoot\plugins\platforms\qwindows.dll" 'Qt Windows platform plugin'
    foreach ($module in @('Core', 'Gui', 'Widgets', 'Concurrent', 'Xml')) {
        $null = Require-File "$qtRoot\lib\cmake\Qt6$module\Qt6${module}Config.cmake" "Qt $module module"
    }
    $null = Require-File "$vcpkgRoot\scripts\buildsystems\vcpkg.cmake" 'vcpkg checkout'
    if (-not (Test-Path -LiteralPath "$vcpkgRoot\vcpkg.exe" -PathType Leaf)) {
        $null = Require-File "$vcpkgRoot\bootstrap-vcpkg.bat" 'vcpkg bootstrap script'
    }
    if ($AudioSmoke -and [string]::IsNullOrWhiteSpace($SoundFontPath)) {
        throw '-AudioSmoke requires -SoundFontPath pointing to a local SF2/SF3 file (not bundled).'
    }
    if ($SoundFontPath) { $SoundFontPath = Require-File $SoundFontPath 'External test SoundFont' }
    $cmake = $settings.CMakeExe
    if (-not $cmake) {
        $cmake = "$vsRoot\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    }
    $cmake = Require-File $cmake 'CMake (install the VS CMake component or set CMakeExe)'
    $ctest = Require-File (Join-Path (Split-Path $cmake) 'ctest.exe') 'CTest'
    $git = (Get-Command git.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    if ($enableVulkan) {
        if (-not $settings.VulkanSdk) { throw 'Configure VulkanSdk or use -Traditional.' }
        $null = Require-File "$($settings.VulkanSdk)\Include\vulkan\vulkan.h" 'Vulkan headers'
        $null = Require-File "$($settings.VulkanSdk)\Lib\vulkan-1.lib" 'Vulkan x64 import library'
        $glslang = Require-File "$($settings.VulkanSdk)\Bin\glslangValidator.exe" 'Vulkan shader compiler'
    }

    # Keep dependency discovery independent of the caller's SDK and CMake setup.
    foreach ($entry in @(Get-ChildItem Env:)) {
        if ($entry.Name -match '^(QT_|QML|Qt6|CMAKE_|VCPKG_OVERLAY|VCPKG_DEFAULT|VCPKG_INSTALLED|VCPKG_CHAINLOAD|VK_|VULKAN_SDK|VCPKG_ROOT)' -or
            $entry.Name -match '^(INCLUDE|LIB|LIBPATH|CL|_CL_|LINK|_LINK_|CC|CXX|CFLAGS|CXXFLAGS|LDFLAGS|VSINSTALLDIR|VCINSTALLDIR|VCTools.*|WindowsSDK.*|UniversalCRTSdkDir|UCRTVersion|VSCMD_.*|__VSCMD_.*)$') {
            [Environment]::SetEnvironmentVariable($entry.Name, $null, 'Process')
        }
    }
    $systemPath = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
    $env:PATH = "$systemPath;$(Split-Path $git)"
    & $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Host
    $compiler = (Get-Command cl.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    if (-not $compiler.StartsWith($vsRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "MSVC was resolved outside VisualStudioRoot: $compiler"
    }
    $dumpbin = (Get-Command dumpbin.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $null = Require-File "$env:WindowsSdkDir\Include\$env:WindowsSDKVersion\um\Windows.h" 'Windows SDK headers'
    $null = Require-File "$env:WindowsSdkDir\Lib\$env:WindowsSDKVersion\um\x64\kernel32.lib" 'Windows SDK x64 libraries'
    $null = Require-File "$env:WindowsSdkDir\Lib\$env:WindowsSDKVersion\ucrt\x64\ucrt.lib" 'Universal CRT x64 libraries'
    $env:PATH = "$qtRoot\bin;$(Split-Path $cmake);$env:PATH"
    $env:QT_ROOT = $qtRoot
    $env:VCPKG_ROOT = $vcpkgRoot
    $env:VCPKG_VISUAL_STUDIO_PATH = $vsRoot
    $env:VCPKG_MAX_CONCURRENCY = "$Jobs"
    $env:VCPKG_DISABLE_METRICS = '1'
    if ($enableVulkan) { $env:VULKAN_SDK = $settings.VulkanSdk }
    $cmakeVersionOutput = @(& $cmake --version)
    $cmakeVersion = $cmakeVersionOutput | Select-Object -First 1
    if ($LASTEXITCODE -ne 0 -or $cmakeVersion -notmatch 'cmake version (\d+\.\d+\.\d+)') {
        throw 'CMake could not report its version.'
    }
    if ([version]$Matches[1] -lt [version]'3.24.0') { throw 'CMake 3.24 or newer is required.' }
    $qtVersion = & $qtPaths --qt-version
    if ($LASTEXITCODE -ne 0 -or [version]$qtVersion -lt [version]'6.8.0' -or [version]$qtVersion -ge [version]'7.0.0') {
        throw "Qt 6.8 or newer within Qt 6 is required; found $qtVersion."
    }
    $qtConfig = Get-Content -LiteralPath "$qtRoot\mkspecs\qconfig.pri" -Raw
    if ($qtConfig -notmatch '(?m)^QT_ARCH\s*=\s*x86_64\s*$' -or $qtConfig -notmatch 'QT_MSVC_MAJOR_VERSION') {
        throw 'QtRoot must point to an x64 MSVC Qt kit.'
    }
    $qtCoreConfig = Get-Content -LiteralPath "$qtRoot\include\QtCore\qconfig.h" -Raw
    if ($qtCoreConfig -notmatch '(?m)^#define QT_FEATURE_shared 1\s*$') { throw 'A shared Qt kit is required.' }
    if ($enableVulkan) {
        $qtGuiConfig = Get-Content -LiteralPath "$qtRoot\include\QtGui\qtgui-config.h" -Raw
        if ($qtGuiConfig -notmatch '(?m)^#define QT_FEATURE_vulkan 1\s*$') {
            throw 'This Qt kit does not support Vulkan. Select another kit or use -Traditional.'
        }
        Invoke-Checked $glslang @('--version')
    }
    $crtDirectory = Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT'
    $null = Require-File "$crtDirectory\vcruntime140.dll" 'MSVC x64 redistributable DLLs'
    Write-Host "Environment OK: MSVC $($env:VCToolsVersion.TrimEnd('\')), Qt $qtVersion, $cmakeVersion; Vulkan=$enableVulkan"
    if ($CheckEnvironment) { return }

    $buildDirectory = Join-Path $repo "build\$BuildName"
    $installedDirectory = Join-Path $repo 'build\dependencies\vcpkg_installed'
    $packageDirectory = Join-Path $repo "dist\$PackageName"
    Assert-OutputPath $buildDirectory (Join-Path $repo 'build')
    Assert-OutputPath $installedDirectory (Join-Path $repo 'build')
    Assert-OutputPath $packageDirectory (Join-Path $repo 'dist')
    $null = New-Item -ItemType Directory -Path $buildDirectory -Force
    $lockPath = Join-Path $repo 'build\.windows-build.lock'
    try { $buildLock = [IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None') }
    catch { throw "Another Windows build is using this repository ($lockPath)." }

    $vcpkg = Join-Path $vcpkgRoot 'vcpkg.exe'
    if (-not (Test-Path -LiteralPath $vcpkg -PathType Leaf)) {
        $bootstrap = Require-File "$vcpkgRoot\bootstrap-vcpkg.bat" 'vcpkg bootstrap script'
        Invoke-Checked $bootstrap @('-disableMetrics')
    }
    Write-Host 'Installing manifest dependencies (x64-windows)...'
    Invoke-Checked $vcpkg @('install', '--triplet=x64-windows', '--host-triplet=x64-windows',
        "--x-manifest-root=$repo", "--x-install-root=$installedDirectory", '--disable-metrics')
    $fluidSynth = Require-File "$installedDirectory\x64-windows\bin\libfluidsynth-3.dll" 'Installed FluidSynth'

    $vulkanOption = if ($enableVulkan) { 'ON' } else { 'OFF' }
    $configure = @('--fresh', '-S', $repo, '-B', $buildDirectory, '-G', 'Visual Studio 17 2022', '-A', 'x64',
        '-T', "v143,host=x64,version=$($env:VCToolsVersion.TrimEnd('\'))",
        "-DCMAKE_SYSTEM_VERSION=$($env:WindowsSDKVersion.TrimEnd('\'))",
        "-DCMAKE_GENERATOR_INSTANCE=$vsRoot", "-DCMAKE_PREFIX_PATH=$qtRoot",
        "-DQt6_DIR=$qtRoot/lib/cmake/Qt6", '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF',
        '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF', '-DBUILD_TESTING=ON', '-DCMAKE_INSTALL_BINDIR=.',
        "-DFLUIDSYNTH_DLL=$fluidSynth", "-DMIDI_PLAY_ENABLE_VULKAN=$vulkanOption",
        "-DMIDI_PLAY_REQUIRE_VULKAN=$vulkanOption",
        "-DMIDI_PLAY_TEST_SOUNDFONT=$SoundFontPath")
    if ($enableVulkan) {
        $configure += @("-DVulkan_INCLUDE_DIR=$($settings.VulkanSdk)/Include",
            "-DVulkan_LIBRARY=$($settings.VulkanSdk)/Lib/vulkan-1.lib", "-DGLSLANG_VALIDATOR=$glslang")
    } else {
        $configure += @('-DCMAKE_DISABLE_FIND_PACKAGE_WrapVulkanHeaders=TRUE')
    }
    Invoke-Checked $cmake $configure
    Invoke-Checked $cmake @('--build', $buildDirectory, '--config', 'Release', '--parallel', "$Jobs")
    Invoke-Checked $ctest @('--test-dir', $buildDirectory, '-C', 'Release', '--output-on-failure', '--timeout', '60')

    $stage = Join-Path $repo "dist\.$PackageName-staging-$([Guid]::NewGuid().ToString('N'))"
    Assert-OutputPath $stage (Join-Path $repo 'dist')
    $null = New-Item -ItemType Directory -Path $stage
    Set-Content -LiteralPath "$stage\.midi-play-package" -Value 'midi-play-windows-package-v1' -Encoding ASCII
    Invoke-Checked $cmake @('--install', $buildDirectory, '--config', 'Release', '--prefix', $stage)
    Invoke-Checked $deployQt @('--release', '--no-compiler-runtime', '--no-translations', '--no-opengl-sw',
        '--qtpaths', $qtPaths, '--dir', $stage, "$stage\midi_play.exe", "$stage\midi_play_cli.exe")
    # windeployqt may only supply the VC installer. Deploy the actual CRT app-local.
    Get-ChildItem -LiteralPath $crtDirectory -Filter '*.dll' | Copy-Item -Destination $stage
    Copy-Item -LiteralPath "$repo\LICENSE" -Destination $stage
    $licenseDirectory = Join-Path $stage 'licenses\vcpkg'
    $null = New-Item -ItemType Directory -Path $licenseDirectory -Force
    Get-ChildItem -LiteralPath "$installedDirectory\x64-windows\share" -Directory | ForEach-Object {
        $copyright = Join-Path $_.FullName 'copyright'
        if (Test-Path -LiteralPath $copyright) {
            $destination = Join-Path $licenseDirectory $_.Name
            $null = New-Item -ItemType Directory -Path $destination
            Copy-Item -LiteralPath $copyright -Destination $destination
        }
    }
    if (Test-Path -LiteralPath "$qtRoot\sbom" -PathType Container) {
        Copy-Item -LiteralPath "$qtRoot\sbom" -Destination "$stage\licenses\qt-sbom" -Recurse
    }

    Write-Host 'Checking the package with SDK directories removed from PATH...'
    $developmentPath = $env:PATH
    $env:PATH = $systemPath
    $env:QT_PLUGIN_PATH = $stage
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = "$stage\platforms"
    try {
        Invoke-Checked $cmake @("-DPACKAGE_DIR=$stage", "-DDUMPBIN_EXE=$dumpbin", '-P', "$repo\cmake\ValidateWindowsPackage.cmake")
        Push-Location $stage
        try {
            Invoke-Checked "$stage\midi_play_cli.exe" @('--help')
            Invoke-Checked "$stage\midi_play_cli.exe" @('--render-test', "$repo\tests\fixtures\deployment.musicxml", "$stage\.smoke.png")
            $null = Require-File "$stage\.smoke.png" 'Packaged Qt render smoke output'
            Remove-Item -LiteralPath "$stage\.smoke.png"
            if ($AudioSmoke) {
                Invoke-Checked "$stage\midi_play_cli.exe" @('--audio-test', $SoundFontPath)
            }
        } finally { Pop-Location }
    } finally { $env:PATH = $developmentPath }

    $revision = & $git -C $repo rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Unable to read source revision.' }
    $dirty = @(& $git -C $repo status --porcelain).Count -gt 0
    if ($LASTEXITCODE -ne 0) { throw 'Unable to read source status.' }
    $manifest = Get-Content -LiteralPath "$repo\vcpkg.json" -Raw | ConvertFrom-Json
    [ordered]@{
        sourceRevision = $revision
        sourceHasLocalChanges = $dirty
        builtAtUtc = [DateTime]::UtcNow.ToString('o')
        configuration = 'Release'
        architecture = 'x64'
        qtVersion = $qtVersion
        msvcVersion = $env:VCToolsVersion.TrimEnd('\')
        vulkanEnabled = [bool]$enableVulkan
        vcpkgBaseline = $manifest.'builtin-baseline'
        audioSmokePassed = [bool]$AudioSmoke
    } | ConvertTo-Json | Set-Content -LiteralPath "$stage\build-info.json" -Encoding UTF8

    # Preserve the last successful package until every deployment check has passed.
    $previous = $null
    if (Test-Path -LiteralPath $packageDirectory) {
        if (-not (Test-Path -LiteralPath "$packageDirectory\.midi-play-package" -PathType Leaf)) {
            throw "Existing package directory is not owned by this script; choose another -PackageName: $packageDirectory"
        }
        $previous = Join-Path $repo "dist\.$PackageName-previous-$([Guid]::NewGuid().ToString('N'))"
        Assert-OutputPath $previous (Join-Path $repo 'dist')
        Move-Item -LiteralPath $packageDirectory -Destination $previous
    }
    try { Move-Item -LiteralPath $stage -Destination $packageDirectory }
    catch {
        if ($previous) { Move-Item -LiteralPath $previous -Destination $packageDirectory }
        throw
    }
    $stage = $null
    if ($previous) {
        try { Remove-OwnedPackage $previous }
        catch { Write-Warning "New package is ready; the previous directory was retained at ${previous}: $($_.Exception.Message)" }
    }
    Write-Host "Release package ready: $packageDirectory"
} finally {
    if ($stage -and (Test-Path -LiteralPath $stage)) {
        Write-Warning "Build did not publish a release; diagnostic staging directory retained: $stage"
    }
    if ($buildLock) { $buildLock.Dispose() }
    foreach ($entry in @(Get-ChildItem Env:)) {
        if (-not $savedEnvironment.Contains($entry.Name)) {
            [Environment]::SetEnvironmentVariable($entry.Name, $null, 'Process')
        }
    }
    foreach ($key in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($key, $savedEnvironment[$key], 'Process')
    }
}
