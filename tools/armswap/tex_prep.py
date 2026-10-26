# Demo-only stand-in for KCD2's clothing shader: per pixel, the _id mask names a feature slot (r + 3g + 9b, each
# channel 0 / half / full); the slot's library material gets an approximate colour (names from CharacterComponent.xml;
# the real ones are textures + saturation / brightness, not reproduced). Writes <item>_albedo.png (colour x the
# greyscale _diff detail, alpha = the _diff alpha for alpha-tested items) and <item>_metal.png.
import sys, numpy as np
from PIL import Image
T = sys.argv[1]
MAT = {  # approximate base colour (linear-ish sRGB 0..1), metallic, roughness
    "Iron": ((0.55, 0.55, 0.56), 1, 0.45), "Scratched": ((0.6, 0.6, 0.6), 1, 0.4), "Pewter": ((0.62, 0.62, 0.6), 1, 0.3),
    "Polished": ((0.75, 0.75, 0.75), 1, 0.2), "Fine_red": ((0.42, 0.05, 0.05), 0, 0.7), "Basic_red_dark": ((0.28, 0.04, 0.04), 0, 0.75),
    "Basic_beige_medium": ((0.55, 0.46, 0.33), 0, 0.8), "Basic_brown_medium": ((0.32, 0.2, 0.11), 0, 0.75),
    "Full_Grain_03_brown_light": ((0.45, 0.29, 0.15), 0, 0.6), "Full_Grain_03_brown_dark": ((0.2, 0.12, 0.07), 0, 0.6),
    "Gambeson_Line_Black": ((0.07, 0.07, 0.07), 0, 0.9), "Gambeson_Diamond_Brown": ((0.3, 0.2, 0.12), 0, 0.9),
}
SLOTS = {
    "gauntlets01_m01_hands": {14: "Polished", 15: "Full_Grain_03_brown_dark", 16: "Full_Grain_03_brown_light", 17: "Scratched"},
    "brigandinearm05_m01_arms": {0: "Gambeson_Line_Black", 1: "Fine_red", 2: "Gambeson_Line_Black", 3: "Gambeson_Diamond_Brown",
        4: "Fine_red", 5: "Gambeson_Diamond_Brown", 7: "Pewter", 9: "Basic_beige_medium", 10: "Pewter", 13: "Iron",
        18: "Basic_brown_medium", 21: "Polished", 23: "Pewter", 26: "Basic_red_dark"},
}
def detail(name):
    d = np.asarray(Image.open(f"{T}/{name}_diff.png").convert("RGBA")).astype(np.float32) / 255
    lum = d[..., :3].mean(-1)
    return np.clip(lum / max(np.median(lum[lum > 0.02]), 0.05), 0, 2.0), d[..., 3]
for item, slots in SLOTS.items():
    det, alpha = detail(item)
    idm = np.asarray(Image.open(f"{T}/{item}_id.png").convert("RGB").resize(det.shape[::-1], Image.NEAREST)).astype(np.float32) / 255
    q = np.rint(idm * 2).astype(int)
    slot = q[..., 0] + 3 * q[..., 1] + 9 * q[..., 2]
    col = np.zeros(det.shape + (3,), np.float32); met = np.zeros(det.shape, np.float32); rough = np.full(det.shape, 0.7, np.float32)
    for s, m in slots.items():
        c, mt, r = MAT[m]; sel = slot == s
        col[sel] = c; met[sel] = mt; rough[sel] = r
    unk = ~np.isin(slot, list(slots)); col[unk] = (0.3, 0.3, 0.3)
    print(item, "pixels per slot", {int(s): int((slot == s).sum()) for s in np.unique(slot)})
    shade = det.copy()
    try:   # metals: KCD2 keeps their diffuse near black and puts the look in _spec; use the spec brightness there
        sp = np.asarray(Image.open(f"{T}/{item}_spec.png").convert("RGB").resize(det.shape[::-1])).astype(np.float32).mean(-1) / 255
        m = met > 0.5
        shade[m] = np.clip(sp[m] / max(np.median(sp[m]), 0.05), 0.4, 1.6)
    except FileNotFoundError:
        pass
    alb = np.clip(col * shade[..., None], 0, 1)
    Image.fromarray((np.dstack([alb, alpha]) * 255).astype(np.uint8), "RGBA").save(f"{T}/{item}_albedo.png")
    Image.fromarray((np.dstack([met, rough, rough]) * 255).astype(np.uint8), "RGB").save(f"{T}/{item}_metal.png")
det, alpha = detail("gambesonlong03_m01_arms")
c = np.array(MAT["Gambeson_Line_Black"][0]) * 2.2   # black quilting, lifted so the stitching reads
Image.fromarray((np.dstack([np.clip(c * det[..., None], 0, 1), alpha]) * 255).astype(np.uint8), "RGBA").save(f"{T}/gambesonlong03_m01_arms_albedo.png")
Image.fromarray(np.dstack([np.zeros_like(det), np.full_like(det, 0.9), np.full_like(det, 0.9)]).__mul__(255).astype(np.uint8), "RGB").save(f"{T}/gambesonlong03_m01_arms_metal.png")
for n in ("gauntlets01_m01_hands", "brigandinearm05_m01_arms", "gambesonlong03_m01_arms"):   # BC5 normal: x, y -> z; DirectX -> OpenGL green
    a = np.asarray(Image.open(f"{T}/{n}_ddna.png").convert("RGB")).astype(np.float32) / 255
    x, y = a[..., 0] * 2 - 1, a[..., 1] * 2 - 1
    z = np.sqrt(np.clip(1 - x * x - y * y, 0, 1))
    Image.fromarray((np.dstack([x * 0.5 + 0.5, -y * 0.5 + 0.5, z * 0.5 + 0.5]) * 255).astype(np.uint8), "RGB").save(f"{T}/{n}_normal.png")
print("tex ok")
