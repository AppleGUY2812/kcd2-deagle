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

## Asset research (2026-10-10)
- Audio is FMOD Studio 2.02.21 (not Wwise). New sounds need a custom bank via KCD2ModLoader rom.audio.load_bank; vanilla paks can only reuse events (c_f_firearm_shot -> event:/combat/weapons/firearms/firearm_shot).
- Muzzle particle: firearms.hand_cannon.frontParticleEmit (Libs/Particles/firearms.xml), emitters carry LightSource Radius=5.
- Hand cannon model: hand_cannon_b.cdf -> hand_cannon.chr + hand_cannon_a.skin; attachment frontParticleEmit on hand_cannon_barrel bone.
- Modding Tools (Steam app 2429020) NOT installed; needed for rc.exe. Blender exporter: Caseter/KCD2-Blender-Toolkit v0.2.10 (Blender 4.3).
- Models: Sketchfab CC-BY Deagles (ELIZION cabde59f..., Minte 334462df..., LukaszKmak 80697a48...). TACZ assets CC BY-NC-ND: private use only; use its timings as reference (slide back 0.017-0.067s, return 0.183s).

## v0.5-0.7 (2026-10-10)
- Keys load on OnGameplayStarted (Script timers are cleared on save load). Player.OnAction wrap works.
- Damage: soul:DealDamage(stamina, health, attackerId, suppressHitreaction=false, bodyPart) works; ServerHit/CreateHit does nothing to souls. Verified in game: headshot kills, torso 60 -> 40/100.
- Items via Libs/Tables/item/item__deagle.xml (MiscItem) + Localization/english_xml.pak text__deagle.xml: verified in game.
- Ammo item: CreateInvItem(…, 49) -> GetCountOfClass returns 1 (stack); disabled (useAmmoItems=false).
- Reload is clock-based (FinishReload polled on input) so save loads can't leave it stuck.
- Blood: collisions.combat.hit_passed_bullet_fabric/_plate, collisions.combat.blood_head, actor:AddBlood(amount, part), bleeding buff 0c903899-…; impacts collisions.destructibles.bullet_<surface>. Rivers of Blood (workshop 3630633973) overrides collisions.xml with HD versions of these.
- Game.SendInfoText queues; call Game.HideInfoText first and only on state changes. Real HUD = custom Scaleform widget later.
- User installing KCD2 Modding Tools to E: (2026-10-10).

## Animation route (verified live 2026-10-10)
- Human:PlayAnim(fragment, tags) does nothing on the player.
- Entity:StartAnimation(0, "<caf name>", layer 9..15, blend, speed, loop) DOES drive Henry's arms on top of mannequin (tested rifle_shot_over). Name = file name without path/ext.
- male.chrparams auto-registers every Animations/humans/male/**/*.caf, so a mod caf in that folder is playable by name.
- Skeleton: Objects/characters/humans/male/skeleton/male.chr (IPL_Characters-part3.pak); IK bone names RightHand/LeftHand/RightArm/LeftArm (Mixamo-like).
- Live dev: tools/live.sh (rcon port 4600 + deagle_lua dofile). Console treats "a = b" as cvar set, so Lua with '=' must go through a file. Game window capture: um win shot --exe KingdomCome.exe (pauses when unfocused).

## 2026-10-11: rig + plan change
- User rule: do NOT restart the game; ask when a restart is needed.
- Layered single-animation hold failed: body yaw/pitch != camera (chest ignores pitch; A/D turns body). Plan: native aim system (hand cannon aim poses) + TACZ-driven hands.
- toolkit's glb skin import re-orients bones (bone heuristic + roll hack) => wrong rotations in game. tools/henry_rig.py rebuilds the armature from glTF inverse bind matrices (frame error 2e-6). Verified in game with tools/pose_test.py: arm distances match to 0.1 mm, finger joint angles to 0.1 deg (game GetBoneDir = joint X axis).
- Fingertip (x4) offsets differ between skin bind and game skeleton; they carry no mesh.
- TACZ deagle clips: static_idle, draw, put_away, shoot, reload_tactical, reload_empty, inspect(+empty/xmag), static_bolt_caught. Viewmodel: idle_view pivot [2,8,16.5], iron_view [0,6.8125,19.275] (px, 1/16), hands righthand/lefthand boxes 3x12x4, gun bone 'Deagle'.

## State at 2026-10-11 ~01:40 (pre-compact checkpoint)
Done/verified:
- Exact-frame rig (tools/henry_rig.py), verified in game (arms 0.1 mm, finger joint angles 0.1 deg).
- Gun attachments on RightHandSlot are INVISIBLE in game (slot bone hides attachments). Our CGF renders when attached to RightHand (raw test). => attach to "RightHand" with an offset.
- TACZ clips evaluated (tools/tacz_eval.py -> assets/build/tacz/*.json); viewmodel reconstruction render (tools/tacz_view.py). Shoot frame 2 recoil looks extreme (sign or real?) - unverified.
- Mixamo finger poses interlock/clip on Henry's hands -> unusable for fingers. Procedural finger fit (grip_fit.py) and gun-to-hand fit (gun_fit.py) both failed visually.
- KCD2-Convertor cannot read KCD2 .dba (ChunkController_905 EndOfStream).
In progress:
- Live capture of Henry's real grip: tools/capture_hand.lua logs "[Deagle] CAP <tag> <bone> pos dir" (GetBonePos + GetBoneDir = joint X axis). Plan: user draws Hunting sword -> capture right-hand grip; reconstruct local rotations from (X axis, child offset); sword handle axis ~ RightHandSlot -> place Deagle grip along it; index finger re-aimed to trigger. Left support hand: capture a two-handed hold later.
Next after grip: native aim system for camera-lock (hand cannon aimposes are loose CAFs: Animations/humans/male/aimposes/rifle_aimposes_aim_upperbody_player.caf), TACZ-driven hands (right hand = TACZ Deagle bone, left = mag_and_lefthand group).
User rules: don't restart the game (ask); put every visual check in Desktop\Deagle Progress; game window capture: um win shot --exe KingdomCome.exe.
Current installed build: v1.3 + posetest anim; hold anim disabled via dev reload (holdAnim = "").
