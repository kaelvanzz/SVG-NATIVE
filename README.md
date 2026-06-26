# SVG Native Handler for Windows

File Explorer SVG thumbnail provider using Direct2D/Direct3D WARP (software rendering).

## 快速安装

已提供预编译的 DLL。

**以管理员身份** 运行：
```cmd
install.cmd
```

脚本会自动：
1. 复制对应架构（x64/x86）的 DLL 到 `C:\Program Files\SVG-NATIVE\`
2. 注册 COM 组件
3. 启用 Explorer 缩略图
4. 将 Windows Photos 添加到 .svg 打开方式
5. 重启 Explorer

## 卸载

以管理员身份运行 `uninstall.cmd`。

## 从源码构建

需要 Visual Studio 2019+ 和 Windows 10 SDK（10.0.14393+）。

```cmd
build\build.bat x64    (或 x86)
```

输出到 `build\x64\SvgThumbProvider.dll`，同时复制到 `bin\x64\`。之后运行 `install.cmd` 安装。

## 说明

- 使用 Direct2D SVG API（`ID2D1SvgDocument`）真实渲染 SVG 内容
- 通过 Direct3D 11 WARP 软件渲染，无需独立显卡
- 若 Direct2D 不可用，自动降级为 GDI 占位符
