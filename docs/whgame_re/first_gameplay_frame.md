# KCD2 1.5.6: what the render thread does with DLSS/NvAPI at the first gameplay frame (static, WHGame.dll, base 0x180000000)
SETTLED = read in the decompile (function address, /tmp/whg.c line). INFERRED = deduced. Helper: ~/re/work/ft_render_stall/fn.sh <addr>; qk daemon used for xrefs and vtable dumps.
Nothing was run. Time spent: about 90 min.

## Class map (SETTLED unless noted)
- `GraphicsPlugin::DLSS` vtable 0x183dfe610 (RTTI COL at 0x183dfe608; ctor FUN_181e06ec0, dump line 6156575 on). Sibling classes: SGSR, FSR_FidelityFX_2_0, FSR, SGSR_2, XeSS (vftable stores at dump lines 6156040-6156527). Slots (base 0x183dfe610):
  - +0x00 dtor FUN_18254d5e0
  - +0x08 IsInitialized = byte[+0x8d4], FUN_181a75a70
  - +0x10 IsReady (initialized and feature handle [+0x8e0] != 0), FUN_182555b20
  - +0x18 IsSupported FUN_181e07fe0 (-> FUN_181dff2f0 = NGX GetFeatureRequirements; logs "NVIDIA DLSS not supported on this hardware")
  - +0x20 Teardown FUN_182556310 (ReleaseFeature FUN_181e10b60, then NGX Shutdown FUN_181e11170, then clears byte[+0x8d5])
  - +0x28 Init FUN_182555bb8
  - +0x30 FUN_181a75a80 (only sets byte[+0x8d5]=1)
  - +0x40 Resize(w,h) FUN_182557730
  - +0x48 RecreateIfDirty FUN_182558b44
  - +0x50 Evaluate FUN_18254f718
  - +0x78 GetOptimalSettings FUN_182556134
- `CSuperResolutionStage` vtable 0x183dfee68 (ctor FUN_18193895c, factory FUN_1819388e4). It is built inside `CStandardGraphicsPipeline::Init` = FUN_180742fe8 (vtable 0x183dfebd0 slot +8); the stage sits at pipeline+0x158. Stage slots: +0x10 Execute FUN_180c1a330 (builds the per-frame passes through FUN_18255e840), +0x38 OnSettings FUN_180c176c4.
- Plugin container (stage+0x18): +8 = DLSS (cvar mode 2), +0x10 = FSR (1), +0x18 = mode 3, +0x20 = mode 4 (cvar value of XeSS), +0x28 = mode 5 (SGSR). SETTLED by FUN_1808968c8 (writes the DLSS preset/quality into the plugin at container+8, offsets +0x284/+0x289) and FUN_182559928 (container+8 wrapper).

