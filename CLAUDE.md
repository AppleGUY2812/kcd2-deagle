# Working on this repo

Read `README.md` (layout, build, test), `TODO.md` (status) and `MODLOG.md` (engine facts, dead ends) first.

## The user's rules
- Never launch or close the game. Say when a restart is needed and let the user do it. Install only while
  `KingdomCome.exe` isn't running (`tasklist.exe | grep -i kingdom`).
- Show visual checks: renders / contact sheets go to `C:\Users\<user>\Desktop\Deagle Progress` (or are sent to the
  phone). Don't just describe them.
- No janky results: animation changes go through `tools/build_vm.sh --audit` (transition audit) and the mock tests.
- Ripped assets (CS:S / Call of Duty / Insurgency / MW19 / TACZ): private use only, never publish.

## Handy
- `tools/live.sh reload` / `tools/live.sh snippet.lua`: talk to the running game over the dev console (rcon). While
  the game window isn't focused the game is paused: timers and animation don't advance.
- Game log: `E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\kcd.log`; plugin log next to the exe.
- Video from the user: extract frames with ffmpeg and look at them (contact sheets with timestamps).
