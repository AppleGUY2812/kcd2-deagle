# Entity-free lights in WHGame 1.5.6 (static RE, capstone; RVAs, image base 0x180000000)

## Q1. Per-frame light with no entity
- C3DEngine vtable RVA 0x40205E8 (RTTI .?AVC3DEngine@@, 306 slots, only COL at offset 0, so it is the object at gEnv+0x08).
- There is NO virtual AddLightToRenderer. `C3DEngine::AddLightToRenderer(this, const CDLight&, float fMult, const SRenderingPassInfo&)`
  is non-virtual at RVA 0x5A4B44: takes spinlock 3DEngine+0x1064, calls renderer vtable +0x570, ++3DEngine+0x630.
  Its only caller is 0x5A4560 (WH per-object lights). CLightEntity::Render (0x657AB8) inlines the same thing.
- Renderer vtable +0x570 (CD3D9Renderer 0x3DFC008 and CRenderer 0x3DF0F68 -> 0x656B14) = EF_AddDeferredLight(const CDLight&, float, passInfo):
  copies the light into a per-type linked list in the render view (passInfo+0x20): type 1 if flags&0x800 (cubemap), 2 if &0x200000 (ambient), else 0.
  It does no count check. It sorts by scissor area (+0x44*+0x46) only when [[view+0x8d60]+0x1534]==1 (that cvar is unknown).
- Both need a live SRenderingPassInfo, so you have to hook something inside the render pass (e.g. CLightEntity vtable +0x18 Render). Calling them from outside is not possible.
- SetupLightScissors(3DEngine, CDLight*, passInfo) = 0x5A4BE0 is called before it (general pass only).
- Cvars (CVars::Init 0x15C0BC0; m_pCVars global 0x5496FD0; register helper 0x23D8168(name,&var,default,flags,help)):
  e_DynamicLights +0x198 default 1 (flags 2 = cheat); read in CLightEntity::Render (0 = off, 2/3 = debug).
  e_DynamicLightsMaxCount +0x1C4 default 512; e_DynamicLightsForceDeferred +0x1F4 default 1;
  e_DynamicLightsFrameIdVisTest +0x19C default 1; e_DynamicLightsConsistentSortOrder +0x5B4 default 1.
  No read of MaxCount, FrameIdVisTest or ConsistentSortOrder was found in any function that loads m_pCVars. They look dead, so MaxCount enforces nothing.

## Q2. Light render nodes (no IEntity): present
- I3DEngine (gEnv+0x08) vtable: RegisterEntity +0x130 (0x4008C0; computes m_fWSMaxViewDist into node+0x48 via node vt+0x168);
  UnRegisterEntityDirect +0x170 (and AsJob +0x178, both jump to 0x4002A8); FreeRenderNodeState +0x458;
  CreateLightSource +0x530 (0xD75914: new CLightEntity of 0x3F8 bytes, pushed into 3DEngine+0x1030);
  DeleteLightSource +0x538 (removes it and deletes it, dtor calls +0x458 which unregisters); GetLightEntities +0x540 (returns this+0x1030).
- CLightEntity vtable RVA 0x3A9BC80 (81 slots): Render +0x18, GetBBox +0x20, GetRenderNodeType +0x38 (=3),
  GetMatrix +0x78 (returns this+0x68), SetMatrix(const Matrix34&) +0x80, SetBBox +0x98, OffsetPosition +0xA0, SetViewDistRatio +0x1D8,
  probably Hide(bool) +0x238 (toggles bit 55 of +0x28; not verified), SetLightProperties(const CDLight&) +0x248 (0x5A3C9C),
  GetLightProperties +0x250 / +0x258 (return this+0xA8), GetShadowFrustum +0x260.
- Members: Matrix34 m_Matrix at +0x68, CDLight m_light at +0xA8, AABB at +0x3D8, m_fWSMaxViewDist at +0x48.
- SetMatrix (0x7DC22C) returns early when the matrix is unchanged. Otherwise it copies the matrix, then 0x20D6B60 rebuilds the bbox,
  writes the origin (+0x10/+0x1E4), CDLight::SetMatrix 0x535F18, SetLightProperties(self) and queues an update (0x20D4170, purpose not confirmed).
  SetLightProperties does a remove plus push_back on 3DEngine+0x1030, so each move is O(number of lights in the level).