## Q1. Who creates the DLSS feature, and when
Chain, bottom up (all SETTLED):
1. `NVSDK_NGX_D3D12_CreateFeature` <- `FUN_182539c5c` <- `FUN_181dff980` (direct call, not virtual; only caller at dump line 6159281) <- `FUN_181e11220(this, renderRes, outRes)` (line 6159215) = DLSS "recreate feature". It releases the old feature (FUN_181e10b60) if there is a handle, recreates the 3 render targets (FUN_18073c4dc), sets the NGX preset hint with SetUI(handle, hint name, this+0x284), calls CreateFeature, then clears this+0x49.
2. FUN_181e11220 has exactly ONE caller, `FUN_182558b44` (line 7362872) = DLSS vtable slot +0x48, which acts only `if (this+0x49 != 0)` (dirty flag). So the feature is created lazily, on the first slot +0x48 call after the dirty flag is set. It is NOT created in Init.
3. Dirty flag +0x49 is set by: Resize FUN_182557730 when byte[+0x8d5]==0 (line 7362156); FUN_1808968c8 (called from the stage Init FUN_180895740 and the settings pass) when the quality/preset value or the sharpen bool changes while the feature exists. Resize comes from `FUN_180c1a0b4` (renderer Resize(w,h), pipeline vtable slot +0x18), which calls slot +0x40 on every initialized plugin. If byte[+0x8d5]==1 at Resize time, FUN_182557730 instead calls slot +0x20 (full teardown: ReleaseFeature + NGX Shutdown).
4. NGX is started in `FUN_181e0b7c0` (line 6157737) -> `FUN_181dffa40`. That builds L"\\Bin\\Win64Shared", calls the static NGX wrapper `FUN_181be4c30` (= NVSDK_NGX_D3D12_Init/Init_Ext, loads _nvngx.dll: "Loaded NGXCore from path (%ls)" at dump line 5760719), then NVSDK_NGX_D3D12_GetCapabilityParameters and reads "SuperSampling.Available / NeedsUpdatedDriver / MinDriverVersion*". FUN_181e0b7c0 is called ONLY from the Init slot FUN_182555bb8, which first allocates 3 targets ("$NVIDIA_DLSS_DecodedMotionVectors / ExposureTarget / IntermediateTarget"). It needs [+0x8d4]==0 and the engine flag `DAT_1852ba9e0 & 0x40000`.
5. What calls Init (slot +0x28): `FUN_182559928(container, bool)` (true: if !IsInitialized then vcall +0x28; false: if IsInitialized then vcall +0x30) <- `FUN_180c1748c(stage, newMode)` = "select technique" (dump line 2423989). It returns at once if newMode == stage[+0x34]; otherwise it enables the new plugin and disables all others (disassembly 0x180c1763c-0x180c176a6 checked: disables pass EDX=0, enables EDX=1) <- `FUN_180c176c4(stage, settings)` = CSuperResolutionStage vtable +0x38 (line 2423787; if the new settings fail it writes cvar r_SuperResolution_Mode=0) <- `FUN_1825476bc` (pipeline vtable +0x30: loops all stages calling their +0x38 until none reports a change) <- `FUN_181e02e60` / `FUN_181e03dd0` (pipeline "update settings"). FUN_181e02e60 (line ~6151640) runs only when `(pipeline[0x1968] & 0x20000) && perFrameSlot != 0`; it snapshots the cvars with FUN_1809a3438 (which reads the r_SuperResolution_* cvars) and applies them.
   => DLSS is initialised in the first pipeline settings pass where r_SuperResolution_Mode (2) differs from the stage current mode (starts at 0). The feature is created at the next slot +0x48 call after Resize/GetOptimalSettings supplied a resolution.
6. First pass timing (INFERRED): the first frame in which the standard graphics pipeline renders the 3D world. For: your sampler shows no nvapi/bcrypt in the menu; and the cvar is already 2 long before gameplay (attributes are applied at SystemInit: CSystem vtable +0x530 = FUN_181dd95d0, called in FUN_18079ffd4 line 1524847 right after "Initializing Renderer..."), so the delay lies in the pipeline/stage, not in the cvar. I did NOT find an explicit "menu has no SR stage" check.
7. Release path (SETTLED): FUN_181e10b60 (ReleaseFeature) has 3 callers only: Teardown FUN_182556310, NGX shutdown FUN_181e11170's caller chain, and Recreate FUN_181e11220. FUN_181e11170 (NGX DestroyParameters + Shutdown1) is called by Teardown and by the Init-failure path only. Teardown (slot +0x20) is called from Resize when byte[+0x8d5]==1. I found NO level-unload or save-load hook that releases DLSS. INFERRED: once created, the feature survives level unload unless a Resize with 8d5==1 or a technique switch happens. Any resolution/quality/preset change later costs a release plus a create.

## Q2. NGX Init and shutdown
- SETTLED: NGX Init only in FUN_181dffa40 <- FUN_181e0b7c0 <- Init slot. Not run at game start unconditionally. Shutdown1 only in FUN_181e11170 (plus a helper inside the static NGX wrapper code, line 5756667).
- SETTLED: the NGX glue is the static NGX helper lib (lines 5755424-5761000: GetProcAddress wrappers, registry key SOFTWARE\NVIDIA Corporation\Global\NGXCore, NGXLoadLibrary).
- SETTLED: IsSupported (slot +0x18) -> NVSDK_NGX_D3D12_GetFeatureRequirements (wrapper at line 5756937 resolves through DAT_185071470, the NGX core module). So a support probe (e.g. options menu) may load the NGX core earlier. I did not find who calls slot +0x18.
- INFERRED: libxess.dll and nvngx_dlssd.dll are loaded passively. XeSS context creation (xessD3D12CreateContext, line 6157649 in FUN_181e0b750; xessD3D12Init in FUN_181e00170) is in the XeSS plugin Init, which only runs for mode 4. Ray reconstruction is never created by the game as far as I saw (only default RayReconstruction hint strings in FUN_181be44f0).

