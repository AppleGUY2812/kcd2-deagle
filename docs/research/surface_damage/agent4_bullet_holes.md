# Bullet holes / impact depth research (agent4)
Tags: VERIFIED = read in a fetched page/PDF this session. INFERRED = my reasoning.

## Depth techniques
- Basic parallax/offset: 1 extra sample, crude, breaks with fast height change (learnopengl.com/Advanced-Lighting/Parallax-Mapping) VERIFIED
- Steep parallax: layered stepping, banding at low layer count, 8-32 layers (same page) VERIFIED
- POM: steep + interpolation between layers; cheaper than relief for same quality; slight artifacts at steep angles; silhouettes unchanged; discard UV outside 0-1 (learnopengl) VERIFIED
- Tatarchuk 2006 slides (advances.realtimerendering.com/s2006/Tatarchuk-POM.pdf, text extracted via pdftotext): older bump/offset methods "swim"/flatten at grazing angles; POM uses 8-50 adaptive samples, soft self-shadow, LOD blend to plain bump at distance; "Silhouettes are not computed correctly", "beware of silhouettes", aliasing at silhouettes. VERIFIED
- Relaxed cone stepping (GPU Gems 3 ch18): 15 cone steps + 6 binary steps typical; needs preprocessed cone map (extra channel); rays pierce surface once; fewer shadow artifacts. VERIFIED. Cone step (Dummer) conservative -> rays stop short; cone/relief fail silhouettes on curved meshes (search-snippet only, not fetched) UNCERTAIN.
- Cowboy Programming "Parallax mapped bullet holes" (2007): relief map = diffuse+alpha outline, normal map with depth in alpha; offset UV = viewTS * depth; plain version has no occlusion (base "climbs" walls); cheap 2-sample trick lets base slide behind alpha mask; ~35->40 ps instrs on 6800GT; streaking at extreme angles at far rim. VERIFIED

## Deferred-decal specifics
- UE forum (realtimevfx.com/t/making-decals-with-depth/5963): deferred decals didn't support AO or Pixel Depth Offset in poster's test; mesh decals recommended; mesh decals z-fight on floors; curved surfaces hard. VERIFIED (forum, anecdotal)
- Epic mesh decal doc: mesh decals do not render to depth, update GBuffer after opaque, no shadows/proper occlusion, no artist sort order, cheaper (fewer pixels, flat backfacing = 0 px, fewer draws) than deferred decal volumes; no easy depth bias. VERIFIED
- => POM in any decal = visual-only depth; depth buffer/SSAO/shadows still see the flat wall. INFERRED from both sources.
- CryEngine docs (not fully read; search snippets only): decal sort order 0-255, flicker if equal. UNCERTAIN (not fetched).

