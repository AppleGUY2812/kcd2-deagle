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
Console: deagle_toggle, deagle_fire, deagle_reload, deagle_set <k> <v>, deagle_cfg, deagle_fx <name>, deagle_log_actions, deagle_input.
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

## 2026-10-11 ~03:00 — live hand capture works; automated gun-in-fist fit is not converging
- FACT: `Entity:GetBoneLocal(bone, t)` returns (a.t, X.t, c.t) for the joint's orthonormal WORLD frame [a X c] (X = GetBoneDir).
  In Henry's glTF-local axes (glb inverse bind matrices) the joint frame is [-a, c, X] -> GetBoneDir = glTF-local Z
  (= -Y of the henry_rig Blender bone). Verified: child joints predicted from parent frame + bind offset, mean 0.08 mm.
  Old captures with only GetBoneDir still give full frames (dir + child offset), <= 0.17 deg. tools/hand_frames.py.
- Captured sword grip (tag sword, assets/build/grip/cap_sword.txt): right fingers curled 56-84 deg at MCP. Rebuilt on the
  exact rig by tools/grip_pose.py, hand-relative joint error 0.16 mm. Sword handle centre: 54 mm toward knuckles,
  20 mm palmar of RightHand, axis along the knuckle line.
- tools/sword_grip_fit.py (numpy LBS = Blender deform to 0.0 mm; gun SDF with 6-ray enclosure inside test) — 4 attempts,
  coordinate descent stalls (fingers open/hyperextend, web far from beavertail). fit2 (hand-frame init) looked closest.
  Circuit breaker hit -> asked user whether to pose by hand in Blender (prepared .blend) vs deterministic close-until-contact.

## 2026-10-11 ~03:00 — RIGHT-HAND GRIP DONE (user-posed)
- User posed it in the kit (Desktop\Deagle Progress\DEAGLE GRIP - continue here.blend; copy: kcd2re\grip\grip_final.blend).
  Measured (tools/read_grip.py -> assets/build/grip/grip_final.json): index pad on trigger 0.1 mm; skin-in-gun palm 7.6,
  pinky 6.3, ring 5.9, thumb 4.3, middle 3.3, index 2.5 mm (mostly underside/palm; judge from the in-game camera).
  JSON has gun_in_hand_blender (gun model space in RightHand Blender-bone space) + finger_basis (evaluated, exact rig).
- Kit tooling: make_pose_kit -> kit_free_pose -> kit_sliders_on_henry -> kit_hybrid (fingertip IK spheres stuck 7 mm off
  the gun + knuckle/thumb-base sliders + per-finger IK on/off). Blender IK ignores basis rotation on chain bones, so
  joints set by sliders must stay outside the IK chain. grip_polish.py (min-change de-clip) exists; user preferred their look.
- How TACZ fits: TACZ gives per-frame gun pose (Deagle bone) + left-hand locator + gun parts; grip gives hand-on-gun;
  arm IK places Henry's arm. Next: left support-hand shape(s), TACZ->CAF converter with camera-view previews, in-game test.

## 2026-10-11 ~05:00 — MWII viewmodel + native plugin (state for the next session)
**Viewmodel (in game, works):** GameBanana 532396 (CS:S, IS Deagle on MWII anims; private use only). SourceIO 5.5.4
(E:\Tools\blender-user\addons\SourceIO; import_include_animations=True path; old path patched) -> mwii_vm.blend ->
tools/vm_export.py (rotate +90 Z: view +Y; fill_unkeyed: gun bones a clip leaves unkeyed get donor poses — fixed the
idle slide + stray round) -> tools/vm_extra_anims.py (@idle_empty, @holster = draw reversed, @ads = j_mainroot moved
rigidly so rear+front sight sit on the eye axis at 0.24 m) -> tools/vm_anims.ps1 (rc, skeleton alias DeagleVM in
animroot SkeletonList, *_add.animsettings additive) -> merge_img. 14 CAFs in Animations/deagle_vm. Textures:
tools/vm_vtf_dump.py + tools/vm_textures.py. deagle_vm.cdf/.chrparams/.mtl hand-written. Spawned as BasicEntity,
LoadCharacter(cdf); Lua places it each frame on System.GetViewCameraPos/Dir/UpDir (1-frame lag = slight jitter).
- Tried and failed for jitter: prediction (amplifies sway), head-bone attach (KCD2 ignores attachment offsets for entity
  attachments), "camera" bone attach (camera == camera bone, but attached entities sit at the character origin),
  SetSlotHud3D (no effect on jitter). DOF off while up (r_DepthOfField 2 -> 0). Body hidden (HideAllAttachments +
  HideAttachmentMaster), worn items hidden by proximity. FP guard: camera > 0.35 m from Head = not first person.
