# Base (unbuffed) stat level: Lua, XP formula, save (static RE, WHGame 1.5.6, 2026-10-20)

Why: the Perk Points window (tools/points_ui.ps1) reads `player.soul:GetStatLevel(stat)`. That returns the buffed level,
so the window showed Strength 30 while the game showed "25 (+5)". Everything here was read statically from the KCD2 Ghidra
daemon on the ProLiant (`~/re/scripts/qk`, WHGame.dll sha256 bdf8f9e4a11257a7..., the same file as the local one) and from
the user's own saves, which were read only. VAs use image base 0x180000000. Nothing was run in game.

## Verdict
- **No soul Lua call returns the base level.** GetStatLevel returns base + buff modifiers, clamped to 0..StatCap (30).
  GetStatProgress returns the progress inside the base level (0..1), which is not enough to get the level back. Facts 1-5.
- **AdvanceToStatLevel compares against the BASE level**, not the buffed one. It adds `T(to) - T(base)` XP when `base < to`.
  So the window's own check `to <= GetStatLevel` is what blocks Raise. The game itself would have raised Strength 25 -> 26.
  Fact 4.
- **Stat XP formula** (fact 6): getting from level L to L+1 costs `(L+1)*Diff + Base` XP. The total XP for level L is
  `T(L) = Diff*L*(L-1)/2 + (Base+Diff)*L`. Strength uses Base 20 / Diff 40, Agility 20 / 35, Vitality 25 / 35 and Speech 25 / 40.
  Strength and Agility take their Base from the code default of 20.
- **The save stores no levels.** It stores each stat's total XP minus the total XP of Henry's starting level 5
  (Storm `dude.xml`), with XP x128. Loading replays that XP from level 0 (facts 7-9). Decoding autosave378 gives
  **Agility 21, Vitality 14 and Speech 14, matching the user's numbers, and Strength 18** (23 % into 19). The log shows
  where the 25 came from: one Raise after loading 378 moved Strength 18 -> 25 (7 levels), and 378 was then reloaded (fact 10).

## Facts
1. **PROVEN: the soul Lua bindings.** They are registered in FUN_1816759ec, using `RegisterFunction` FUN_180a4c140 / FUN_181676334
   with a functor at +0x10:
   GetStatLevel -> 0x182cf7518 (`JMP FUN_182cd6820`), GetStatProgress -> FUN_182cf7520,
   AdvanceToStatLevel("stat,level") -> FUN_182cee570, AddStatXP("stat,xp") -> 0x182cee4a8 (-> FUN_182cd3a00),
   GetNextLevelStatXP("stat,level") -> FUN_182cf6178, GetDerivedStat -> 0x180be3d54. Every one of them turns the stat name into an id
   with FUN_180be27b8 (an id of 10 or more means unknown). The soul comes from the Lua table's `__ThisWUID` (FUN_18041e964 -> FUN_18041ed20:
   `FUN_181f985d0(*(u64*)(G+0x80)+0x38, &wuid)`, G = the qword at 0x1853322a0).
2. **PROVEN: the live stat storage.** Each stat is a pair `{u32 level, u32 xpInLevel (x128)}` at **`soul + 0x56c + 8*stat`**
   (the struct at soul+0x560, field +0xc). Stat ids (Libs/Tables/rpg/stat.xml): 0 strength, 1 agility, 2 vitality, 3 speech,
   8 storyProgress, 9 prestige. The base getter is FUN_180469d7c:
   ```c
   if (stat == 7 && (*(uint*)(soul+0xcf0) >> 3 & 1)) return (uint)derivedStat(soul, 0x1c);
   return *(uint*)(soul + 0x56c + stat*8);
   ```
   **The soul+0x840 struct is a second copy of the same layout. It holds the starting ("template") values**, which the
   Storm generator writes: FUN_18046bdbc (`storm::C_AttributeOperator<Set>`) calls `FUN_180fa3148(soul+0x84c+i*8, value)`,
   and FUN_181fee550 does the same for soul2skald_soul_stat rows.
3. **PROVEN: GetStatLevel is base + modifiers, clamped** (FUN_182cd6820 -> FUN_180469d28 = FUN_180469d7c then FUN_180469dc4):
   ```c
   v = (float)base;
   for (m = *(soul+0xa8); m; m = m->next /*+0x20*/)
       if (m->active /*+0x10*/ && m->stat /*+0x28*/ == stat) FUN_180469f2c(m, base, &v, ctx);  // add/sub/mul/min/max/set
   v = max(v, 0); v = min(v, (float)rpg.StatCap /*rpg+0x138 = 30*/); return v;
   ```
   The modifier weight (FUN_180469ff8) comes from the call context. Lua can only fill `opponentSoulId` / `opponentWeaponId`
   (optional 2nd argument, a table). The context field that turns every modifier off (ctx+0x18 == 1) is always 0 from Lua.
   The optional 3rd argument (a table) gets a list of strings from a trace collector (FUN_1808988b8). Neither argument gives the base.
   The fractional part in the log (stat0/1/3 = x.3392, then x.327, then x.3185) is one fading buff weight on Str/Agi/Speech.
