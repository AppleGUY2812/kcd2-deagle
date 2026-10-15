# Working on this repo

Read `README.md` (layout, build, test), `TODO.md` (status) and `MODLOG.md` (engine facts, dead ends) first.

## The user's rules
- Never launch or close the game. Say when a restart is needed and let the user do it. Install only while
  `KingdomCome.exe` isn't running (`tasklist.exe | grep -i kingdom`).
- Show visual checks: renders / contact sheets go to `C:\Users\<user>\Desktop\Deagle Progress` (or are sent to the
  phone). Don't just describe them.
- No janky results: animation changes go through `tools/build_vm.sh --audit` (transition audit) and the mock tests.
- Ripped assets (CS:S / Call of Duty / Insurgency / MW19 / TACZ): private use only, never publish.
- Keep the user's Notion checklist current, always: after any change that moves an item (built, installed, user-confirmed),
  update "kcd ii gun checklist" (https://app.notion.com/p/c1db163ef3524a5cbbfd4389e48bca0f) and the "KCD II Deagle mod"
  notes (https://app.notion.com/p/3f8955aa69318189a51ed4fce80d504d). Those notes are one page (the user's choice): "Needs you"
  at the top (every action for the user), then one section per test area (0-6, owner square 🟥 / 🟦), then a collapsed
  "Log" toggle for history. Anything the user must do goes into "Needs you" and gets a "👉 NEW" tag in its section. Status marks: ✅ user-confirmed in game,
  🟡 built / waiting for the user's test, ⚪ not started. Tick a box only after the user confirms it; add new items
  for new work.
- Keep the wiki current, always: whenever a command, key or setting is added or changed, update the commands wiki
  artifact (https://claude.ai/artifact/2pJ2zJeNNAfxa5MD9VyitH; source `docs/DEBUG.md`) in the same session.

## Who edits what (four Claude sessions can run at once)
Scripts are one file per system in `src/Scripts/Deagle/` (README "Where things are"); `deagle_main.lua` loads them. A new
script file needs an owner here: `tests/run_all.sh` fails otherwise, or when this list names a file that's gone.
- 🟥 `ballistics.lua`, `killcam.lua` + `guns/penetration.lua`: rounds in flight, ricochets, corners, wallbangs,
  armour (the "ricochet" session). They use the shared helpers through `Deagle.U` (set in `deagle_main.lua`): keep
  that table in sync.
- 🟩 Sound (the "sound" session): `sound.lua`, the `vmSoundCues` table at the top of `deagle_main.lua` and the `snd`
  column of `surfaces.lua`; `guns/ron_handling.lua`, `guns/shot_gain.lua`; `native/src/ron.cpp` and the sounds
  section of `native/src/main.cpp`; `tools/sound/`, `tools/ron/FmodRender`, `tools/ron/FmodCoexist`,
  `tools/ron/fsb*.py`, `tools/ron/fmodbank.py`; the sound docs (`docs/ron_fmod_events.md`, `mk5_sound_spec.md`,
  `sound_echo_plan.md`).
- 🟦 Impact effects (the "effects" session): `effects.lua` and the `fx` column of `surfaces.lua`; `deagle_impacts.xml`
  and its textures, the effects tools (`tools/ron/mk_*fx*.py`, `tools/ron/mk_water*_tex.py`, the fx converter).
- 🟪 Everything else: `deagle_main.lua` (settings, `Deagle.U`, input, start-up, commands), `flash.lua`, `recoil.lua`,
  `weapon.lua`, `viewmodel.lua`, `hits.lua` (and the `hole` column of `surfaces.lua`), `npc.lua`, `tracers.lua`,
  `shells.lua`, `freecam.lua`, `physics.lua`, `debug.lua`, `motion.lua`, and the rest of `native/`.
- Commit only your own hunks (never `git commit -a`, check `git diff --cached --stat` first), keep
  `bash tests/run_all.sh` passing after every edit (a hot reload loads every session's work), and tell the owner before
  touching their file or functions.

## Handy
- `tools/live.sh reload` / `tools/live.sh snippet.lua`: talk to the running game over the dev console (rcon). A reload
  sets the user's kept settings (deagle_settings.txt) again by itself, like a normal load. While
  the game window isn't focused the game is paused: timers and animation don't advance.
- Game log: `E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\kcd.log`; plugin log next to the exe.
- Video from the user: extract frames with ffmpeg and look at them (contact sheets with timestamps).
