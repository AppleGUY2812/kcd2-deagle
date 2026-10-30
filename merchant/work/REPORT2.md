# Merchant Resale — Round 2

- [V] Code commits: `0a1d61e`, review fix `0e6b10f` (report committed separately to avoid a self-referential hash).
- [V] Standalone C++17 logic and logging-only plugin source added; no Windows build, install, game launch, or game-state mutation performed.
- [I] Ruling: keep both requested hooks, but add proven `C_Shop` entry `0x180c406d8`; supplied `0x180a4a118` is only a labelled probe (otherwise shop-open evidence would be false).
- [I] Ruling: fixed-roster single-link 300m markets; stack units get separate rows using pre-sale demand; cap additions without deleting vanilla over-cap coins.

## Native answers (full evidence: docs/merchant_resale_native.md)
| Answer | V/I/U | Source |
|---|---|---|
| Class GUID: item+0x48 -> definition+8, 16 bytes | V | 0x182abc0bc, 0x1804695b4, 0x18070849c |
| Class name/UI key: definition vt+0xc8 -> record+0x18/+0xa0 | V | 0x182abc25c, 0x182abc354 |
| Base price: definition component 0x16 -> vt+0xc0 -> int+0x5c; deciGroschen interpretation | V/I | 0x180469510, 0x180fcd148, 0x18096f77c |
| Native shop-filter category/type/subtype accessor not resolved; logger emits U | U | 0x18023ca20, 0x181eea6b0; shop.xml filter rows |
| tx+0x10 resolves PLAYER-side owner, not merchant; keeper WUID from shop-open arg/+0x188; NPC name/32-bit ID/position unresolved | V/I/U | 0x180c47d14, 0x1808d285c, 0x180c406d8 |
| Supplied open address registers listeners; true shop entry is C_Shop vt0x183a949b8+0x38 -> 0x180c406d8 | V | 0x180bc8f88 disassembly, 0x180a4a118, 0x183a949f0 data |
| Purse: resolved shop+8 collection -> integer sum; exact checkout concrete type needs live confirmation | V/I/U | 0x18152ebfc, 0x1808c57ac |
| OnSaveAI/OnLoadAI serialize ScriptData in engine; real KCD2 Player save route and added-state roundtrip unproven | V/I/U | 0x18358320c, 0x183584328, 0x183583340; player.lua:920-932 |

## Printed verification
```text
markets=2 connected_merchants=3
fresh_plate_eta_days=2.302483 resale_coin=1200.000000
flood_count=30 armor_demand=0.150000 last_plate_eta_days=25.102867 demand_after_14_days=0.924870
buyback_rows_left=0 buyback_coin=0.000000
restock_bonus=1200.000000 restock_purse=2000.000000 capped_purse=8000.000000 capped_bonus=7200.000000
settlement_cap_added=10.000000 settlement_cap_purse=8000.000000
roundtrip_equal=1 serialized_bytes=2174 deterministic_etas=30 restored_resale_coin=36000 malformed_rejected=4
invalid_sales_rejected=2
ALL MERCHANT LOGIC TESTS PASS
```
- [V] Requested g++ command passed, also -Wextra -Werror -pedantic and ASan+UBSan; offline Lua commands: 8 assertions; manifest XML valid.
- [V] Existing inventory Lua suite: 46 checks, 0 failed; existing dice C++ suite: all passed (binary/cache redirected to /tmp/opencode).
- [V] Test-first initial compile failed because merchant_logic.h did not exist; later runs above passed.
- [U] Skipped Windows compilation/live sale-buyback/open/save-load validation (forbidden here), and full tests/run_all.sh (writes non-allowlisted native/build paths); no whole-suite claim.
- [U] Missing native fields remain U, not fabricated. Broader sym scans caused remote 20/30/60s timeouts; targeted queries recovered after scan completion.
- [V] Fresh read-only review: important build-identity gap fixed RED->GREEN; PE+RSDS gate before all hooks. Printed: wrong-build hooks=0, mismatches rejected=9, known-build installation attempts=4.
- [U] Deferred minor: failed keeper/instance-token reads may print 0x0 instead of U (no live mutations rely on them).
- [U] Deferred minor: Deserialize allows multi-unit rows beyond AddSale's per-unit invariant (offline model only).