## Q3. CDLight (SRenderLight) layout: sizeof 0x320
- Constructor 0x5A337C; LoadLight table parser 0xA286E4 (called by ScriptBind 0xAB4474).
- +0x0C flags; +0x10 origin (Vec3); +0x1C effective radius; +0x20 colour RGBA; +0x30 specular multiplier;
  +0x34 volumetric_multiplier_wh; +0x38 gi_multiplier_wh; +0x3C hdrdyn; +0x44/+0x46 scissor w/h (int16);
  +0x58..+0x60 box_size; +0x64 tod_mul_weight; +0x68..+0x70 box w/h/l; +0x74 falloff byte; +0x75 sort_priority;
  +0xA0 owner render node; +0xB8..+0xD5 shadow parameters; +0xD8 / +0xE0 projector texture; +0x158 proj_fov (default 45);
  +0x15C proj_nearplane; +0x160 name; +0x1A8 light_animation; +0x1E4 base origin; +0x1F0 base colour; +0x200 base specular;
  +0x204 base radius (default 100); +0x208 / +0x20C vertical clip down/up; +0x210 attenuation_bulbsize (default 0.1);
  +0x21C..+0x22C area wall settings; +0x234 fog_radial_lobe; +0x238 fade_in_radius; +0x23C near_clip; +0x244..+0x24B anim/style/flare.
- SetRadius(r, bulb) 0x37DE53C writes +0x204 and +0x210. SetColor 0x5A3720 writes +0x20 and +0x1F0.
  Both call 0x5A3760, which sets +0x1C = min(base radius, a cutoff derived from luminance and bulb size). SetLightProperties does NOT recompute +0x1C.
- Flag bits (from the parser): 0x40 point (set when there is no projector), 0x80 projector, 0x100 spot shadow, 0x200 irradiance_volumes,
  0x400 ignore_visareas, 0x800 deferred cubemap, 0x4000 area, 0x10000 cutscene, 0x20000 fake (the tiled pass skips it), 0x80000 ss_shadows_fallback_wh,
  0x100000 this_area_only, 0x200000 ambient, 0x400000 indoor_only, 0x800000 volumetric_fog, 0x2000000 attach_to_sun,
  0x4000000 trackview, 0x8000000 volumetric_fog_only, 0x30000020 cast_shadow, 0x40000000 disable_shadow_cutoff.
  There is no separate "deferred" flag: every light goes to the deferred lists.

## Q4. Cost / cap
- r_DeferredShadingTiled default 3 (tiled deferred + forward, light lists built by rasterising volumes).
- CTiledLightVolumesStage (vt 0x3A59F48, size 0xB250). Init 0xB3E00C creates GPU buffers of 255 entries (0x50 and 0xF0 per light) and 255 light volumes (0x90).
- Light-list builder 0x7762A4: walks the 3 view lists (env probes, ambient, deferred). Per light: skip if fake; ++total; `cmp r15d,0xFF; je skip`.
  So the hard cap is 255 tiled lights per frame for ALL types together (probes and ambient count too). Anything past it is dropped silently
  and shows as "Skipped" in r_DeferredShadingTiledDebug ("Env:%d, Amb:%d, Def:%d, Total:%d, Skipped:%i").
  Deferred lights come last, so ours are the first to be dropped. Volume builder 0xB825C4 bins lights into 6x255 buckets.

## Cheapest test (no hooks, one call per frame)
p3D = *(gEnv+0x08); L = vt(p3D,+0x530)(p3D); CDLight* d = vt(L,+0x250)(L);
d->flags(+0xC) |= 0x40; colour +0x20 and +0x1F0 = (1,0.27,0.15,1)*k; +0x204 and +0x1C = 8; +0x210 = 0.3; +0x30 and +0x200 = 1;
Matrix34 m = identity with translation = player position + 2 m up; vt(L,+0x80)(L,&m); vt(p3D,+0x130)(p3D,L,-1,-1).
Each frame, call vt(L,+0x80) with a new translation. The test passes if the ground lights up with no entity and nothing in the physics world.
Remove with vt(p3D,+0x538)(p3D,L).

## Unknown
- What the queue at 0x20D4170 does (whether SetMatrix re-registers the node by itself).
- The exact RegisterEntity argument list (taken from CE3: nSID -1).
- The sort cvar at [view+0x8d60]+0x1534.
- Whether +0x238 is Hide.
- Per-light GPU cost, which has to be measured in game.
