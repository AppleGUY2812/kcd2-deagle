# First save load: findings (current state only; replace, don't append)

Goal 20 s from Continue to control. Owners: steps 1-4 kcd2-deagle-5a, step 5 kcd2-deagle-66. Dashboard:
https://claude.ai/artifact/L2V5buU7a27KcbqmSVdSB4. RE notes: ProLiant ~/re (KCD2.gpr), WHGame.dll 1.5.6.
Marks: M measured, S read in code, I inferred.

## Where the 48.3 s go (22:00 boot, 2026-10-23; s after Continue)
| Step | s | Thread | Bottleneck |
|---|---|---|---|
| 1 open level | 0-2.5 | Main | XML + pak reads (M) |
| 2a terrain loaders | 2.5-6.0 | IO thread | 14,153 CGF-header reads, one at a time, unbuffered (S,M) |
| 2b objects + entities | 6.0-16.4 | Main | busy 7.2 s, idle 3.2 s, cause unproven (M) |
| 2c wait for quests | 16.4-20.3 | Main idle | quest job (below) (M) |
| 2d finish level | 20.3-22.9 | Main | quest/concept objects, XML (M) |
| 3 first frames | 22.9-25.5 | Main waits | PSO-cache parse 1.1 s on the loading thread (M) |
| 4 apply save | 25.5-32.6 | Main | 4,797 quest XML loads, port/edge resolve, ID registry (M) |
| 5 settle | 32.6-48.4 | Main | readiness gate 0x3f93 waits for every streaming task incl. textures (S) |

## Quest job (ConceptLoadJob), 3.5-20.2 s, one worker (S unless marked)
- Submitted when the save's `*m_ConceptManager` is read (FUN_1804da314 -> FUN_18183631c); main waits in FUN_1811414d8.
- 2 projects: barbora.xml (21,648 files), tavernchat (160). Parallel projects: ~1 % gain.
- Phase 1 XML tree ~4.8 s (FUN_180979068): parse is pure per file -> xmlahead serves it (full list since 23:45).
- Phase 2a node build ~6.7 s (rttr deserializer FUN_181e25520), 2b CreatePorts/Edges ~5 s: one recursion,
  unlocked state, non-atomic CXmlNode refcount -> not splittable by hooks.
- Allocator 30-40 % of phase 2 (BucketAllocator FUN_1804f7610, lock-free SLIST; >0x200 B via DAT_18549d368) (M).
- rttr type registration re-sorts on every insert (~0.4 s) (M).
- Our plugin was 11 % of phase-1 samples in boot 11 (M): hook overhead to check.

## Built (installed 2026-10-24 00:0x, plugin 748,544 B, fc784b1)
- fastzip (libdeflate): 61 -> 54 s (M). pakahead (unpack ahead, needs >= ~4 GB free RAM): 54 -> 47 s (M).
- xmlahead full list (33,012 files), streamahead (records next boot, serves after), loadwatch quest timing,
  readiness gate log + `readiness early` (off), mainreads, streamio counting. Untested in game.

## Open, by expected gain
1. Step 5 deferral (`readiness early 1`): -13 s on paper; pop-in to judge (kcd2-deagle-66).
2. Quest job earlier/shorter: start at Continue (-3.5 s), allocator (-2..4 s?), xmlahead phase 1 (-4.4 s).
3. Step 2b idle 3.2 s: cause unknown.
4. Textures on worker threads (re_renderload design b).
5. streamio 1 (bigger reads): ~-3 s (peer test).

## Measuring rules
- RAM: pakahead needs free RAM - 3.5 GB >= 512 MB at start; below that the boot is not comparable.
- One change per boot. Boot log: scratchpad bootN_log.txt, samples bootN_samples.csv (threadsample.ps1).
