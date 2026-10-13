"""Listening pack: Ready or Not's MK V (.50 AE, internal name Grizzly) as our Deagle sound.
Each place = one MK V shot + Ready or Not's own MK V echo file for that kind of place, mixed as the files come
(same as the Colt rounds of tests 3-4). Nothing generated. Usage: mk5_pack.py <out dir>
Sounds: tools/ron/fsbx.py Weapons.bank -> E:/Tools/deagle-work/sound/ron_grizzly (private use, not in git)."""
import base64, html, json, os, sys, wave
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from measure4 import rd, SR
G = os.environ.get("RON_GRIZZLY", "/mnt/e/Tools/deagle-work/sound/ron_grizzly")
DE = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "assets/build/sounds/decho")
OUT = sys.argv[1]; os.makedirs(OUT, exist_ok=True)
PLACES = [("Forest", "EXT_Base", "outdoors, medium"), ("Field + tree line + hill", "EXT_Large", "outdoors, big and open"),
          ("Village yard", "EXT_Small", "outdoors, buildings close"), ("House room", "INT_Small", "indoors, small"),
          ("Stone cellar", "INT_Base", "indoors, medium"), ("Castle hall", "INT_Large", "indoors, big")]
def pad(x, n): return np.vstack([x, np.zeros((max(0, n - len(x)), 2))])[:n]
items = []
for i, (name, ref, note) in enumerate(PLACES):
    shot = rd(f"{G}/Grizzly-{i % 4 + 1}.wav"); echo = rd(f"{G}/GRIZZLY_Reflection_{ref}_0{i % 2 + 1}.wav")
    n = max(len(shot), len(echo)); items.append((f"{i + 1:02d} {name}.wav", pad(shot, n) + pad(echo, n), name, f"{note} - echo file {ref}"))
for k in range(1, 5):
    items.append((f"shot {k} (no echo).wav", rd(f"{G}/Grizzly-{k}.wav"), f"MK V shot {k}, no echo", "the bare shot, all 4 variants"))
items.append(("now in game - CoD Deagle.wav", rd(f"{DE}/decho-1.wav"), "What the mod plays now", "the Call of Duty file with its own echo baked in"))
peak = max(np.abs(x).max() for _, x, _, _ in items); ref_lvl = np.sqrt((rd(f"{G}/Grizzly-1.wav") ** 2).mean())
for f, x, _, _ in items:
    if f.startswith("now in game"):                       # same loudness as the MK V shot over its first 0.3 s
        x *= np.sqrt((rd(f"{G}/Grizzly-1.wav")[:int(.3 * SR)] ** 2).mean() / (x[:int(.3 * SR)] ** 2).mean())
    w = wave.open(f"{OUT}/{f}", "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((np.clip(x * .95 / peak, -1, 1) * 32767).astype(np.int16).tobytes()); w.close()
KEYS = "1234567890qw"
btns = "\n".join(f'<button class="pl" data-f="{html.escape(f)}" data-k="{KEYS[i]}"><kbd>{KEYS[i]}</kbd><div><span>{html.escape(t)}</span>'
                 f'<small>{html.escape(s)}</small></div></button>' for i, (f, _, t, s) in enumerate(items))
aud = json.dumps({f: base64.b64encode(open(f"{OUT}/{f}", "rb").read()).decode() for f, _, _, _ in items})
page = """<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>MK V sounds</title>
<style>body{background:#15171a;color:#e8e6e1;font:16px system-ui,sans-serif;max-width:900px;margin:28px auto;padding:0 16px}
h1{font-size:22px;margin:0 0 4px} p{color:#9a9890;margin:0 0 10px} #st{color:#e0a84a;font-weight:600;min-height:1.3em}
button{font:inherit;color:inherit;background:#1f2226;border:1px solid #2c3036;border-radius:8px;padding:10px 14px;cursor:pointer;text-align:left;display:flex;width:100%;margin:6px 0}
button:hover{border-color:#4a5058} button.on{border-color:#e0a84a;background:#2a2620} button div{display:flex;flex-direction:column}
kbd{font:600 17px ui-monospace,monospace;background:#2c3036;border-radius:6px;padding:3px 9px;margin-right:10px;align-self:flex-start}
span{font-weight:600} small{color:#9a9890;font-size:13px}</style>
<h1>Ready or Not's MK V (.50 AE) as our Deagle</h1>
<p>Every sound here is Ready or Not's own recording: the MK V shot plus Ready or Not's MK V echo for that kind of place.
Nothing is generated. Click or press the key. Space stops.</p><p id="st">Ready.</p>
BTNS
<script>
const A = AUD, st = document.getElementById('st'); let ctx, cur, bufs = {};
function b64(s){const b=atob(s),u=new Uint8Array(b.length);for(let i=0;i<b.length;i++)u[i]=b.charCodeAt(i);return u.buffer;}
function stop(){ if(cur){ try{cur.s.stop?cur.s.stop():cur.s.pause();}catch(e){} cur.el.classList.remove('on'); cur=null; } }
async function play(el){ stop(); const f = el.dataset.f; el.classList.add('on'); st.textContent = 'Playing ' + el.querySelector('span').textContent + '...';
  try { ctx = ctx || new (window.AudioContext||window.webkitAudioContext)(); if (ctx.state!=='running') await ctx.resume();
        bufs[f] = bufs[f] || await ctx.decodeAudioData(b64(A[f])); const s = ctx.createBufferSource(); s.buffer = bufs[f]; s.connect(ctx.destination);
        s.onended = () => { el.classList.remove('on'); if (cur && cur.el===el) { cur=null; st.textContent='Done.'; } }; cur = {s, el}; s.start(); }
  catch(e) { const a = new Audio(encodeURI(f)); a.onended = () => { el.classList.remove('on'); st.textContent='Done.'; }; cur = {s:a, el}; a.play(); } }
document.querySelectorAll('.pl').forEach(b => b.onclick = () => { play(b); b.blur(); });
addEventListener('keydown', e => { if (e.key===' ') { e.preventDefault(); stop(); st.textContent='Stopped.'; return; }
  const b = document.querySelector('.pl[data-k="'+e.key.toLowerCase()+'"]'); if (b) play(b); });
</script>"""
open(f"{OUT}/PLAY ME.html", "w").write(page.replace("BTNS", btns).replace("AUD", aud))
print(len(items), "sounds")
