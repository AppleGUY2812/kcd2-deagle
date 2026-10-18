"""KCD2's post chain for one HDR colour, read from its own shaders and time-of-day data, so a colour can be checked
(and solved for) the way KCD2 shows it on screen. Owner: Claude 1.

Sources (Engine/Shaders.pak and Data/Levels/trosecko/level.pak, extracted to E:/Tools/deagle-work/tonemap/):
- HDRPostProcess.cfx:331-387 HDRComposition: exposure x scene, saturation + colour balance, ToneMap_FilmMapping,
  LinearToSRGB, colour chart (TexColorChart2D), film grain (ignored here: noise around 0.5).
- imageLib.cfi:39-54 ComputeExposure: EV100 from the adapted illuminance, auto compensation, clamp to [EV Min,
  EV Max] - photo-mode shift (.w, 0 in play); exposure = 1 / (1.2 * 2^EV / 10000).
- CommonColorTransforms.cfi:296-328 ToneMap_FilmMapping: Hable's curve, shoulder 0.22 x, linear 0.3 x, angle 0.1,
  toe 0.2, toe numerator 0.01 x, toe denominator 0.3, divided by the curve at the white point, saturated.
- imageLib.cfi:7-31 TexColorChart2D: a 16^3 chart as 256x16, red across a slice, green down, blue = slice.
- CommonMath.cfi:237-241 LinearToSRGB.
- Time of day: mission_mission0.xml <TimeOfDay>: the base set and 12+ weather profiles, each variable with a
  Spline and a <ColorSpaceOverride Type="1"> spline. WHGame.dll 0x8B2844 reads the override into the variable's
  spline slot 1 (0x20E00C0: slot = var + 0x30 + type * 8). Which slot the game evaluates isn't traced; the saved
  "Value" of every variable equals the override's value at the saved time, so the editor ran on Type 1: used here.

    python3 tools/ron/kcd2_tonemap.py       prints the exposures and a few colours, day and night
"""
import math, re, struct, functools
import numpy as np

WORK = '/mnt/e/Tools/deagle-work/tonemap'
MISSION = WORK + '/trosecko/mission_mission0.xml'
LUTS = WORK + '/Textures/colorcharts'
LIGHT_UNIT_SCALE = 10000.0      # imageLib.cfi:37: HDR 1.0 = 10000 cd/m2


@functools.lru_cache(None)
def _tod():
    s = open(MISSION, encoding='utf-8', errors='replace').read()
    blocks = {}
    first = s.find('<Profile ')
    blocks['base'] = (s[:first], None)
    for m in re.finditer(r'<Profile Name="([^"]+)"[^>]*?ColorGradingLut="([^"]*)"[^>]*>(.*?)</Profile>', s, re.S):
        blocks[m.group(1)] = (m.group(3), m.group(2))
    return blocks


def _keys(txt):
    out = []
    for k in txt.strip(',').split(','):
        if not k: continue
        t, rest = k.split(':', 1)
        if rest.startswith('('):
            v, _ = rest[1:].split(')', 1); v = [float(x) for x in v.split(':')]
        else:
            v = [float(rest.split(':')[0])]
        out.append((float(t), v))
    return sorted(out)


def _eval(keys, t):   # linear between keys, held at the ends (the engine's Hermite tangents aren't modelled)
    if t <= keys[0][0]: return np.array(keys[0][1])
    for (t0, v0), (t1, v1) in zip(keys, keys[1:]):
        if t <= t1:
            f = (t - t0) / max(t1 - t0, 1e-9)
            return np.array(v0) + (np.array(v1) - np.array(v0)) * f
    return np.array(keys[-1][1])


