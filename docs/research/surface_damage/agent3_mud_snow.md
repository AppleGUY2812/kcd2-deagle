# Mud/snow deformation research (agent3)
Sources fetched: gdcvault.com/play/1020379 (Batman abstract), taylorfrancis b22483-18 (title only), gameenginegems id=1314 (bibliographic only),
gamingbolt RDR2 tech analysis, mudrunnermods.com/?p=40604, 80.lv RDR2 tutorial (no technique), cryengine.com docs pages (truncated, no content).
Search-only (not fetched): GPU Pro 7 chapter summary, Batman GDC slides, RDR2 reddit/polycount speculation, foro3d SnowRunner article, UE VHM API pages.
See final report for verified/inferred split. KCD1/2 footprints: nothing found.
Ranking: 1 per-footprint POM decals (cheap, in-engine), 2 plugin-fed height RT -> custom shader (hard, fork shaders), 3 ITerrain heightmap edits (coarse), 4 e_TerrainDeformations crater system reuse.

## SECOND PASS (verified by actual fetch)
- Batman AO slides (slideshare colinbb/gdc2014-...): ankle-high ORTHO frustum rendered from BELOW surface; clear black, actors white, filter+accumulate in ping/pong RT; UV = frustum 0-1; res = min(512, surface/4); 2MB consoles, 2-4MB PC (FP16/32); inactive heightmaps' memory reused; PC DX11 tessellation (min height field + projected displacement, "like a z-buffer"); snow refills by subtracting small value each accumulation; save/reload = future work; some meshes untessellatable (geometry visible under snow).
- blog post colinbarrebrisebois.com/2014/08/16/deformable-snow-and-directx-11-in-batman-arkham-origins/ links slideshare+YouTube.
- God of War (80.lv Ragnarok snow article): 2018 = screen-space parallax in pixel shader (artifacts); Ragnarok = HW tessellation displacement, capsules carve each frame, VFX top-down alpha mask for detail normal, GPU-driven spawned meshes, terrain meshlet chunks via indirect draw, 2 draws per terrain mesh, compute culls chunks in displacement area.
- Ghost of Yotei (blog.playstation.com/?p=411201): tessellated terrain deformable at runtime; particles+geometry rendered to displacement buffer; persistence not stated.
- Kodeco UE4 snow trails: orthographic scene capture BELOW objects, custom depth vs scene depth compare (25u offset), grayscale RT, limited area, persistence via ping-pong RT; RT blends textures + displaces landscape.
- RDR2 PC (Shacknews guide): Tessellation Quality mainly affects snow and mud tracks, Ultra gives deep tracks with self-shadow, 2-4% perf cost. (so RDR2 tracks are tessellation-displacement based = VERIFIED-ish)
- Frozen Wilds (gamebanshee): snow deformation, trails persist a while, grass shows through.
- MudRunner (80.lv Zagrebelny): terrain = regular vertex grid + height map + mud layer; separate higher-res grid for mud; mud displacement on GPU; sim only near camera.
- CE3 docs tess page: Illum/HumanSkin/Vegetation only (NOT terrain) support tessellation; _displ alpha; sys_spec=4; params. CE5 terrain layer docs unreadable (truncated).
- UE RVT docs: caches shading data; can store height/displacement; static objects only; no depth buffering. VHM: API page only.
- KCD2: nothing official; Nexus 'Harsh Winter' mod (search snippet only) claims footprints and credits a separate footprints mod => community made, not vanilla.
