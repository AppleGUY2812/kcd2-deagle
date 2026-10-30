# Merchant Resale — Round 3 BLOCKED (2026-10-30)

- [V] Read Rounds 1–3, decided design, native evidence, existing model/plugin and supplied phase-1 runtime log.
- [U] Phase 2 is NOT implemented. The harness denied required read-only SSH/Ghidra queries and Linux g++ tests before execution (`permission.rejected: shell`).
- [V] The interactive permission question was also rejected: this is a non-interactive run. No permissions were bypassed and `opencode.json` was not changed.

## Step A: evidence / gaps
| Question | V/I/U and source |
|---|---|
| Checkout committed? | [V] Prior evidence: void checkout `0x1808d5150`, payment `0x181951800`, failure test `0x18197b1fc` (native research:65–72). [U] Exact result layout/success gate; entry logging is insufficient. |
| Native game days? | [U] `Calendar.GetWorldTime` native accessor, ABI and units not established. Round 1's Lua description is not a native calling contract. |
| Keeper -> entity/position? | [V] Open `0x180c406d8` stores keeper WUID at shop+0x188 (native research:47–58). [U] Resolution, entity/name/position and fixed market roster. |
| Native category? | [V] Definition at item+0x48, name getter `0x182abc25c`, price getter `0x180469510` (native research:24–42). [U] Class-table type path. |
| Credit purse? | [V] Getter `0x1808c57ac`; Round 1 identifies Lua binding `0x182efe260`. [U] Its native credit path, arguments, units and safe pre-open contract. |
| Remove instance? | [V] Token at item+0x30 (`0x1808d5150`, prior evidence). [U] Safe lookup/removal, missing-item behavior and partial-stack semantics. |
| Per-save identity? | [V] Local source load candidates `0x1825bdc7c`/`0x1825bda54` (samelevel.cpp:14–24,40–61). [U] Save counterpart, stable key and success/completion semantics. |

- [V] Additional local A3 lead: `G=*(u64*)0x1853322a0`; soul lookup `0x181f985d0(*(u64*)(G+0x80)+0x38,&wuid)` (`docs/whgame_re/stat_base.md:23–29`, `native/src/perkpoints.cpp:71–92`). Name field soul+0x360 (`stormmemo.cpp:251–252,289`, documented getter `0x180698f40`). [U] Keeper-specific validity, localization and soul -> entity bridge.
- [V] Load candidates are local-source leads, not newly verified decompilation; existing wrappers forward eight integers generically, not an exact recovered ABI.
- [V] `docs/LOADTIME.md:177–180` documents two load calls for cross-level Continue; an entry hook cannot alone establish successful load completion.
- [V] `native/src/questmenu.cpp:173–208` scans `playlineN/<filename>.whs`; it does not resolve the selected save at a verified save/load boundary.
- [I] Ruling/fallback: keep phase 1 unchanged for every unresolved dependency; no guessed offsets, global/manual sidecar, wall-clock timing or newest-save inference. Cost: no playable resale until research can run.

## Changes / checks
- [V] Only this report and the Round 3 appendix in `docs/merchant_resale_native.md` were authored; no plugin, model, settings, build, install or game-state changes.
- [U] Step B skipped: no verified commit, time, stock-write or per-save contract; enabling mutation would risk item loss, duplicate coins and cross-save contamination.
- [U] Step C: requested test execution blocked; there is NO Round 3 test output, no passing-test claim and no Windows compilation.
```text
nice -n 19 g++ -std=c++17 -O2 -Wall -I native/src native/test/merchant_logic_test.cpp -o /tmp/opencode/mlt_baseline && /tmp/opencode/mlt_baseline
permission.rejected: shell (command did not execute)
```
- [U] No whole-suite run or native runtime validation. Round 2 test numbers are historical and are not repeated as fresh results.
- [V] Denied query targets included `dec 0x1808d5150`, `str GetWorldTime`, `dec 0x182efe260` and `dec 0x1825bdc7c`; failure was harness permission, not a remote timeout or an empty search result.
- [V] No Windows program, game launch, game-folder write, installation or download was performed.
- [V] Native-evidence appendix commit: `b1ad7cb`; this report is committed separately to avoid a self-referential hash. Implementation commits: none.

## Resume / required in-game checks
- [I] First enable the brief's read-only SSH/qk and nice/g++ test commands externally; do not alter `opencode.json` here. Re-run Step A, implement verified paths, then run/record Step C before a Windows build by Claude.
- [I] After implementation/build only: test committed vs cancelled/rejected and mixed checkout; partial stacks/buybacks; 300m shared demand; lazy ETA settlement; missing/stolen/destocked instances; cap/reset bonus; resale 0/1 persistence; old-save/death reload, branching saves, failed save/load and missing/corrupt sidecars.
- [I] Do not test real resale with the current binary: it remains logging-only. The supplied phase-1 log is the only in-game evidence used this round.
