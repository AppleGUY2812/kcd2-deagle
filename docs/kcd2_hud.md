# KCD2's HUD and how a gun HUD can use it

Sources: `IPL_GameData.pak` (`Libs/UI/UIElements/HUD.xml`, `Libs/UI/hud.gfx`, decompressed CFX → AS2 symbols;
`Libs/UI/fontconfig.xml`) and WHGame.dll bind strings (1.5.6). **Guess** marks inference. Live probes are in section 4.

## 1. How the HUD is built
- One **Scaleform (AS2) movie** `Libs/UI/hud.gfx` (184 KB, `CFX`, ActionScript 2 `__Packages.*`), element `hud`
  (`<UIElement name="hud" render_lockless="0" mouseevents="1">`, `<GFx file="hud.gfx" layer="25">`, fullscreen
  align).
- Text uses Scaleform fonts packed in `gfxfontlib*.gfx` (`fontconfig.xml`: DefaultFont → "Kingdom Come Regular",
  LightFont → "Kingdom Light Regular", DisplayFont → "Kingdom Come Display", Manuscript → "Warhorse Manuscript").
- Movie clips (XML aliases → instance paths) relevant to us:

| alias | instance | what |
|---|---|---|
| `QAMWeapon` | `bc.mc_StatsContainer.qamWeapon` | weapon quick-slot, bottom centre next to the stats. AS2 has `m_MainIcon / m_MainCount / m_OffIcon / m_OffCount` (**guess**: main / off-hand item + count, e.g. arrows) |
| `QAMFood` | `bc.mc_StatsContainer.qamFood` | food quick-slot |
| `Stats` / `HealthBar` | `bc.mc_StatsContainer.mc_StatsEle(.mc_HealthBar)` | stat bars |
| `ItemTransfer` | `bl.itemTransfer` | bottom-left "item received / lost" notifications |
| `InfoText` | `cccursor.infoText` | centre info text |
| `Cursor` / `CursorCross` / `CursorOutline` | `cccursor.actioncursor(.ActionCursorCross / .ActionCursorOutline)` | interaction cursor and centre cross |
| `CombatCursorCross` (+ `.pbslot`, `.lock`) | `cccursor.actioncursor.CombatCursorCross` | combat centre cross (perfect-block slot, lock-on) |
| `Hints` | `br.actionHints` | bottom-right key hints |
| `ShootingContest` | `bl.shootingContest` | archery contest table |
| `MasterSwitch` | `all` | the whole HUD |

- HUD functions (`HUD.xml`, called as `fc_*` in AS2), the ones useful to us:

