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
        scratch = Path(temporary)
        master = scratch / "master.png"
        convert("-background", "none", "-density", "192", directory / "app.svg", "-resize", "2048x2048", "-strip", "PNG32:" + str(master))
        images = {}
        for size in sizes:
            target = scratch / f"{size}.png"
            convert(master, "-resize", f"{size}x{size}", "-strip",
                    "-define", "png:compression-level=9", "-define", "png:compression-filter=5", "PNG32:" + str(target))
            images[size] = target.read_bytes()

    # 只提交三个平台直接使用的产物；所有尺寸保留透明像素，ICO 内使用 Windows 10 支持的 PNG。
    ico_sizes = [size for size in sizes if size <= 256]
    offset = 6 + 16 * len(ico_sizes)
    entries = []
    for size in ico_sizes:
        entries.append(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(images[size]), offset))
        offset += len(images[size])
    (directory / "app.ico").write_bytes(struct.pack("<HHH", 0, 1, len(ico_sizes)) +
                                       b"".join(entries) + b"".join(images[size] for size in ico_sizes))
    (directory / "app-256.png").write_bytes(images[256])
    # macOS 保留标准和 Retina 图标，避免靠删减分辨率缩小应用包。
    representations = {"icp4": 16, "icp5": 32, "icp6": 64, "ic07": 128, "ic08": 256, "ic09": 512, "ic10": 1024,
                       "ic11": 32, "ic12": 64, "ic13": 256, "ic14": 512}
    chunks = []
    for kind, size in representations.items():
        image = images[size]
        chunks.append(kind.encode("ascii") + struct.pack(">I", 8 + len(image)) + image)
    payload = b"".join(chunks)
    (directory / "app.icns").write_bytes(b"icns" + struct.pack(">I", 8 + len(payload)) + payload)
    print("Generated SVG-derived PNG, ICO and ICNS assets in", directory)


if __name__ == "__main__":
    main()
