# SVG Native Handler for Windows

File Explorer SVG thumbnail provider using Direct2D/Direct3D WARP (software rendering).

## 快速安装

已提供预编译的 DLL：

1. **以管理员身份** 右键 `src\SvgThumbProvider.inf` → **安装**
2. 重启 Explorer 或注销重登

INF 会自动将对应架构（x64/x86）的 DLL 从 `bin\` 复制到 `C:\Program Files\SVG-NATIVE\` 并注册。

## 从源码构建

需要 Visual Studio 2019+ 和 Windows 10 SDK（10.0.14393+）。

```
build\build.bat x64    (或 x86)
```

输出到 `build\x64\SvgThumbProvider.dll`，同时自动复制到 `bin\x64\`。

## 卸载

以管理员身份右键 `src\SvgThumbProvider.inf` → **卸载**

或命令行：
```
rundll32.exe setupapi,InstallHinfSection DefaultUninstall 128 "src\SvgThumbProvider.inf"
```

## 说明

- 使用 Direct2D SVG API（`ID2D1SvgDocument`）真实渲染 SVG 内容
- 通过 Direct3D 11 WARP 软件渲染，无需独立显卡
- 若 Direct2D 不可用，自动降级为 GDI 占位符
- 将 Windows Photos 添加到 .svg 的"打开方式"列表
