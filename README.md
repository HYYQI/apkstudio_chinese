<h1 align="center">
  <a href="http://www.vaibhavpandey.com/apkstudio/">
    <img src="https://raw.githubusercontent.com/vaibhavpandeyvpz/apkstudio/master/resources/icon.png" alt="APK Studio" height="192px">
  </a>
  <br>
  APK Studio
</h1>

基于 [Qt6](https://www.qt.io/) 的开源、跨平台集成开发环境，用于逆向工程 [Android](https://android.com/) 应用程序包。它提供友好的 IDE 布局，内置支持 \*.smali 代码文件语法高亮的代码编辑器。

[![截图](resources/screenshot.png)](resources/screenshot.png)

<p align="center">
  <a href="https://github.com/vaibhavpandeyvpz/apkstudio/actions">
    <img src="https://github.com/vaibhavpandeyvpz/apkstudio/workflows/Build/badge.svg" alt="构建状态">
  </a>
  <a href="https://github.com/vaibhavpandeyvpz/apkstudio/releases/latest">
    <img src="https://img.shields.io/github/release/vaibhavpandeyvpz/apkstudio.svg" alt="最新版本">
  </a>
  <a href="https://github.com/vaibhavpandeyvpz/apkstudio/releases">
    <img src="https://img.shields.io/github/downloads/vaibhavpandeyvpz/apkstudio/total.svg" alt="下载量">
  </a>
  <a href="https://github.com/vaibhavpandeyvpz/apkstudio/blob/master/LICENSE">
    <img src="https://img.shields.io/github/license/vaibhavpandeyvpz/apkstudio.svg" alt="许可证">
  </a>
</p>

### 功能特性

- **跨平台**，支持 **Linux**、**Mac OS X** 和 **Windows**
- 反编译/重新编译、签名及安装 APK
- **自动下载和安装工具** - APK Studio 可自动下载并安装所需工具（Java、Apktool、JADX、ADB、Uber APK Signer）
- **框架支持** - 安装和使用制造商特定的框架文件（如 HTC、LG、三星），并支持可选标签用于反编译和重新编译 APK
- **命令行打开 APK** - 通过"打开方式"上下文菜单或命令行参数直接从文件系统打开 APK 文件
- **额外的 apktool 参数** - 为反编译和重新编译操作提供附加命令行参数（例如 `--force-all`、`--no-res`）
- **搜索功能** - 在打开的文件和项目树中快速搜索项目名称
- 内置代码编辑器（\*.java；\*.smali；\*.xml；\*.yml），支持语法高亮
- 内置图片（\*.gif；\*.jpg；\*.jpeg；\*.png）查看器
- 内置二进制文件十六进制编辑器
- **支持深色/浅色主题** - 原生 Qt 6 主题，与系统集成

### 下载

** 暂未提供可执行文件，请自行构建 **

请前往 [Releases](https://github.com/vaibhavpandeyvpz/apkstudio/releases) 页面进行下载。

**注意：** APK Studio 在首次启动时可自动下载并安装所需工具（Java、Apktool、JADX、ADB、Uber APK Signer）。若您希望使用自己安装的版本，可在设置中进行配置。

**提示：** 您可以通过右键点击 `.apk` 文件，选择"打开方式" → "选择其他应用"，然后浏览到 APK Studio 可执行文件，直接从文件系统打开 APK 文件。或者，您也可以将 APK 文件路径作为命令行参数传递。反编译对话框将自动打开并显示所选文件。

### 构建

#### 系统要求

- **CMake** 3.16 或更高版本
- **Qt6** 6.10.1 或更高版本（Core、Gui、Network、Widgets 组件）
- 支持 **C++17** 的编译器
- **Git**（用于版本信息）

#### 构建说明

1. **克隆仓库**（包含子模块）：
   ```bash
   git clone --recursive https://github.com/vaibhavpandeyvpz/apkstudio.git
   cd apkstudio
```

1. 配置构建：
   ```bash
   cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
   ```
   在 macOS 上，您可能需要指定 Qt 路径：
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.10.1/clang_64
   ```
2. 构建项目：
   ```bash
   cmake --build build --config Release
   ```
3. 部署 Qt 依赖项（可选，用于分发）：
   · Windows：使用 Qt 安装中的 windeployqt
   · Linux：使用带有 Qt 插件的 linuxdeploy 创建 AppImage
   · macOS：使用 macdeployqt 打包 Qt 框架

可执行文件将位于：

· Windows：build/bin/Release/ApkStudio.exe
· Linux/macOS：build/bin/ApkStudio（macOS 上为 build/bin/ApkStudio.app）

CI/CD 持续集成

项目使用 GitHub Actions 在 Windows、Linux 和 macOS 上进行自动化构建。每次推送、拉取请求和发布时，都会自动创建并上传构建工件。

致谢

· iBotPeaches 提供的 apktool
· patrickfav 提供的 uber-apk-signer
· skylot 提供的 jadx
· linuxdeploy 团队提供的 linuxdeploy 和 linuxdeploy-plugin-qt
· Antonio Davide 提供的 QHexView
· p.yusukekamiyamane 提供的 Fugue 图标
· Icons8 提供的各种图标
· Surendrajat 在我无法维护期间维持项目

注意：如果遇到任何问题，请查看 IDE 底部的控制台输出，以获取程序实际执行的命令结果。与 APK Studio 相关的问题请在此 GitHub 仓库报告。请注意，apktool 的问题不属于 APK Studio 的问题。提交工单前，请先核实问题的具体情况。

---

免责声明

与 apktool 相同，APK Studio 既无意用于盗版，也不用于其他非法用途。它可以用于应用本地化、添加新功能或支持自定义平台、分析应用等等。
