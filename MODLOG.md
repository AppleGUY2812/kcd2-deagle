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

## 2026-10-11 ~20:40 — transitions audited, magazine rounds, empty variants
- tools/vm_tracks_dump.py + tools/vm_transition_audit.py: 50 transitions (clip ends, starts, every interruption frame,
  queued draw/holster) -> 0 jumps. Rules in the script: clip end -> rest instant (every clip ends on its rest pose),
  rest -> clip 0.08, refire 0.12, a shot cut by reload/holster/aim 0.2, an inspect cut 0.3, from ADS 0.2; draw blocks
  fire/reload/inspect; draw/holster/reload finish before a requested toggle; tests/run_transitions.lua checks them.
- The normal reload ends with the spare mag (j_mag2) seated and j_mag1 parked: any blend out of it flew both mags
  across the view; VmPlay switches to idle instantly first (identical image).
- New clips: @draw_empty/@holster_empty (slide locked), @inspect_empty (slide locked, no chamber round; MWII shows
  inspects empty since S1), @shoot_empty + 10-frame settle, @reload_empty eased in from @idle_empty over 8 frames.
- Rounds of the gun's mag (j_ammo_01) are their own skin attachment vm_rounds (deagle_vm_rounds.skin), hidden at 0 ammo
  and shown 0.69 s into an empty reload (the same bone returns as the new mag at frame 18).
- Script flash light removed (intensity 6 / 9 m whited out the scene); the muzzle particle has its own light.
- Casings: ejectImpulse 0.035 (~1.75 m/s), higher arc; were on screen ~2 frames at 3 m/s.
- Slow motion for checking VFX in game: t_Scale 0.1 (exists in KCD2), back with t_Scale 1.

## 2026-10-11 ~23:00 — GameBanana CS importer + converter (tools/gb), Glock 17 converted
- gb_import.sh <id>: download, extract (Windows tar: zip/rar/7z), inspect MDL (SourceIO MdlV49: sequences with
  activity / frames / fps / events, bones, attachments), soundscripts + weapon script (clip_size, cycletime) ->
  guns/<slug>.json; preview sheet. gb_convert.sh <slug>: gb_build.py -> gb_anims.ps1 -> gb_vtf.py + gb_tex.py -> stage.
- Source sequences have their own fps (Deagle 30 / shots 40, Glock 30 / shots 50 / last shot 45); our Deagle pipeline
  assumed 24. rc stores i_caf keys on a 30 fps grid: >30 fps clips fail ("repeated or non-sorted time key") -> resample.
- Blender sub-frame evaluation of the imported curves swings through garbage between frames: never sample between
  frames (the resampler blends whole frames itself).
- Zero transforms are only a Source section gap when the same bone moves elsewhere in the clip: the Glock's
  tag_barrel_attach.001 is genuinely at its parent origin in every clip (repairing it moved slide + barrel 1 m away).
- Rigs differ per gun: Glock rounds hang on one bone per round (tag_origin.00N under j_ammo_0N under j_mag1); both of
  its reloads end with BOTH mags seated (overlapping); its empty reload uses the spare mag as the new one and brings
  j_mag1 back at 1.65 s. All measured by gb_build (swapTail, newMagAt), the script reads them per gun.
- Shared MW-hands materials (arms / sleeve / gloves) are found in other imported guns' folders automatically.

## 2026-10-12 — CS2 handling + fire modes
- CS2 moved weapon stats out of items_game.txt into scripts/weapons.vdata (KV3 text, `_base` inheritance); SteamDB's
  GameTracking-CS2 mirrors it (CS2 isn't installed here). Key by entry name: the R8 (weapon_revolver) reuses the
  class weapon_deagle. Deagle: 53 dmg, 0.225 s, head x3.9, armor ratio 1.864, range mod 0.85 / 500 u, recoil 48.2+-18.
- Mapping: headshotMult = CS2 head / 1.5 (KCD2's DealDamage already applies head x1.5); damage x rangeMod^(m / 12.7);
  kick scaled by CS2 recoil magnitude relative to the Deagle's 48.2 (its tuned 2.6 deg); modes from m_bIsFullAuto /
  m_bHasBurstMode (burst rounds 0.06 s apart, burstCycle after). Rapid fire stacks the additive recoil clip on the
  idle (converted guns now export <clip>_add too), crossfading 0.04 s when the previous kick is still playing.

## 2026-10-12 — speed, reload timing, slide locks, muzzle flash on the gun
- YabrakProds' own videos of both mods (Downloads/YTDown...): Deagle tactical reload back at idle ~1.7 s after the
  first motion, mag seated ~1.3 s in = our clip at 30 fps. The mod's MDL: Deagle sequences 30 fps, shots 40 (shoot_empty
  30); our Deagle clips were exported frame for frame at 24 fps -> gun.rate (default 30/24, shots 40/24) scales playback,
  cue times and mag timings (Deagle:Rate). The Glock already uses its sequences' own fps.
- The mods' weapon scripts fire much faster than CS2: Deagle cycletime 0.08 / 75 dmg, Glock 0.075 / 40 (CS2: 0.225 / 53,
  0.15 / 30). We use CS2's (user to decide).
- Reload timing from each clip's tracks (tools/clip_times.py; gb_convert runs gb_tracks_dump + it, gb_stage writes
  `times`): cancel (old mag in, spare off screen: ~0.3 s), seat (new mag in for good: ammo), ready (gun within 30 mm /
  10 deg of idle, 3 mm / 1 deg if the clip ends with the spare mag seated, after the slide closes on an empty one),
  trim (3 mm / 1 deg to the end: clip ends there, 0.1 s blend). At seat time the gun is still ~200 mm / 80-90 deg off
  idle, so a shot there would fire sideways: ready waits for the gun. Mid-reload cancel (mag out / spare in hand) isn't
  clean: a blend flies the spare 1.2 m off screen and pulls the dropped mag back in -> needs the mags as separate
  attachments (hide during the blend).
- Glock: its source last shot (glock_firelast) never keys j_slide -> no lock. gb_build now borrows the single shot's
  cycle up to full travel (35 mm at 0.06 s) and holds it; empty reload keeps it locked until the charge (frame 42,
  found on the untouched source clip; the ease-in starts the clip on the locked idle).
- Deagle: j_safetylock (safety-lever block on the slide) moves 55 mm with the slide in the shots; the empty clips only
  locked j_slide / j_slidestop, leaving it forward of the slide. Now in LOCK_BONES (vm_extra_anims.py).