## Shipped games
- R6 Siege: Ubisoft interview confirms procedural material-driven destruction (RealBlast) (news.ubisoft.com ... lheureux) VERIFIED, but page says nothing on bullet holes. NME article: single bullets no longer make see-through holes (anti-peek change); shotguns/sustained fire still do; single bullets still pierce glass & prop cores. VERIFIED. GDC Mansouri slides: PDF has no text layer (couldn't read); search snippet said holes degrade occlusion efficiency UNCERTAIN.
- Doom 2016 (Courreges): decals applied in clustered FORWARD pass as meshes render, atlas texture, CPU voxelized into 3072 clusters (16x8x24), up to 256 decals/cluster. VERIFIED
- Insurgency Sandstorm: tiny substep line-trace ballistics, penetration depends on thickness/material (search result snippet; not fetched) UNCERTAIN.
- Hunt Showdown: penetration of wood/sheet metal depends on ammo (forum snippets only); NO CryEngine decal info found. UNCERTAIN.
- Tarkov, CoD, Max Payne 3, BF/Frostbite, RDR2, The Finals, Crysis, FEAR: no technical source found. (FEAR decals use depth buffer so they don't hang off corners - forum snippet only, UNCERTAIN.)

## See-through fakes
- Interior mapping (80.lv article, fetched): ray from camera into virtual room boxes, cheap math, flat painted room, inconsistencies. VERIFIED. Applying to a hole = cubemap/box "behind" the wall is a lie of fixed content; INFERRED.
- Light shafts: generic fakes (cone mesh w/ falloff, camera-facing sprites) from search snippets only. No bullet-hole-specific source. UNCERTAIN.
- Real see-through requires real geometry cut (Siege) or alpha-tested thin geometry. INFERRED.

## Budgets
- Meta UE guide (fetched): decal geometry cost scales with number of decals in frustum even if occluded; shading cost with screen coverage, overlap, properties modified; hidden decals no shading cost; keep count low, avoid overlap. VERIFIED
- Decals need lifetime/fade (Godot/Torque snippets) UNCERTAIN as sources.
- No source found for typical max counts in shipped games other than Doom 256/cluster.

## Recommendation (INFERRED)
1. POM decal with small depth (KCD2 already ships this shader variant), stronger normal + dark AO baked into albedo, per-material atlas variants.
2. Tight step count + distance LOD fade to normal-only.
3. Spawn debris particles + dust at impact; chips decal ring.
4. Thin planks/doors: exit hole decal on back side via second raycast; light leak via small spot light only if cheap.
5. Real see-through only via swapping geometry (not feasible for walls).

# SECOND PASS (appended)
## CryEngine docs (fetched via curl+strip because WebFetch got only nav)
- Silhouette POM (cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/1048726): "mix between POM and tessellation", heavier than POM, alters mesh silhouette WITHOUT tessellating; PC very-high spec only; same textures as POM + 2 params: Number of steps, Step size view independence (more samples at grazing). Does NOT extrude shells. VERIFIED
- POM (CE5 pages/29450125, CE3 pages/1048721): POM High/VeryHigh PC, falls back to OBM elsewhere; params POM Displacement, Height Bias, Self Shadow Strength; height map = _displ texture alpha (BC4). Used on flat surfaces where silhouette does not change. Side-view artifacts worsen with displacement (0.05 ok frontal, bad from side). "Incorrect height bias": darkening because shadows, SSAO, AA etc take the displacement into account (i.e. CryEngine POM feeds deferred effects -> likely writes displaced depth on regular geometry). Self-shadow forces monochrome specular (gbuffer packing). VERIFIED
- Deferred Decal Setup (CE3 pages/1048875): deferred faster than old projection but slower than Planar; keep decals Planar where possible; cubemap decal materials unsupported; 90deg projection stretches; r_deferredDecalsDebug 1 colours cost red/green/blue; cost = number of objects projected onto, geometry cost, overdraw. VERIFIED
- Decals page (CE3 pages/1048871): 4 projection types; Planar cheapest, placed flat else "floating"; ProjectOnStaticObjects is deferred pass. VERIFIED
- e_Decals* cvars (MaxCount, DeferredDynamic): NOT found in any doc. UNCERTAIN/unknown; the only per-decal lifetime found is fLifeTime in CryEngineDecalInfo (search snippet).
## Frostbite destruction masking (Kihl, SIGGRAPH 2010)
- slideshare.net/DICEStudio/siggraph10-arrdestruction-maskinginfrostbite2 and the realtimerendering PDF (pdftotext): artist spheres -> signed distance field in ~8x8x8 volume texture (~2 m/texel) per object, detail-noise added to threshold; drawn as a DEFERRED DECAL (convex volume, depth-buffer reconstruct position, alpha blend over G-buffer) that masks (erases) the wall visually; PS3 costs 0.31 ms deferred volume vs 0.77 forward, 0.20 triangle culling. Needs tangents not in G-buffer. VERIFIED. KEY INSIGHT: a deferred decal CAN visually "remove" wall pixels only by blending over G-buffer to something else (e.g. showing hole interior) - it cannot reveal what is behind; they paired it with real geometry for the debris/interior. INFERRED.
## Siege
- gdcvault.com/play/1023003: "Art of Destruction in R6 Siege", L'Heureux, GDC 2016: Realblast as core gameplay feature; description only. VERIFIED. Ubisoft dev blog Y5S1: explosive damage uses raycasts; Realblast radius; shrapnel holes give visual feedback. Nothing on bullet-hole geometry. No source found for hole-stamp/boolean method. UNCERTAIN.
## Other games
- Hunt (huntshowdown.com/news/inside-hunt-s-weapon-system-with-david-west): penetration depends on hit material, thickness, number of objects already penetrated; damage decays per penetration; no decal info. VERIFIED. Shacknews 1896 interview: no decal/penetration info. 
- Tarkov, Ready or Not (only 2017 announcement snippets: "parallax bullet holes" on flesh, snippet only), Sandstorm slides (page 403): no decal technique source. UNCERTAIN.
- 80.lv "Setting up advanced bullet holes mechanics in Unity": Project Ferocious (OMYOG) shows shoot-through thin surfaces, see-through holes and light passing through them; no implementation detail. VERIFIED existence only.
- Teardown, The Finals: no hole-rendering detail found (search snippets: The Finals server-side destruction, GDC 2024 talk 1034280 login-locked). UNCERTAIN.
## Depth-correct POM
- UE forum (forums.unrealengine.com/t/pdo-pom/52782): PDO output goes to material's Pixel Depth Offset; PDO affects only shadow RECEIVING, not casting -> self-shadow artifacts; disable cast shadow. Decals not discussed. VERIFIED. Other thread (dark spots) had no replies.
- Unity HDRP POM node doc: has Depth Offset output so depth-buffer effects (shadows, SSAO) follow the POM. Decal use not confirmed. VERIFIED (node) / UNCERTAIN (decals).
- Unity forum (Policarpo relief w/ silhouettes): border clamp (alpha 0 outside tile) as silhouette fix on flat quads; relaxed cone 15 linear+5 binary. VERIFIED. UFRGS page lists Oliveira&Policarpo papers; I did not read the curved-relief paper; "quadric per-vertex" claim only from a gamedev blog snippet. UNCERTAIN.
## Stencil / interior
- Unity forum snippets (stencil window): stencil portal cuts show other walls between camera and window; runtime alpha mask alternative. Snippets only, UNCERTAIN.
- gamedeveloper.com interior mapping: UE4 has built-in InteriorCubeMap; SimCity 2013 atlas variant. VERIFIED.
