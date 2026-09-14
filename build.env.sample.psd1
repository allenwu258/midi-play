@{
    # Copy to build.env.psd1 and replace these paths. Values are literal strings.
    # VS 2022 with Desktop development with C++, Windows SDK, and v143 tools.
    VisualStudioRoot = 'C:\path\to\Microsoft Visual Studio\2022\Community'
    QtRoot = 'C:\path\to\Qt\6.8.3\msvc2022_64'
    VcpkgRoot = 'C:\path\to\vcpkg'
    VulkanSdk = 'C:\path\to\VulkanSDK\1.4.341.0'

    # Empty uses the CMake bundled with the configured Visual Studio.
    CMakeExe = ''
    EnableVulkan = $true
}