- Muzzle flash: KCD2 particles support MoveRelativeEmitter, Intensity=(v,ParticleAge=(...)) light curves,
  CameraMinDistance, DrawNear; entities have LoadParticleEffect / SetSlotWorldTM / SetAttachmentEffect /
  CreateBoneAttachment. The flash is now an emitter in a viewmodel slot moved onto the muzzle every frame (no more
  walking into it); emissive 3-4 (was 10-60: blown out to an opaque blob; KCD2's hand cannon uses 1-2); light ramps with
  the flash. Smoke split into deagle.muzzle_smoke (world, CameraMinDistance 0.3).
- Glock burst "no animation" (user): the additive clips load in game (GetAnimationLength 0.53 / 0.37 s) and compile like
  the Deagle's working ones; waiting for the user's video.
- User video (old build): bursts barely kicked (the additive restarted every 0.06 s and never developed) and burst
  during an inspect kept the inspect playing (additive on top). The Glock mod ships MWII's burst clip
  (glock_fireburst1: 3 slide cycles 0.08 s apart, 8 deg each): now exported as shoot_alt -> shootBurst, played once per
  trigger on the base layer (cuts an inspect over 0.3 s), rounds burstGap apart (from its slide cycles). Rapid fire
  without a burst clip cuts a busy base clip to rest before stacking the additive.
- Empty inspects: the loaded inspect's slide check (Glock 13 mm press check, Deagle 20 mm) left the locked slide
  frozen while the hand pulled; now it moves 6 mm past the lock with the pull and settles back (Deagle: j_safetylock
  follows along its own travel axis).
- Broke the Glock in game (every clip "did not start", bind pose): tools/build_vm.sh rewrote src/Animations/Animations.img
  from the game's img + the Deagle's only, so the Deagle rebuild after a Glock conversion dropped the Glock's CAF
  headers. build_vm.sh now merges assets/build/gun_imgs/*.img too, and build.sh runs tools/check_img.py (every
  src/Animations/**/*.caf must be in the img) before packing. Removed an unused leftover deagle_posetest.caf.

## 2026-10-12 (later) — reload at the click, realistic cancel, magazines as pieces, flash findings
- Seat = the mag-in click cue (clip_times.py; Deagle 1.375 / 1.125, Glock 1.167 / 0.733 clip s), HUD drops RELOADING.
- Magazines (split_off: faces dominated by the mag bone subtree) and every modelled round (split_rounds: loose parts,
  each part type's copies paired down the stack; Deagle 6 x 300 verts, Glock 2) are their own skins. Mid-swap cancel
  hides both mags while the hands blend; realistic: only the chambered round is left and the gun stays without a mag
  until the next reload (cfg.cancelKeepsRounds = arcade).
- Flash: emitters in a slot of the viewmodel load and sit on the muzzle (GetSlotWorldPos) but never render; world
  SpawnEffect and a separate BasicEntity's slot render. A slot that still holds the effect doesn't restart on a second
  LoadParticleEffect: FreeSlot first, alternate two slots. In a dark room auto-exposure blows the additive flash out.
- R in KCD2's draw_holster_torch map (toggle_torch) took the torch out: map off while drawn, like combat.
- Fire rate from the mods' weapon scripts (Deagle 0.08, Glock 0.075); burst 0.05 s, burst clip played gun.burstGap /
  gap faster.

## 2026-10-12 — cartridge-driven handling (first version, provisional constants)
- guns/calibers.lua: .50 AE 300 gr 442 m/s from 6", 2.01 g powder (Speer 28-34 gr); 9x19 124 gr 350 m/s from 4",
  0.285 g (Hodgdon ~4.4 gr Titegroup). Guns by CS2 id: Deagle 2.05 kg, 152 mm, bore 50 mm; Glock 0.905 kg, 114 mm, 30 mm.
- ShotPhysics: v scales (barrel / ref)^0.25; impulse = m_b v + m_powder 1.75 v; free recoil v = p / M, E = p^2 / 2M;
  flip = p x bore / (M 0.1^2 + 0.004); flash = (powder g)^(1/3) x (ref barrel / barrel)^0.5. Deagle 10.15 N s,
  4.95 m/s, 25 J, flip 20.7 rad/s; Glock 3.07 N s, 3.40 m/s, 5.2 J, 7.1 rad/s (energies match published figures).
- Camera: kick = 0.217 deg per rad/s (Deagle 4.5, Glock 1.5), 65% returns (rate 5/s), rises at 70/s, applied as
  per-frame deltas through PlayerSetViewAngles (watch for eaten mouse input in game). Gun: plugin springs (w 28,
  slightly underdamped) on "[DN] kick back pitch yaw": Deagle 3.5 cm / 6 deg, Glock 2.4 cm / 2 deg. Flash scale
  0.476 x flash (Deagle 0.6, Glock 0.3), smoke with it. All in cfg.physics, live via deagle_set physics.<key>.

## 2026-10-12 — the flash, settled
- KCD2's entity:LoadParticleEffect takes plain arguments (slot, effect, bPrime, pulsePeriod, scale, countScale,
  attachType, attachForm), found in WHGame.dll's binding strings. We passed a table as bPrime: the scale never applied
  (and the flash may have been primed). With plain arguments it returns nothing, so code that checked its return
  silently fell back to a world flash ("not glued").
- Recorded test (forced gun kicks): flash in a plugin-glued helper entity = nothing drawn; SetAttachmentEffect on a
  bone attachment of the viewmodel (CreateBoneAttachment on the muzzle bone) = a flash on every call, riding the gun;
  System.SpawnEntity class ParticleEffect = nil (EntityModule.MakeParticleEffectActive/Idle exist but need that).
- Now: the flash is a bone attachment on the muzzle; the barrel direction in bone axes comes from GetBoneLocal at the
  first shot (rest pose). No fallback: a failure is logged. Helper entity, slot alternation, MuzzleLocal and the
  plugin's flash / flashpos / flashoff are gone. User rule: no silent fallbacks; intended paths must work every time.

## 2026-10-12 — physics design built (defaults), performance
- Damage: health from deposited energy (tissue drag C_d 0.3 over a 0.3 m torso path, sqrt, 9 mm FMJ at 360 m/s =
  35 HP: .50 AE 62), air drag for range (lambda = 2m / (rho C_d A), C_d 0.37: -8 % at 50 m), stamina from momentum
  (9 mm 20, .50 AE 59), head x2 on top of KCD2's x1.5, body push = 25.6 x momentum. Gas factor 1.5 for pistols.
- Muzzle gas energy = powder x 4.5 MJ/kg - bullet energy; blast scale G = cube root vs a 9 mm (Deagle 2.08) drives
  flash size and loudness (+(20/3) log10 energy ratio dB, half strength: Glock 0.69x); shot samples evened out to
  -12 dBFS blast (tools/shot_loudness.py -> guns/shot_gain.lua).
- Recoil: elastic hold (holdRange 2.9 deg two-handed, x1.3 aiming; past it the kick stays), per-gun return rate
  5 x sqrt(0.0245 / inertia), sideways = sigma 0.15 x a seeded per-gun pattern (learnable), gun roll 0.5 x yaw
  (plugin kick takes a 4th value), shots leave along the still-kicked barrel (Lua mirror of the plugin spring:
  Deagle +3.8 deg 0.08 s after a shot). Stamina / skill / stance / moving and the simulator flash light wait for a
  live check of the KCD2 functions (no unverified paths).
- Flash: deagle.muzzle 2-3 frames, deagle.muzzle_large 3-4 frames for blast >= 1.5 (Deagle).
- Performance: casings from a pool of 12 (ResetPhysics on reuse; were a physics entity per shot living 20 s);
  the plugin skips repeated camera calls within a frame (same matrix < 1 ms apart; was 2-3x per frame).
- Checks added: tools/check_native.py (every command Lua sends has a plugin handler: the kick handler had been cut
  out with the flash code), tests/run_all.sh (all suites).

