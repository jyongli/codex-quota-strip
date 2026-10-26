"""从仓库内的矢量源生成各平台图标；只用于维护资产，程序运行和正常构建不需要此脚本。"""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    directory = root / "assets" / "icons"
    magick = shutil.which("magick")
    if not magick:
        raise SystemExit("ImageMagick 7 is required to regenerate icons; prebuilt icons are already committed.")

    def convert(*arguments):
        subprocess.run([magick, *map(str, arguments)], check=True)

    sizes = (16, 24, 32, 48, 64, 128, 256, 512, 1024)
    # 先以两倍分辨率栅格化，再缩小；小尺寸保留抗锯齿，透明边缘不合成到背景色。
    with tempfile.TemporaryDirectory(prefix="quota-icon-") as temporary:
        master = Path(temporary) / "master.png"
        convert("-background", "none", "-density", "192", directory / "app.svg", "-resize", "2048x2048", "-strip", "PNG32:" + str(master))
        for size in sizes:
            convert(master, "-resize", f"{size}x{size}", "-strip", "PNG32:" + str(directory / f"app-{size}.png"))

    # ICO 同时携带七档尺寸，供资源管理器、任务栏和标题栏各自选择，避免仅缩放一张大图。
    convert(*(directory / f"app-{size}.png" for size in sizes if size <= 256), "-strip", directory / "app.ico")
    # macOS ICNS 容器直接封装带 alpha 的 PNG，并提供普通与 Retina 表示。
    representations = {"icp4": 16, "icp5": 32, "icp6": 64, "ic07": 128, "ic08": 256, "ic09": 512, "ic10": 1024,
                       "ic11": 32, "ic12": 64, "ic13": 256, "ic14": 512}
    chunks = []
    for kind, size in representations.items():
        image = (directory / f"app-{size}.png").read_bytes()
        chunks.append(kind.encode("ascii") + struct.pack(">I", 8 + len(image)) + image)
    payload = b"".join(chunks)
    (directory / "app.icns").write_bytes(b"icns" + struct.pack(">I", 8 + len(payload)) + payload)
    print("Generated SVG-derived PNG, ICO and ICNS assets in", directory)


if __name__ == "__main__":
    main()
