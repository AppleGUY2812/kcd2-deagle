"""Build PLAY ME.html next to the WAVs: sounds decoded with Web Audio (no <audio>, no data: URLs),
falling back to the plain WAV file beside the page if decoding fails.
Step 2 shows each of our places (B) next to the matching Ready or Not reference (C)."""
import base64, glob, html, json, os, sys

os.chdir(sys.argv[1])
files = sorted(glob.glob("*.wav"))
keys = "1234567890qwertyuiop"
notes = {"A1": "old short cut", "A2": "longer cut, keeps the boom", "A3": "longer cut + Colt low thump"}
# (place, ours, Ready or Not, note)
PAIRS = [("Village yard / courtyard", "B3", "C1", "buildings close around you"),
         ("Forest", "B1", "C2", "Ready or Not has no forest, so this is its nearest: a medium outdoor space"),
         ("Open field", "B2", "C3", ""),
         ("Field + tree line + hill", "B7", "C3", "same Ready or Not sound as the row above"),
         ("House / small room", "B4", "C4", ""),
         ("Cellar / medium room", "B5", "C5", ""),
         ("Cave / big hall", "B6", "C6", "")]
PAIR_KEYS = "zxcvbnm"

items = []
for k, f in zip(keys, files):
    code, rest = f[:2], f[3:-4]
    title = rest.split(" (")[0].replace("bang - ", "").replace("RoN Colt - ", "Ready or Not: ")
    items.append({"k": k, "code": code, "title": title, "sub": notes.get(code, rest[len(rest.split(" (")[0]):].strip(" ()")),
                  "file": f, "b64": base64.b64encode(open(f, "rb").read()).decode()})
idx = {it["code"]: i for i, it in enumerate(items)}

def btn(i):
    it = items[i]
    return (f'<button data-i="{i}"><kbd>{it["k"]}</kbd><b>{it["code"]}</b><span>{html.escape(it["title"])}</span>'
            f'<small>{html.escape(it["sub"])}</small></button>')

bangs = "\n".join(btn(i) for i, it in enumerate(items) if it["code"][0] == "A")
pairs = "\n".join(
    f'<h3>{html.escape(place)}{" <em>- " + html.escape(note) + "</em>" if note else ""}</h3>'
    f'<div class="pair">{btn(idx[ours])}{btn(idx[ron])}'
    f'<button class="both" data-pair="{idx[ours]},{idx[ron]}" data-k="{pk}"><kbd>{pk}</kbd><span>both</span>'
    f'<small>ours, then theirs</small></button></div>'
    for pk, (place, ours, ron, note) in zip(PAIR_KEYS, PAIRS))
data = json.dumps([{k: it[k] for k in ("k", "code", "file", "b64")} for it in items])

