# Q1: pfx2 (Wavicle) in WHGame.dll 1.5.6?

## Verdict: absent. pfx1 is the only working particle system. The pfx2 and GPU particle traces left are dead: a cvar registered but never read, and shader source that was never compiled.

Confidence: high (about 95 %).

## Evidence (RVAs are WHGame 1.5.6)

**RTTI.** The binary has 51,763 `.?AV` names. None is in the `pfx2::` or `gpu_pfx2::` namespaces. There is no CParticleComponent, CParticleSystem, CFeature*, CParticleComponentRuntime or gpu_pfx2::CManager. The particle classes that are there are all pfx1: CParticleManager, CParticleEffect, CParticleEmitter, CParticleContainer, CParticleSource, CREParticle and CParticleBatchDataManager, plus the shared interfaces IParticleManager, IParticleEffect, IParticleEmitter and IParticleAttributes. IParticleAttributes and TParticleCounts are 5.x shared-interface types, which shows the base is a CE 5.x interface with the pfx2 implementation stripped out.

**Strings.** None of these appear in ASCII or UTF-16: `.pfx`, `Wavicle`/`pfx2` outside help text, the feature names (SpawnRate, RenderSprites, MotionPhysics), `e_ParticlesThread` or `e_ParticlesJobsPerThread`. The only pfx2 mentions are cvar help texts: 0x4033c70 ("Convert pfx1 to pfx2…"), 0x4032fe0 ("Wavicle only:" for e_ParticlesProfiler) and 0x4033420 ("PFx1 only:"). The only library path in the binary is `Libs/Particles/` + `System.xml` (pfx1 XML).

**What `e_ParticlesConvertPfx1` does.** The name is at 0x40338f8. CVars::Init (0x15c0bc0) registers it at 0x15c130d, and it is stored at CVars+0x34C. The CVars object is created at 0xa667e4 (new 0xA98, ctor 0x23d19c8) and its pointer is kept in the global 0x5496fd0 (Cry3DEngineBase::m_pCVars).
- I checked all 775 code references to that global. None reads +0x34C.
- I walked the call trees of FindEffect, LoadEffect and LoadLibrary (77 functions, 2 levels deep). None of them touches +0x34C.
- So the convert code is gone, and the cvar is a leftover from the registration list.

**What `r_GpuParticles` does.** The renderer registers it at 0x10182cb ("Enables processing of GPU particles"), storing it at 0x50dbcd0. The only code reference to 0x50dbcd0 is that registration, so it is never read. `r_GpuParticlesGpuBoundingBox` (stored at 0x5570ff4) is the same. The two other references to these names, 0x9a48a5 and 0x150eeac, are cvar-name lookup lists.

**Shaders.**
- Shaders.pak still has `GpuParticles.cfx`, `GpuParticlesCommon.cfi` and `GpuPhysicsParticleFluid.cfx` (Crytek 2001-2020 header). GpuParticles.cfx defines the compute techniques FeatureUpdate, FeatureInitialization, PrepareSort and others. The renderer also keeps "GpuParticles" in its system shader name table (0x492e008 → 0x40a9278).
- ShaderCache.pak has **no** `cgcshaders/gpuparticles@*` entries, while clouds, volumelighting, sgsr2 and others are there. `particles@gpuparticlevs` is a 16-byte empty stub.
- So the GPU particle shaders were never compiled or used.

**Assets.** I listed 109 paks (Data, Levels, Localization, Mods) and found 0 `.pfx` files. The particle libraries are pfx1 XML: IPL_GameData.pak `Libs/Particles/*.xml` (11 libraries) and the mod's deagle.pak. They are `<ParticleLibrary … ParticleVersion="28">`, which is the pfx1 format.

## Useful pfx1 API: CParticleEmitter vtable (0x3A55F48)

Builds on docs/muzzle_flash_research.md.
- **+0x2C8 `EmitParticle(const EmitParticleData*)`** at 0x3500080. It makes the containers if needed (0x5dd8a0), passes the call to the first container (0x34fff9c), and resets the emitter's death age. The container queues a 0x50-byte copy, and the particles are made in the next update. So one emitter can emit many particles per shot, each with its own position and velocity.
  - **EmitParticleData (0x50 bytes):** +0x00 IStatObj* (AddRef'd), +0x08 IPhysicalEntity*, +0x10 QuatTS Location (q 16, t 12, s 4), +0x30 Velocity3 (vLin 12, vRot 12), +0x48 bool bHasLocation, +0x49 bool bHasVel.
  - **Caveat:** only `m_Containers.front()`, the first sub-effect, emits.
- **Other slots:**
  - +0x2B8 SetTarget(ParticleTarget)
  - +0x2C0 Update()
  - +0x2D0 GetAttachedEntityId
  - +0x2D8 GetAttachedEntitySlot (+0x1D8)
  - +0x2E0 forwards to effect vfunc +0xD8 (likely GetAttributes / params)
  - +0x2E8 last slot (byte +0x150)
  - Kill +0x260 is confirmed: DeleteEmitter calls it.
- **IParticleManager (gEnv+0x68)**, vtable 0x402ae90 (CParticleManager at offset +8):
  - +0x30 FindEffect ("Particle effect not found")
  - +0x38 LoadEffect
  - +0x48 LoadLibrary (`Libs/Particles/`)
  - +0x50 default/System.xml load
  - +0x68 CreateEmitter(const QuatTS&, const ParticleParams&, flags, SpawnParams*). It wraps the params in a new 0x40-byte effect, so it can make an emitter from code with no XML.
  - +0x70 DeleteEmitter (it calls emitter->Kill +0x260)
  - +0x78 DeleteEmitters(mask)
  - +0x80 SerializeEmitter
  - +0xA0 / +0xA8 the "ParticleSystemPreUpdate" / "ParticleSystemUpdate" profile markers

## Unknown
- The +0x38 to +0x58 and +0x68 slot names are matched to the CE 5.x header by order and are not fully checked.
- The renderer may still hold dormant GPU-particle C++ code without RTTI. Nothing reads its switch, so it doesn't matter.

## One in-game test that settles it
In the dev console, type `e_ParticlesConvertPfx1 1`, then spawn any effect, for example `Particle.SpawnEffect("firearms.hand_cannon.frontParticleEmit", …)`. A pfx2 build would log the conversion or write a `.pfx`. Expect nothing new in kcd.log and no `.pfx` in the user folder. If the cvar isn't even accepted (`e_ParticlesConvertPfx1` with no value prints its value), that also settles it.
