"""產生程式圖示 src/app/AuralisPlayer.ico（介面規範 §7「程式圖示與 Logo」）。

圖形（以邊長為 1）：Void 的 bg 方塊底、置中的 ink 圓線（直徑 0.60，線寬 max(1px, 邊長/40)）、
置中的琥珀實心點（直徑 0.24）。每個尺寸各自以 8 倍取樣畫好再縮小，小尺寸的線寬才不會糊掉。

用法：python tools/make_app_icon.py [輸出的 .ico 路徑]（需要 Pillow；圖示改了才要重跑，產物進 repo）
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw

BG = (0x05, 0x05, 0x05, 255)      # Theme.bg（Void）
INK = (0xF2, 0xF0, 0xEB, 255)     # Theme.ink（Void）
AMBER = (0xE0, 0xA3, 0x4A, 255)   # Theme.amber（Void）
RING = 0.60
DOT = 0.24
SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
SCALE = 8


def render(size: int) -> Image.Image:
    big = size * SCALE
    image = Image.new("RGBA", (big, big), BG)
    draw = ImageDraw.Draw(image)
    center = big / 2
    line = max(1.0, size / 40) * SCALE
    radius = RING * big / 2
    # 線寬以圓線的半徑為中心向內外各一半
    outer = radius + line / 2
    draw.ellipse((center - outer, center - outer, center + outer, center + outer), fill=INK)
    inner = radius - line / 2
    draw.ellipse((center - inner, center - inner, center + inner, center + inner), fill=BG)
    dot = DOT * big / 2
    draw.ellipse((center - dot, center - dot, center + dot, center + dot), fill=AMBER)
    return image.resize((size, size), Image.LANCZOS)


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "src" / "app" / "AuralisPlayer.ico"
    images = [render(s) for s in SIZES]
    largest = images[-1]
    largest.save(out, format="ICO", sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    print(f"已寫入 {out}（{', '.join(str(s) for s in SIZES)} px）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
