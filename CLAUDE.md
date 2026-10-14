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
  notes (https://app.notion.com/p/3f8955aa69318189a51ed4fce80d504d). Those notes are a short hub (Needs you, one child
  page per test area with a gray status line, who does what) plus the area pages and a Log page: read the hub and only
  your own area pages; anything the user must do goes into the hub's "Needs you" and gets a "👉 NEW" tag on the area
  page; finished news goes to the Log. Never drop a `<page>` line from the hub (it deletes that child page). Status marks: ✅ user-confirmed in game,
  🟡 built / waiting for the user's test, ⚪ not started. Tick a box only after the user confirms it; add new items
  for new work.
- Keep the wiki current, always: whenever a command, key or setting is added or changed, update the commands wiki
  artifact (https://claude.ai/artifact/2pJ2zJeNNAfxa5MD9VyitH; source `docs/DEBUG.md`) in the same session.

## Who edits what (two Claude sessions can run at once)
- `src/Scripts/Deagle/ballistics.lua` + `guns/penetration.lua`: rounds in flight, ricochets, corners, wallbangs,
  armour (the "ricochet" session). It uses `deagle_main.lua`'s helpers through `Deagle.U`: keep that table in sync.
- `src/Scripts/Deagle/deagle_main.lua`: everything else (viewmodel, sounds, effects, holes, NPC reactions, tracers).
- Commit only your own hunks (never `git commit -a`), keep `bash tests/run_all.sh` passing after every edit (a hot
  reload loads both sessions' work), and tell the other session before touching its file.

## Handy
- `tools/live.sh reload` / `tools/live.sh snippet.lua`: talk to the running game over the dev console (rcon). While
  the game window isn't focused the game is paused: timers and animation don't advance.
- Game log: `E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\kcd.log`; plugin log next to the exe.
- Video from the user: extract frames with ffmpeg and look at them (contact sheets with timestamps).
