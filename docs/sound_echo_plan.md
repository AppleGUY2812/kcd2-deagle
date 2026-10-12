# Gun sound / echo: handoff (from session johnpork-9f, 2026-10-12)

Built from 9f's transcript (`~/.claude/projects/-home-johnpork/81286427-298b-45b8-8c38-b64077090665.jsonl`) and files,
not from 9f's memory. Times are UTC (the Windows clock is UTC-7). Snapshot: 2026-10-12 ~20:50 UTC.

Nothing in the mod plays differently yet. All of this is offline listening tests (WAV packs + an HTML player on the
Desktop) and an echo model in Python. Ready or Not (RoN) and Call of Duty sounds are private-use only: they never go in
the repo or in anything shared.

## 1. Goal (the user's words)
- 17:11: "i had the other claude do some sound research to level up our kcd gun sounds"
- 20:16: "whats our next steps to make our deagle echos crazy good environmentally and as good sounding as ready or not
  in the same environment"
- Along the way: KCD2's terrain is organic, so a fixed list of places won't do (18:37: "what if we have a big cave and an
  even bigger cave. what if the cave is pipe shaped [...] standing right next to a cliff wall or in a ravine"). Indoors
  should get louder (18:28: "in real life indoors is louder no?"). Echoes should come from the right side (18:43).

Scope: the player's own Deagle shot (the listener stands at the gun). Other people's shots, occlusion and distance
layers are later, once NPCs have guns.

## 2. Where things stand
1. **Test 3 lost the blind test to Ready or Not**: RoN picked 4 times, ours 0, can't tell 2. Against round 2 the new echo
   won once (round cave), 5 can't tell (section 5).
2. **Why (measured):** outdoors most of our echo lands inside the bang and is masked ("heard" 25 % village yard, 24 %
   forest, vs 95 % for RoN). In the 0.3-1 s window ours is ~17-19 dB quieter than RoN's. The user: "yea cause some of
   them didnt even have an echo".
3. **Test 4 was approved** ("go", 20:31) and 9f started it in its scratchpad (section 11). It isn't finished.
4. **Nothing in game yet.** Step 0 (time the game's ray checks; confirm the plugin's own FMOD system can add DSPs) was
   never done. 9f's check of `fmod.dll` exports at 17:12 failed because E: wasn't mounted in WSL then.
5. **The bang itself is unsolved.** `decho-*.wav` (Call of Duty, `p25_pi_decho` naming) are 3.97 s long with about 2.2 s
   of baked echo (-40 dB at 2.20-2.30 s). The test bang (A2) is a cut of `decho-1.wav` and still carries ~0.5 s of CoD's
   tail. No clean recording has been bought or found.

## 3. Files
| Path | What |
|---|---|
| `tools/sound/sound_test.py` | test 1 pack (07) |
| `tools/sound/sound_test2.py` | test 2 pack (08), now also writes the C1-C6 RoN references (9f made those inline at 17:57) |
| `tools/sound/make_player.py` | test 2 page `PLAY ME.html` + `PLAY ALL (media player).m3u` |
| `tools/sound/sound_test3.py` | **current echo model**, test 3 pack (10): WAVs, blind-test files, `manifest.json`, `what the lines see.png` |
| `tools/sound/make_player3.py` | test 3 page from `manifest.json` (blind test + all places) |
| `tools/sound/echo_audible.py` | how much of each echo is audible over the bang (9f's `/tmp/st3_audible.py`, 19:56) |
| `C:\Users\<user>\Desktop\Deagle Progress\07 Gun sound test\` | test 1 WAVs (no page) |
| `...\08 Gun sound test 2\PLAY ME.html` | test 2 page (A, B, C files, pairs) |
| `...\10 Gun sound test 3\PLAY ME.html` | test 3 page (blind test, 12 places, picture) |
| `E:\Tools\deagle-work\sound\ron_python\` | the 40 RoN Colt Python / .357 WAVs the tests use (extracted again 2026-10-12 with `tools/ron/fsbx.py --wav`, byte-identical to 9f's scratchpad `ron_python/`) |
| `assets/build/sounds/decho/decho-1..8.wav` | the Deagle shots (gitignored build input) |
| `~/reports/KCD2 gun sound propagation.md` + `~/research_notes/KCD2 gun sound propagation/` | research report 1 (pasted 17:11) |
| `~/reports/KCD2 gun sound material reflections.md` + `~/research_notes/KCD2 gun sound material reflections/` | research report 2 (johnpork-18, sent 19:39) |

Kept only in 9f's scratchpad (`/tmp/claude-1000/-home-johnpork/81286427-298b-45b8-8c38-b64077090665/scratchpad/`,
temporary): `sound_test3_v1.py` (test 3 before the 19:28 fixes), and the test 4 work in progress (section 11).
The audio venv 9f used is `/tmp/fsbenv` (temporary too).

## 4. Rebuild and open the test pages
Needs Python 3 with numpy and matplotlib (test 3), `fsb5` + libvorbis + ffmpeg only to re-extract the RoN WAVs.
```
python3 -m venv ~/.venvs/deagle-audio && ~/.venvs/deagle-audio/bin/pip install numpy matplotlib fsb5
PY=~/.venvs/deagle-audio/bin/python
# only if E:\Tools\deagle-work\sound\ron_python is missing (read-only on the bank, ~30 s):
$PY -I tools/ron/fsbx.py "/mnt/c/Program Files (x86)/Steam/steamapps/common/Ready Or Not/ReadyOrNot/Content/FMOD/Desktop/Weapons.bank" \
    /mnt/e/Tools/deagle-work/sound/ron_python '^(Python_(Fire|Fire_Snub|MidShot|MidTail|MidReflection|Reflection_(Ext|Int)_\w+)|357_Dist\w+)[-_ ]?\d*$' --wav
D="/mnt/c/Users/<user>/Desktop/Deagle Progress"
$PY tools/sound/sound_test.py  "$D/07 Gun sound test"
$PY tools/sound/sound_test2.py "$D/08 Gun sound test 2" && $PY tools/sound/make_player.py "$D/08 Gun sound test 2"
$PY tools/sound/sound_test3.py "$D/10 Gun sound test 3" && $PY tools/sound/make_player3.py "$D/10 Gun sound test 3"
$PY tools/sound/echo_audible.py          # prints the audibility table, writes nothing
```
- Options: `--decho <dir>` (default `assets/build/sounds/decho`), `--ron <dir>` (default the E: folder above), and for
  test 3 `--test2 <dir>` (default the 08 folder: its A2 and B1-B6 are reused for the blind test).
- Checked 2026-10-12: all three packs, both pages, `manifest.json` and the picture rebuild byte-identical to the
  Desktop copies, and `echo_audible.py` prints 9f's 19:56 numbers.
- A new pack goes in the next free Desktop number (11 is free; 09 is the flash-mesh preview picture).
- **Open:** double-click `PLAY ME.html`, or paste
  `file:///C:/Users/<user>/Desktop/Deagle%20Progress/10%20Gun%20sound%20test%203/PLAY%20ME.html`. After a rebuild press
  Ctrl+F5. The user once had the old 08 page open and thought the blind test was missing (19:36-19:45): the title must
  read "Deagle sound test 3: echoes made from the lines".
- The page decodes the WAVs embedded in it with Web Audio, and falls back to the WAV beside the page. The first test 2
  page used `<audio>` with data: URLs and the user's browser refused it ("Browser blocked the sound: The element has no
  supported sources."); don't go back to that. `PLAY ALL (media player).m3u` is the backup.
- Keys, test 3: Enter starts the blind test, X / Y / B (X then Y) play, 1 / 2 / 3 = X more real / Y more real / can't
  tell, Space stops; outside the test 1-9, 0, q, w play the 12 places. Test 2: 1-3 = A1-A3, 4-9 and 0 = B1-B7,
  q-y = C1-C6, z-m = ours then RoN for each place.
- The blind test keeps its state in the browser's localStorage under `deagle-blind-3c`. Change `KEY` in
  `make_player3.py` (9f went 3 -> 3b -> 3c) to give the user a fresh test after the sounds change.

## 5. The three tests and the user's feedback
### Test 1: `07 Gun sound test` (17:22)
Seven WAVs. The cut bang is `decho-1.wav` kept to 0.30 s and faded out by 0.45 s. Echoes in 3-7 are set so the
0.5-2.5 s energy matches the original file's own echo (-21.4 dB vs its 10 ms peak).
1. Deagle now (echo baked in) / 2. Deagle cut, no echo / 3. Deagle cut + Colt outdoor echo (`Python_Reflection_Ext_Base_01`)
/ 4. same echo darkened for a forest (1.1 kHz low-pass, extra decay) / 5. Deagle cut + generated forest echo with KCD2's
`rvb_outdoor_forest_01` numbers (160 ms early, +88 ms late, 1.7 s decay, highs cut at 1.1 kHz) / 6. Colt bang + Colt
echo / 7. Deagle cut + field echo + tree line 60 m (0.35 s) + hill 240 m (1.4 s).

User (17:29): "2 is too thin. 4 how can it be that muffled if the gun is being shot from you . 5 cause it doesnt sound
like we put a pringle can suppressor on it." Then: take a Deagle from elsewhere, or simulate the echoes?
- 9f: 5 wins (make echoes ourselves); 4 was darkened twice; the bang is thin because CoD's echo is mixed into the boom.

### Test 2: `08 Gun sound test 2` (17:45, player 17:48-17:53, RoN references 17:57, pairs 18:31)
- A1 short cut (test 1's) / A2 longer cut (full to 0.55 s, faded by 0.80 s) / A3 = A2 + the Colt's low thump (< 150 Hz),
  all at the same loudness.
- B1-B6: A2 through one generated echo per KCD2 snapshot (FMOD SFX-Reverb-like IR: sparse early taps + noise tail):
  forest_01 1.7 s, 160+88 ms, 1.1 kHz cut, 5.2 ms echo / open field = KCD2's `RVB_OUT` bus 1.5 s + 360 ms echo at 43 %
  feedback / yard_01 0.9 s, 20+25 ms, 4.8 kHz, 72 ms echo / house_01 0.55 s, 700 Hz / cellar 1.2 s, 1.7 kHz /
  cave_IR1 1.9 s, 24 ms echo 22 %. Outdoor echo level copied from the CoD file, indoors +6 dB, yard -3 dB.
  B7 = B2 + tree line 60 m + hill 240 m.
- C1-C6: RoN's Colt bang + its own `Python_Reflection_{Ext_Small, Ext_Base, Ext_Large, Int_Small, Int_Base, Int_Big}_01`.

User:
- "i dont hear anything" -> "Browser blocked the sound: The element has no supported sources." -> page rebuilt with
  Web Audio -> **"holy shit this is actually goated"** (17:55).
- "2 and 3 sound the same am i trippin" (A2 vs A3): A3 only adds bass below 150 Hz, which earbuds barely play. 9f: go
  with A2. Also "i dont really have a reference [...] like a professional example" -> the C files + Sonniss links.
- "should it sound louder in doors? in real life indoors is louder no?" (18:28). Measured over the first 0.2 s, vs the
  outdoor version: ours house +0.3 dB, cellar +0.9 dB, cave 0; RoN small room +2.5, medium +4.0, big hall +5.0 dB.
- "the large open field the echo kind of sounds like grass blowing" (18:35): 9f's mistake. B2's tail kept its highs
  (7.7 % of the tail energy above 2 kHz vs 3.1 % for RoN's big open space), was ~13 dB too loud, and the 360 ms echo
  repeats made whoosh after whoosh. The cave (B6, 6.7 %) had the same hiss.

### Test 3: `10 Gun sound test 3` (19:12, rebuilt 19:28 and 19:48)
No list of places: 12 small 3D scenes, rays cast from the head, and an echo made only from what the rays return
(section 6). Places: open field, field + tree line (60 m) + hillside (240 m), forest, village yard, next to a cliff
(20 m left), ravine (walls 8 m left / 10 m right, 15 m high), house room 4 x 5 m, stone cellar 5 x 7 m, castle hall
12 x 24 x 9 m, rock tunnel 6 m wide, round cave 30 m, bigger round cave 60 m.

User (19:15): **"stil lsounds like wind on 5. also the hill 240m it sounds too preserved if u know what i mean. how can
it sound like the original that much genuine question"** (key 5 = "Next to a cliff").
- 9f's diagnosis: every echo copied the whole cut bang, CoD tail included (the "wind" behind the cliff slap; the
  "preserved" hill); the hill had been boosted 31 dB to reach RoN's level.
- 19:28 changes (diff of `sound_test3_v1.py` vs `sound_test3.py`): echoes made from the bang's first 90 ms only (faded by
  130 ms); a bass cut on every echo copied from RoN's echo files (`TONE`); rock scattering 0.30 -> 0.15 and sparse
  "click" texture per material (`DENS`); scattered energy x cos^2 x 1.5; smear cap 150 -> 450 ms with a slow rise on
  long smears (far echoes "roll in"); levels matched by BS.1770 K-weighted loudness instead of raw energy, with a new
  level rule (section 6).
- "hey what is this blind test i can tell theyre colt and deagle" (19:46): see below.

### Blind test 3
- Setup (current page): 12 rounds in random order, X / Y randomly swapped.
  - 6 Colt rounds: RoN's Colt bang + RoN's own echo file vs the same bang + our echo made from the Colt's first 90 ms,
    both echoes at the same K-weighted loudness. Village yard = Ext_Small, forest = Ext_Base, field + tree line + hill =
    Ext_Large, house room = Int_Small, stone cellar = Int_Base, castle hall = Int_Big.
  - 6 Deagle rounds: new echo vs round 2 (open field B2, forest B1, village yard B3, house B4, cellar B5, round cave B6).
- **The bug the user caught (19:46):** the first version (19:12-19:48) put RoN's Colt echo on our Deagle bang for the
  RoN rounds. RoN's echo files are recordings of the Colt, so that side sounded like a Colt and was always
  recognisable. 9f fixed it at 19:47-19:48 (Colt on both sides) and reset the stored results (key `deagle-blind-3c`).
- **Results** (pasted 19:54:56, 6 minutes after the fix; 9f treated them as the fixed version, not confirmed):

  | Round | Picked |
  |---|---|
  | Stone cellar: new vs RoN Int_Base | RoN |
  | Village yard: new vs RoN Ext_Small | RoN |
  | Field + tree line + hill: new vs RoN Ext_Large | RoN |
  | Forest: new vs RoN Ext_Base | RoN |
  | Castle hall: new vs RoN Int_Big | same |
  | House room: new vs RoN Int_Small | same |
  | Round cave: new vs round 2 B6 | new |
  | Stone cellar / Forest / House room / Village yard / Open field: new vs round 2 | same |

  "same" is the can't-tell button; the user later: "when i said same i meant i dont know what it would sound like in
  real life." Test 4 should label it "Can't judge".
- Measured afterwards (`echo_audible.py`, Colt rounds, echo vs bang per window):

  | Round | heard | 0-0.1 s | 0.1-0.3 s | 0.3-1 s | 1 s+ |
  |---|---|---|---|---|---|
  | Village yard, ours / RoN | 25 % / 95 % | -10.4 / -47.3 | -9.6 / -18.7 | -26.5 / -9.6 | -60.9 / -34.2 dB |
  | Forest, ours / RoN | 24 % / 95 % | -14.5 / -55.4 | -17.6 / -20.9 | -30.8 / -11.5 | -41.8 / -33.8 dB |
  | Stone cellar, ours / RoN | 85 % / 100 % | -5.2 / -4.6 | -3.4 / +0.3 | -10.7 / -10.8 | -32.9 / -58.6 dB |

  RoN's outdoor echo files start 145-189 ms after their onset and peak at 311-360 ms; Int_Base starts at 50 ms, Int_Small
  at 7 ms, Int_Big at once (9f's measurement, 18:56).

## 6. The current echo model (`tools/sound/sound_test3.py`)
- **Bang (dry):** `decho-1.wav` full to 0.55 s, faded by 0.80 s (A2). **Echo source:** its first 90 ms, faded by 130 ms,
  mono.
- **Rays:** 128 directions (Fibonacci sphere from 15 deg below level to straight up) from the head; ground 1.7 m below;
  max 400 m. In game the plan is ~32 rays a second, the echo using the last 4 s (= 128 directions), plus a few extra
  casts to find mirror points (128-245 casts per place here). A ray returns only what KCD2's `RayWorldIntersection`
  gives: distance, surface normal, surface type. The scenes themselves (`Rect`, `SphereIn`, `TubeIn`, `Trunks`,
  `Canopy`, `Ground`) stand in for the game world; the echo maker never sees them.
- **Hit classes:** open (nothing within 400 m), floor (level ground under you, ignored: the recording already has it), hit.
- **Clean echoes (mirrors):** hits grouped by plane (normals within ~14 deg, plane distance within max(1 m, 5 %)),
  skipping trees and leaves (scattering >= 0.9); for the 10 strongest groups, extra casts walk to the point that faces
  you (cos > 0.999), nearest point first, else outward (domes). One sharp copy per mirror:
  amplitude sqrt((1-s)(1-a)) / (2 * max(d, 0.75)) x air loss, delay 2d / 343.
- **Bounces:** for each mirror a ray straight behind; a parallel facing wall gives an image-source series
  (until 3 s or amplitude < 3e-5): ravine flutter, tunnel, rooms.
- **Scattered echoes:** every hit sends a smeared copy: energy s(1-a) cos^2 x 1.5 x dOmega / (pi d^2); smear = material
  smear + geometric spread, max 450 ms, built from random clicks at a per-material density (rock 250/s, leaves and
  grass 1500/s); smears >= 60 ms rise over their first 35 % (sin^2) then decay.
- **Tail:** from hits within 80 m. "closed" = share of non-floor rays hitting within 80 m; the tail fades in between
  closed 0.3 and 0.7. Volume and surface from the hit distances -> mean free path 4V/S -> Eyring T60 per octave band
  (125 Hz-16 kHz) with air absorption; level from the room formula; band-split noise, each band decaying at its own T60,
  ramping in from the first-hit round trip to the median-hit round trip; panned toward the closer side.
- **Air:** ISO 9613-1, 20 C, 50 % RH, dB per metre per band (.0004 .001 .0019 .0037 .0097 .0328 .117 .38).
  **TONE:** -8.5 / -4.0 / -0.5 dB at 125 / 250 / 500 Hz on every echo (RoN's echo files vs its close bang).
- **Direction:** equal-power pan from the x component plus up to 0.66 ms interaural delay. No HRTF.
- **How places are classified:** they aren't, except for the level. "roof" = share of rays above 65 deg hitting
  something hard within 80 m. Level rule (K-weighted echo vs bang): outdoors one value, the mean of RoN's three Ext files
  (-4.7 dB); indoors -0.4 + 2.40 x log2(mean free path), capped at RoN's loudest + 1 dB (+8.4 dB); blended by roof.
  Physics only decides timing, direction, tone and length (the physics level would put the far tree line at -54 dB).

| # | Place | clean echoes (manifest) | tail at 1 kHz | echo vs bang |
|---|---|---|---|---|
| 1 | Open field | none | none | no echo |
| 2 | Field + tree line + hill | soft: leaves 67 m, hillside 333 m | none | -5 dB (RoN Ext_Large -5) |
| 3 | Forest | soft: trees 7 m, leaves 12 m | 2.3 s | -5 dB (RoN Ext_Base -5) |
| 4 | Village yard | walls 6 / 7 / 9 m + 1, bounces | 0.7 s | -5 dB (RoN Ext_Small -5) |
| 5 | Next to a cliff | rock 20 m left | none | -5 dB |
| 6 | Ravine | rock 8 m left, 10 m right, bounces | 0.9 s | -5 dB |
| 7 | House room | ceiling 1 m, walls 2 / 2 m + 2 | 0.5 s | +2 dB (RoN Int_Small +1) |
| 8 | Stone cellar | ceiling 1 m, walls 2 / 3 m + 2 | 1.3 s | +3 dB (RoN Int_Base +4) |
| 9 | Castle hall | wall 4 m, ceiling 7 m, wall 8 m + 2 | 3.0 s | +8 dB (RoN Int_Big +7) |
| 10 | Tunnel | rock 2 m, ceiling 4 m, bounces | 3.3 s | +5 dB |
| 11 | Round cave | cave wall 22 m above | 2.9 s | +8 dB |
| 12 | Bigger round cave | cave wall 44 m above | 5.5 s | +8 dB |

## 7. Known problems
1. **Echo arrives during the bang.** Outdoors the scattered copies from nearby trees and walls land in the first
   0.3 s and are masked (section 5 table). RoN's outdoor echo starts after the bang.
2. **Fade-in on distant echoes.** The slow sin^2 rise on long smears (added 19:28) makes the hill build up over
   ~280 ms (9f's estimate). Research: every echo needs a defined onset; a hillside smears over 30-120 ms.
3. **The noise tail sounds like wind.** Tails are synthetic band-split noise. Research: use real impulse responses
   (Koli forest, OpenAIR) or a scattering reverberator; give tails a defined onset and let the treble die first.
4. **The forest tail is long:** 2.3 s at 1 kHz vs ~2 s measured in real forests (1.2-3 s) and 1.7 s in KCD2's
   `rvb_outdoor_forest_01`.
5. **TONE cuts the bass of every echo,** copying RoN's mix. Research: a 240 m hill echo keeps its low end (< 2 dB loss
   below 1 kHz) and loses its treble (about 2 / 4 / 13 / 46 dB at 1 / 2 / 4 / 8 kHz over the 480 m path).
6. **Caves and the hall are +8.4 dB louder than the bang** (matching RoN's big room). Research: the bang should stay
   the loudest part.
7. **Open field has no echo at all** (physically right, may feel empty). Outdoor level is one constant, not distance.
8. **The bang carries CoD's tail** (~0.5 s, only ~4 dB down at 300 ms). Surface absorption values are estimates, not
   KCD2 data.

## 8. Decisions and corrections (what turned out wrong)
| When | 9f said / decided | Outcome |
|---|---|---|
| 17:12 | The Deagle's reload sounds come from RoN, so its shots probably do too | Wrong. RoN has no Deagle; the shots are Call of Duty (`p25_pi_decho`). Corrected 17:16 |
| 17:16 | Keep the Deagle bang, borrow RoN's Colt Python echoes; make the forest echo ourselves | Became "make all echoes ourselves" after test 1 |
| 17:30 | "Of your installed games, these have a Deagle: PUBG, Rainbow Six Siege" | Wrong: leftover folders (5 files each). User: "i dont have pubg or rs6". Steam manifests list RoN, GTA V Enhanced, Unturned, Dead Island 2 among the shooters. Corrected 17:38 |
| 17:58 | A3 only adds bass below 150 Hz; use A2 | The user had said "2 and 3 sound the same" |
| 18:36 | Open-field tail "grass blowing" | 9f's own mistake (highs, level, 360 ms repeats) |
| 18:38-18:41 | Measure the space with rays instead of classifying places; echo made live from ray results | The model in section 6 |
| 19:28 | "Real echoes don't carry the deep boom" (as physics) | Wrong. It is RoN's mix choice; research says a 240 m echo keeps its lows. Corrected 19:40. **The bass cut is still in the code** |
| 19:12-19:48 | Blind test RoN rounds = RoN Colt echo on our Deagle bang | Unfair, the user heard the Colt. Fixed 19:48 |
| 19:12 | Didn't check the echoes were audible over the bang | Self-audit 20:28: the cause of "no echo" |
| 19:55 | "We can't use their files in the mod" | Wrong: the report lists the ripped games' tails as usable because the mod is private (keep them out of the repo). Corrected 20:01 |
| 19:58 | The Sonniss price "around $150" was "a guess"; forgot OpenAIR | Wrong: $149 had been found at 17:57; OpenAIR was named at 18:43. Corrected 20:01 / 20:28 |
| 20:13 | k-Wave not used | Right: never used; it hadn't come up before the user asked (20:12) |

9f ran behind the pxpipe proxy (history sent as images) from ~16:05 until the user took it off before 20:27. 9f's own
audit (20:28): the three memory misses at 19:58-20:01 were probably the compressor; PUBG / Siege, the blind test, the
"deep boom" claim and not checking audibility were bad calls, not memory.

## 9. Sources
- **The Outdoor Gun Acoustics Library** (Pole Position Production): https://sonniss.com/?p=15672 (streaming preview).
  A Sound Effect lists it at **$149, 244 files, 6.08 GB** (Sonniss: 399 files, 6.08 GB). Real guns on a forest road, in
  a big open area, a field with banks, a valley with forest at the far end. 9f recommends it as the real-world answer key
  (outdoors is where ours failed). Not bought; license for use in a mod not checked.
- **The Urban Gun Acoustics Library:** https://sonniss.com/sound-effects/the-urban-gun-acoustics-library/, $199: covered
  hall, deserted factory, open hall, small factory, small silo, open-air quarry. An Indoor Gun Acoustics Library exists too.
- **OpenAIR** (University of York, recorded impulse responses, mostly Creative Commons, license per entry):
  https://www.openair.hosted.york.ac.uk/?page_id=2. At 20:33 the site showed "Account Suspended"; 9f found the files at
  `https://webfiles.york.ac.uk/OPENAIR/IRs/<name>/<name>.zip` (Koli National Park forest IRs: CC BY 4.0 per 9f's
  archived-page read). Koli forest IRs: DAFx 2013 paper (link in report 2).
- **k-Wave** (wave simulation, asked about at 20:12 and 20:21): not used. It doesn't make gun sounds, only how a space
  changes one. On the user's RTX 3070 Ti (8 GB) / 15.9 GB RAM, 9f's estimate (3 points per wavelength, ~100 B per grid
  point): rooms up to ~2 kHz, village yard ~1 kHz, forest ~500 Hz, the 240 m hill not at all (~75 GB at 500 Hz).
  9f's view: skip until test 4; maybe later as an offline check of the cellar's low end.
- **KCD2's own reverb:** 151 hand-placed `ATLEnvironment` zones -> 47 FMOD snapshots on one listener-centred bus,
  plus `RVB_OUT` (1.5 s reverb + 360 ms, 43 % echo) and convolution reverb buses (`RVB_C_hollow`, `RVB_C_mines`). Values in research report 1 /
  `kcd2_existing_audio.md` section 3.2. The Deagle plays on the plugin's own FMOD core system (`native/src/main.cpp`
  "sounds" section), so it gets none of this; injecting into the game's FMOD Studio system is untried.
- **Clean Deagle candidates (not checked further):** FirearmFX Desert Eagle pack (itch.io, free, made from household
  items, not a real gun); MASTERGUN Desert Eagle on A Sound Effect (page wouldn't load; other MASTERGUN titles $40-125).
- **RoN Weapons.bank, Colt Python set (40 WAVs):** `Python_Fire_01-04`, `Python_Fire_Snub_01-04`, `Python_MidShot1-4`,
  `Python_MidTail_01-02`, `Python_MidReflection_01-04`, `Python_Reflection_{Ext,Int}_{Small,Base,Large/Big}_01-02`,
  `357_DistShot1-4`, `357_DistTail1-2`, `357_DistReflection_01-04`. 48 kHz stereo.

## 10. Research reports, main points
Report 1, "Fake the echoes, probe the world" (`~/reports/KCD2 gun sound propagation.md`):
- Shipped shooters (CoD MW 2019, Battlefield 6, Wildlands, Arma 3, Hunt) don't ray-trace sound: layered shot, a tail
  chosen by environment, 1-3 delayed echoes placed on big reflectors, ray-based muffling. RoN uses FMOD rooms and
  portals placed by hand. Full ray tracing (Steam Audio, Vercidium) is a later experiment.
- KCD2: FMOD Studio 2.02.21; vanilla `firearm_shot` cuts off at 75 m with no occlusion and no real echo; the forest
  snapshot (1.7 s, 160 / 88 ms, 1.1 kHz cut) is the tuning reference.
- Forests want a dark, diffuse, fairly short tail with no slapback (trees spawn no echo); open fields want a nearly dry
  shot plus late discrete echoes (0.3 s at 50 m, 3 s at 500 m).
- Build order: 0 time the game's rays + check FMOD DSPs on the mod's system; 1 split the shot (transient / mechanics /
  body, no baked tail); 2 4-6 tails ~6-12 dB under the shot; 3 ~32-64 probe rays pick the tail; 4 6-12 rays from the
  muzzle, 1-3 echo voices on hits > ~40 m (not vegetation), delayed 2d/343; 5 tune against KCD2's snapshots; 6 "gunshot
  bubble" (birds go quiet) + AI hearing; 7 occlusion only for non-player shooters; 8 optional Steam Audio.
Report 2, "Air, not stone, shapes gunshot echoes" (`~/reports/KCD2 gun sound material reflections.md`):
- Stone vs wood vs plaster differs by 0.1-1.2 dB per bounce; air over distance dominates. Use three surface classes:
  hard (slap), soft / grassy (duller), forested (weak smeared rumble, never a slap).
- Fixes: far echoes from body / low layers only, never the crack; per-band air loss (table per distance); 30-120 ms
  smear; no feedback repeats; keep the dry shot unfiltered and dominant; defined onsets, treble dies first; 1-3 echo
  voices; forest tail ~2 s; real IRs (Koli) instead of noise.

## 11. Test 4 (in progress at the handoff)
Plan (9f, 20:18 and 20:28, approved 20:31): outdoor echoes as 1-3 separate arrivals after the bang at RoN's level in
the 0.3-1 s window; the hill echo from the boom only, air-filtered (4 kHz -13 dB, 8 kHz -46 dB) and smeared over
30-120 ms; real recorded tails (OpenAIR / Koli, or RoN's echo files) instead of noise, forest 1.2-2 s; each place scored
per band and time window (0-50 ms, 50-300 ms, 0.3-1 s, 1-2 s) to within 3 dB of RoN before the user listens; blind test 4
with the Colt on both sides and a "Can't judge" button.
State at ~20:50 (all in 9f's scratchpad, temporary, not copied): `measure4.py` (band x window measuring), `ron_env.png`,
`dl_koli/koli_summer.zip`, `dl_openair/` (maes-howe, falkland-palace-bottle-dungeon, terrys-typing-room,
york-guildhall-council-chamber, elveden-hall-suffolk-england, trollers-gill, tyndall-bruce-monument, creswell-crags,
gill-heads-mine), unpacked to `ir_raw/`, `irlib.py` / `irlist.py` (load and measure the IRs), `ir_env.png`,
`sound_test4.py` (being written; one rewrite corrupted it and 9f recreated it), output in `st4_out/`. No Desktop folder yet.
The OpenAIR Read Me files state no license; only Koli's CC BY 4.0 was checked.

## 12. Open questions for the user
1. Distant echoes: keep the deep boom (physics, research) or cut it like RoN? (asked 19:40, unanswered)
2. Buy the Outdoor Gun Acoustics Library ($149) as the real-world reference? (offered as "go + library"; the user said "go")
3. A clean, echo-free Deagle bang: buy one, or keep the cut CoD shot?
4. Target: "as good as Ready or Not in the same environment", or real life where they differ (RoN's loud post-bang
   echo may be a design choice)?
5. Later: k-Wave cellar check, Steam Audio, HRTF for front / back?

## 13. Next steps
1. Finish test 4 (section 11): fix audibility first, score against RoN with `echo_audible.py` / band x window
   measurements, then run the blind test with "Can't judge". Save it as `tools/sound/sound_test4.py` and a new Desktop
   folder (11 is free).
2. Get the user's answers to section 12, questions 1-3.
3. In game, with the main Deagle session (it owns `deagle_main.lua` and `native/src/main.cpp`): time
   `Physics.RayWorldIntersection` (64 rays) and check that `fmod.dll` exports `FMOD_System_CreateDSPByType`,
   `FMOD_ChannelGroup_AddDSP`, `FMOD_DSP_SetParameterFloat`, `FMOD_Channel_SetDelay` for the plugin's own system.
   Never launch or close the game yourself.
4. A clean bang (layers without CoD's tail) before anything goes in game.
5. Plugin echo maker: probe rays every ~1 s pick tail / measure the space, 1-3 echo voices on reflectors > ~40 m placed
   in 3D at the hit points, plus a math-check harness (one wall at 20 m -> echo at 0.117 s; walls 15 m apart -> repeats
   every 0.087 s).
