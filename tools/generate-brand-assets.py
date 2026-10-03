"""Reproduce original CampusPulse SVG/PNG/ICO assets without a model or web download.

Development-only dependency: Pillow. The installed desktop app does not use Python.
"""
from pathlib import Path
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
SIZES = (16, 24, 32, 48, 64, 128, 256)


def app_icon():
    factor = 4
    image = Image.new("RGBA", (256 * factor, 256 * factor))
    draw = ImageDraw.Draw(image)

    def box(values, radius, color):
        draw.rounded_rectangle(tuple(v * factor for v in values), radius * factor, fill=color)

    def line(points, color, width):
        coords = [(x * factor, y * factor) for x, y in points]
        draw.line(coords, fill=color, width=width * factor, joint="curve")
        r = width * factor / 2
        for x, y in coords:
            draw.ellipse((x - r, y - r, x + r, y + r), fill=color)

    box((0, 0, 256, 256), 56, "#174875")
    box((45, 51, 211, 215), 23, "#ffffff")
    box((45, 51, 211, 103), 23, "#387bcc")
    draw.rectangle((45 * factor, 76 * factor, 211 * factor, 103 * factor), fill="#387bcc")
    line([(78, 43), (78, 72)], "#ffffff", 12)
    line([(178, 43), (178, 72)], "#ffffff", 12)
    line([(65, 153), (90, 153), (104, 127), (126, 178), (147, 141), (167, 153), (191, 153)], "#137e69", 11)
    return image.resize((256, 256), Image.Resampling.LANCZOS)


def navigation_icon(name, size):
    factor = 4
    image = Image.new("RGBA", (size * factor, size * factor))
    draw = ImageDraw.Draw(image)
    scale = size * factor / 24
    color = "#4a78a6"

    def line(points):
        coords = [(round(x * scale), round(y * scale)) for x, y in points]
        width = max(1, round(1.7 * scale))
        draw.line(coords, fill=color, width=width, joint="curve")
        r = width / 2
        for x, y in coords:
            draw.ellipse((x - r, y - r, x + r, y + r), fill=color)

    def rect(box):
        draw.rounded_rectangle(tuple(round(x * scale) for x in box), radius=round(2 * scale),
                               outline=color, width=max(1, round(1.7 * scale)))

    if name == "notices":
        line([(5, 17), (5, 9), (7, 5), (12, 3), (17, 5), (19, 9), (19, 17), (5, 17)])
        line([(10, 20), (14, 20)])
    elif name == "sources":
        rect((3, 3, 10, 10))
        rect((14, 14, 21, 21))
        line([(14, 5), (19, 5), (19, 10)])
        line([(5, 14), (5, 19), (10, 19)])
    elif name == "subscriptions":
        rect((4, 3, 20, 21))
        line([(8, 8), (16, 8)])
        line([(8, 12), (16, 12)])
        line([(8, 16), (13, 16)])
    elif name == "tasks":
        rect((4, 4, 20, 21))
        line([(8, 4), (8, 2), (16, 2), (16, 4)])
        line([(8, 13), (11, 16), (17, 10)])
    elif name == "universities":
        line([(3, 8), (12, 3), (21, 8), (3, 8)])
        for x in (6, 12, 18):
            line([(x, 10), (x, 18)])
        line([(3, 21), (21, 21)])
    elif name == "ai":
        rect((6, 6, 18, 18))
        for x in (9, 15):
            line([(x, 3), (x, 6)])
            line([(x, 18), (x, 21)])
            line([(3, x), (6, x)])
            line([(18, x), (21, x)])
        line([(9, 12), (15, 12)])
    elif name == "calendar":
        rect((3, 5, 21, 21))
        line([(3, 10), (21, 10)])
        line([(7, 3), (7, 7)])
        line([(17, 3), (17, 7)])
        line([(7, 15), (10, 15), (12, 18), (16, 14)])
    else:
        raise ValueError(name)
    return image.resize((size, size), Image.Resampling.LANCZOS)


def main():
    ASSETS.mkdir(exist_ok=True)
    icons = ASSETS / "icons"
    navigation = ASSETS / "navigation"
    icons.mkdir(exist_ok=True)
    navigation.mkdir(exist_ok=True)
    source = app_icon()
    entries = [("campuspulse.svg", "campuspulse.svg")]
    for size in SIZES:
        filename = f"icons/campuspulse-{size}.png"
        source.resize((size, size), Image.Resampling.LANCZOS).save(ASSETS / filename)
        entries.append((filename, filename))
    source.save(ASSETS / "campuspulse.ico", format="ICO", sizes=[(s, s) for s in SIZES])
    for name in ("notices", "sources", "subscriptions", "tasks", "universities", "ai", "calendar"):
        for size in (24, 48):
            filename = f"navigation/{name}-{size}.png"
            navigation_icon(name, size).save(ASSETS / filename)
            entries.append((filename, filename))
    rcc = ET.Element("RCC")
    resource = ET.SubElement(rcc, "qresource", prefix="/campuspulse")
    for filename, alias in entries:
        ET.SubElement(resource, "file", alias=alias).text = filename
    ET.indent(rcc, space="  ")
    (ASSETS / "campuspulse.qrc").write_text(ET.tostring(rcc, encoding="unicode") + "\n", encoding="utf-8")
    print(f"Generated {len(entries) - 1} PNG resources and {len(SIZES)} ICO sizes.")


if __name__ == "__main__":
    main()