def tod(name, hour, profile='base', override=True):
    """a time-of-day variable at an hour, from a weather profile (falls back to the base set), Type 1 keys if any"""
    for p in ([profile, 'base'] if profile != 'base' else ['base']):
        txt = _tod()[p][0]
        m = re.search(r'<Variable Name="%s"[^>]*>(.*?)</Variable>' % re.escape(name), txt, re.S)
        if not m: continue
        body = m.group(1)
        o = re.search(r'<ColorSpaceOverride Type="1" Keys="([^"]*)"', body)
        k = re.search(r'<Spline Keys="([^"]*)"', body)
        keys = _keys(o.group(1)) if (override and o) else _keys(k.group(1))
        v = _eval(keys, (hour % 24) / 24.0)
        return v if len(v) > 1 else float(v[0])
    raise KeyError(name)


def profiles():
    return [p for p in _tod() if p != 'base']


@functools.lru_cache(None)
def lut(file):
    b = open(f'{LUTS}/{file.split("/")[-1].replace(".tif", ".dds")}', 'rb').read()
    h, w = struct.unpack_from('<II', b, 12)
    px = np.frombuffer(b[128:128 + w * h * 4], np.uint8).reshape(h, w, 4)   # B G R A (masks R 0xff0000)
    return px[..., [2, 1, 0]].astype(np.float64) / 255.0


def apply_lut(c, chart):   # TexColorChart2D (imageLib.cfi:7-31), bilinear in a slice, linear between slices
    c = np.clip(np.asarray(c, float), 0, 1)
    n = 16.0
    r, g, b = c[..., 0] * (n - 1), c[..., 1] * (n - 1), c[..., 2] * (n - 1)
    def sample(x, y):   # texel centres: x in 0..255, y in 0..15
        x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int)
        x1 = np.minimum(x0 + 1, chart.shape[1] - 1); y1 = np.minimum(y0 + 1, chart.shape[0] - 1)
        fx = (x - x0)[..., None]; fy = (y - y0)[..., None]
        return (chart[y0, x0] * (1 - fx) + chart[y0, x1] * fx) * (1 - fy) + (chart[y1, x0] * (1 - fx) + chart[y1, x1] * fx) * fy
    # the shader's slice = (b * 15/16) * 16 = b * 15; red inside the slice, clamped there by the bias
    s0 = np.floor(b).astype(int); fs = (b - s0)[..., None]; s1 = np.minimum(s0 + 1, 15)
    return sample(r + s0 * 16, g) * (1 - fs) + sample(r + s1 * 16, g) * fs


def film(x, curve):   # ToneMap_FilmMapping, CommonColorTransforms.cfi:306-328; curve = shoulder, midtones, toe, white
    sh, mid, toe, w = curve
    s1, l1, l2, t1, t2, t3 = 0.22 * sh, 0.3 * mid, 0.1, 0.2, 0.01 * toe, 0.3
    f = lambda c: (c * (s1 * c + l2 * l1) + t1 * t2) / (c * (s1 * c + l1) + t1 * t3) - t2 / t3
    return np.clip(f(np.maximum(np.asarray(x, float), 0)) / f(w), 0, 1)


def film_inv(y, curve, lo=0.0, hi=None):   # numeric inverse (monotonic), 60 bisection steps
    y = np.asarray(y, float)
    hi = curve[3] if hi is None else hi
    a = np.full(y.shape, lo); b = np.full(y.shape, float(hi))
    for _ in range(60):
        m = (a + b) / 2
        lt = film(m, curve) < y
        a = np.where(lt, m, a); b = np.where(lt, b, m)
    return (a + b) / 2


def srgb(x):   # CommonMath.cfi:237
    x = np.clip(np.asarray(x, float), 0, 1)
    return np.where(x < 0.0031308, 12.92 * x, 1.055 * np.power(x, 1 / 2.4) - 0.055)


def srgb_inv(x):
    x = np.clip(np.asarray(x, float), 0, 1)
    return np.where(x <= 0.04045, x / 12.92, np.power((x + 0.055) / 1.055, 2.4))


def exposure(illum_lux, ev_min, ev_max, comp, shift=0.0):   # imageLib.cfi:39-54; illum in lux
    I = illum_lux / LIGHT_UNIT_SCALE
    ev = math.log2(max(I, 1e-12) * LIGHT_UNIT_SCALE * 100 / 330)
    ev -= (min(max(math.log10(I * LIGHT_UNIT_SCALE + 1), 0.1), 5.2) - 3.0) * 0.5 * comp
    ev = min(max(ev, ev_min), ev_max) - shift
    return 1.0 / (1.2 * 2 ** ev / LIGHT_UNIT_SCALE), ev