| function | params |
|---|---|
| `ShowItemCount` / `HideItemCount` | `Id` string, `UiName` string, `IconId` int, `Count` int / `Id` |
| `ShowInfoText` / `HideInfoText` | `Text` string, `Priority` int (higher wins), `Duration` int ms, `Background` bool |
| `ShowNotification` | `infoText` string |
| `ShowShootingHit` | `xPos`, `yPos` float -1..1: the archery-contest hit marker on the target card |
| `SetQAMSlots`, `SetSelectedQAMSlot` | `Type` (1 weapon, 2 food), `SlotId` 0-3 (driven by the game's inventory; leave alone) |
| `SetGameMode` | `Mode` int |
| `SetActionHints`, `SetProgress` | action hints |

- **Lua side:** CryEngine `UIAction` binds are present (arg names from the DLL):
  - `UIAction.CallFunction(elementName, instanceID, functionName, ...)`
  - `UIAction.SetVariable(elementName, instanceID, varName, value)` / `GetVariable`
  - `UIAction.SetVisible(elementName, instanceID, mcName, bVisible)` / `IsVisible`
  - `UIAction.SetScale(elementName, instanceID, mcName, vScale)`
  - `UIAction.SetPos(elementName, instanceID, mcName, vPos)`
  - `UIAction.SetAlpha(elementName, instanceID, mcName, fAlpha)`
  - `UIAction.SetRotation(..., vRotation)`
  - `UIAction.GotoAndPlay` / `GotoAndStop(..., mcName, frameNum)` (+ `...FrameName`)
  - `UIAction.ShowElement` / `HideElement(elementName, instanceID)`
  - `UIAction.ReloadElement` / `UnloadElement`
  - `UIAction.RegisterElementListener(scripttable, elementName, instanceID, eventName, callback)`

  `instanceID` -1 = all instances (CryEngine convention). `mcName` is the XML alias (e.g. `"CursorCross"`).
  KCD2's own Lua barely uses UIAction; the native GUI module drives the HUD.

## 2. Options for our ammo / fire-mode display
**(a) Reuse a HUD function: best first try.**
- `UIAction.CallFunction("hud", -1, "ShowItemCount", "deagle", "Desert Eagle", <iconId>, ammo)` on every ammo
  change; `HideItemCount("deagle")` on holster.
- What it draws is **unknown until probed**. The AS2 code sits with `fc_showItemTransfer`, so it may be the
  bottom-left item counter. The QAM weapon slot's `m_MainCount` is another candidate (**guess**). Either way it's KCD2's
  font, frame and fades.
- Risks:
  - The game may call `HideItemCount` / reset (`ResetAll`) on state changes: dialog, menus, mounting. Re-show on our
    next ammo change or every 2 s while drawn.
  - `IconId` semantics are unknown (int, maybe an index into an icon atlas). Try 0 and an item's icon id.
- Fire mode / reload / "Jammed": `ShowInfoText("SEMI", 5, 1200, false)`, or `ShowNotification("Burst")`. Centre /
  queue text in the game's style.

**(b) Our own element (most control, most work).** Author a small AS2 `.swf` (KCD2 movies are AS2 / Scaleform 4;
`.gfx` = CFX-compressed SWF), e.g. with JPEXS FFDec or by hand (Ming / SWFTools). Embed or import KCD2's font via
`gfxfontlib.gfx`. Ship `Libs/UI/DeagleHud.gfx` + `Libs/UI/UIElements/DeagleHud.xml`
(`<UIElement name="DeagleHud"><GFx file="DeagleHud.gfx" layer="26"><Constraints><Align mode="fullscreen"/></Constraints></GFx><functions><function name="SetAmmo" funcname="fc_setAmmo"><param name="Mag" type="int"/><param name="Reserve" type="int"/></function>...</functions></UIElement>`),
then `UIAction.ShowElement("DeagleHud", 0)` + `CallFunction`.
- Risks: the authoring toolchain, font import, and UI element discovery. Our pak's `Libs/UI/UIElements/*.xml` must
  be picked up; CryEngine loads every XML in that folder (**guess**: true in KCD2).

**(c) Keep DrawText.** `System.DrawText` uses CryFont (`Fonts/*.xml`) fonts, **not** the Scaleform "Kingdom Come"
fonts. KCD2 ships no CryFont version of them in the paks I listed, so the look can't match. Only placement (bottom
centre, above the stat bars ≈ y 0.92) and colour (off-white ~0.86/0.82/0.72, soft shadow) can be copied. Fallback only.

## 3. Crosshair
- The HUD has a centre cross: `CursorCross` (interaction) and `CombatCursorCross` (combat; with `.pbslot` and `.lock`
  children). KCD2's bow and hand cannon have **no dedicated aim crosshair** (no aim / reticle / bow symbols in
  hud.gfx).
- From Lua: `UIAction.SetVisible("hud", -1, "CombatCursorCross", true)`, then scale it with the current spread every
  frame: `UIAction.SetScale("hud", -1, "CombatCursorCross", { x = s, y = s, z = 1 })` with
  `s = 1 + k * CsAccuracy()` (CS2-style bloom). This scales the whole cross, not just the gap. A true gap-growing
  CS2 cross needs four arms, so (b) or the plugin's own drawing.
- Risk: the game shows / hides the cursor itself (interaction prompts, combat lock-on). Re-assert visibility while
  aiming, and restore it on holster.
- Hit marker: `ShowShootingHit(x, y)` belongs to the contest table (bottom-left), so it's not usable as a centre hit
  marker.

## 4. First live experiment (console, harmless UI calls; revert lines included)
```lua
-- 0. read-only: is the element there?
#System.LogAlways(tostring(UIAction.IsVisible("hud", -1, "QAMWeapon")))
-- 1. the counter (watch bottom-left and the weapon quick-slot)
#UIAction.CallFunction("hud", -1, "ShowItemCount", "deagle", "Desert Eagle", 0, 7)
#UIAction.CallFunction("hud", -1, "HideItemCount", "deagle")
-- 2. info text in the game's style
#UIAction.CallFunction("hud", -1, "ShowInfoText", "SEMI-AUTO", 5, 1500, false)
-- 3. crosshair visibility + scale
#UIAction.SetVisible("hud", -1, "CombatCursorCross", true)
#UIAction.SetScale("hud", -1, "CombatCursorCross", { x = 2, y = 2, z = 1 })
#UIAction.SetScale("hud", -1, "CombatCursorCross", { x = 1, y = 1, z = 1 })
#UIAction.SetVisible("hud", -1, "CombatCursorCross", false)
```
Record what appears and where, then pick (a) or (b). If `ShowItemCount` shows nothing, try `IconId` from an item
(e.g. the shot_ball item's icon) and instance 0 instead of -1.
