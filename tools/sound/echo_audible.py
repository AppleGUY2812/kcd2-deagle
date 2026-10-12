"""How much of each test 3 echo you can actually hear over the bang (9f, 2026-10-12 19:56 UTC; this is the measurement
behind "about three quarters of our echo arrives in the first 0.3 s" and "17-19 dB quieter than Ready or Not").
Runs sound_test3.py's echo maker without writing files, then per 10 ms window (K-weighted, both ears): "heard" = share of
the echo's energy in windows where it is within 6 dB of the bang or louder; levels per window 0-.1 / .1-.3 / .3-1 / 1 s+.
usage: echo_audible.py [same --ron / --decho / --test2 options as sound_test3.py]"""
import os, sys, numpy as np
sys.argv = [sys.argv[0], "/nonexistent-unused-out"] + sys.argv[1:]   # sound_test3 needs an out folder; nothing is written
_src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sound_test3.py")
_code = open(_src).read().split("# ---------------- write the files")[0]
_code = _code.replace('os.makedirs(f"{OUT}/blind test files", exist_ok=True)\n', "")
exec(compile(_code, _src, "exec"))
def env(x, ms=10):                       # level per 10 ms window, K-weighted, both ears
    n = int(ms / 1000 * SR); m = len(x) // n
    return np.array([loud(x[i*n:(i+1)*n]) + 1e-15 for i in range(m)])
def report(name, bang, echo):
    n = max(len(bang), len(echo)); b, e = env(pad(bang, n)), env(pad(echo, n))
    heard = e > b * 10 ** (-6 / 10)      # echo within 6 dB of the bang in that window or above it = you can hear it
    share = e[heard].sum() / e.sum()
    t = np.arange(len(e)) * .01
    def lvl(a, z):
        k = (t >= a) & (t < z); return 10 * np.log10(e[k].sum() / b[:10].sum()) if k.any() and e[k].sum() > 0 else -99
    print(f"{name:34s} heard {100*share:4.0f}% | echo vs bang: 0-.1s {lvl(0,.1):6.1f}  .1-.3s {lvl(.1,.3):6.1f}  .3-1s {lvl(.3,1):6.1f}  1s+ {lvl(1,9):6.1f} dB")
colt_punch = cut(fire, .09, .13).mean(1)
print("COLT ROUNDS (ours vs Ready or Not, same loudness)")
for k, v in REF.items():
    r = byname[k]
    ours = np.stack([conv(colt_punch, r["ir"][:, 0]), conv(colt_punch, r["ir"][:, 1])], 1)
    ours *= np.sqrt(loud(ron[k]) / loud(ours))
    report(f"{k} - ours", fire, ours); report(f"{k} - Ready or Not", fire, ron[k])
print("DEAGLE (new)")
for r in results:
    if np.isfinite(r["phys"]): report(r["sc"].name, dry, r["wet"] * 10 ** ((r["final"] - r["phys"]) / 20))
