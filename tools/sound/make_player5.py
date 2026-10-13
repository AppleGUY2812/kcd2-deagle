"""Build PLAY ME.html for test pack 4 from manifest.json: a blind A/B test that keeps score, then all places.
Sounds are decoded with Web Audio from bytes inside the page; falls back to the WAV file beside the page."""
import base64, html, json, os, sys

os.chdir(sys.argv[1])
man = json.load(open("manifest.json"))
files = [p["file"] for p in man["places"]] + [b["b"] for b in man["blind"]]
AUD = {f: base64.b64encode(open(f, "rb").read()).decode() for f in dict.fromkeys(files)}
PIC = base64.b64encode(open("what the lines see.png", "rb").read()).decode()
KEYS = "1234567890qw"

def place_btn(i, p):
    lines = "".join(f"<small>{html.escape(l)}</small>" for l in p["lines"])
    return (f'<button class="pl" data-f="{html.escape(p["file"])}" data-k="{KEYS[i]}"><kbd>{KEYS[i]}</kbd>'
            f'<div><span>{i + 1}. {html.escape(p["name"])}</span> <em>{html.escape(p["note"])}</em>{lines}</div></button>')
places = "\n".join(place_btn(i, p) for i, p in enumerate(man["places"]))
data = json.dumps({"aud": AUD, "blind": man["blind"]})