page = """<!doctype html><meta charset="utf-8"><title>Deagle sound test</title>
<style>
body{background:#15171a;color:#e8e6e1;font:16px system-ui,sans-serif;max-width:1000px;margin:32px auto;padding:0 16px}
h1{font-size:22px;margin:0 0 4px} p{color:#9a9890;margin:0 0 12px}
h2{font-size:14px;text-transform:uppercase;letter-spacing:.08em;color:#9a9890;margin:26px 0 8px}
h3{font-size:15px;margin:16px 0 0} em{color:#9a9890;font-weight:400;font-style:normal;font-size:14px}
.pair{display:grid;grid-template-columns:1fr 1fr 130px;gap:8px}
.head{display:grid;grid-template-columns:1fr 1fr 130px;gap:8px;color:#9a9890;font-size:13px;font-weight:600;margin-top:8px}
button{display:grid;grid-template-columns:44px 40px 1fr;grid-template-rows:auto auto;align-items:center;width:100%;
 text-align:left;background:#1f2226;color:inherit;border:1px solid #2c3036;border-radius:8px;padding:10px 14px;margin:6px 0;
 cursor:pointer;font:inherit}
button.both{grid-template-columns:44px 1fr}
button:hover{border-color:#4a5058} button.on{border-color:#e0a84a;background:#2a2620}
kbd{grid-row:1/3;font:600 18px ui-monospace,monospace;background:#2c3036;border-radius:6px;padding:4px 0;text-align:center;width:32px}
b{grid-row:1/3;color:#e0a84a} span{font-weight:600} small{color:#9a9890;grid-column:3}
button.both small{grid-column:2}
#st{color:#e0a84a;font-weight:600;min-height:1.2em}
</style>
<h1>Deagle sound test</h1>
<p>Click a button or press its key. A new sound stops the last one. Space stops.</p>
<p id="st">Ready. Click a button or press a key.</p>
<h2>Step 1 - pick a bang</h2>BANGS
<h2>Step 2 - ours next to Ready or Not, place by place</h2>
<p>All our places use bang A2. Ready or Not's use its Colt revolver. Both bangs are set to the same loudness.<br>
Ready or Not indoors: the echo starts right away, is as loud as the bang and is gone in about 0.3 s.<br>
Ready or Not outdoors: the echo is about 10 dB quieter and comes 0.15-0.2 s after the shot.</p>
<div class="head"><div>Ours (Deagle)</div><div>Ready or Not (pro reference)</div><div>Back to back</div></div>
PAIRS
<script>
const D = DATA;
const st = document.getElementById('st');
const say = (t, bad) => { st.textContent = t; st.style.color = bad ? '#ff6b6b' : '#e0a84a'; };
let ctx = null, bufs = [], cur = null, seq = 0;

function b64buf(s) { const bin = atob(s), u = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i); return u.buffer; }
function mark(i, on) { document.querySelectorAll('[data-i="' + i + '"]').forEach(b => b.classList.toggle('on', on)); }
function stop() {
  seq++;
  if (cur) { try { cur.s.stop ? cur.s.stop() : cur.s.pause(); } catch (e) {} mark(cur.i, false); cur = null; }
  document.querySelectorAll('.both.on').forEach(b => b.classList.remove('on'));
}
// plays one sound; resolves true if it finished on its own, false if something else stopped it
function playOne(i, my) {
  const d = D[i]; mark(i, true); say('Playing ' + d.code + '...');
  return new Promise(async res => {
    const fin = () => { if (cur && cur.i === i && seq === my) { mark(i, false); cur = null; } res(seq === my); };
    try {                                  // 1st choice: Web Audio, decoded from the bytes in this page
      if (!ctx) ctx = new (window.AudioContext || window.webkitAudioContext)();
      if (ctx.state !== 'running') await ctx.resume();
      if (!bufs[i]) bufs[i] = await ctx.decodeAudioData(b64buf(d.b64));
      if (seq !== my) { mark(i, false); return res(false); }
      const s = ctx.createBufferSource(); s.buffer = bufs[i]; s.connect(ctx.destination);
      s.onended = fin; cur = { s, i }; s.start(); return;
    } catch (e) { var webErr = e && (e.message || e.name); }
    try {                                  // 2nd choice: the WAV file sitting next to this page
      const a = new Audio(encodeURI(d.file)); a.onended = fin; cur = { s: a, i }; await a.play();
      say('Playing ' + d.code + ' (from the file)');
    } catch (e) { mark(i, false); cur = null; say('Could not play. Web Audio: ' + webErr + ' | File: ' + (e.message || e.name), true); res(false); }
  });
}
async function play(i) { stop(); const my = seq; if (await playOne(i, my)) say('Done. Click a button or press a key.'); }
async function both(btn) {
  stop(); const my = seq; btn.classList.add('on'); const [x, y] = btn.dataset.pair.split(',').map(Number);
  if (await playOne(x, my)) {
    say('Ours done. Ready or Not next...'); await new Promise(r => setTimeout(r, 600));
    if (seq === my && await playOne(y, my)) say('Done: ' + D[x].code + ' (ours), then ' + D[y].code + ' (Ready or Not).');
  }
  if (seq === my) btn.classList.remove('on');
}
document.querySelectorAll('button[data-i]').forEach(b => b.onclick = () => { play(+b.dataset.i); b.blur(); });
document.querySelectorAll('button.both').forEach(b => b.onclick = () => { both(b); b.blur(); });
addEventListener('keydown', e => {
  if (e.key === ' ') { e.preventDefault(); stop(); say('Stopped.'); return; }
  const k = e.key.toLowerCase(), pb = document.querySelector('button.both[data-k="' + k + '"]');
  if (pb) return both(pb);
  const i = D.findIndex(d => d.k === k); if (i >= 0) play(i);
});
</script>"""
page = page.replace("BANGS", bangs).replace("PAIRS", pairs).replace("DATA", data)
open("PLAY ME.html", "w").write(page)
# media-player playlist in the same order: bangs, then ours/theirs pairs
order = [it["file"] for it in items if it["code"][0] == "A"]
for _, ours, ron, _ in PAIRS: order += [items[idx[ours]]["file"], items[idx[ron]]["file"]]
open("PLAY ALL (media player).m3u", "w", newline="\r\n").write("#EXTM3U\n" + "\n".join(order) + "\n")
print(len(items), "sounds,", round(len(page) / 1e6, 1), "MB,", len(order), "in playlist")