## Q3. Settings mapping
- FUN_181dd95d0 (CSystem vtable +0x530, line 6115304; also called at FUN_18151c10c line 4279060): input struct {upscalingTechnology, upscalingMode} at param_2+1. Technology 0 -> r_SuperResolution_Mode 0; 1 -> FSR (mode 1, FSR_QualityMode = mode==1 ? 0 : 2); 2 -> DLSS (mode 2, r_SuperResolution_NVIDIA_DLSS_QualityMode = upscalingMode==1 ? 1 : 3); 4 -> XeSS (mode 4); 5 -> SGSR (mode 5, sharpness 0). Technology 3 is not handled (returns early). For upscalingTechnology=2, upscalingMode=0 this path gives DLSS QualityMode 3 = Performance (names in FUN_181e0ac40: 1 Quality, 2 Balanced, 3 Performance, 4 UltraPerformance, 5 DLAA, 0 = a 4-char string, probably Auto). The PC options UI also binds each cvar separately (lines 1157904-1157970), so which path the user actually has is NOT settled.
- Cvars (registered at dump lines 3292580-3292658, no help strings): r_SuperResolution_Mode (default 0; 0 off, 1 FSR, 2 DLSS, 4 XeSS, 5 SGSR), r_SuperResolution_NVIDIA_DLSS_QualityMode (default 2), r_SuperResolution_NVIDIA_DLSS_Preset (default 0), ..._DLSS_ExposureCompensation, r_SuperResolution_NVIDIA_APP_ID (0), r_SuperResolution_Sharpness (0.5).
- Preset (SETTLED): the cvar goes unchanged into plugin[+0x284] (FUN_1808968c8) and then into `NVSDK_NGX_Parameter_SetUI(handle, "DLSS.Hint.Render.Preset.<quality name>", value)` (FUN_181e11220). So 0 = no explicit app hint and NGX chooses its default for DLSS 310.1.0 (version string "NVIDIA DLSS (310.1.0)", FUN_181e0b100). Whether that default is transformer or CNN is decided inside nvngx_dlss.dll: not answerable statically. FUN_181be44f0 fills fallback hints 0x80000005 (Quality/Balanced/Performance) and 0x80000006 (DLAA/UltraQuality/UltraPerformance) when GetUI finds none; I do not know what these values mean. "DlssTransformer"/"DlssConvolutional" (line 5538740, FUN_181abf170) are UI label strings; I did not see how they map to the preset cvar.
- INFERRED: with DLSS selected no FSR/XeSS/SGSR setup runs: the disable wrapper only acts if IsInitialized is true. The pipeline ctor still allocates all plugin objects (cheap).