page = """<!doctype html><meta charset="utf-8"><title>MK V sound test 5</title>
<style>
body{background:#15171a;color:#e8e6e1;font:16px system-ui,sans-serif;max-width:1040px;margin:28px auto;padding:0 16px}
h1{font-size:22px;margin:0 0 4px} p{color:#9a9890;margin:0 0 10px}
h2{font-size:14px;text-transform:uppercase;letter-spacing:.08em;color:#9a9890;margin:30px 0 8px}
button{font:inherit;color:inherit;background:#1f2226;border:1px solid #2c3036;border-radius:8px;padding:10px 14px;cursor:pointer;text-align:left}
button:hover{border-color:#4a5058} button.on{border-color:#e0a84a;background:#2a2620}
kbd{font:600 17px ui-monospace,monospace;background:#2c3036;border-radius:6px;padding:3px 9px;margin-right:10px}
.pl{display:flex;align-items:flex-start;width:100%;margin:6px 0} .pl div{display:flex;flex-direction:column}
.pl span{font-weight:600} em{color:#9a9890;font-style:normal;font-size:14px;margin-left:4px}
small{color:#9a9890;font-size:13px;margin-top:2px}
#st{color:#e0a84a;font-weight:600;min-height:1.3em}
#bl{background:#1b1d21;border:1px solid #2c3036;border-radius:10px;padding:16px}
#bl .row{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px;margin:10px 0}
#bl .row button{text-align:center;padding:14px} #round{font-size:18px;font-weight:600}
table{border-collapse:collapse;margin:10px 0;width:100%} td,th{border-bottom:1px solid #2c3036;padding:6px 8px;text-align:left;font-size:14px}
th{color:#9a9890;font-weight:600} .win{color:#e0a84a;font-weight:600}
textarea{width:100%;height:70px;background:#1f2226;color:#e8e6e1;border:1px solid #2c3036;border-radius:8px;font:13px ui-monospace,monospace}
img{width:100%;border-radius:8px;border:1px solid #2c3036;margin-top:6px}
</style>
<h1>MK V sound test 5: Ready or Not's own echo vs our computed echo</h1>
<p>Every sound here is Ready or Not's MK V (.50 AE). Same shot on both sides of every round; only the echo
differs: Ready or Not's own MK V echo file for that kind of place, or ours, computed from what 128 lines find around
you (distance, direction, surface) plus a real recorded tail of a real place. Space stops a sound.</p>
<p id="st">Ready.</p>

<h2>Step 1 - blind test (do this first, before listening to step 2)</h2>
<div id="bl">
 <div id="intro"><p>10 rounds: 5 places, 2 takes each (another shot, Ready or Not's other echo file). X and Y are
 the same MK V shot; only the echo differs. Pick the one that sounds more real. Can't judge = you can't tell or
 don't know what it would sound like. The page shows at the end which was which.</p>
 <button id="go"><kbd>Enter</kbd>Start blind test</button></div>
 <div id="play" hidden>
  <div id="round"></div><p id="where"></p>
  <div class="row"><button data-p="x"><kbd>X</kbd>Play X</button><button data-p="y"><kbd>Y</kbd>Play Y</button><button data-p="xy"><kbd>B</kbd>X then Y</button></div>
  <div class="row"><button data-c="x"><kbd>1</kbd>X sounds more real</button><button data-c="y"><kbd>2</kbd>Y sounds more real</button><button data-c="=">
  <kbd>3</kbd>Can't judge</button></div>
  <p>While the test runs, keys 1-3 pick an answer instead of playing places.</p>
 </div>
 <div id="done" hidden><div id="score"></div><table id="tab"></table>
  <p>To send me the result, copy this:</p><textarea id="copy" readonly></textarea>
  <button id="again">Start over</button></div>
</div>

<h2>Step 2 - all places (MK V shot + our echo)</h2>
<p>Each button says what the lines found. "Clean echo" = a surface facing you sends back a sharp copy.
"Far echoes" = a hillside, cliff or tree line answers as one rolling echo. "Tail" = which real recording rings
after the shot, and for how long. All use our Deagle bang.</p>
PLACES
<h2>What the lines see, from above</h2>
<img src="data:image/png;base64,PIC">
<script>
const D = DATA, st = document.getElementById('st');
const say = (t, bad) => { st.textContent = t; st.style.color = bad ? '#ff6b6b' : '#e0a84a'; };
let ctx = null, bufs = {}, cur = null, seq = 0;
function b64buf(s) { const bin = atob(s), u = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i); return u.buffer; }
function stop() { seq++; if (cur) { try { cur.s.stop ? cur.s.stop() : cur.s.pause(); } catch (e) {} if (cur.el) cur.el.classList.remove('on'); cur = null; } }
function playOne(f, el, label, my) {
  if (el) el.classList.add('on'); say('Playing ' + label + '...');
  return new Promise(async res => {
    const fin = () => { if (el) el.classList.remove('on'); if (cur && cur.f === f && seq === my) cur = null; res(seq === my); };
    try {
      if (!ctx) ctx = new (window.AudioContext || window.webkitAudioContext)();
      if (ctx.state !== 'running') await ctx.resume();
      if (!bufs[f]) bufs[f] = await ctx.decodeAudioData(b64buf(D.aud[f]));
      if (seq !== my) { if (el) el.classList.remove('on'); return res(false); }
      const s = ctx.createBufferSource(); s.buffer = bufs[f]; s.connect(ctx.destination); s.onended = fin; cur = { s, f, el }; s.start(); return;
    } catch (e) { var webErr = e && (e.message || e.name); }
    try { const a = new Audio(encodeURI(f)); a.onended = fin; cur = { s: a, f, el }; await a.play(); say('Playing ' + label + ' (from the file)'); }
    catch (e) { if (el) el.classList.remove('on'); cur = null; say('Could not play. Web Audio: ' + webErr + ' | File: ' + (e.message || e.name), true); res(false); }
  });
}
async function play(f, el, label) { stop(); const my = seq; if (await playOne(f, el, label, my)) say('Done.'); }
document.querySelectorAll('.pl').forEach(b => b.onclick = () => { play(b.dataset.f, b, b.querySelector('span').textContent); b.blur(); });

// ---- blind test ----
const KEY = 'mk5-blind-5a', $ = id => document.getElementById(id);
let T = JSON.parse(localStorage.getItem(KEY) || 'null');
const save = () => localStorage.setItem(KEY, JSON.stringify(T));
function shuffle(a) { for (let i = a.length - 1; i > 0; i--) { const j = Math.floor(Math.random() * (i + 1)); [a[i], a[j]] = [a[j], a[i]]; } return a; }
function start() {
  T = { i: 0, rounds: shuffle(D.blind.map((p, n) => n)).map(n => ({ n, swap: Math.random() < .5, pick: null })) }; save(); show();
}
const side = (r, s) => { const p = D.blind[r.n], aFirst = !r.swap; return (s === 'x') === aFirst ? { f: p.a, kind: p.kind_a, label: 'our new echo' } : { f: p.b, kind: p.kind_b, label: p.b_label }; };
function show() {
  const running = T && T.i < T.rounds.length;
  $('intro').hidden = !!T; $('play').hidden = !running; $('done').hidden = !(T && !running);
  if (running) { const r = T.rounds[T.i]; $('round').textContent = 'Round ' + (T.i + 1) + ' of ' + T.rounds.length; $('where').textContent = 'Place: ' + D.blind[r.n].place; }
  else if (T) results();
}
function choose(c) { stop(); T.rounds[T.i].pick = c; T.i++; save(); show(); say(T.i < T.rounds.length ? 'Saved. Next round.' : 'Done. Results below.'); }
function results() {
  let rows = '<tr><th>Place</th><th>X was</th><th>Y was</th><th>You picked</th></tr>', w = { ron: [0, 0, 0], old: [0, 0, 0] }, txt = [];
  for (const r of T.rounds) {
    const p = D.blind[r.n], x = side(r, 'x'), y = side(r, 'y'), other = p.kind_b;
    const won = r.pick === '=' ? 'same' : (side(r, r.pick).kind === 'new' ? 'new' : other);
    w[other][won === 'new' ? 0 : won === 'same' ? 2 : 1]++;
    rows += '<tr><td>' + p.place + '</td><td' + (r.pick === 'x' ? ' class="win"' : '') + '>' + x.label + '</td><td' + (r.pick === 'y' ? ' class="win"' : '') + '>' + y.label +
            '</td><td>' + (r.pick === '=' ? "can't judge" : r.pick.toUpperCase()) + '</td></tr>';
    txt.push(p.place + ': new vs ' + p.b_label + ' -> ' + (won === 'new' ? 'new' : won === 'same' ? "can't judge" : p.b_label));
  }
  $('tab').innerHTML = rows;
  $('score').innerHTML = '<p class="win">Our new echo vs Ready or Not: ours picked ' + w.ron[0] + ', theirs ' + w.ron[1] + ', can\\'t judge ' + w.ron[2] +
    '.</p>';
  $('copy').value = 'blind test 5 results\\n' + txt.join('\\n');
}
function blindPlay(which, el) {
  const r = T.rounds[T.i];
  if (which === 'xy') { stop(); const my = seq; el.classList.add('on');
    playOne(side(r, 'x').f, null, 'X', my).then(ok => ok ? new Promise(z => setTimeout(() => z(seq === my), 500)) : false)
      .then(ok => ok ? playOne(side(r, 'y').f, null, 'Y', my) : false).then(ok => { el.classList.remove('on'); if (ok) say('Done: X, then Y.'); });
    return; }
  play(side(r, which).f, el, which.toUpperCase());
}
$('go').onclick = start; $('again').onclick = () => { stop(); T = null; localStorage.removeItem(KEY); start(); };
document.querySelectorAll('[data-p]').forEach(b => b.onclick = () => { blindPlay(b.dataset.p, b); b.blur(); });
document.querySelectorAll('[data-c]').forEach(b => b.onclick = () => { choose(b.dataset.c); b.blur(); });
addEventListener('keydown', e => {
  if (e.key === ' ') { e.preventDefault(); stop(); say('Stopped.'); return; }
  const k = e.key.toLowerCase(), running = T && T.i < T.rounds.length;
  if (!T && e.key === 'Enter') return start();
  if (running) {
    const pb = { x: 'x', y: 'y', b: 'xy' }[k]; if (pb) return blindPlay(pb, document.querySelector('[data-p="' + pb + '"]'));
    const c = { '1': 'x', '2': 'y', '3': '=' }[k]; if (c) return choose(c);
  }
  const b = document.querySelector('.pl[data-k="' + k + '"]'); if (b && !(running && '123'.includes(k))) b.click();
});
show();
</script>"""
page = page.replace("PLACES", places).replace("PIC", PIC).replace("DATA", data)
open("PLAY ME.html", "w").write(page)
open("PLAY ALL (media player).m3u", "w", newline="\r\n").write("#EXTM3U\n" + "\n".join(p["file"] for p in man["places"]) + "\n")
print(len(AUD), "sounds,", round(len(page) / 1e6, 1), "MB")
