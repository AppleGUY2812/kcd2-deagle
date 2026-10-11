# Desert Eagle for KCD2: MODLOG

## Facts (verified from files)
- Game: Steam, E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2, wh_sys_version 1.5.6. Exe Bin\Win64MasterMasterSteamPGO\KingdomCome.exe, engine code in WHGame.dll.
- Saves: C:\Users\<user>\Saved Games\kingdomcome2. Backup: C:\Users\<user>\Documents\KCD2-save-backup-20261010 (restore = copy back).
- Mods: <game>\Mods\<modid>\mod.manifest + data\*.pak (stored zip). Scripts/Mods/<modid>.lua runs after Scripts/main.lua. Table patches: Libs/Tables/.../<table>__<modid>.xml.
- Log: <game>\kcd.log.
- Hand cannon item: MissileWeapon Class 13 ("rifle", two-handed, powder flask + ramrod reload, combat_gun action map: gun_prepare/gun_abort/gun_execute). Model manmade/weapons/firearms/hand_cannon_b.cdf.
- Lua binds present in WHGame.dll: Physics.RayWorldIntersection, Particle.SpawnEffect, entity LoadLight/FreeSlot/SetSlotPos, actor SetViewShake, System.Get/SetViewCameraFov, g_gameRules:CreateHit -> game:ServerHit, soul:DealDamage, AddImpulse, ActionMapManager.LoadFromXML/EnableActionMap/SetActionListener, PlayerStateHandler.PlayAnimationAction, CreateBoneAttachment/SetAttachmentObject, ExecuteAudioTrigger + AudioUtils.LookupTriggerID.
- Audio triggers that exist: c_f_firearm_shot, q_m32_pistala_shot, q_M09_pistala_shot.
- Input: Player:OnAction -> g_gameRules.Client.OnActorAction (we wrap it). Free keys: h, u, y, mouse4/5. R = torch (conflict risk for reload).

## Route
Scripted pistol in Lua (own action map + gamerules action hook), not the native hand-cannon pipeline (two-handed, ramrod reload = janky for a pistol).

## v0.1 (2026-10-10): gameplay core, no model
Console: deagle_probe, deagle_toggle, deagle_fire, deagle_reload, deagle_set <k> <v>, deagle_cfg, deagle_fx <name>, deagle_log_actions, deagle_input.
Keys (if LoadFromXML works): H draw, LMB fire, RMB aim, R reload.

## Open questions (need in-game test)
1. Does Scripts/Mods/deagle.lua load? ("[Deagle] loaded v0.1" in kcd.log)
2. Does ActionMapManager.LoadFromXML add our map without breaking defaults?
3. hitMode "hit" (CreateHit): do NPCs flinch/stagger/die/report crime? vs "soul".
4. Light on player slot 15 visible? SetViewShake strength ok?
