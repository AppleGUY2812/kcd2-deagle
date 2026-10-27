"""Mock pictures of the weight map (not game screenshots): the layout comes from the map's own code
(test_layout.js --layout), the drawing copies WM_Build / WM_DrawBox (colours, alphas, text and icon rules), the icons
are the game's own (IPL_GameData.pak). Fonts differ: the game uses "Kingdom Come Regular".

usage: python3 -I mock_render.py <out_dir>
"""
import io, json, os, subprocess, sys, tempfile, zipfile
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PAK = os.environ.get("KCD2_DIR", "/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2") + "/Data/IPL_GameData.pak"
COLORS = [0x7A2E22, 0x4E5A66, 0x6E7A2E, 0x9A7B3E, 0x2E5E5A, 0x6A5040]
NAMES = ["WEAPONS", "ARMOUR", "FOOD", "DOCUMENTS", "CRAFTING", "OTHER"]
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"
FONTB = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"

HENRY = [
    [0, "Longsword", 2.8, 1, "longswordCommon"], [0, "Mace", 2.6, 1, "mace01"], [0, "Crossbow", 4.5, 1, "crossbowHeavyNormal01"],
    [0, "Bolts", 0.1, 40, "bolt_cutting"], [0, "Arrows", 0.08, 30, "arrow_cutting"],
    [1, "Cuirass", 13.5, 1, "Cuirass01_m01_C2"], [1, "Gambeson", 5.1, 1, "GambesonLong01_m01_C3"],
    [1, "Kettle hat", 3.2, 1, "KettleHat01_m01_E2"], [1, "Coif", 2.4, 1, "CoifCap01_m01_C"], [1, "Ankle boots", 1.6, 1, "BootsAnkle01_m01_C"],
    [1, "Gauntlets", 1.4, 1, "Gauntlets01_m01_D2"], [1, "Hose", 0.9, 1, "HoseJoined01_m01_D"],
    [2, "Bread", 0.4, 6, "Bread"], [2, "Deer meat", 0.3, 9, "deerMeat"], [2, "Saviour schnapps", 0.3, 4, "potion_saviourSchnapps"],
    [2, "Apple", 0.2, 12, "Apple"], [4, "Chamomile", 0.1, 35, "herb_chamomile"], [4, "Comfrey", 0.1, 20, "herb_comfrey"],
    [5, "Lockpick", 0.1, 18, "player_lockpick"], [5, "Pewter jug", 1.8, 2, "loot_pewterJug"], [5, "Bandage", 0.1, 10, "bandage_classic"],
    [5, "Sack of nails", 2.5, 1, "loot_sackOfNails"],
]
FOOD = [
    [2, "Smoked horse meat", 0.5, 59, "deerMeat"], [2, "Dried horse meat", 0.5, 58, "foodInSack"], [2, "Drinking water", 1.0, 19, "potion_saviourSchnapps"],
    [2, "Dried wolf meat", 0.2, 153, "foodInSack"], [2, "Strong Bane poison", 0.3, 60, "potion_saviourSchnapps"], [2, "Dried chicken", 0.3, 24, "foodInSack"],
    [2, "Schnapps", 0.5, 11, "potion_saviourSchnapps"], [2, "Dried roe deer meat", 0.3, 19, "foodInSack"], [2, "Smoked venison", 0.3, 21, "deerMeat"],
    [2, "Henry's Artemisia", 0.3, 45, "potion_saviourSchnapps"], [2, "Smoked venison", 0.3, 15, "deerMeat"], [2, "Smoked boar meat", 0.3, 15, "deerMeat"],
    [2, "Moonshine", 0.5, 8, "potion_saviourSchnapps"], [2, "Cabbage", 0.6, 6, "Apple"], [2, "Strong Marigold", 0.3, 9, "potion_saviourSchnapps"],
    [2, "Henry's Marigold", 0.3, 9, "potion_saviourSchnapps"], [2, "Henry's Bane", 0.3, 9, "potion_saviourSchnapps"], [2, "Smoked deer ribs", 1.2, 2, "deerMeat"],
    [2, "Dried venison", 0.3, 7, "foodInSack"], [2, "Dried perch", 0.2, 7, "foodInSack"], [2, "Wine", 0.3, 5, "potion_saviourSchnapps"],
    [2, "Dried wolf meat", 0.2, 7, "foodInSack"], [2, "Dried hare meat", 0.2, 7, "foodInSack"], [2, "Mead", 0.3, 4, "potion_saviourSchnapps"],
    [2, "Strong Buck's", 0.3, 4, "potion_saviourSchnapps"], [2, "Henry's Fox", 0.3, 6, "potion_saviourSchnapps"], [2, "Dried cheese", 0.2, 8, "Bread"],
    [2, "Wolf meat", 0.5, 19, "deerMeat"], [2, "Smoked beef", 0.3, 4, "deerMeat"], [2, "Walnuts", 0.05, 16, "Apple"],
] + [[2, n, 0.3, c, "potion_saviourSchnapps"] for n, c in [("Quickfinger", 3), ("Saviour", 3), ("Weak", 3), ("Smoked hare", 2), ("Henry's", 2), ("Moonshine", 2), ("Bane poison", 2), ("Fine wine", 2), ("Strong", 2), ("Mead", 2)]] + [[2, "Dried", 0.2, c, "foodInSack"] for c in (2, 2, 1, 1, 1)] + [[2, "Herbs", 0.1, c, "herb_chamomile"] for c in (3, 2, 2, 1, 1, 1, 1)]
CHEST = [
    [0, "Cuman shield", 4.0, 1, "Shield_Cuman_01_A"], [0, "Mace", 2.6, 2, "mace02"], [0, "Crude arrows", 0.08, 60, "arrow_crude"],
    [1, "Arm plates", 3.1, 1, "ArmPlate01_m01_C2"], [1, "Hood", 0.5, 2, "F_Hood01_m01_C"], [1, "Gloves", 0.3, 1, "Gloves01_m01_B1"],
    [2, "Food sack", 6.0, 1, "foodInSack"], [2, "Bread roll", 0.2, 8, "breadroll"], [5, "Copper jug", 1.5, 3, "loot_copperJug"],
    [5, "Sack of nails", 2.5, 2, "loot_sackOfNails"], [4, "Dandelion", 0.1, 14, "herb_dandelion"],
]


