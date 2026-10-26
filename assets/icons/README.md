# Application icon / 应用图标

`app.svg` is the editable master: a mint quota ring and two usage bars on a dark tile. The artwork is original and is covered by the repository's MIT license.

`app.svg` 为可编辑源文件，以薄荷绿圆环和两根刻度条表达配额；图形为本项目原创，沿用仓库 MIT 许可。

- `app.ico`: Windows EXE, native windows, shortcuts and installer (16/24/32/48/64/128/256 px).
- `app.icns`: macOS application bundle, with standard and Retina representations through 1024 px.
- `app-*.png` and `app.svg`: Linux launchers/windows and documentation.

Generated files are committed. Normal app builds and runtime do not require Python or ImageMagick. To regenerate from the SVG, install Python 3 and ImageMagick 7, then run from the repository root:

生成文件已纳入源码，正常构建和运行不需要 Python 或 ImageMagick。维护矢量源后，可安装 Python 3 与 ImageMagick 7，再在仓库根目录执行：

```sh
python scripts/Generate-Icons.py
```

Numeric tray/menu-bar quota indicators remain generated separately so that they can continue showing live values.

托盘／菜单栏的额度数字使用独立的动态绘制，以保留实时数值显示。