4. **PROVEN: AdvanceToStatLevel compares against the base** (FUN_182cee570):
   ```c
   if (stat < 10 && (cur = FUN_180469d7c(soul, stat)) < to) {        // BASE level, raw soul+0x56c
       FUN_180fa30dc(rpg, &a, cur, stat); FUN_180fa30dc(rpg, &b, to, stat);
       xp = b - a; FUN_182cd3ae0(..., &xp);                             // = AddStatXP path, level-ups give perk points
   }
   ```
   It ignores the XP already inside the current level, so the stat lands on `to` with that remainder kept.
   When `to <= base` it does nothing, which makes it a no-op probe, never a lowering.
5. **PROVEN: GetStatProgress = xpInLevel / need(base)** (FUN_180469c04: `(xp<<7)/need * (1/128)`; DAT_18409a450 = 0x3c000000 = 1/128).
   GetNextLevelStatXP(stat, L) = need(L) (FUN_180469c64 * 1/128). It is pure formula and never reads the soul. Together they cannot
   recover the base without a third value.
6. **PROVEN: the XP formula.** All values are fixed point x128.
   - FUN_180469ca0 picks `{Base, Diff}` by stat: 1 -> rpg+0x108/0x10c, 2 -> 0x118/0x11c, 3 -> 0x120/0x124, 8 -> 0x128/0x12c,
     9 -> 0x130/0x134, everything else (strength) -> 0x110/0x114.
   - Names: in FUN_180ce3f40 each param's name string is loaded just before its registration call (`R9D` = offset).
     The order was checked on `StatCap` -> 0x138 and `SkillCap` -> 0xb4. The names are AgilityXPLevelBase 0x108, AgilityXPLevelDiff 0x10c,
     StrengthXPLevelBase 0x110, StrengthXPLevelDiff 0x114, VitalityXPLevelBase/Diff 0x118/0x11c, SpeechXPLevelBase/Diff 0x120/0x124,
     StoryProgressXPLevelBase/Diff 0x128/0x12c, PrestigeXPLevelBase/Diff 0x130/0x134, StatCap 0x138.
   - Defaults (rpg params ctor FUN_1810bc164): every Base = 0xa00 (20.0), every Diff = 0xf00 (30.0), StatCap = 30.
     rpg_param.xml overrides AgilityDiff 35, StrengthDiff 40, Vitality 25/35 and Speech 25/40. **So Strength Base = 20 and Agility Base = 20.**
   - need(L) (FUN_180469c64) = `((L+1)*Diff + Base)`. T(L) (FUN_180fa30dc) = `Diff*L*(L-1)/2 + (Base+Diff)*L`. Both are in x128 units.
   - Level-up loop FUN_180c65888: while `level < StatCap`, if `xp + add >= need(level)`, then level++, the carry-over goes on, and xp = 0.
     | stat | need(L)      | T(L)                 | T(5) |
     |------|--------------|----------------------|------|
     | Str  | 40(L+1)+20   | 20L^2 + 40L          | 700  |
     | Agi  | 35(L+1)+20   | 17.5L(L-1) + 55L     | 625  |
     | Vit  | 35(L+1)+25   | 17.5L(L-1) + 60L     | 650  |
     | Spe  | 40(L+1)+25   | 20L(L-1) + 65L       | 725  |
7. **PROVEN: where the stats sit in the save.** Path: `0x01f4 > 0x01f8 > 0x7308 > 0x3529 > 0x1161` (all souls, 6379 records).
   Henry's soul is the `0x115e` record whose value starts with his GUID twice, as bytes
   `fb cf 2d 4c a1 de 63 62 72 d7 b3 9f 4d b2 d8 b5` (soul.xml `4c2dcffb-dea1-6263-72d7-b39f4db2d8b5`, player_henry).
   Its name record 0x12f9 says "Dude". The other "Dude" (GUID ...4db2d666) is player_bohuta.
   Inside it: `0x12fb > 0x0927` holds:
   - `0x1385` **stat XP map**: (i32 stat, i32 xpDiff x128) pairs up to i32 -1
   - `0x138b` six floats (vitals)
   - `0x138d` skill XP map (same scheme)
   - `0x137e` perks
   The writer is FUN_181fdd030 -> FUN_1816e8844(soul+0x560, node, soul+0x840) -> FUN_181fdca80(tag 0x1385) -> FUN_181fdec30.
   For every stat whose `{level, xp}` differs from the template, it writes `i` and `Total(cur) - Total(template)`, where
   Total = T(level) + xpInLevel (FUN_180fa3080).
