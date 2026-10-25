"""Generates ui/icons/voicestudio.ico: the sidebar logo (Segoe MDL2 microphone on the teal accent).

Development helper only (needs Pillow):  python tools/make_icon.py
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ACCENT = (0x1E, 0xD6, 0xC8, 255)
GLYPH = (0x11, 0x13, 0x17, 255)
MIC = "\uE720"
FONT = r"C:\Windows\Fonts\segmdl2.ttf"
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]


def render(size: int) -> Image.Image:
    scale = 4  # supersample for smooth edges
    s = size * scale
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    radius = round(s * 10 / 34)  # same proportions as the 34 px sidebar tile with radius 10
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=radius, fill=ACCENT)
    small = size <= 32  # tray / title bar: larger, bolder glyph so the thin outline stays readable
    font = ImageFont.truetype(FONT, round(s * (0.62 if small else 0.56)))
    stroke = round(s * 0.012) if small else 0
    box = d.textbbox((0, 0), MIC, font=font, stroke_width=stroke)
    w, h = box[2] - box[0], box[3] - box[1]
    d.text(((s - w) / 2 - box[0], (s - h) / 2 - box[1]), MIC, font=font, fill=GLYPH, stroke_width=stroke,
           stroke_fill=GLYPH)
    return img.resize((size, size), Image.LANCZOS)


out = Path(__file__).resolve().parent.parent / "ui" / "icons" / "voicestudio.ico"
images = [render(n) for n in SIZES]
images[-1].save(out, format="ICO", sizes=[(n, n) for n in SIZES], append_images=images[:-1])
images[-1].save(out.with_suffix(".png"))
print("wrote", out)