## Q4. Other NvAPI / Aftermath work
- The game links only a tiny static NvAPI: wrappers at 0x180001050 (Initialize, ids 0x150e828 / 0xad298d3f, plus 0x33c7358c, 0x593e8644), 0x180001340 (0x2926aaad), 0x180001410 (0x7f9b368), 0x1800014e0 (0xe5ac921f), 0x1800015b0 (0x9abdd40d). Game callers: "NvAPI_GPU_GetMemoryInfo failed" / "NvAPI_EnumPhysicalGPUs failed" at line 7329436-7329443. The ids resemble (from memory, INFERRED) initialize, enum GPUs, memory info, driver version. There is no D3D12 cubin / PSO-extension / Reflex / latency-marker NvAPI id in the module (the only "Reflex" hits are lighting terms). SETTLED: the 55 % in nvapi64_impl.dll is NOT NvAPI calls from WHGame. It is inside NVIDIA code: NGX/DLSS kernel setup, the driver compiling shaders/PSOs, or Aftermath.
- Aftermath cvars (SETTLED, lines 3293712-3293721): r_EnableNvidiaAftermath (default 0), r_EnableNvidiaAftermathShaderDumps (0), r_EnableNvidiaAftermathCallstackCollection (0), r_EnableNvidiaAftermathShaderErrorReportingWH (0). The log says "Aftermath: Enabled" (printed at line 3234916), so the user's cfg sets it. FUN_181009e58 (line 3234790) calls GFSDK_Aftermath_EnableGpuCrashDumps(0x218, DX, flags 1, ...); device setup FUN_181df7bc0 (line 6144206) calls GFSDK_Aftermath_DX12_Initialize(0x218, flags, device) with flags = 0xb | (callstack ? 0x40000000 : 0) | (shader error reporting ? 0x10 : 0). INFERRED from my memory of the SDK enum: 0xb = EnableMarkers | EnableResourceTracking | GenerateShaderDebugInfo (0x8). All of this runs at device creation (boot), but its cost is paid per shader/PSO creation.
- Per-shader Aftermath (SETTLED): FUN_18253a978 (line 7343359) calls GFSDK_Aftermath_GetShaderDebugInfoIdentifier for each shader blob and copies the bytecode, under an SRW lock; only if r_EnableNvidiaAftermathShaderDumps != 0 (unknown for the user).
- STRONG ALTERNATIVE (INFERRED, not proven): the 4 s may be lazy PSO creation of the first world frame rather than DLSS. The ft_render_stall report shows about 16k of 137k PSOs precached at level load, the rest created at first draw. With Aftermath on, each shader creation hashes bytecode (bcryptPrimitives fits) and the driver compile runs in nvapi64_impl. The single imagehlp sample fits PE checks when loading NGX DLLs (fits DLSS). Both can be present. The sampler cannot tell them apart.

## Q5. Ways to move the cost out of the first gameplay frame
1. Measure first: kcd.log lines "Loaded NGXCore from path" / NGXLoadLibrary and the "NVIDIA DLSS ..." lines timestamp the NGX Init. If NGXCore loads at boot or in the menu, NGX Init is not the 4 s; if it appears at the first gameplay frame it is.
2. Decisive cheap tests with existing cvars: (a) boot with r_SuperResolution_Mode 0 (attribute upscalingTechnology=0): if the 4 s vanish it is DLSS; if not it is PSO/shader work. (b) r_EnableNvidiaAftermath 0 in system.cfg/user.cfg (before device creation, not at runtime). (c) r_SuperResolution_Mode 1 (FSR, no vendor DLL).
3. Pre-initialise earlier (plugin hook, medium risk): the trigger is the pipeline settings pass (FUN_1825476bc -> stage FUN_180c176c4), guarded by dirty bit 0x20000 at pipeline+0x1968. If the standard pipeline already runs during loading, toggling r_SuperResolution_Mode 0 -> 2 there would run Init in the loading screen (I did NOT verify that a cvar write sets the 0x20000 bit). Otherwise call the DLSS vtable directly on the render thread (object = container+8, container = stage[+0x18], stage = pipeline[+0x158]): slot +0x28 (Init). The create (probably the bigger part, loads the 47 MB nvngx_dlss.dll) needs dirty flag +0x49 and a resolution; trigger with renderer Resize FUN_180c1a0b4 at the final resolution, then slot +0x48.
4. Keep the feature alive: there is no level-unload release (Q1.7). Do not change resolution, quality or preset cvars after creation.
5. Fallback: a technique without a vendor DLL (FSR) removes this cost at a quality cost; only if test 2a proves DLSS is the cause.

## NOT found / open
- Meaning of byte[+0x8d5] and who calls DLSS slot +0x30 on a normal path; whether the menu or loading screen runs the standard pipeline settings pass (decides if early init is possible).
- Callers of IsSupported (+0x18); whether the options menu loads the NGX core at boot.
- What the cvar-change callback does with the 0x20000 bit; the per-frame-slot condition in FUN_181e02e60.
- NGX hint values 0x80000005/6 and the NGX default preset for DLSS 310.1.0; the real quality-mode path for the user (Performance via FUN_181dd95d0 vs the PC UI).
- Whether the sampled time is DLSS at all (Q4 alternative). No call-stack evidence exists.