def rgba(c, a):
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255, int(round(a * 2.55)))


def layout(scene):
    with tempfile.TemporaryDirectory() as d:
        a, b = os.path.join(d, "in.json"), os.path.join(d, "out.json")
        json.dump(scene, open(a, "w"))
        subprocess.run(["node", os.path.join(HERE, "test_layout.js"), "--layout", a, b], check=True, capture_output=True)
        return json.load(open(b))


def icon(z, name):
    try:
        return Image.open(io.BytesIO(z.read(f"Libs/UI/Textures/Icons/Items/{name}_icon.dds"))).convert("RGBA")
    except KeyError:
        return None


def fit(d, text, font, w):
    while text and d.textlength(text, font=font) > w:
        text = text[:-1]
    return text


SEL = (0, 3, 1808, 94)
HEAD = (1, 103, 1806, 55)


def paint(img, tex, x, y, w, h, strip):
    """Paints a list-texture strip into a box the way WM_Paint does (two halves keep both ragged ends)."""
    sx, sy, sw, sh = strip
    w, h = int(round(w)), int(round(h))
    if w < 2 or h < 2:
        return
    sc = h / sh
    band = tex.crop((sx, sy, sx + sw, sy + sh)).resize((max(1, int(round(sw * sc))), h), Image.LANCZOS)
    if band.width >= w:
        half = w // 2
        img.alpha_composite(band.crop((0, 0, half, h)), (int(x), int(y)))
        img.alpha_composite(band.crop((band.width - (w - half), 0, band.width, h)), (int(x) + half, int(y)))
    else:
        img.alpha_composite(band.resize((w, h), Image.LANCZOS), (int(x), int(y)))


GROUP_COLORS = [0x3F6FC4, 0xC8502C, 0x3F9A3A, 0xB03A5A, 0x7AA83C, 0xDC8E24]
GROUND = (0x1C, 0x1E, 0x14)


def shade(c, f):
    return tuple(min(255, round(((c >> s) & 255) * f)) for s in (16, 8, 0))