## 2026-10-12 (night) — the flash, actually glued; the Deagle's missing flash
- User video: Glock flash trailing the gun on turns, off to the side; Deagle no flash at all.
- In-game tests (slow motion x0.02, camera turned by script, recorded; test records glue*.mkv):
  - bone-attachment effects (SetAttachmentEffect): one Glock flash rendered, one didn't, in the same session; the
    Deagle's never rendered, even moved to the real muzzle; they trail by a frame (the engine positions attachments
    before the plugin's camera step; KCD2 runs CEffectAttachment updates through a deferred global queue);
  - an emitter in a slot of the viewmodel entity: created (table form) but never renders;
  - an emitter on a separate BasicEntity: renders every time.
- LoadParticleEffect: CryEngine's table form (slot, effect, { Scale = ... }) returns the slot; plain arguments return
  nil and create nothing (checked live: IsSlotValid false). The 2026-10-12 "plain arguments" change was wrong, and it's
  why the plugin-glued flash entity once showed nothing. Mock now does what the game does.
- Now: the flash burns on its own entity (deagle_flash, two alternating slots); Lua sends the muzzle (position + barrel
  direction in the viewmodel's axes) each frame while it burns; the plugin places the entity right after the gun in
  the camera step (flash / flashpos / flashoff). No more attachments or GetBoneLocal measuring at the first shot.
- Deagle: tag_flash sat 7 cm under the barrel, 12 cm ahead and tilted 18 deg in idle (measured in game). Cause: the
  idle clip never keys it, and vm_export.py baked whatever pose the previously exported clip left on it. The exporter
  (and gb_build.py for converted guns) now drops the muzzle bone's keys and resets it to rest: every clip has it on the
  bore, +Y down the barrel. The Glock's shipped clips already had it right (within 1 mm); re-converting applies the rule.
- Notes for later (WHGame 1.5.6): IEntity::GetCharacter = vtable +0x328, GetSlotWorldTM +0x2D8;
  ICharacterInstance::SetAttachmentLocation_DEPRECATED = +0x190 (only stores m_location at +0xA08),
  FinishAnimationComputations +0x188, GetIAttachmentManager +0x40.

## 2026-10-12 (late night) — why every glued flash vanished, and the fix
- Plugin-spawned flash (FindEffect +0x30 / Spawn +0x30 / SetLocation +0x2B0, refcount at +0x58): spawned and never
  moved = 5 / 5 drawn; moved every camera step = 0-1 / 5 (recorded, normal speed).
- SetLocation, when the location changed, resets the emitter's AABB (+0x1A8..+0x1BC) to +-1e15 (empty) and queues it
  for the particle update. Frame order logged per frame (flash box valid / empty): Lua -> particle update -> camera
  step (twice) -> render. So a flash moved in the camera step was drawn with an empty box: culled every frame (the gun
  always sways a little, so every frame moved it). The same thing hid entity and attachment flashes on moving frames.
- Fix: put the box back after the move, shifted by it. Recorded with the user swinging the mouse: 5 / 5 drawn and the
  flash stayed on the muzzle (centre steady within ~10 px while the view moved 12-16 px a frame); unmoved flashes
  drifted with the world (51 px for a 45 px view move).
- Also found: a new emitter's particles are aged by a whole frame at their first update (CParticleEmitter starts at
  the frame start), so flash parts must outlive ~2 frames (tools/check_fx.py). Research notes: CryEngine GameSDK keeps
  one emitter on the weapon and calls EmitParticle per shot (particles born at age 0).

