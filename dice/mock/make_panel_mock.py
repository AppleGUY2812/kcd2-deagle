"""Draw Dice Advisor's HUD panel exactly as tools/hud/advice.as lays it out, on the game's own screenshots.

Advice mode on Screenshot_3 (dice table), picker mode on Screenshot_1 (dice menu), plus the two glow markers.
Coordinates are the AS2 ones (1920x1080), scaled onto each screenshot exactly like the HUD fills the screen.
CENTAUR.TTF stands in for the game's DefaultFont/DefaultFontBold.
"""
from math import hypot
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT = Path("/mnt/c/Windows/Fonts/CENTAUR.TTF")

GOLD = (0xF6, 0xE8, 0x90)
CREAM = (0xFF, 0xFF, 0xFF)
BRIGHT = (0xFF, 0xF3, 0xA9)
LINE = (0xBF, 0xA5, 0x8C)
DIE_FILL = (0x1A, 0x14, 0x0C)
GLOW = (0xFF, 0xD0, 0x7A)

DIE = 26
GAP = 5
ROW = 26
ADVICE_W = 430
SS = 4  # supersampling for the panel

# panel top-left in 1920x1080 UI coordinates (advice: container-local 470,-352 => screen 728)
ADVICE_X, ADVICE_Y = 470, 728
PICKER_X, PICKER_Y = 960, 150


def scale(image):
    return image.width / 1920, image.height / 1080


def font(size):
    return ImageFont.truetype(str(FONT), int(round(size * SS)))


def text(draw, x, y, value, size, colour, bold=False, anchor="la"):
    kwargs = {}
    if bold:
        kwargs["stroke_width"] = max(1, int(SS // 2))
        kwargs["stroke_fill"] = colour + (255,)
    draw.text((x * SS, y * SS), value, font=font(size), fill=colour + (255,), anchor=anchor, **kwargs)


def pip(draw, cx, cy, r):
    box = [(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS]
    draw.ellipse(box, fill=BRIGHT + (230,))


def die(draw, x, y, face):
    s = DIE
    draw.rounded_rectangle(
        [x * SS, y * SS, (x + s) * SS, (y + s) * SS],
        radius=4 * SS,
        outline=GOLD + (225,),
        width=max(1, int(1.5 * SS)),
        fill=DIE_FILL + (76,),
    )
    if face == "J":
        text(draw, x + s / 2, y + s / 2 + 0.5, "J", 12, BRIGHT, bold=True, anchor="mm")
        return
    a, b, c = s * 0.27, s * 0.5, s * 0.73
    r = 1.55
    n = int(face)
    if n in (1, 3, 5):
        pip(draw, x + b, y + b, r)
    if n in (2, 3, 4, 5, 6):
        pip(draw, x + a, y + a, r)
        pip(draw, x + c, y + c, r)
    if n in (4, 5, 6):
        pip(draw, x + c, y + a, r)
        pip(draw, x + a, y + c, r)
    if n == 6:
        pip(draw, x + a, y + b, r)
        pip(draw, x + c, y + b, r)


def panel_canvas(width, height):
    # margin around the panel for the divider and descenders, in game pixels
    left, top = 25, 12
    return Image.new("RGBA", (int((width + left * 2) * SS), int((height + top * 2) * SS)), (0, 0, 0, 0)), left, top


def divider(draw, left, top, height):
    x = -6
    draw.line([(x + left) * SS, (top - 2) * SS, (x + left) * SS, (top + height) * SS], fill=LINE + (179,), width=SS)


def advice_panel(faces, action, line2, line3):
    why = [line for line in str(line3 or "").split("\n") if line]
    height = 94 + len(why) * 21 + 4
    image, left, top = panel_canvas(ADVICE_W + 10, height)
    draw = ImageDraw.Draw(image)

    text(draw, x=left, y=top, value="Advice", size=25, colour=GOLD, bold=True)
    dice = [f for f in str(faces).split(",") if f]
    for i, face in enumerate(dice):
        die(draw, left + i * (DIE + GAP), top + 34, face)
    if action:
        text(draw, x=left + len(dice) * (DIE + GAP) + 6, y=top + 32,
             value=action, size=22, colour=BRIGHT, bold=True)
    if line2:
        text(draw, x=left, y=top + 68, value=line2, size=20, colour=CREAM)
    for i, line in enumerate(why):
        text(draw, x=left, y=top + 94 + i * 21, value=line, size=18, colour=CREAM)
    divider(draw, left, top, height)
    return image, left, top


def picker_panel(rows, why):
    why_lines = [line for line in str(why or "").split("\n") if line]
    why_y = 36 + len(rows) * ROW + 10
    height = why_y + len(why_lines) * 21 + 4
    image, left, top = panel_canvas(440, height)
    draw = ImageDraw.Draw(image)
    text(draw, x=left, y=top, value="Bring these 6", size=25, colour=GOLD, bold=True)
    for i, row in enumerate(rows):
        text(draw, x=left, y=top + 36 + i * ROW, value=row, size=20, colour=CREAM)
    for i, line in enumerate(why_lines):
        text(draw, x=left, y=top + why_y + i * 21, value=line, size=18, colour=CREAM)
    divider(draw, left, top, height)
    return image, left, top


def place(screenshot, image, left, top, x, y):
    sx, sy = scale(screenshot)
    w = int(round(image.width / SS * sx))
    h = int(round(image.height / SS * sy))
    image = image.resize((w, h), Image.Resampling.LANCZOS)
    out = screenshot.copy()
    out.alpha_composite(image, (int(round((x - left) * sx)), int(round((y - top) * sy))))
    return out


def glow(radius=60, peak_alpha=0.75):
    # [0, 75, 0] at ratios [0, 175, 255] of the radius: a ring of light with a clear centre
    size = radius * 2 + 1
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    pixels = image.load()
    mid = radius * 175 / 255
    for y in range(size):
        for x in range(size):
            d = hypot(x - radius, y - radius)
            if d > radius:
                continue
            a = peak_alpha * (d / mid) if d <= mid else peak_alpha * (1 - (d - mid) / (radius - mid))
            pixels[x, y] = GLOW + (int(round(a * 255)),)
    return image


def add_glow(image, centre):
    mark = glow()
    image.alpha_composite(mark, (int(round(centre[0] - mark.width / 2)), int(round(centre[1] - mark.height / 2))))


def main():
    if not FONT.exists():
        raise FileNotFoundError(f"Required serif font not found: {FONT}")

    advice = Image.open(ROOT / "ref" / "Screenshot_3.png").convert("RGBA")
    for centre in ((975, 528), (892, 462)):
        add_glow(advice, centre)
    panel, left, top = advice_panel(
        "1,6,6,J",
        "then THROW AGAIN",
        "Throwing on: 11 % chance to lose 700 points",
        "Why: keeps your Weighted in hand (+1.6 % win)",
    )
    advice = place(advice, panel, left, top, ADVICE_X, ADVICE_Y)
    advice.convert("RGB").save(ROOT / "mock" / "panel_advice.png", optimize=True)

    picker = Image.open(ROOT / "ref" / "Screenshot_1.png").convert("RGBA")
    panel, left, top = picker_panel(
        ["Favourable x3", "Painted x2", "Weighted"],
        "Why: Weighted: a 1 every 2nd throw;\n2140 pts/turn vs 1925 with plain dice",
    )
    picker = place(picker, panel, left, top, PICKER_X, PICKER_Y)
    picker.convert("RGB").save(ROOT / "mock" / "panel_picker.png", optimize=True)
    print("wrote mock/panel_advice.png and mock/panel_picker.png")


if __name__ == "__main__":
    main()
