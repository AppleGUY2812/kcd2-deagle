# Brief (cloud session): native round simulation core, testable without the game

For a Claude Code cloud session on the code-only mirror (no game, no Windows, no MSVC). Read docs/ARCHITECTURE.md first
(the bar, milestone 1 step 6, Claude 1's contract) and src/Scripts/Deagle/ballistics.lua (`MoveRound`, `StepRound`,
`RoundsTick`, `LaunchRound`, `FlightHit`, `waterCross`): that Lua is the semantic reference.

## Build
`native/sim/` (new): a C++20 library with no engine or Windows headers.
- Rounds in structure-of-arrays (pos, vel, age, dist, hops, tumble, skip, phys index), a free list, no allocation per
  round or per step. 100,000 rounds alive must work.
- The same integrator as `MoveRound` (drag factor `1 / (1 + speed h / dragLength)`, gravity, midpoint move), steps of
  `ROUND_STEP`, dt capped like `ROUND_TICK_MAX`; gravity / drag in double.
- Sub-frame spawn: `Fire(pos, dir, phys, t)` with t inside the frame; the round's first step covers only its share.
- The world behind an interface: `struct World { virtual Hit Ray(from, move, skip) = 0; virtual Water(...) }`. The
  game will plug KCD2's ray (and later our own tracer) into it. Tests plug a scripted world.
- Hits come out as events in order (pos, normal, entity, surface, dist, energy, round id); the caller answers
  goesOn + new pos / vel / skip / tumble (Lua keeps ApplyHit). Randomness only from a seed the caller sets.
- Per-step statistics: rays asked, rounds stepped, ns per round (std::chrono), for the in-game counters later.

## Tests (must run in the cloud: g++ or clang, plus luajit)
- `native/sim/test/`: a harness that runs the same scripted shots through ballistics.lua (luajit with the mock in
  tests/) and through the C++ core, with the same scripted world, and compares every step's pos / vel / hit within
  1e-4 m. Add it to tests/run_all.sh only if the cloud image and the user's WSL both have a C++ compiler (check
  `which g++ clang++` in tests/run_all.sh and skip with a message if neither).
- A benchmark: 100,000 rounds, 600 frames, a world with no hits: ns per round per step.

## Rules
- Don't change ballistics.lua or Claude 1's tests; if the reference is ambiguous, write the question into
  `native/sim/QUESTIONS.md`. Commit on a branch `cloud/native-sim`, small commits, `bash tests/run_all.sh` passing.
- Write plainly in comments; numbers with sources.