## 2026-10-12 (day) — bullets fly, tracers, impacts, hit reactions (parallel sessions)
Research by two other Claude sessions + a background agent (docs/ron/*, docs/kcd2_*.md, docs/cartridges.md); code and
live probes here. Facts found:
- Lua hit API (live dump): `g_gameRules:CreateHit` -> `game:ServerHit` (no effect on souls), `soul:DealDamage(stamina,
  health, attackerId, suppress, bodyPartName)`. DealDamage ignores armor; tested 5 hp on torso / leg / head = 5 each.
  It tags every hit as HitReactionType Fall (5), which has no reaction rows, so a small hit shows nothing (user
  watched). A full Deagle hit knocked a story NPC unconscious (ragdoll).
- Hit reactions: S_HitReactionEvent (ctor RVA 0xB2454C writes up to +0x9B) -> HitReactionComponent::ProcessHit; hitType
  0 has the living rows (weak / minor / major x 4 directions); strength by the game's rule (health lost = minor 5,
  >= 50 = major 6). Plugin command `hitreact` (docs/kcd2_hit_reaction_re.md). Live test pending.
- Particles: EFinalCollision = "Die, Ignore, Stop" (needs MaxCollisionEvents > 0), EPhysicsType "None, SimpleCollision,
  SimplePhysics, RigidBody"; params Stretch / TailLength / MinPixels / Connection / MaintainDensity exist (KCD2's libs
  use them). e_ParticlesObjectCollisions = 2 (particles collide with dynamic objects too).
- SurfaceTypes.xml: 74 entries (73 names). Invisible helpers (mat_window "prevents interaction through windows",
  level / water / corpse barriers, player / NPC / horse-only colliders, designer nodraw, raycast, slope) and foliage
  stop rays: rounds now fly through them (deagle_main.lua SURFACES "pass").
- KCD2 has no round bullet-hole decal and no plate-hit spark effect (collisions.combat.hit_stopped_bullet_plate is
  referenced but missing): Ready or Not's holes / impacts fill it (private textures, gitignored DDS).
Built: rounds with speed / gravity / drag (first step covers one frame: close hits land the frame they're fired);
tracer per round (one self-flying particle per muzzle-speed bucket); impacts + holes per surface sized by landing
energy^(1/3); hit reactions wired. tests/run_ballistics.lua 40 checks. User in game: "spread and bullet drop looks good".
- Later the same day: wallbang (far side found by a ray back from as deep as the round can reach; KCD2 ray normals and
  one- vs two-sided faces unverified, so a hit at the near face counts as "too thick"), ricochets (chance x 30 deg /
  angle, Ready or Not's surfaces), armour per worn layer (inventory items = outfit; KCD2 stab defense x (0.6 + 0.4 x
  condition) against punch 104 x energy ratio x cos), through-and-through bodies, Ready or Not blood, hit zones from
  bone capsules. NPC rays: always mat_flesh, partid nil (the living capsule), checked live on an armoured guard.
  Offline: tests/run_penetration.lua (35 checks). Not yet seen in game.
- Console commands: KCD2 passes %1 / %line QUOTED ("clear"); and System.AddCCommand doesn't replace a command that
  already exists, so a dev reload keeps the old command code until the game restarts (Lua functions it calls do update).
- Debug lines (System.DrawLine) render in retail; at ~3000 per frame the newest weren't drawn (buffer limit): cap 600.

## 2026-10-12 ~10:00 — tracers don't draw, the "fireball", settings kept
Found live (probes over rcon, screenshots via `um win shot --exe KingdomCome.exe`, which captures the game window while
it's behind other windows; the game kept running unfocused this time: timers fired, clouds moved):
- Particle Lua API in KCD2: CreateEffect(name, params), SpawnEffect(effectName, pos, dir, [scale], [entityId],
  [partId]), SpawnEffectLine(effectName, startPos, endPos, dir, scale, slices), SpawnParticles(params, pos, dir),
  IsEffectAvailable, DeleteEffect, CreateDecal, CreateMatDecal. SpawnEffectLine works (user saw 5 flashes in a row).
  CreateEffect returns nothing and IsEffectAvailable says true after it, but the effect drew nothing with XML-style
  keys (numbers or strings): its param names are unknown (the old CE2 Lua keys aren't in WHGame.dll).
- deagle_tracers.v250 spawned 3-4 m ahead, away / up into open sky, every 30 ms: never seen in any capture. The effect
  exists (IsEffectAvailable true), its texture exists (IPL_Textures-part1.pak Textures/particles/spark_single_diff.dds).
  Cause unknown: deagle_lab.xml has it with one thing changed each (collision, size / MinPixels, stretch, alpha, speed,
  facing) to find out live. The tracer is now the trail (rounds lay still pieces along their path,
  deagle_tracers.trail); flying kept as tracer.mode "flying".
- The "fireball" floating in the air: a live-flash emitter (flashMode "live" was the default) left burning after a
  save load. The plugin's ReleaseEmitter only drops our reference (refcount at +0x58); it never stops a continuous
  emitter, and the hello after a load forgot it without even that. So flashdisarm never stopped the live flash either.
  Stopping one needs IParticleEmitter::Kill / Activate(false) (vtable offsets not found yet). flashMode is "gun" by
  default now.
- Settings: Lua has no io in KCD2; the plugin keeps every deagle_set (native/src/settings.cpp,
  Bin\...\deagle_settings.txt) and replays them as deagle_set lines after the hello handshake (console queue).
- Tracer lab, in game ~10:50 (deagle_lab.xml, captures of the game window): only t_nocoll drew. Every variant with
  SimpleCollision (even at 20 m/s, fired up into open sky) died before it was ever drawn: particle collision kills our
  tracers at once (cause unknown; terrain / static / dynamic not split yet). A stretched particle's streak grows with
  its age: the still trail piece (1 m/s, Stretch 0.5, 0.12 s life) drew only as a ~0.15 m dash, while the same at 1.5 s
  life reached the full metre. CryEngine keeps a streak from reaching back past where the particle started (KCD2 has
  Stretch "...,OffsetRatio=1" in one effect: a forward-only stretch may avoid it; to test). The emitter scale scales
  both length and speed (scale 3 = 3x longer). Facing="CameraX" quads (OrientToVelocity, Speed 0.01) drew nothing.
  Lua-spawned pieces do draw (thousands of them, no trouble).
- Flash timing oracle (no user needed): `um win record --exe KingdomCome.exe --fps 60` records the game window even
  behind other windows; fire with a deagle_lua snippet (Deagle:Shoot(true)) during the recording, then diff frames.
  The capture repeats a frame until the game redraws, so count unique frames. 2026-10-12, gun mode: first recoil
  frame without flash, flash on the next game frame, gone the one after (game at ~25 fps while recorded unfocused).
  Size and colour look right (white-hot, orange edge); the flash is one game frame late.
- Tracers, settled in game 2026-10-12 evening: a single Lua-spawned piece only showed with >= 0.4 s of life (0.12 / 0.25
  never; the game skips a new emitter for ~0.3 s). Plugin-spawned pieces whose location the plugin sets again for their
  first 3 camera steps (like the flash it moves every frame) show the frame after the shot. With pieces glowing about
  one frame (fx "auto": trail_025 at >= 50 fps), the tracer is one short white-hot streak running with the round, one
  per frame, like the user's filmed .50 cal reference; user recording 17-17-41 (Glock mag dump, freecam from the side):
  flash on frame 0, streak from frame 1 at the muzzle, then one streak per frame receding along the aim line.
  Rounds / tracers now fly 1000 m (round life 6 s).

## 2026-10-12 — reload sound cues vs the clips, and slow motion's two clocks
- Cue check from tracks.json (gun-space j_mag1 / j_mag2 / j_slide): the MWII events sit ~4 frames before the impacts in
  our clips and every file has a lead-in (magin 53 ms, empty magin 42, slideback 42, slideclose 53), so at normal speed
  the hits came 80-90 ms early. Retimed (deagle_main.lua vmSoundCues, seat times follow the mag-in cue): reload magin
  1.375 -> 1.475 (seat frame 37), empty magin 1.125 -> 1.240 (frame 31), slideback 1.750 -> 1.781 (rearmost frame 44),
  slideclose 1.833 -> 1.934 (home frame 48). Glock: only the empty mag_in was off (48 ms late): 0.733 -> 0.685.
  Inspect cues are 3-7 frames early as well (not retimed).
- Slow motion (t_Scale 0.1) runs two clocks: Script.SetTimer follows t_Scale exactly (cue spacing in recording
  2026-10-12 17-24-44 = 10x), but the viewmodel clip ran ~1.4-1.6x faster than that (the empty reload took ~13 s real
  instead of ~20; shot log t= advanced 0.21 s over 1.68 s real = 0.125). So in slow motion every timer-scheduled cue drifts
  late with its time in the clip (mag-in +2.2 s real, slide close +5.2 s): not a cue error. Judging sync in slow motion
  needs cues driven by the clip's own time (e.g. polling GetAnimationTime), not SetTimer.

## 2026-10-13: spawned test NPCs froze the game
- `CombatDebug.SpawnFriend` (Scripts/Debug/CombatDebug.lua) fails in the shipped game: it calls `XGenAIModule.SpawnEntity`,
  which is nil.
- `System.SpawnEntity{ class = "NPC", properties = { sharedSoulGuid = ... } }` returns an entity with soul and actor,
  and `actor:EquipClothingPreset` works. But a few minutes after two such spawns (game paused in the menu, plus a dev
  reload), the game hung: the window went "Not Responding", the plugin's camera frames slowed, then stopped, and rcon
  got no answer.
- Most likely cause: an NPC spawned outside XGenAIModule. `deagle_testnpc` no longer spawns: it makes the NPC you
  aim at unhurtable instead.

## 2026-10-13: KCD2 objects per surface type (for deagle_testwall)
- The .cgf and .mtl files are in `IPL_Objects-part0..5.pak`; `Objects-part*.pak` holds only streamed meshes
  (.cgfm) and textures.
- What a cgf's material chunk tells you (0x1014 v0x802): the mtl path it uses, and the physics type of each
  sub-material: 0x1100 = proxy, 0x1000 = physicalised mesh, -1 = render only.
- The proxy sub-materials line up in order with the .mtl's SurfaceType values.
- No object uses mat_bells: church bells are mat_metal (bell_alarm.mtl, "proxymetal").
- Hay sheaves, hay stands and straw targets are mat_thatch. mat_organic_stuff is used for food, fish and mushrooms.
- mat_wood_soft is only on small items: lute, twigs, whip, and the like.
- Every stairs-stone object also has a mat_slope proxy, which is pierceable.

## 2026-10-13: ricochets, rock hulls, corner clips
- Big rocks hit as a handful of flat faces: 209 rays over one boulder (by the coppiced beech) hit only 6 distinct
  normals (`Physics.RayWorldIntersection`; drawn as lines in game, `Desktop\Deagle Progress\rock_hull.png`). The
  angle a round meets a rock at comes from that hull, not the rounded rock on screen: shots that looked like skims
  measured 22-36 deg. `p_draw_helpers` exists (0) but draws nothing in the shipped game, `s_g` included.
- Ready or Not's rule (30 deg for every surface, 10-30 %) did skip 6 of 31 rock shots; the user couldn't tell them
  apart. Replaced by measured critical angles per material (guns/penetration.lua `crit`, sources there): pistol
  bullets skip only below ~5-9 deg on steel, wallboard, glass, water; 11-13 on concrete; 15-17 on tile; 14-18 on
  plywood. Above it a hard surface breaks the round up; a stopped round on plate now sprays fragments along it
  (FBI: most at ~20 deg off the plate) instead of bouncing back at the shooter.
- A Deagle shot skimming a rock's edge at 4 deg went straight through a 2 cm sliver of the hull (the wallbang
  check ignored the angle). Now a round out through a face that isn't the far side is bent away from the material
  and tumbles (cfg.deflect, guess: no study of corner clips found).
- Dev reload (`tools/live.sh reload`) reruns deagle_main.lua only; guns/*.lua come from the installed pak. To try a
  changed guns file live: copy it to E:\Tools\deagle-dev and `deagle_lua dofile([[E:/Tools/deagle-dev/<file>]])`
  after the reload. The game console splits commands on `;`.
- Re-sending saved settings with `while read ... powershell.exe` stops after one line: powershell eats the loop's
  stdin. Give it `< /dev/null`.

## 2026-10-13: casings (spin cap, throw)
- A rigid body's spin is capped at 20 rad/s in flight: `SetVelocityEx(v, {150,0,0})` read back w = 20.000 (live). It's
  CRigidEntity m_maxw (+0x24C, ctor writes 20.0f); the set-velocity action clamps w to it right away. On a contact
  step w spiked to 260+ rad/s, so the cap only bites in free flight. Free-flight damping measured ~0.1 /s.
- Lua can't change it: SetPhysicParams(PHYSICPARAM_SIMULATION = 5) parses max_time_step, sleep_speed / min_energy,
  damping, freefall_damping, gravity, freefall_gravity, mass, density, max_logged_collisions, never maxRotVel.
  WHGame 1.5.6 (read from the binary): IEntity +0x278 GetPhysics; IPhysicalEntity +0x20 SetParams(p, threadSafe),
  +0x28 GetParams, +0x30 GetStatus, +0x38 Action; pe_simulation_params type 10, 0x4C bytes, maxRotVel +0x30
  (iSimClass +4, maxTimeStep +8, minEnergy +0xC, damping +0x10, gravity +0x14, dampingFreefall +0x20,
  gravityFreefall +0x24, mass +0x34, density +0x38, maxLoggedCollisions +0x3C), unused = 0xFFBFFFFF / 0x80000000.
  The plugin's `casespin <id> <rad/s>` sets it (not yet seen in game).
- The old throw (AddImpulse 0.035 on 20 g: 1.77 m/s, ~52 deg up) landed 0.88 m out, measured live: that was the
  user's "0.8 m and hangs at the top". Now SetVelocityEx: 2.8-4.2 m/s, 20-40 deg up, 5-25 deg back, plus the
  shooter's velocity; first ground contact 2.79 m out in game (ground 0.4 m lower there). The casing model's long
  axis is its local Y.
- `Entity:GetAttachmentBone(slot, name)` returns the bone name, nil for no such attachment.

## 2026-10-13 (night): sounds, water, attachments (Claude 2)
- Ray masks: KCD2's `ent_all` = 287 and there's no `ent_water` global. Rays with ent_all go through water surfaces:
  measured at a lake, the hit was the bed (mat_mudwater / mat_mud) up to 5 m under the surface; ent_all + 0x200 (CryEngine
  ent_water) stops on mat_water at the surface. Every round into water used to splash on the bed (fixed in ballistics.lua).
- RoN handling cues: `ronClick` is real ms, but it was subtracted from clip-timeline seconds; on a clip played at Rate
  (Deagle 30/24) the event started only lead/1.25 early. Fixed (`lead * Rate`), plus `vm.cueLead` 0.045 s for the RoN
  engine's output latency (256 x 4 mix buffer + device), and cues fire one frame ahead. The Deagle's reload / inspect cue
  times were MWII-file times moved early by those files' lead-ins; now the clips' own frames (tracks.json, gun space):
  tac mag out 0.729, seat 1.542; empty mag out 0.229, seat 1.292, slide home 2.000; inspect out 0.312, in 3.208, slide
  4.667 (24 fps timeline).
- RoN's own mix (rendered offline): 1P Grizzly Fire -3.2 dB, Grizzly MagIn -11, FN57 Dry Fire -40, G19 Dryfire Old -46.7.
  Impact event: 3D position alone changes nothing; PropagateDistance fades it 5 m -3.9, 10 m -22.9, 12 m+ silent
  (ricochets: 20 m -10.6, 45 m silent). Now the plugin's `Falloff=<m>` marker applies inverse distance instead.
- 1P fire events are 2D (always in the head); `3P Grizzly Fire` / `3P G19 Fire` are 3D (1-150 m). `ron` without a position
  puts a 3D event on the listener (the free camera when it's on).
- Character attachments (WHGame 1.5.6, read from the binary): `CreateBoneAttachment(slot, bone, name)` ignores a 4th
  argument; the new CA_BONE attachment has joint id -1 and identity transforms, and the joint is only resolved on the
  character's next re-sort (characters in the animation queue). `GetAttachmentBone` returns "" until then. Projection makes
  relative = inverse(joint bind) x identity, i.e. the model origin, unless SetAttachmentPos / Angles ran in the creation
  frame (that keeps the relative transform). `SetAttachmentCGF(slot, name, path)` binds nothing if the load fails, and
  always unhides. `ChangeAttachmentMaterial(name, mtl)` has no slot. A CGF attachment draws only with a resolved joint,
  bit 13 (visible, set by the update when the character rendered in the last 5 frames) and bit 16 (hide) clear.
- The mesh flash cards: created with the viewmodel they report bone tag_flash, yet nothing drew on the muzzle even
  unhidden; a known-good model swapped onto a card wasn't seen either (but the gun was mid-inspect). Open.
- Bullet holes: every CreateMatDecal call succeeds, no log line, none seen. `e_Decals 2` doesn't stick in retail. KCD2's
  own bullet decals use size 0.2-0.3; ours 0.035 x energy with the hole covering 5-30 % of the texture. Open.
- The pause menu shows a frozen frame: entities / decals spawned while it's up don't appear in window captures.
- A save load re-runs the installed pak's scripts: every hot-reloaded change is gone until the next dev reload, and the
  first `tools/live.sh reload` after a load runs the pak's old DevReload, which doesn't set devLoading, so guns/*.lua and
  ballistics.lua come from the pak. Reload twice after a load (or check a value) before trusting the live state.

## 2026-10-13 (night): how loud a bullet on steel is next to the gunshot (Claude 3, sound)
- Ready or Not's own mix (rendered, final fader 0.5): its steel hit (Surface 44) at the listener carries 1.8 dB more
  A-weighted energy than the MK V bang (first 150 ms); with `impactVolume 2` the mod's hit is +5 dB over the shot at
  4 m, -3 at 10 m, -11 at 25 m. RoN's steel takes ring ~0.2 s (30 dB down).
- Real life: shot at the shooter's ear (Meinke et al. 2014, NIOSH, Table 4: .44 Magnum 132.3 dB SEL_A, .357 Magnum
  133.1); no published measurement of a bullet hitting steel exists, so physics (`tools/sound/impact_level_ab.py`): the
  splash hands the plate m v in ~bullet length / v ("Ballistic Impacts with Bullet Splash", PMC10254373, 2023: .308 on hard steel, impulse = m v, 36-49 us),
  energy into bending waves = J^2 / pulse x point mobility, a free plate radiates most of it. Deagle on a hanging 10 mm
  plate vs its own shot: 4 m -18 dB, 10 m -27, 25 m -35, 50 m -42 (bolted plate ~6 dB less; Glock ~3 dB less vs its own
  shot, its hit ~10 dB under the Deagle's). A hanging plate rings T60 ~0.8 s at 3 kHz. Uncertainty about +-6 dB.

## 2026-10-14: Ready or Not's real impacts are Niagara GPU effects; decoded by running their shaders (Claude 2)
- RoN's in-game bullet impacts (ImpactEffects_Pistol etc.) are Niagara systems (`NS_*`) for every surface but water
  (`P_BulletImpact_Water`, Cascade). The Cascade `P_*` impacts are older, unused versions: the metal port of 2026-10-13
  copied the wrong effect.
- Niagara's particle scripts are GPU compute shaders; their numbers (counts aside) are literals in the shader. Each
  `GPUComputeScript` export holds its shader map hash; the shader is in `ShaderArchive-ReadyOrNot_Chunk0-PCD3D_SM5`
  (pakchunk0; FSerializedShaderArchive v2, Oodle Kraken, libooz works) as one cs_5_0 DXBC. Disassembled with Windows
  d3dcompiler_47 (`D3DDisassemble`, flag 0x80 for exact hex literals). Layout: particle floats in a buffer (component k
  of particle i at i + k x ComponentBufferSize, attributes in alphabetical order = the SpawnScript's `Attributes`),
  ints in another, curve LUTs in a third (`ShaderLUT` of each data interface back to back); CB0 = spawn info
  (offsets [base+1..2], params [base+3..]), counts [base+11], dt [base+12].z, the system's rotation rows [base+25..27],
  owner position [base+34] (previous frame [base+42]); data-interface params after [base+49]; user colours in two small
  CBs (current / previous). base = 0, except emitters with distance-field collision (base 30, two 3D textures first).
- Spawn bursts are in the system update script (CPU VectorVM, legacy bytecode): constant operands are byte offsets into
  [parameter store][ScriptLiterals]; `enter_stat_scope` operands are constants holding the StatScopes index. A burst =
  `min + int(rand x (max-min+1))`, x SpawnCountScale, at least 1.
- Gravity in the shaders is world -Z; spawn directions go through the system rotation; the hit normal is the effect's
  local +Z (debris and the spark burst leave along it).
- Pistol on steel (`NS_Bullet_Impact_Metal_Big_Shotgun`): flash 1 (0.1-0.2 s, 16-63 cm, HDR 13909 nits), main sparks
  2-5 (2.2-7.75 m/s in every direction, 0.05-0.3 s, 2-21 cm streaks ~ speed, 1.1 g, up to 60000 nits), core sparks 3-5
  (drag 9.2/s), one 28-145 cm spark-burst flipbook (drag 21/s, faint), 2-4 dark chips (2.25-8 m/s, 0.5-1.5 s, 0.81 g).
  No dust, no light. Tools: `tools/ron/niagara_gpu.py`, `sm5emu.py`, `ns_stats.py`, `fxrender.py` (docs/ron/niagara_gpu.md).
- Why RoN's sparks never showed in KCD2: impacts were spawned with Lua `Particle.SpawnEffect`, and a new Lua-spawned
  emitter isn't drawn for ~0.3 s (tracer lab, 2026-10-12): everything living under ~0.3 s (the sparks, the flash) was
  invisible, only dust and bits showed. Impacts and blood now go through the plugin's `fxs` (cfg.impactSpawn). Its hold
  is 2 s of real time: in F9 slow motion long parts are cut early (to fix in the plugin: hold by game time).
- **Correction (same night): matching that physics is wrong.** The user's Hickok45 clip (Sunday Shoot-a-Round #351,
  8:50; real outdoor recording, camera at the shooter): steel dings 0.4-1.3 s after the shot (roughly 80-250 m) peak
  8-15 dB under the bang and 7-13 dB over the range's echo at that moment: clearly heard. Our game at ~160 m puts the
  hit 30 dB under the bang, under RoN's echo. Recordings (and hearing) squash the bang (the clip's shot overloads
  the mic: a flat-topped pulse, then a slow recovery), so the sound-pressure ratio at the ear isn't what anyone hears.
  Match recordings, not the pressure ratio.
- **Correction 2 (same night): those Hickok "dings" are probably brass, not steel.** In "Desert Eagle 50 AE"
  (youtu.be/nb7tWcohYSY) the 5.5 kHz ring comes 0.9-1.0 s after every shot whether he shoots the far steel field
  (734 s) or a jug / can on a stump up close (1106 s, 1421 s): same delay, same pitch = his .50 AE casings landing.
  So the "7-15 dB under the bang" reference is withdrawn. The user's clip (Sunday Shoot-a-Round #351, 0:08 in the
  video, sCV1pilQxxU: a pistol at steel in a thin forest; events 0.44 s after 5 shots, ~1 s after the first) is not
  settled by measurement alone. Look at frames before trusting any sound measurement from a video.
- **Water (Claude 2, later that night).** RoN's P_BulletImpact_Water switches off 4 of its 7 emitters (Required
  bEnabled = false: ring, 2nd DropletMicro, DustFine mist, RoundDistortion drops); our water drew all 7. Rebuilt from
  the 3 on (caustic sheets, droplets, dust puff), textures baked from the decoded pixel shaders (tools/ron/mk_water2_tex.py).
  Caustic sheets are square: Cascade's default PSA_Square draws Size X by X. **KCD2 Facing="CameraX" +
  OrientToVelocity="true" stretches Aspect ACROSS the velocity**, not along it (in game: streaks lay sideways over
  rising droplets), so for a streak Size = half its length along the flight and Aspect < 1 = width / length.
- **F9 slow motion and fxs**: the plugin held fxs pieces 2 s of wall time (x0.1 cut them at 0.2 s game time); now 2 s
  of game time (wall dt x the speed command, 2962b97). Script.SetTimer runs on game time too (3 spawns 1 s apart showed
  one in a 3.6 s recording at x0.1). Check the log for "slow motion x0.1" before reading a recording's timing.

## 2026-10-14: why the mesh flash never drew (Claude 4)
- Runtime bone attachments (`CreateBoneAttachment`) sat at the viewmodel's model origin, which is the camera, so
  nothing on them was ever seen. A roll set with `SetAttachmentAngles` in the creation frame did **not** keep them on the
  bone (that corrects the 2026-10-13 note). Measured live: the card alone on a BasicEntity glows fine (material and
  mesh OK). The pistol model on `vm_flash_1` or on a fresh attachment never showed. The same card loaded in an entity
  slot of the viewmodel at the muzzle drew. `SetAttachmentPos(0, name, {0,0,0})` + `SetAttachmentAngles`, once
  `GetAttachmentBone` names the bone, put a card on the muzzle and it drew (screens: Desktop\Deagle Progress\17 Flash fix).
  `FlashMeshPlace` does that every frame until the cards' bone has resolved.
- The muzzle bone's axes in the viewmodel: +Y = forward (down the barrel), X and Z flipped (180 deg about the barrel).
- Seen on the gun, the tongues (planes through the bore, side sheet) show as bright streaks. The front cards (face-on,
  front sheet) weren't visible in a held frame: look and strength still to judge with real shots.
- Decal check (same run): ours and KCD2's own `decal_wood_02` at 0.25 on a test-wall metal door (a BasicEntity),
  with and without the entity id: none visible at 3.9 m at night. The terrain pair was off screen. Open.
- `Deagle.kept` mirrors the plugin's kept settings (Set fills it, a load's replay goes through Set); `DevReload` sets
  them again, so a dev reload no longer drops the player's settings.
- Same night, "there is no flash" (user, daylight): two more causes. (1) Illum's forward pass adds the glow times the
  pixel's alpha (Opacity x the sheet's alpha; Shaders.pak Illum.cfx `OUT.Color.a = GatherAlpha`), and at Opacity 0.03
  that's ~0.5 HDR units: seen at night, invisible by day even "hot". Opacity 0.99 in all three flash mtls; shown live
  first with `Entity:SetMaterialFloat(slot, sub, "opacity", v)` on a holder entity with the card model (the material is
  shared by name, so the attachments changed too; `emissive_intensity`, `emissive_color`, `alpha` also work).
  (2) The shot comes before its frame's viewmodel tick, so counting ticks hid card a before it was drawn; the sequence
  now counts `System.GetFrameID()`. Measured: a card unhidden for exactly 1 engine frame is drawn in that frame (no
  draw latency). Recorded shot with 4 frames: fire on the muzzle from the shot frame through the recoil.

## 2026-10-14: Ready or Not's own muzzle flash as a look (Claude 4)
- The Grizzly (MK V .50 AE, DLC3 `BP_Secondary_LARGrizzly`) uses `WeaponFlash_Pistol_V3` (not V2), 13.6 cm ahead, 2.5 cm
  up; smoke `P_WeaponSmoke_M2011_Muzzle` / `_Ejection`. V3 = V2's layers with a 4x4 fireball sheet
  (`MI_MuzzleFlash_SMG`, 25-35 cm, 25 cm ahead) and its side flame. Read from the data: the fireball's burst is 0-1
  particles (on about half the shots), the core's colour and flipbook frame are a uniform random re-rolled every
  frame (it flickers), the sparks are a burst of 0-10 at 10 ms (500-5000 cm/s within 13.5 deg, drag 2.5-15/s,
  stretched by speed). Textures: 4 sheets, opaque, made to be added on black.
- KCD2 can't draw a 0.035-s particle on time, so the look is baked onto cards: `tools/ron/flash_collect.py` (system,
  materials, textures) -> `tools/ron/flash_bake.py` (Claude 2's `cascade_sim.py` runs it, plus the cone / speed
  stretch / event side flame / pivot it skips; 4 variants x 10 stages over 0.095 s; quads in the muzzle bone's axes;
  one sub-material per texture, hue and power-of-two brightness, nits / 100 = Illum intensity) ->
  `tools/flash_mesh_ron.py` (Blender + toolkit + rc: 80 CGFs). Preview: `tools/ron/flash_preview.py`
  (Desktop\Deagle Progress\18 Flash light vs Ready or Not\ron flash baked preview.png).
- Velocity-aligned sprites (the side flame, sparks) lie in the plane through the barrel that faces the eye; with our
  eye 4 cm above the bore that's nearly edge-on (a streak leaning in from the muzzle), as Ready or Not would draw it
  from there. PivotOffset read as -1 = the sprite's edge, the texture's top leading (not checked against a render).

## 2026-10-14: a real steel plate's ring from Hickok45, hits far away (Claude 3, sound)
- Sunday Shoot-a-Round #351, the 10 steel hits of pack 16: 3 plates, each with its own set of pitches that comes back on
  every hit (plate A ~20 m: 1242, 1441, 2133, 3445 Hz ...; B ~9 m: 1980, 2965, 3340, 4090, 4676 ...; C ~100 m: 2320,
  2766, 2977, 3398 ...), 16-48 steady pitches each, up to ~14 kHz. `tools/sound/plate_rings.py` keeps only those
  pitches (band-pass, each until it sinks to 6 dB over the bands beside it); the gunshot, its echo, voice and music
  spread over every pitch and stay out. Requiring a pitch in two windows (strike +0.02-0.2 and +0.15-0.45 s) is what
  separates plate tones from flickers of the gun's echo (one window: 48+ "tones" per hit).
- SAM Audio (Meta, web playground) can't clean these: removing the laugh and music from hit_01 flattened 16 of its 18
  plate pitches from 10-26 dB over their neighbours to 1-6 dB; isolating "metal plate ding" lost the 5-11 kHz tones.
  A decaying-tone fit (Theil-Sen, matrix pencil) failed: the pitches beat and the echo and voice overlap them.
- Ready or Not's impact events get no echo outdoors: the steel hit rendered with ReverbMaster + ReverbFOREST=1 is the
  same as dry (0.4-0.8 s after: -62 vs -63 dB). Their echo comes only with the guns (baked EXT/INT layers + the weapon
  bus's reverb send).
- Hits far away: the plugin's `Falloff` plays Ready or Not's point-blank impact at any distance, only quieter (RoN's own
  PropagateDistance fade is off). Measured against the MK V shot (100 ms max): a fresh ricochet at 3 m +4 dB, 25 m -5,
  100 m -17. Air absorption (ISO 9613-1, 15 C 70 %) per 100 m: 1 kHz -0.4 dB, 4 kHz -2.7, 8 kHz -9.5; the ricochet's
  spectral centroid falls from 3.2 to 1.5 kHz at 100 m. `tools/sound/impact_distance_ab.py` (pack 18).
- First in-game try of `deagle_flash ron`: no Ready or Not card drew from the gun, while the same model on a BasicEntity
  did (a bright red fireball). Each model's material list held only its own 4 names while its faces used ids up to 25
  (the .mtl's order). `GetAttachmentCGF` still named the new model, but nothing changed on screen: a recorded swap
  kept showing the old card. Every model now carries all 25 materials in the .mtl's order (tools/flash_mesh_ron.py).
  Not checked in game yet. KCD2 doesn't load loose models: a path outside the paks gives
  `%ENGINE%/EngineAssets/Objects/Default.cgf`.
- On the entity the Ready or Not fireball came out red with a white core: by Claude 2's nits -> intensity mapping its
  green (1.2 % of red) stays dark in KCD2. Brightness steps `_x10` / `_x30` added to match its on-screen colour live.

## 2026-10-14: attachments made at run time draw stale models; the flash cards moved into the .cdf (Claude 4)
- In game (pak 9ca340a3, game running and in front): on the viewmodel, a card attachment made at run time
  (`CreateBoneAttachment` + `SetAttachmentCGF`) drew one and the same thing whatever it held: each of the 40 Ready or Not
  cards, the empty last stage (a 1 mm quad), our own 4 cards after a look switch, KCD2's `primitive_pyramid.cgf`, an
  apple, a basket. `ChangeAttachmentMaterial` (a plain grey material too) changed nothing either. `SetAttachmentPos`
  moved the thing and hiding the attachment hid it: the attachment drew, just not its model. A fresh attachment showed
  another leftover (a stage-4 card's streak). The same models on BasicEntities drew their own shapes. So "the 25
  materials" (above) may not have been what failed the first try: that was a look switch too (4 destroyed, 40 made).
  The "old card stayed" swap matches this. Not found: which object the renderer keeps, and why.
- While the game sits in its pause menu (window not in front), attachment changes don't show at all: test only with
  the game running in front.
- Fix (not yet seen in game): every flash card (ours 2 x 4, Ready or Not's 2 x 40) is a `CA_BONE` attachment in both
  viewmodel .cdf files (`tools/flash_cdf.py`, KCD2's own format: `RelRotation="1,0,0,0" RelPosition="0,0,0"
  BoneName=...`), named after its CGF and hidden by the script at load; a look switch only hides and shows. The
  magazines and rounds (`CA_SKIN` in the .cdf) never had the problem.

## 2026-10-14: every model on the muzzle bone draws the first card; the flash cards are skins now (Claude 4)
- The .cdf fix above failed in game (pak f66d5fb2, the user's clip `mkv sound clips/2026-10-14 08-40-10.mkv`): our look
  and Ready or Not's drew the same fireball, and in F9 slow motion one Ready or Not flash kept that shape for its whole
  0.95 s through all 10 stages (it should end in a 1 mm quad).
- Live test, game in front: each of the 88 `CA_BONE` attachments reported its own CGF and `tag_flash`. Ours 2 and Ready
  or Not's empty stage 10, held alone, drew our card 1's flame. `DestroyAttachment` on the 80 Ready or Not cards, then on
  every card but ours 1: ours 1 still drew that flame, hidden it was gone. So every CGF attachment on the muzzle bone
  draws one model (here the .cdf's first, `deagle_vm_flash_1`), whatever it binds; not their number, not the Ready or
  Not models. The viewmodel's skins (gun, both magazines, 6 rounds: different models at once) each draw their own.
- Fix: every card is a skin of its gun (`CA_SKIN`), every vertex on `tag_flash` at weight 1: `tools/flash_skin.sh`
  (Blender + toolkit + rc, `tools/flash_skin.py`) into `<gun dir>/flash/<card>.skin`, 112 per gun (ours 2 sizes x 4
  shapes x 4 quarter turns: the roll is baked, skins can't be turned at run time; Ready or Not's 2 x 40). Skins are
  built per skeleton: the Deagle's from `mwii_vm_extra.blend`, the Glock's from `glock_17_vm_roles.blend`, with the
  guns' export steps replayed (Source +X -> CryEngine +Y, bone names, the muzzle bone unkeyed).
- A skin carries its whole skeleton (`CompiledBones` 0x2000, `CompiledPhysicalBones` 0x2001), byte-identical in the
  gun's .chr and its skins. It is the rest pose (the Deagle's muzzle at -0.04, -0.15, 1.15), not the export frame's;
  the card vertices are put there by the .chr's `m_DefaultB2W` (offset 264 of each 584-byte bone, after 32 bytes).
  `tools/flash_cdf.py` checks every card skin: both chunks equal the .chr's, every vertex on the muzzle bone alone
  (`CompiledIntSkinVertices` 0x2005: 32 bytes, then 64 per vertex, bone ids u16 x4 at 36, weights f32 x4 at 44).
  Every Ready or Not card corner sits within 0.001 mm of the old CGF's, relative to the muzzle bone.
- Toolkit traps: `bcry.add_cry_export_node` files a mesh of 4 vertices or fewer on an armature as `<armature>.chr` (its
  skeleton stand-in), whatever node type you ask for: the 22 one-quad cards went missing. The script makes the
  `<name>.skin` collections itself. rc's second pass (one rc per skin) runs on after Blender quits: wait for it.

## 2026-10-14: the skins draw; Ready or Not's sheets were solid, so its cards drew dark squares (Claude 4)
- In game (pak f934c3e1, the user's clip `mkv sound clips/2026-10-14 10-15-06.mkv`): the card skins draw their own
  shapes. Ours differs every shot; one Ready or Not flash in F9 steps through its stages and is gone by ~40 ms.
- Ready or Not's cards drew dark-red squares over the glow behind them. Its 5 flash sheets are solid (alpha 255
  everywhere) and every flash material is additive (`M_Particle_Additive`, `BLEND_Additive`): black adds nothing there.
  KCD2's Illum cards blend by alpha (`MtlFlags` 0x4 "additive" didn't make them add), so dark texels covered what was
  behind. `tools/ron/flash_bake.py --textures`: alpha = the texel's brightest channel, colour / alpha, so by alpha a
  texel adds what it added in Ready or Not (colour x alpha = the original within 0.5 of 8 bits); the grey sheets turn
  white with the shape in alpha. Compiled in `flash_ron/tif` with rc `/preset=AlbedoWithGenericAlpha`, staged in
  `src/Textures/particles/deagle/ron_flash/`. Not seen in game yet.
- Still red: Ready or Not's hue is (1, 0.0118, 0.0052) x up to 327 nits/100; its tonemapper turns the core white and
  the body orange, KCD2's keeps the body red. Brightness steps `_x10` / `_x30` to compare live.

## 2026-10-14: shots after a save load flew along NaN; chickens are Boids; a round in the skull didn't kill (Claude 4)
- "Can't damage the NPCs in a house": after a save load the game clock went back 322 s (shot 97 at 15073.6, shot 98 at
  14751.2). The gun's kicks (`gunKicks`, kept over the load) then lay ahead of the clock: `now - k.t` < 0 passed the
  `< 0.6` test, `exp(-zeta w t)` blew up, and every shot's direction was NaN (`dir -nan(ind)` in the shot log) until the
  clock caught up: no hits at all. Now a load clears the kicks, the camera recoil, the motion state and the CS2
  accuracy timers, and `GunKickNow` drops a kick from ahead of the clock. The street fights worked because they came
  before the load.
- Chickens (also cats, rats, fish) are flocks: one `Boid` entity per animal (`Scripts/Entities/Boids/Boid.lua`), no soul.
  The game kills one through `Boid:OnHit` -> `Boids.OnBoidHit(flock, boid, hit)` (reads `pos`, `dir`, `damage`; the body
  is shoved by dir x damage). Rounds treated it as a prop: a world impact, no kill. Now `ApplyHit` calls
  `Boids.OnBoidHit` (damage = the round's momentum), bleeds, and the round flies on through.
- A Glock round through a bare head at 0.7 m left 31 of 100 HP: `headMult` 2 assumed KCD2 adds its own head x1.5, but
  `soul:DealDamage(stam, hp, attacker, false, part)` doesn't. Now a round that reaches the head with 0.4 J/mm² of its face
  (bone perforation, Sellier & Kneubuehl; 9 mm 26 J, .50 AE 51 J; `ArmorHit` returns `left`) kills; a helmet that stops
  it or padding that drains it below that is a wound x `headMult`.