class Scene:
    """KCD2's post settings at an hour under a weather profile; illum = the eye's adapted illuminance in lux (outdoors
    by day this is far above what EV Max allows, so the day exposure is EV Max's)"""
    def __init__(self, hour, illum_lux, profile='base', override=True):
        g = lambda n: tod(n, hour, profile, override)
        self.curve = (g('Film curve shoulder scale'), g('Film curve midtones scale'), g('Film curve toe scale'),
                      g('Film curve whitepoint'))
        self.exposure, self.ev = exposure(illum_lux, g('EV Min'), g('EV Max'), g('EV Auto compensation'))
        self.saturation, self.balance = g('Saturation'), np.array(g('Color balance'))
        lutfile = _tod()[profile][1] if profile != 'base' else None
        self.chart = lut(lutfile) if lutfile and lutfile != '0' else None   # "0": no chart
        self.name = f'{profile} {hour:g}h {illum_lux:g} lx: EV {self.ev:.2f}, exposure {self.exposure:.3g}'

    def graded(self, hdr):   # HDRComposition up to the curve's input
        c = np.asarray(hdr, float) * self.exposure
        l = c @ np.array([0.2126, 0.7152, 0.0722])
        c = l[..., None] + self.saturation * (c - l[..., None])
        return c * self.balance

    def display(self, hdr):   # HDR scene colour -> display sRGB 0..1 (no grain, no bloom, no vignette)
        d = srgb(film(self.graded(hdr), self.curve))
        return apply_lut(d, self.chart) if self.chart is not None else d

    def solve(self, target_srgb, iters=4):
        """the HDR scene colour (before exposure) KCD2 shows as target_srgb (per channel; the chart is undone by
        fixed-point steps, saturation / balance by their inverse)"""
        t = np.asarray(target_srgb, float)
        want = t.copy()
        for _ in range(iters if self.chart is not None else 1):
            c = film_inv(srgb_inv(want), self.curve) / self.balance
            l = c @ np.array([0.2126, 0.7152, 0.0722])
            c = l[..., None] + (c - l[..., None]) / max(self.saturation, 1e-6)
            hdr = np.maximum(c, 0) / self.exposure
            if self.chart is None: break
            want = np.clip(want + (t - self.display(hdr)), 0, 1)
        return hdr


DAY = dict(hour=13.0, illum_lux=80000.0)   # the sun's ToD intensity at 13:00 (80000), outdoors
NIGHT = dict(hour=23.0, illum_lux=0.5)     # moonlit: clamps at EV Min
# the scene the flash cards are solved for (flash_bake.py) and checked in (flash_check.py): daytime outdoors, the eye
# adaptation on the profile's EV Max (10-11.5 over the 22 open-world profiles; foggy_drizzly = the median 11.0, with
# neutral balance and saturation). Other days are within a stop; at night (EV Min 7) the flash shows 16x brighter
FLASH_SCENE = dict(profile='foggy_drizzly', **DAY)


if __name__ == '__main__':
    for prof in ['base'] + profiles()[:6]:
        for k, kw in (('day', DAY), ('night', NIGHT)):
            s = Scene(profile=prof, **kw)
            print(f'{k:5s} {s.name}  curve {tuple(round(float(x), 3) for x in s.curve)}  sat {s.saturation:.2f} '
                  f'balance {np.round(s.balance, 3)}  lut {"yes" if s.chart is not None else "no"}')
    s = Scene(profile=profiles()[5], **DAY)
    for hdr in ([0.01, 0.003, 0.001], [0.1, 0.03, 0.01], [1, 0.3, 0.1]):
        d = s.display(np.array(hdr)); back = s.solve(d)
        print('hdr', hdr, '-> srgb', np.round(d * 255).astype(int), '-> solved back', np.round(back, 4))
