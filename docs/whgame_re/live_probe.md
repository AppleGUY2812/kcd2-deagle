# A live reflection probe for the viewmodel? (WHGame.dll 1.5.6, static, 2026-10-20)

Verdict: a GTA V-style live probe (a cubemap re-rendered at the gun every frame) is **not possible** through KCD2's
probe system. The cheap, safe path that is left is a static "weapon cubemap" probe that rides with the camera.

## Facts
- **Probes are baked, not rendered at runtime.** `Textures/cubemaps/<level>/<probe>/<hh>_<mm>_cm.dds` (+ `_cm_diff.dds`)
  in IPL_Textures-part0/1.pak: ~33,700 specular cubemaps, one per probe per time-of-day key (03_00, 04_00, 04_45, 05_45 ...).
  Every header read (300 per pak) is **128 x 128, BC6H_UF16 (DXGI 95), 6 mips** (diffuse: 128, 1 mip).
  EnvironmentLight's `OptionsAdvanced.bDynamic` -> `tod_dynamic` picks the ToD set (Scripts/Entities/Lights/EnvironmentLight.lua).
- **The atlas.** FUN_180b3e00c makes `$TiledSpecProbeTexArr` (format 0x1d, 0x180 slices, mips = log2(size) - 1) and
  `$TiledDiffuseProbeTexArr`. The slice size is `wh_r_ProbeCubemapTexSize` (int at 0x1850dbdb8, default 0x20 in code,
  system.cfg sets 128). Its help text: "Size of one texture in the atlas. You will have to change probe entity properties,
  rc.ini, re-generate all cubemaps and restart editor after changing this cvar."
- **A probe texture smaller than the atlas is refused.** FUN_182231c1a (called from FUN_1807746b4) accepts a probe
  texture only if atlas width <= texture width, the texture is square and the format equals the atlas'. Otherwise it logs
  "TiledShading: Unsupported texture properties: %s (W:%i H:%i F:%s)" and returns -1, i.e. the probe is dropped.
  **So `wh_r_ProbeCubemapTexSize = 512` drops every baked (128 px) probe**: no local reflections or probe ambient anywhere.
  It does not sharpen anything.
- **Cubemap generation** (r_CubemapGenerationTimeout, r_CubemapGenerationAdditionalWaitFramesWH, r_HideSunInCubemaps,
  r_HideSSReflectionsInCubemaps, CEnvironmentProbeBatchRendererWH: "Creating environment probe batch renderer,
  overriding cvars, creating graphics pipeline" FUN_182520120, "Destroyed ... in %f s" FUN_1825211c8, generation
  FUN_1825263f8) is the editor's bake tool compiled into the shared engine: it waits for streaming, renders, and writes
  files that rc then compresses to BC6H. Created only through a renderer-interface factory (FUN_1824e8294, vtable
  entries); no console command or Lua binding reaches it. Seconds per probe, not per frame.
- **Why live can't plug in:** the atlas is block-compressed (BC6H); a live render target (RGBA16F) can't be copied
  into it without a GPU BC6H encoder, and the slice size is fixed at 128 for the whole game.

## What can be done
- **Static weapon cubemap that follows the camera** (what many FPS games do for viewmodels). A Lua-spawned
  `EnvironmentLight` (Properties: BoxSizeX/Y/Z, SortPriority, Projection.bBoxProject, OptionsAdvanced.texture_deferred_cubemap,
  bDynamic=false, Color.fSpecularMultiplier / fDiffuseMultiplier 0), box ~0.8 m around the eye, top SortPriority, moved
  with the camera every frame, with our own cubemap: a 128 px BC6H, 6-mip `_cm.dds` built by rc from an HDR studio or
  outdoor panorama (any size >= 128 works per FUN_182231c1a; 128 keeps memory equal). Weak points (all untested):
  - it lights any world surface inside the box too (a wall you press against);
  - the gun reflects that panorama, not the real place (indoors it shows the panorama's sky);
  - whether a deferred probe picks up the viewmodel's pixels correctly (its depth range) is unknown;
  - whether moving a probe entity every frame is cheap (each move re-inserts the light).
  - Cost estimate: 2-3 h to build + an in-game check.
- **Keep 128.** Remove `wh_r_ProbeCubemapTexSize = 512` from the game's user.cfg.