## Added 2026-10-24 by the step-5 session: Aftermath shader dumps (SETTLED in code, effect NOT measured)
- The game's own `system.cfg` (shipped by Warhorse, dated 2026-10-09 10:41 like the 1.5.6 files) sets
  `r_EnableNvidiaAftermath = 1` and `r_EnableNvidiaAftermathShaderDumps = 1` (registered at dump line 3293715 as an int
  bound to `DAT_1850dc260`, flags 0, read on every call).
- With dumps on, every shader the D3D12 layer creates goes through `FUN_18253a978` (DX12GpuDebug.cpp): an exclusive
  SRW lock at this+0x38, `GFSDK_Aftermath_GetShaderDebugInfoIdentifier(0x218, bytecode, size, &id)`, then a copy of the
  whole bytecode into a map at this+0x28, kept for the session. One lock for all threads, a driver call per shader, and
  memory that only grows.
- Fits the boot-17 evidence for the 4.1 s first gameplay frame (render thread 55 % in nvapi64_impl, 12 % hashing,
  ~340 MB paged in) with the lazy PSO creation above (~16k of ~137k PSOs precached at level load). Test: one boot with
  `r_EnableNvidiaAftermathShaderDumps 0` (and one with `r_EnableNvidiaAftermath 0`, which must land before the device
  is created; the log line "NVidia Aftermath: Enabled/Disabled" shows which took). Cost of turning them off: GPU crash
  dumps for Warhorse's crash reporter only.

## MEASURED 2026-10-24 01:46 boot (the load session's, a50aeaf's file list + stacks): the 4 s is NGX start-up
- The 3994 ms first gameplay frame is the render thread starting NVIDIA NGX (DLSS core), not shaders: `_nvngx.dll`
  loads at 113 ms; then CUDA + an NGX model .bin (913 ms), nvngx_dlssg.dll from the exe folder (960 ms, loaded and
  dropped), vulkan-1 + the NVIDIA store's nvngx_dlssg (1296 ms), nvngx_dlssd.dll from the exe folder (76 MB, 1539 ms,
  stays loaded), the store's nvngx_dlssd (1943 ms), NvTelemetryAPI64 (2860 ms). The exe-folder dlssd/dlssg are DLSS
  Enabler leftovers (2026-10-21) that KCD2 never uses.
- In between, about 15 rounds of ~200 ms (3.1 of the 3.7 s): each re-reads the driver settings database
  (`C:\ProgramData\NVIDIA Corporation\Drs\nvdrsdb0.bin`, 1.9 MB, 18 times) and checks for d3d9/ddraw/opengl32 next
  to the exe (195 times each), which looks like the driver re-matching its application profile for each NGX query; plus
  Windows signature checks of each DLL (wintrust/catroot, bcrypt hashing of the whole file).
- Stacks (in-game scan): `_nvngx.dll` on 35 % of samples, nvapi64_impl 53 %, wintrust 24 %, bcrypt 23 %, WHGame 5 %.
- The 2026-10-23 18:31 OptiScaler run had the same start-up as one 5.15 s frame at 18:31:31-36 (26 s after launch),
  not in the first gameplay frame (1.4 s there, DLSS feature creation 0.6 s). So it is a once-per-launch cost whose
  place depends on when the game first runs the super-resolution settings pass. The measured boots load the save with
  `wh_sys_AutoLoadLastSave` (no main menu): whether a normal launch pays it at the main menu instead is NOT known.
- Moving it: `FUN_181e0b7c0` (DLSS NGX init, sets +0x8d4) has no early return before the full NGX init, and
  `FUN_182555bb8` (Init) skips creating the 3 DLSS targets once +0x8d4 is set, so an early call from another thread
  needs either a second NGX init (behaviour unknown) or the targets made off the render thread: not done.
- Cheaper candidates (need the user's go, their files and NVIDIA app settings): move the DLSS Enabler leftovers out of
  the exe folder (sl.*.dll, nvngx_dlssd/dlssg/dlssnr.dll, dlssg_to_fsr3_amd_is_better.dll), and turn off the NVIDIA
  app's Ray Reconstruction / Frame Generation overrides for KCD2 (the game has neither). Each should remove rounds.
