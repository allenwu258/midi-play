#Requires -Version 5.1
# Windows 发布入口：校验 SDK、隔离工具环境，依次配置、构建、测试和暂存打包。
[CmdletBinding()]
param(
    # 本地 SDK 配置文件；留空时读取仓库根目录 build.env.psd1。
    [string] $EnvironmentFile = '',
    # 构建子目录标识，只允许安全的字母、数字、下划线和连字符。
    [ValidatePattern('^[a-zA-Z0-9][a-zA-Z0-9_-]*$')]
    [string] $BuildName = 'windows-release',
    # dist 下的最终包目录名；仅允许清理本脚本标记拥有的旧包。
    [ValidatePattern('^[a-zA-Z0-9][a-zA-Z0-9_-]*$')]
    [string] $PackageName = 'midi-play-windows-x64',
    # 编译并行数，默认按逻辑处理器数限制在 1..8。
    [ValidateRange(1, 128)]
    [int] $Jobs = [Math]::Max(1, [Math]::Min(8, [Environment]::ProcessorCount)),
    # 强制传统渲染构建，覆盖配置文件的 Vulkan 开关。
    [switch] $Traditional,
    # 仅验证工具和依赖环境，跳过后续配置、构建及打包。
    [switch] $CheckEnvironment,
    # 额外运行需要真实音源的音频冒烟测试。
    [switch] $AudioSmoke,
    # 外部 SF2/SF3 测试音源路径；启用 AudioSmoke 时必须显式提供。
    [string] $SoundFontPath = '',
    # 发布门禁：所有视频背景集成路径必须运行；缺设备/工具导致跳过视为失败。
    [switch] $RequireVideoIntegration,
    # 外部 FFmpeg 仅用于测试和导出，不复制到发布包。
    [string] $FfmpegTestPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# 统一从仓库根目录解析输入和输出，避免脚本从其他当前目录运行时污染环境。
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $EnvironmentFile) { $EnvironmentFile = Join-Path $repo 'build.env.psd1' }

function Require-File([string] $Path, [string] $Description) {
    # 所有外部工具和 SDK 入口先做文件级校验，并返回规范绝对路径。
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description not found: $Path. Check $EnvironmentFile."
    }
    return [IO.Path]::GetFullPath($Path)
}

function Invoke-Checked([string] $Program, [string[]] $Arguments) {
    # 统一转发命令输出；非零退出码立即中止后续构建或打包步骤。
    & $Program @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$([IO.Path]::GetFileName($Program)) failed with exit code $LASTEXITCODE."
    }
}

