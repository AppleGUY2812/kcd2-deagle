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
  notes page (https://app.notion.com/p/3f8955aa69318189a51ed4fce80d504d). Status marks: ✅ user-confirmed in game,
  🟡 built / waiting for the user's test, ⚪ not started. Tick a box only after the user confirms it; add new items
  for new work.

## Handy
- `tools/live.sh reload` / `tools/live.sh snippet.lua`: talk to the running game over the dev console (rcon). While
  the game window isn't focused the game is paused: timers and animation don't advance.
- Game log: `E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\kcd.log`; plugin log next to the exe.
- Video from the user: extract frames with ffmpeg and look at them (contact sheets with timestamps).
