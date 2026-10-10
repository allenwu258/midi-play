@{
    # Windows 发布脚本读取的本地 SDK 配置模板；复制为 build.env.psd1 后替换示例路径。
    # 复制为 build.env.psd1 并替换以下路径；所有值均按字面字符串读取。
    # Visual Studio 2022 需安装使用 C++ 的桌面开发、Windows SDK 和 v143 工具。
    VisualStudioRoot = 'C:\path\to\Microsoft Visual Studio\2022\Community'
    QtRoot = 'C:\path\to\Qt\6.8.3\msvc2022_64'
    VcpkgRoot = 'C:\path\to\vcpkg'
    VulkanSdk = 'C:\path\to\VulkanSDK\1.4.341.0'

    # 留空时使用所配置 Visual Studio 自带的 CMake。
    CMakeExe = ''
    EnableVulkan = $true
}