function Assert-OutputPath([string] $Path, [string] $Root) {
    # 只允许写入仓库受控子树，并拒绝穿越链接或 junction。
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
    # 仅删除带所有权标记且不含链接的本脚本产物，保护用户已有目录。
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
    # 先验证宿主、环境文件和 SDK，再清理进程环境，确保依赖发现不受调用者污染。
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
    # Traditional 模式强制关闭 Vulkan；否则沿用环境文件中的能力开关。
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
    foreach ($module in @('Core', 'Gui', 'Widgets', 'Concurrent', 'Xml', 'Multimedia')) {
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
    if ($RequireVideoIntegration -and -not $enableVulkan) {
        throw '-RequireVideoIntegration requires a Vulkan build.'
    }
    if ($FfmpegTestPath) { $FfmpegTestPath = Require-File $FfmpegTestPath 'FFmpeg integration executable' }
    if ($RequireVideoIntegration -and -not $FfmpegTestPath) {
        throw '-RequireVideoIntegration requires -FfmpegTestPath.'
    }
    if ($enableVulkan) {
        if (-not $settings.VulkanSdk) { throw 'Configure VulkanSdk or use -Traditional.' }
        $null = Require-File "$($settings.VulkanSdk)\Include\vulkan\vulkan.h" 'Vulkan headers'
        $null = Require-File "$($settings.VulkanSdk)\Lib\vulkan-1.lib" 'Vulkan x64 import library'
        $glslang = Require-File "$($settings.VulkanSdk)\Bin\glslangValidator.exe" 'Vulkan shader compiler'
    }

    # 清理调用者注入的 SDK/CMake/Vulkan 变量，使后续探测只使用本次配置。
    foreach ($entry in @(Get-ChildItem Env:)) {
        if ($entry.Name -match '^(QT_|QML|Qt6|CMAKE_|VCPKG_OVERLAY|VCPKG_DEFAULT|VCPKG_INSTALLED|VCPKG_CHAINLOAD|VK_|VULKAN_SDK|VCPKG_ROOT)' -or
            $entry.Name -match '^(INCLUDE|LIB|LIBPATH|CL|_CL_|LINK|_LINK_|CC|CXX|CFLAGS|CXXFLAGS|LDFLAGS|VSINSTALLDIR|VCINSTALLDIR|VCTools.*|WindowsSDK.*|UniversalCRTSdkDir|UCRTVersion|VSCMD_.*|__VSCMD_.*)$') {
            [Environment]::SetEnvironmentVariable($entry.Name, $null, 'Process')
        }
    }
    # 先用最小系统 PATH 启动 VS 开发环境，防止错误版本的 cl.exe 被提前命中。
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
    # CheckEnvironment 在完成全部工具和版本校验后提前返回，不创建构建或发布目录。
    if ($CheckEnvironment) { return }

    # 构建、依赖安装和最终包分别位于 build/dist 受控目录，并用锁阻止并发发布。
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
    # 以仓库 manifest 和隔离安装根安装固定 triplet 依赖，避免使用用户全局包。
    Write-Host 'Installing manifest dependencies (x64-windows)...'
    Invoke-Checked $vcpkg @('install', '--triplet=x64-windows', '--host-triplet=x64-windows',
        "--x-manifest-root=$repo", "--x-install-root=$installedDirectory", '--disable-metrics')
    $fluidSynth = Require-File "$installedDirectory\x64-windows\bin\libfluidsynth-3.dll" 'Installed FluidSynth'
    $webPConfig = Require-File "$installedDirectory\x64-windows\share\WebP\WebPConfig.cmake" 'Installed WebP CMake package'
    $lameConfig = Require-File "$installedDirectory\x64-windows\share\mp3lame\mp3lame-config.cmake" 'Installed LAME CMake package'

    # 生成 CMake 参数：Release、x64、测试开启，并将实际依赖路径显式传入。
    $vulkanOption = if ($enableVulkan) { 'ON' } else { 'OFF' }
    $configure = @('--fresh', '-S', $repo, '-B', $buildDirectory, '-G', 'Visual Studio 17 2022', '-A', 'x64',
        '-T', "v143,host=x64,version=$($env:VCToolsVersion.TrimEnd('\'))",
        "-DCMAKE_SYSTEM_VERSION=$($env:WindowsSDKVersion.TrimEnd('\'))",
        "-DCMAKE_GENERATOR_INSTANCE=$vsRoot", "-DCMAKE_PREFIX_PATH=$qtRoot",
        "-DQt6_DIR=$qtRoot/lib/cmake/Qt6", '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF',
        '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF', '-DBUILD_TESTING=ON', '-DCMAKE_INSTALL_BINDIR=.',
        "-DFLUIDSYNTH_DLL=$fluidSynth", "-DWebP_DIR=$(Split-Path $webPConfig)",
        "-Dmp3lame_DIR=$(Split-Path $lameConfig)",
        "-DCMAKE_MODULE_PATH=$($installedDirectory.Replace('\', '/'))/x64-windows/share/ffmpeg",
        "-DMIDI_PLAY_ENABLE_VULKAN=$vulkanOption",
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
    if ($FfmpegTestPath) { $env:PATH = "$(Split-Path $FfmpegTestPath);$env:PATH" }
    Invoke-Checked $ctest @('--test-dir', $buildDirectory, '-C', 'Release', '--output-on-failure', '--timeout', '60')
    if ($RequireVideoIntegration) {
        # Direct execution deliberately treats skip code 77 as failure.
        Invoke-Checked "$buildDirectory\Release\midi_play_video_background_tests.exe" @(
            '-platform', 'windows', '--audio', '--audio-export', '--vulkan', '--integration', '--window')
    }

    # 先在随机 staging 目录完成安装、Qt 部署和离线验证，全部通过后再替换正式包。
    $stage = Join-Path $repo "dist\.$PackageName-staging-$([Guid]::NewGuid().ToString('N'))"
    Assert-OutputPath $stage (Join-Path $repo 'dist')
    $null = New-Item -ItemType Directory -Path $stage
    Set-Content -LiteralPath "$stage\.midi-play-package" -Value 'midi-play-windows-package-v1' -Encoding ASCII
    Invoke-Checked $cmake @('--install', $buildDirectory, '--config', 'Release', '--prefix', $stage)
    Invoke-Checked $deployQt @('--release', '--no-compiler-runtime', '--no-translations', '--no-opengl-sw',
        '--qtpaths', $qtPaths, '--dir', $stage, "$stage\midi_play.exe", "$stage\midi_play_cli.exe")
    # windeployqt 可能只提供 VC 安装器；这里显式部署实际的 app-local CRT。
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

    # 从 PATH 移除开发 SDK 后运行依赖扫描、CLI 渲染和 MP3 导出 smoke test。
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
            Invoke-Checked "$stage\midi_play_cli.exe" @('--video-background-test', "$repo\tests\fixtures\background-video.mp4")
            Invoke-Checked "$stage\midi_play_cli.exe" @('--render-test', "$repo\tests\fixtures\deployment.musicxml", "$stage\.smoke.png")
            $null = Require-File "$stage\.smoke.png" 'Packaged Qt render smoke output'
            Remove-Item -LiteralPath "$stage\.smoke.png"
            Invoke-Checked "$stage\midi_play_cli.exe" @('--export-mp3', "$repo\tests\fixtures\deployment.musicxml",
                "$stage\.smoke.mp3", '--soundfont', "$repo\assets\metronome.sf2")
            $mp3Smoke = Require-File "$stage\.smoke.mp3" 'Packaged MP3 export smoke output'
            if ((Get-Item -LiteralPath $mp3Smoke).Length -lt 1000) {
                throw 'Packaged MP3 export produced an unexpectedly small file.'
            }
            Remove-Item -LiteralPath "$stage\.smoke.mp3"
            if ($AudioSmoke) {
                Invoke-Checked "$stage\midi_play_cli.exe" @('--audio-test', $SoundFontPath)
            }
        } finally { Pop-Location }
    } finally { $env:PATH = $developmentPath }

    # 写入源码版本、工具版本和 Vulkan 配置，形成可追溯的发布元数据。
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

    # 保留上一份成功包，直到新包全部验证通过并完成原子目录替换。
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
    # 无论成功或失败都释放锁、恢复调用前环境；失败 staging 目录故意保留供诊断。
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