**Native plugin (native/, MSVC + MinHook 1.3.3, Ultimate ASI Loader 9.7.4 as dinput8.dll in Bin\Win64MasterMasterSteamPGO):**
- gEnv: static struct @RVA 0x492D800 (lea pattern unique). +0x08 3DEngine, +0x28 ScriptSystem, +0x80 Timer, +0xA0
  EntitySystem, +0xA8 Console, +0xC0 System, +0xE0 Log, +0x108 Renderer.
- Camera: hook CCamera::UpdateFrustumPlanes (TPVCamera patterns); game view = *(cam-0xE8) has RTTI .?AVCView@@;
  camera Matrix34 row-major at cam+0, FOV cam+0x30.
- IEntitySystem vt+0x70 GetEntity(id), +0x80 FindEntityByName. IEntity vt+0xF0 SetWorldTM(const Matrix34&, int),
  +0x168 SetPosRotScale. IConsole vt+0x118 ExecuteString(cmd, silent, deferred), +0x108 AddCommand, +0xB8 GetCVar.
- Lua->native: hook the System.LogAlways handler (RVA 0x39A996C, pattern in native/src/main.cpp), string via
  GetParam<const char*> (0x41DC20); "[DN] ..." lines are swallowed. native->Lua: ExecuteString("deagle_native_ok").
- v1 commands: hello, vm 0x<id>, follow 0|1, off r f u, snd <rel wav> <vol>, vol <x>. Sounds: own FMOD core system
  from the game's fmod.dll 2.02.21, files in Mods\deagle\sounds (loose, build.sh copies assets/build/sounds).
- Log: Bin\Win64MasterMasterSteamPGO\deagle_native.log. Uninstall: native/install.ps1 -Remove.
- INSTALLED 04:49 but NOT YET RUN in game (user asleep). First launch: check deagle_native.log + kcd.log "native plugin linked".

## 2026-10-11 ~20:00 — first in-game test fixes (facts)
- A non-looping clip on a layer > 0 holds its last frame (IsAnimationRunning(0,1) true, GetAnimationTime 1.0) until
  StopAnimation(0, layer). The empty-mag additive recoil ends with the slide locked: it rode on every later clip.
- StartAnimation returns false when that exact clip is already on the layer (not an error).
- rc deletes CAF tracks equal to the chr rest pose (CompressionSettingsTable eps changes nothing; a >=1 mm offset keeps
  them). Fine: the engine falls back to the rest pose for missing tracks (measured), and our rest = idle gun pose.
- SourceIO: Source keeps long sequences in 30-frame sections; bones a section doesn't animate come in as a zero
  parent-relative transform instead of the bone default. @inspect had it (slide, safety, spare round, mag);
  vm_export.py fix_section_zeros puts those frames back to rest.
- Lua-spawned BasicEntities go into saves; one with object_Model "" comes back as objects/default/primitive_pyramid.cgf
  (the white triangle). ENTITY_FLAG_NO_SAVE = 32768; SetFlags(f, 0) ORs, (f, 2) clears. System.GetEntitiesByClass works.
- Live debugging: tools/live.sh <snippet.lua> over the rcon port works while the game is unfocused, but the game is
  paused then (timers and animation don't advance).