8. **PROVEN: how the save loads.** FUN_181fdfd80 is a switch on the tag. For `case 0x1385` it reads (i, diff) until -1 and calls FUN_180fa2f7c:
   `total = Total(template_i) + diff; cur_i = {0, 0}; FUN_180c65888(cur_i, total)`. So the **base level = the level T(L) reaches
   from 0 with T(template) + diff**.
9. **PROVEN (file): Henry's template is 5** for strength, agility, vitality, speech and storyProgress:
   Libs/Storm/abilities/dude.xml (IPL_GameData.pak), rule `player_henry`, `<setAttribute stat="strength" value="5" />` and so on.
   The NPC rules in base.xml and combatLevel*.xml are all `isNotPlayer`.
10. **PROVEN against the save** (autosave378, 14:57; autosave377 is identical): the map is `{0: 855086, 1: 1057725, 2: 448480, 3: 521776, 8: 173888}`.
    That decodes to **Str 18 (180/780 XP into the level), Agi 21 (383/790), Vit 14 (129/550), Speech 14 (251/625)**.
    Agi, Vit and Speech match the user's numbers. The template is pinned between 4 and 6 by these three stats, and 5 is the file value.
    Older saves rise steadily (autosave365: Str 17, Agi 21, Vit 13, Speech 14).
    **INFERRED (kcd.log 804-824)** for Strength 25:
    - After 378 was loaded (line 759), stat0 read 24.34. That is 18 + 6.34 of buffs.
    - Then stat0 read 30, main level went 18.90 -> 21.64, the real perk list gained 3 stat0 points, and the reply was "lvlset stat0 30".
    - That fits one window Raise of Strength to 25: AdvanceToStatLevel added T(25) - T(18) = 6300 XP, i.e. 7 levels.
    - 25 + 6.34 clamps to 30, and the stat screen shows that as "25 (+5)", the cap minus the base.
    - Line 824 reloaded autosave378, so live Strength is 18 again unless the user has played and saved since.
11. **INFERRED, not used:** the 3 "perk lists" perkpoints.cpp finds are probably the live list (soul+0x560+0x1f8) and the template list
    (soul+0x840+0x1f8) plus one more. That is not checked, so do not derive the soul pointer from them.

## Recommendation
1. **Raise (Lua only, works now):** call `player.soul:AdvanceToStatLevel('<stat>', to)` without the window's
   `to <= GetStatLevel` pre-check. The game does its own check against the base (fact 4) and does nothing when `to <= base`.
   Stop computing `to` as "GetStatLevel + 1": with buffs that skips levels. On 2026-10-20 it jumped 18 -> 25.
   Let the user type the base number the stat screen shows (the 25 of "25 (+5)").
   - Check success with `GetStatProgress` / `GetDerivedStat('lvl')` changing.
   - Do not rely on GetStatLevel to check it. It stays at 30 when clamped.
2. **Showing the base live** needs native code. Add a DN function to deagle_native that reads `*(u32*)(soul+0x56c+8*stat)` (level)
   and `*(u32*)(soul+0x570+8*stat)/128` (XP in level). Or it can call FUN_180469d7c(soul, stat).
   - Get the soul the way the game does: Lua passes `player.soul`; the native reads its `__ThisWUID` (a handle) and calls
     `FUN_181f985d0(*(*(DAT_1853322a0)+0x80)+0x38, &wuid)`.
   - FUN_18041ed20 (RVA 0x41ED20) is a 60-byte function holding both the global and the call. An AOB for it gives both by rel32 decode.
   - Untested; needs one in-game check against the stat screen.
3. **Showing the base from the save** (what the user asked for; exact as of the save, stale after any Raise or level-up since):
   ```python
   B = {0: (20, 40), 1: (20, 35), 2: (25, 35), 3: (25, 40)}           # stat: (XPLevelBase, XPLevelDiff)
   T = lambda L, s: B[s][1] * L * (L - 1) / 2 + (B[s][0] + B[s][1]) * L
   def base(diff_x128, s, tmpl=5, cap=30):                              # diff from the 0x1385 map
       x, L = T(tmpl, s) + diff_x128 / 128, 0
       while L < cap and T(L + 1, s) <= x: L += 1
       return L, (x - T(L, s)) / ((L + 1) * B[s][1] + B[s][0])          # level, progress (= GetStatProgress)
   ```
   - To find the map: kcd2_save.read(); find the Henry GUID bytes preceded by `5e 11` (tag 0x115e); the first `85 13` after it
     is tag 0x1385, followed by its u32 length and the (i32, u32) pairs.
   - A good cross-check is `progress == player.soul:GetStatProgress(stat)` live: if they match, the save base is still current.