def cushion(img, x, y, w, h, c):
    """Radial light from the upper left, darker to the edges (WM_Cushion: stops 1.5x / 1x / 0.5x at 0 / 120 / 255)."""
    w, h = int(round(w)), int(round(h))
    if w < 1 or h < 1:
        return
    import math
    hi, mid, lo = shade(c, 1.5), shade(c, 1.0), shade(c, 0.5)
    cx, cy = -0.45 * w + w * 1.7 / 2, -0.5 * h + h * 1.7 / 2
    rx, ry = w * 1.7 / 2, h * 1.7 / 2
    tile = Image.new("RGB", (w, h))
    px = tile.load()
    for yy in range(h):
        for xx in range(w):
            r = min(1.0, math.hypot((xx - cx) / rx, (yy - cy) / ry))
            tt = r * 255
            if tt < 120:
                a, b, u = hi, mid, tt / 120
            else:
                a, b, u = mid, lo, (tt - 120) / 135
            px[xx, yy] = tuple(int(a[i] + (b[i] - a[i]) * u) for i in range(3))
    img.paste(tile, (int(round(x)), int(round(y))))


def draw(scene, z, out, scale, header, button, tip_on=None):
    L = layout(scene)
    k, light, R = L["k"], L["light"], L["R"]
    S = lambda v: v * scale
    tex = Image.open(io.BytesIO(z.read("Libs/UI/Textures/Apse/list.dds"))).convert("RGBA")
    band = S(90 * k)
    W, H = int(S(R["w"]) + S(40 * k)), int(S(R["h"]) + band + S(20 * k))
    bg = (206, 190, 148) if light else (27, 21, 17)
    img = Image.new("RGBA", (W, H), bg + (255,))
    ox, oy = S(20 * k), band
    INK = (0x2A, 0x2A, 0x2A)
    f = lambda sz, bold=False: ImageFont.truetype(FONTB if bold else FONT, max(8, int(S(sz))))
    d = ImageDraw.Draw(img, "RGBA")
    d.rectangle([0, 0, W, band - S(10 * k)], fill=(38, 52, 86) if light else (20, 16, 12))
    d.text((W * 0.32, (band - S(10 * k)) / 2 - S(22 * k)), button, font=f(40 * k), fill=(0xE3, 0xCF, 0xA0))
    shown = L["shown"]
    left, right, over = header(shown)
    paint(img, tex, ox + S(R["x"]), oy + S(R["y"]), S(R["w"]), S(L["hh"]), HEAD)
    d = ImageDraw.Draw(img, "RGBA")
    fh = f(46 * k, True)
    ty = oy + S(R["y"] + (L["hh"] - 46 * k * 1.3) / 2)
    d.text((ox + S(R["x"] + 14 * k), ty), left, font=fh, fill=INK)
    d.text((ox + S(R["x"] + R["w"] - 14 * k) - d.textlength(right, font=fh), ty), right, font=fh, fill=(0x99, 0, 0) if over else INK)
    d.rectangle([ox + S(R["x"]), oy + S(L["top"]), ox + S(R["x"] + R["w"]), oy + S(R["y"] + R["h"])], fill=GROUND)
    boxes = []
    for g in L["groups"]:
        if g.get("label"):
            fl = f(30 * k, True)
            lab = f"{NAMES[g['g']].title()}  {g['v']:.1f}"
            d.text((ox + S(g["lx"] + 4 * k), oy + S(g["ly"])), lab, font=fl, fill=(0xE3, 0xCF, 0xA0))
        for b in g["kids"]:
            boxes.append(b)
            gap = 1.5 * k
            x, y, w, h = b["x"] + gap, b["y"] + gap, b["w"] - 2 * gap, b["h"] - 2 * gap
            if w < 1 or h < 1:
                continue
            fct = [1, 0.78, 1.18, 0.62][b.get("cat", 0) % 4]
            c = shade(GROUP_COLORS[g["g"]], fct)
            cushion(img, ox + S(x), oy + S(y), S(w), S(h), (c[0] << 16) | (c[1] << 8) | c[2])
            d = ImageDraw.Draw(img, "RGBA")
            big = w >= 300 * k and h >= 200 * k
            ls = (38 if big else 30) * k
            pad = 6 * k
            textH = 0
            if w >= 120 * k and h >= ls * 1.5:
                fk, fn = f(ls, True), f(ls)
                kt = f"{b['v']:.1f}"
                kw = d.textlength(kt, font=fk)
                d.text((ox + S(x + w - pad) - kw, oy + S(y + pad * 0.4)), kt, font=fk, fill=(0xFF, 0xEE, 0x99))
                room = S(w - pad * 2 - 12 * k) - kw
                if room >= S(50 * k):
                    lab = b["name"] + (f" x{b['n']}" if b["n"] > 1 else "")
                    if d.textlength(lab, font=fn) > room - S(4 * k):
                        while lab and d.textlength(lab + "…", font=fn) > room - S(4 * k):
                            lab = lab[:-1]
                        lab += "…"
                    if len(lab) >= 5 or lab == b["name"]:
                        d.text((ox + S(x + pad), oy + S(y + pad * 0.4)), lab, font=fn, fill=(255, 255, 255))
                textH = ls * 1.35
            top = y + pad * 0.4 + textH
            isz = min(w - 2 * pad, y + h - pad - top, 220 * k)
            if isz >= 64 * k and b.get("icon"):
                im = icon(z, b["icon"])
                if im:
                    s = int(S(isz))
                    img.alpha_composite(im.resize((s, s), Image.LANCZOS), (int(ox + S(x + (w - isz) / 2)), int(oy + S(top + (y + h - pad - top - isz) / 2))))
                    d = ImageDraw.Draw(img, "RGBA")
    # hover: white outline + dark card on a small item
    tb = tip_on or sorted(boxes, key=lambda b: b["w"] * b["h"])[len(boxes) // 3]
    d.rectangle([ox + S(tb["x"] + 2 * k), oy + S(tb["y"] + 2 * k), ox + S(tb["x"] + tb["w"] - 2 * k), oy + S(tb["y"] + tb["h"] - 2 * k)], outline=(255, 255, 255), width=max(2, int(S(4 * k))))
    ts = 40 * k
    title = tb["name"]
    lines = [f"x{tb['n']}   {tb['each']:.1f} each   {tb['v']:.1f} total" if tb["n"] > 1 else f"{tb['v']:.1f}"]
    ft, fl2 = f(ts, True), f(ts * 0.85)
    tw = max([d.textlength(title, font=ft)] + [d.textlength(x, font=fl2) for x in lines]) + S(32 * k)
    th = S(16 * k * 0.6 + ts * 1.35 + ts * 1.15 * len(lines) + 16 * k * 0.7)
    cx, cy = ox + S(tb["x"] + tb["w"] * 0.5 + 28 * k), oy + S(tb["y"] + tb["h"] * 0.5 + 28 * k)
    if cx + tw > ox + S(R["x"] + R["w"]):
        cx -= tw + S(44 * k)
    if cy + th > oy + S(R["y"] + R["h"]):
        cy -= th + S(44 * k)
    d.rectangle([cx, cy, cx + tw, cy + th], fill=(0x16, 0x12, 0x0D, 235), outline=(0x8A, 0x73, 0x46, 255), width=max(1, int(S(2 * k))))
    d.text((cx + S(16 * k), cy + S(16 * k * 0.6)), title, font=ft, fill=(255, 255, 255))
    d.text((cx + S(16 * k), cy + S(16 * k * 0.6 + ts * 1.35)), lines[0], font=fl2, fill=(0xE3, 0xCF, 0xA0))
    img.convert("RGB").save(out)
    print("saved", out, img.size)


ALL = FOOD + [
    [1, "Saxon hauberk", 19, 2, "CollarChain01_m01_C3"], [1, "Italian hauberk", 15, 1, "Cuirass01_m02_C2"], [1, "Italian bascinet", 13.5, 1, "KettleHat01_m02_E2"],
    [1, "Milanese breastplate", 12.7, 1, "Cuirass01_m01_C2"], [1, "Long pourpoint", 14.6, 1, "GambesonLong01_m01_C3"], [1, "Gambeson", 12.5, 1, "GambesonLong01_m02_C3"],
    [1, "Noble brigandine", 11.8, 1, "Cuirass01_m03_C2"], [1, "Half plate", 11, 1, "ArmPlate01_m01_C2"],
] + [[1, n, w, 1, "CoifCap01_m01_C"] for n, w in [("Mail coif", 6.7), ("Mail coif", 6.7), ("Bell-sleeve", 5.2), ("Noble hose", 5.5), ("Padded coif", 5.7), ("Silesian", 5.3), ("Kettle hat", 6.2)] + [("Boots", 3)] * 6 + [("Cap", 2)] * 6 + [("Ring", 1)] * 8] + [
    [5, "Wolf pelt", 3.5, 12, "loot_cloth"], [5, "Sheepskin", 3, 7, "loot_cloth"], [5, "Spade", 4, 4, "loot_pewterJug"], [5, "Horse hide", 6, 2, "loot_cloth"],
    [5, "Roe-deer hide", 3.5, 6, "loot_cloth"], [5, "Hare pelt", 1, 19, "loot_cloth"], [5, "Boar hide", 4, 4, "loot_cloth"], [5, "Soap", 0.2, 58, "bandage_classic"],
] + [[5, "Trinket", w, 1, "loot_comb"] for w in (7, 5, 4.5, 3.9, 3, 3, 2.4, 2, 1.5, 1.4, 1.3, 1.2, 1, 1, 1, 0.8, 0.5)] + [
    [4, "Dried mint", 0.1, 101, "herb_chamomile"], [4, "Scrap metal", 1, 10, "loot_sackOfNails"], [4, "Dried valerian", 0.1, 91, "herb_comfrey"],
    [4, "Dried nettle", 0.1, 67, "herb_dandelion"], [4, "Dried mallow", 0.1, 70, "herb_chamomile"], [4, "Dried St. John's", 0.1, 74, "herb_comfrey"],
    [4, "Antler", 0.6, 12, "loot_comb"], [4, "Pig skin", 0.6, 13, "loot_cloth"], [4, "Cobweb", 0.1, 57, "herb_dandelion"], [4, "Steel", 1, 7, "loot_sackOfNails"],
] + [[4, "Herb", w, 1, "herb_chamomile"] for w in (3.5, 3.2, 3.7, 3.7, 1.8, 2.1, 1.3, 1.2, 1.4, 2.7, 1.1, 1.4)] + [
    [0, "Torch", 1, 9, "mace01"], [0, "Rider's hammer", 6.3, 1, "mace02"], [0, "Knight shield", 6.8, 1, "Shield_Cuman_01_A"], [0, "Adorned axe", 5.7, 1, "mace03"],
    [0, "Elm longbow", 4, 1, "crossbowLightCheap01"], [0, "Piercing arrows", 0.1, 32, "arrow_cutting"], [0, "Falchion", 2.9, 1, "longswordCommon"],
] + [[0, "Arrows", w, 1, "arrow_crude"] for w in (3.2, 3, 3.1, 3.4, 2.9, 2.2, 1.8, 1, 1, 1)]


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    z = zipfile.ZipFile(PAK)
    cap, car = 78.0, None
    hen = sum(w * n for _, _, w, n, _ in HENRY)

    def inv_header(shown):
        return (f"{shown:.1f} in this tab", f"{hen - cap:.1f} over" if hen > cap else f"{cap - hen:.1f} free", hen > cap)

    draw({"R": {"x": 0, "y": 0, "w": 1526, "h": 1526}, "k": 1, "light": True, "items": HENRY + [[3, "Letter", 0, 1, None]]}, z,
         os.path.join(out, "weightmap_inventory.png"), 1.0, inv_header, "List")
    draw({"R": {"x": 0, "y": 0, "w": 1880, "h": 1526}, "k": 1, "light": True, "items": ALL}, z,
         os.path.join(out, "weightmap_all.png"), 1.0, lambda shown: (f"{shown:.1f} in this tab", "99.0 over", True), "List")
    draw({"R": {"x": 0, "y": 0, "w": 1600, "h": 1526}, "k": 1, "light": True, "items": FOOD}, z,
         os.path.join(out, "weightmap_food.png"), 1.0, lambda shown: (f"{shown:.1f} in this tab", "76.0 free", False), "List")
    draw({"R": {"x": 0, "y": 0, "w": 800, "h": 440}, "k": 0.62, "light": False, "items": CHEST}, z,
         os.path.join(out, "weightmap_chest.png"), 2.0, lambda shown: (f"{shown:.1f} in this tab", f"you can carry {max(0, cap - hen):.1f} more", False), "List")


main()
