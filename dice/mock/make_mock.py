from pathlib import Path
import random
import statistics

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "ref" / "Screenshot_3.png"
OUTPUT = ROOT / "mock" / "advisor_mock.png"
CROP_OUTPUT = ROOT / "mock" / "advisor_mock_crop.png"
FONT = Path("/mnt/c/Windows/Fonts/CENTAUR.TTF")

PANEL_X, PANEL_Y = 440, 756
PANEL_W, PANEL_H = 420, 244
SCALE = 4


def sampled_selector_gold(image):
    pixels = []
    for red, green, blue, *_ in image.crop((915, 465, 1035, 590)).getdata():
        if red > 185 and green > 160 and 70 < blue < 180:
            pixels.append((red, green, blue))
    pixels.sort(key=sum)
    brightest = pixels[-max(1, len(pixels) // 8):]
    return tuple(round(statistics.median(channel)) for channel in zip(*brightest))


def font(size):
    return ImageFont.truetype(str(FONT), size * SCALE)


def add_selector(image, center, gold, radius=58):
    padding = 14
    size = (radius + padding) * 2 + 1
    large_size = size * SCALE
    ring = Image.new("RGBA", (large_size, large_size))
    draw = ImageDraw.Draw(ring)
    box = tuple(value * SCALE for value in (padding, padding, padding + radius * 2, padding + radius * 2))

    glow = Image.new("RGBA", ring.size)
    glow_draw = ImageDraw.Draw(glow)
    glow_draw.ellipse(box, outline=(*gold, 125), width=7 * SCALE)
    glow = glow.filter(ImageFilter.GaussianBlur(3.2 * SCALE))
    ring.alpha_composite(glow)

    draw.ellipse(box, outline=(*gold, 255), width=4 * SCALE)
    inset = SCALE
    inner_box = (box[0] + inset, box[1] + inset, box[2] - inset, box[3] - inset)
    draw.ellipse(inner_box, outline=(255, 235, 157, 115), width=SCALE)

    ring = ring.resize((size, size), Image.Resampling.LANCZOS)
    image.alpha_composite(ring, (center[0] - size // 2, center[1] - size // 2))


def textured_background(source):
    texture = source.crop((1180, 730, 1180 + PANEL_W, 730 + PANEL_H)).convert("RGBA")
    texture = ImageEnhance.Brightness(texture).enhance(0.43)
    texture = Image.blend(texture, Image.new("RGBA", texture.size, (25, 17, 10, 255)), 0.34)

    rng = random.Random(17)
    noise = Image.new("L", texture.size)
    noise.putdata([rng.randint(70, 185) for _ in range(PANEL_W * PANEL_H)])
    noise = noise.filter(ImageFilter.GaussianBlur(0.45))
    noise_rgba = Image.merge("RGBA", (noise, noise, noise, Image.new("L", texture.size, 22)))
    texture.alpha_composite(noise_rgba)
    return texture.resize((PANEL_W * SCALE, PANEL_H * SCALE), Image.Resampling.LANCZOS)


def draw_text(draw, position, text, text_font, fill, anchor="lt", shadow=True):
    x, y = position
    if shadow:
        draw.text((x + SCALE, y + SCALE), text, font=text_font, fill=(0, 0, 0, 185), anchor=anchor)
    draw.text((x, y), text, font=text_font, fill=fill, anchor=anchor)


def make_panel(source):
    panel = textured_background(source)
    draw = ImageDraw.Draw(panel)

    outer = (128, 99, 49, 235)
    inner = (190, 157, 82, 145)
    pale_gold = (229, 210, 143, 255)
    draw.rectangle((SCALE, SCALE, panel.width - 2 * SCALE, panel.height - 2 * SCALE), outline=outer, width=2 * SCALE)
    draw.rectangle((8 * SCALE, 8 * SCALE, panel.width - 9 * SCALE, panel.height - 9 * SCALE), outline=inner, width=SCALE)

    corner = 18 * SCALE
    for x, x_direction in ((8 * SCALE, 1), (panel.width - 9 * SCALE, -1)):
        for y, y_direction in ((8 * SCALE, 1), (panel.height - 9 * SCALE, -1)):
            draw.line((x, y, x + x_direction * corner, y), fill=(218, 181, 93, 190), width=SCALE)
            draw.line((x, y, x, y + y_direction * corner), fill=(218, 181, 93, 190), width=SCALE)

    title_font = font(31)
    body_font = font(28)
    draw_text(draw, (25 * SCALE, 14 * SCALE), "Advice", title_font, pale_gold)
    draw_text(draw, (25 * SCALE, 59 * SCALE), "Set aside: 1 1 5", body_font, pale_gold)
    draw.line((24 * SCALE, 101 * SCALE, (PANEL_W - 24) * SCALE, 101 * SCALE), fill=(151, 119, 58, 145), width=SCALE)

    row_left = 18
    row_right = PANEL_W - 18
    row_top = 113
    row_height = 45
    rng = random.Random(62)
    highlight = Image.new("RGBA", ((row_right - row_left) * SCALE, row_height * SCALE))
    highlight_draw = ImageDraw.Draw(highlight)
    for y in range(row_height * SCALE):
        shade = rng.randint(-7, 7)
        colour = (202 + shade, 167 + shade, 72 + shade, 246)
        highlight_draw.line((0, y, highlight.width, y), fill=colour)
    highlight_draw.line((0, 0, highlight.width, 0), fill=(242, 214, 132, 230), width=SCALE)
    highlight_draw.line((0, highlight.height - SCALE, highlight.width, highlight.height - SCALE), fill=(104, 73, 27, 210), width=SCALE)
    panel.alpha_composite(highlight, (row_left * SCALE, row_top * SCALE))

    ink = (43, 29, 14, 255)
    draw_text(draw, (34 * SCALE, 119 * SCALE), "Throw again", body_font, ink, shadow=False)
    draw_text(draw, ((PANEL_W - 34) * SCALE, 119 * SCALE), "62 %", body_font, ink, anchor="rt", shadow=False)
    draw_text(draw, (34 * SCALE, 174 * SCALE), "Bank", body_font, pale_gold)
    draw_text(draw, ((PANEL_W - 34) * SCALE, 174 * SCALE), "48 %", body_font, pale_gold, anchor="rt")

    return panel.resize((PANEL_W, PANEL_H), Image.Resampling.LANCZOS)


def main():
    if not FONT.exists():
        raise FileNotFoundError(f"Required serif font not found: {FONT}")

    source = Image.open(SOURCE).convert("RGBA")
    image = source.copy()
    gold = sampled_selector_gold(source)

    add_selector(image, (781, 491), gold)
    add_selector(image, (842, 579), gold)

    shadow = Image.new("RGBA", image.size)
    shadow_draw = ImageDraw.Draw(shadow)
    shadow_draw.rectangle((PANEL_X - 5, PANEL_Y - 4, PANEL_X + PANEL_W + 7, PANEL_Y + PANEL_H + 8), fill=(0, 0, 0, 150))
    shadow = shadow.filter(ImageFilter.GaussianBlur(9))
    image.alpha_composite(shadow)
    image.alpha_composite(make_panel(source), (PANEL_X, PANEL_Y))

    image.convert("RGB").save(OUTPUT, optimize=True)
    crop = image.crop((390, 300, 1110, 1040))
    crop = crop.resize((crop.width * 3 // 2, crop.height * 3 // 2), Image.Resampling.LANCZOS)
    crop.convert("RGB").save(CROP_OUTPUT, optimize=True)


if __name__ == "__main__":
    main()
