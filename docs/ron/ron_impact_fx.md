# Ready or Not bullet-impact & tracer visuals

Read-only decode of the Steam install (UE5.3), private use. Textures are full-resolution mip 0 as PNG (BC5 normals get a rebuilt Z). `-` = not set (engine default); **name-only** = the property exists but holds no readable value.
Exports: `E:\Tools\deagle-work\ron_fx\decals\` (64 PNG), `E:\Tools\deagle-work\ron_fx\particles\` (95 PNG), raw decode JSON in `E:\Tools\deagle-work\ron_fx\_data\`.

## How impacts are picked

- Bullet hit → `ImpactEffect` BP chosen by weapon/ammo: **AllImpactEffects** (rifle/default), **ImpactEffects_Pistol**, **ShotgunImpactEffects**, **ExitImpactEffects** (exit wounds/holes), **RicochetImpactEffects**. Each has one `ImpactFx` per physical surface `RON_*`: ParticleFx (Niagara `NS_` or Cascade `P_`), Decal material, optional DecalMesh (glass) and PaintMaterialTexture (flesh/paper mesh-paint).
- Decal size is random between DecalMinSize and DecalMaxSize (UE units = cm): ImpactEffects_Pistol 3.0–4.0, AllImpactEffects 4.0–6.0, ShotgunImpactEffects 3.0–5.0, ExitImpactEffects 6.0–12.0. DecalMeshScaleMultiplier (glass meshes): All 0.12, Shotgun 0.14. Ricochet ParticleScale 0.33.
- **Decal lifetime / fade / random rotation: name-only.** No DecalComponent template exists in the BPs; decals are spawned in C++ (ImpactEffect.DecalComponent, DecalScale) with code defaults.
- Niagara impacts are GPU sims in world space with fixed bounds. **Spawn counts, lifetime, velocity and gravity are compiled into Niagara VM bytecode and are not readable** (only curves as baked LUTs, renderer setup, user colours). Cascade (`P_`) systems are fully readable (module tables below). Cascade value notation: a uniform range is `minX/minY/minZ/maxX/maxY/maxZ` (cm, cm/s, s). A curve is its flattened baked samples. A burst is `low-high@time`.

## plaster / drywall

### RON_Plaster

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_BulletImpact_Plaster` | `M_BulletImpact_Drywall_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\t_airfield_concrete_a.png (1024²,BC7) |
| ImpactEffects_Pistol | `NS_BulletImpact_Plaster` | `M_BulletImpact_Drywall_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\t_airfield_concrete_a.png (1024²,BC7) |
| ShotgunImpactEffects | `NS_BulletImpact_Plaster` | `Decal_Impact_Inst_Plaster`  | E:\Tools\deagle-work\ron_fx\decals\Plaster_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Plaster_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Plaster_Exit` | `Decal_Impact_Inst_Plaster`  | E:\Tools\deagle-work\ron_fx\decals\Plaster_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Plaster_N.png (256²,BC5) |
| RicochetImpactEffects | `P_BulletImpact_Plaster` | `M_BulletImpact_Drywall_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\t_airfield_concrete_a.png (1024²,BC7) |

- **NS_BulletImpact_Plaster** (Niagara, `/Game/ReadyOrNot/VFX/VFXImpacts/Niagara/NS_BulletImpact_Plaster`)
  - `Base_Dust_16`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ThinSmoke_01` subUV [8.0, 4.0] align 0 | curves: Curve_8: 1.5 → max 1.5 → 0 (56 samples); Curve_8: 1.5 → max 1.5 → 0 (56 samples)
  - `DEBRIS_HIGH_SETTINGS_3`: GPUComputeSim, localSpace 0, bounds -25..25 / -25..25 / -25..25 | Sprite mat `DebrisMaster` subUV [2.0, 2.0] align 0 | curves: Curve_7: 1 → max 1 → 0 (82 samples); Curve_7: 1 → max 1 → 0 (82 samples)
  - `DEBRIS_LOW_SETTINGS_2`: GPUComputeSim, localSpace 0, bounds -25..25 / -25..25 / -25..25 | Sprite mat `DebrisMaster` subUV [2.0, 2.0] align 0 | curves: Curve_7: 1 → max 1 → 0 (82 samples); Curve_7: 1 → max 1 → 0 (82 samples)
  - `Shot_Spatter_15`: GPUComputeSim, localSpace 0, bounds -25..25 / -25..25 / -25..25 | Sprite mat `MI_ParticleGeneric_DebrisSpray` subUV [4.0, 1.0] align VelocityAligned | curves: Curve_8: 1 → max 1 → 0.05 (56 samples); Curve_8: 1 → max 1 → 0.05 (56 samples); Vector2DCurve_8: 0.3 → max 3 → 3 (168 samples); Vector2DCurve_8: 0.3 → max 3 → 3 (168 samples)
  - `Smoke_Big_18`: GPUComputeSim, localSpace True, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ThinSmoke_01` subUV [8.0, 4.0] align 0 | curves: Curve_16: 0.2 → max 1.5 → 1.5 (62 samples); Curve_17: 1.5 → max 1.5 → 0 (54 samples); Curve_16: 0.2 → max 1.5 → 1.5 (62 samples); Curve_17: 1.5 → max 1.5 → 0 (54 samples)
  - `Smoke_Kick_17`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_DustImpact` subUV [16.0, 2.0] align VelocityAligned | curves: Curve_8: 1.75 → max 1.75 → 0 (62 samples); Curve_8: 1.75 → max 1.75 → 0 (62 samples); Vector2DCurve_8: 0.4 → max 1.25 → 1 (84 samples); Vector2DCurve_8: 0.4 → max 1.25 → 1 (84 samples)
  - user params: User.Debris Color=[1.0, 1.0, 1.0, 1.0], User.Dust Color=[0.79, 0.79, 0.79, 1.0], User.Smoke Color=[1.0, 1.0, 1.0, 1.0]
  - material `MI_DustImpact` → E:\Tools\deagle-work\ron_fx\particles\T_dustImpact_16x4.png (512x128,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_ParticleGeneric_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `DebrisMaster` → E:\Tools\deagle-work\ron_fx\particles\Stones2x2.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\Gravel2x2_normal.png (512x512,BC5)
- **NS_BulletImpact_Plaster**: see above
- **NS_BulletImpact_Plaster**: see above
- **P_VFX_Impact_Plaster_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Plaster_Exit`)
  - emitter `Smoke_Impact`
    - Required: material `Whisps_trans`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 2@0s
    - Lifetime: 0.2/0.3 s
    - Size: 50/50/50/100/100/100
    - Velocity: -10/-10/20/10/10/500 radial 0
    - ColorOverLife: color 0.90104/0.90104/0.90104 alpha 0.1/0.29291/0.48583/0.67874/0.87165/1.06457/1.25748/1.45039/1.64331/1.83622/2.02913/2.22205/2.41496/2.60787/2.80079/2.9937/3.18661/3.37953/3.57244/3.76535/3.95827/4.15118/4.34409/4.53701/4.72992/4.92283/4.98634/4.9647/4.94443/4.92551/4.90789/4.89155/4.87645/4.86257/4.84986/4.83831/4.82787/4.81852/4.81021/4.80293/4.79664/4.7913/4.78688/4.78336/4.78069/4.77885/4.77781/4.77753/4.77798/4.77913/4.78095/4.78339/4.78645/4.79007/4.79423/4.79889/4.80403/4.80961/4.8156/4.82197/4.82868/4.83571/4.84302/4.85057/4.85834/4.8663/4.87441/4.88264/4.89095/4.89933/4.90772/4.91611/4.92446/4.93274/4.94091/4.94894/4.95681/4.96447/4.9719/4.97907/4.98593/4.99247/4.99865/5.00443/5.00979/5.01469/5.0191/5.02299/5.02632/5.02906/5.03119/5.03267/5.03346/5.03354/5.03287/5.03142/5.02916/5.02605/5.02207/5.01719/5.01136/5.00456/4.99326/4.94622/4.85896/4.73505/4.57809/4.39165/4.17934/3.94473/3.69142/3.42299/3.14304/2.85514/2.56289/2.26987/1.97968/1.6959/1.42212/1.16192/0.91889/0.69663/0.49872/0.32875/0.1903/0.08697/0.02234/0
    - SizeMultiplyLife: 0/0/0/0.11264/0.11264/0.11264/0.27776/0.27776/0.27776/0.4699/0.4699/0.4699/0.66364/0.66364/0.66364/0.83353/0.83353/0.83353/0.95413/0.95413/0.95413/1/1/1
    - Acceleration: -
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 5@0s
    - Lifetime: 0.1/0.2 s
    - Size: 8/1/1
    - Acceleration: -
    - SizeMultiplyLife: -
    - ColorOverLife: color 25/23.5784/16.125 alpha 1.5/0
    - Velocity: -50/-50/0/50/50/0 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `Bits`
    - Required: material `PolyBits2x2_mat2`, subUV 2x2, localSpace 0, duration 0.1, loops 1
    - Spawn: rate 0, bursts 10@0s
    - Lifetime: 0.8/1.25 s
    - Size: 2/2/2/4/4/4
    - Velocity: 0/0/0/150/150/250 radial 0
    - ColorOverLife: color 0.815/0.815/0.815 alpha 1/1/1/1/1/1/1/1/1/1/1/1/0.99055/0.66667/0.33333/0
    - Acceleration: -
    - TypeData Gpu: 
    - Spawn: rate 0, bursts 7@0s
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `PolyBits2x2_mat2` → E:\Tools\deagle-work\ron_fx\particles\Stones2x2.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\Gravel2x2_normal.png (512x512,BC5), E:\Tools\deagle-work\ron_fx\particles\Polystyrene_bits.png (1024x1024,DXT5), E:\Tools\deagle-work\ron_fx\particles\polystyrene_bits_normal.png (512x512,BC5)
  - material `Whisps_trans` → E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
- **P_BulletImpact_Plaster** (Cascade, `/Game/ReadyOrNot/VFX/VFXImpacts/P_BulletImpact_Plaster`)
  - emitter `SandKickMicro`
    - Required: material `M_DustFine`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 2-5@0s
    - Lifetime: 0.5/1.5 s
    - Size: 100/100/100/400/400/400
    - Velocity: -200/-200/350/200/200/700 radial 0
    - Color: color 0.52604/0.39687/0.3096 alpha 0.5/1
    - ColorOverLife: color 1/1/1 alpha 1/1.09363/0.9517/0.71679/0.53146/0.49944/0.49535/0.48748/0.47613/0.46165/0.44434/0.42452/0.40253/0.37868/0.35331/0.32672/0.29925/0.27122/0.24294/0.21474/0.18694/0.15987/0.13385/0.10921/0.08625/0.06531/0.04671/0.03076/0.01779/0.00813/0.00209/0
    - SizeMultiplyLife: 0/0/0/0.07317/0.39615/0.06837/0.17121/0.61115/0.16856/0.2451/0.69626/0.24504/0.27419/0.72581/0.27419/0.30108/0.75448/0.30108/0.32796/0.78315/0.32796/0.35484/0.81183/0.35484/0.38172/0.8405/0.38172/0.4086/0.86918/0.4086/0.43548/0.89785/0.43548/0.46237/0.92652/0.46237/0.48925/0.9552/0.48925/0.51613/0.98387/0.51613/0.54301/1.01254/0.54301/0.56989/1.04122/0.56989/0.59677/1.06989/0.59677/0.62366/1.09857/0.62366/0.65054/1.12724/0.65054/0.67742/1.15591/0.67742/0.7043/1.18459/0.7043/0.73118/1.21326/0.73118/0.75806/1.24194/0.75806/0.78495/1.27061/0.78495/0.81183/1.29928/0.81183/0.83871/1.32796/0.83871/0.86559/1.35663/0.86559/0.89247/1.3853/0.89247/0.91935/1.41398/0.91935/0.94624/1.44265/0.94624/0.97312/1.47133/0.97312/1/1.5/1
  - emitter `None`
    - Required: material `DebrisMaster`, subUV 2x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 12-24@0s
    - Lifetime: 1/2 s
    - Size: 2/2/0.05/7.5/7.5/0.5
    - Velocity: -100/-100/50/100/100/1000 radial 0
    - SizeMultiplyLife: 1/1/1/0/0/0
    - Color: color 0.75/0.75/0.75/1/1/1 alpha 1
    - Acceleration: -
    - TypeData Gpu: 
  - emitter `ShootSpatter`
    - Required: material `MI_DebrisSpray`, subUV 4x1, localSpace True, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0.025s
    - Lifetime: 0.175/0.275 s
    - Size: 45/73.5/15/45/105/12
    - SizeMultiplyLife: 0.3/0.5/0/0.71666/0.92355/0.16526/1.12792/1.33698/0.35085/1.51655/1.73307/0.54007/1.8653/2.10463/0.71627/2.15696/2.44444/0.86277/2.37427/2.7453/0.96291/2.5/3/1
    - Velocity: 0/0/25 radial 0
    - ColorOverLife: color 0.25/0.25/0.25/0.5/0.5/0.5 alpha 1/1/1/0.99443/0.81547/0.65552/0.5185/0.40282/0.30685/0.22894/0.1675/0.12085/0.08743/0.06555/0.05363/0.05
    - Acceleration: 0/0/-980
  - emitter `SmokeKick`
    - Required: material `M_Smoke1`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1-3@0s
    - Lifetime: 0.5/0.8 s
    - Size: 150/150/150/200/200/200
    - Velocity: -100/-100/200/100/100/1000 radial 0
    - Color: color 1/1/1 alpha 25
    - ColorOverLife: color 1/1/1/0.27604/0.15752/0.10495 alpha 2/2.06085/1.91366/1.64628/1.34658/1.10242/1.00095/0.99528/0.9818/0.96026/0.93142/0.89603/0.85487/0.80872/0.75836/0.70454/0.64803/0.5896/0.53002/0.47005/0.41047/0.35203/0.29552/0.2417/0.19134/0.1452/0.10405/0.06866/0.03979/0.01822/0.0047/0
    - SizeMultiplyLife: 0/0/0/0.06304/0.33019/0.05695/0.17489/0.57385/0.16581/0.31979/0.74483/0.31013/0.48196/0.85694/0.47347/0.64565/0.92403/0.63937/0.79508/0.95994/0.79139/0.91449/0.97848/0.91308/0.98811/0.9935/0.98801/1.01476/1.02952/1.01476/1.03682/1.07363/1.03682/1.05887/1.11774/1.05887/1.08093/1.16186/1.08093/1.10298/1.20597/1.10298/1.12504/1.25008/1.12504/1.1471/1.29419/1.1471/1.16915/1.33831/1.16915/1.19121/1.38242/1.19121/1.21327/1.42653/1.21327/1.23532/1.47065/1.23532/1.25738/1.51476/1.25738/1.27944/1.55887/1.27944/1.30149/1.60298/1.30149/1.32355/1.6471/1.32355/1.34561/1.69121/1.34561/1.36766/1.73532/1.36766/1.38972/1.77944/1.38972/1.41177/1.82355/1.41177/1.43383/1.86766/1.43383/1.45589/1.91177/1.45589/1.47794/1.95589/1.47794/1.5/2/1.5
    - Acceleration: 0/0/-3/0/0/-1
  - emitter `SmokeKick`
    - Required: material `MI_DustImpact`, subUV 16x2, localSpace True, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.66/0.9 s
    - Size: 87.5/145/150/162.5/225/200
    - Color: color 1/1/1 alpha 0.5/1
    - ColorOverLife: color 1/1/1 alpha 1.75/1.26505/1.01207/0.94118/0.86275/0.78431/0.70588/0.62745/0.54902/0.47059/0.39216/0.31373/0.23529/0.15686/0.07843/0
    - SizeMultiplyLife: 0.4/0.75/0/0.56175/0.88776/0.29926/0.65497/0.93387/0.48216/0.78718/0.96367/0.63113/0.94353/0.98294/0.77143/1.09328/0.99392/0.88904/1.20569/0.99885/0.96991/1.25/1/1
    - Acceleration: 0/0/-250
  - emitter `BaseDust`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.35/0.75 s
    - Size: 30/150/150/65/200/200
    - SizeMultiplyLife: 0.25/0.25/0.25/0.51463/0.51463/0.51463/0.79885/0.79885/0.79885/0.98929/0.98929/0.98929/1.07473/1.07473/1.07473/1.15362/1.15362/1.15362/1.22822/1.22822/1.22822/1.29864/1.29864/1.29864/1.36497/1.36497/1.36497/1.42732/1.42732/1.42732/1.48579/1.48579/1.48579/1.54047/1.54047/1.54047/1.59148/1.59148/1.59148/1.63891/1.63891/1.63891/1.68288/1.68288/1.68288/1.72349/1.72349/1.72349/1.76084/1.76084/1.76084/1.79504/1.79504/1.79504/1.82618/1.82618/1.82618/1.85437/1.85437/1.85437/1.87972/1.87972/1.87972/1.90232/1.90232/1.90232/1.92226/1.92226/1.92226/1.93967/1.93967/1.93967/1.95465/1.95465/1.95465/1.9673/1.9673/1.9673/1.97771/1.97771/1.97771/1.98601/1.98601/1.98601/1.99228/1.99228/1.99228/1.99663/1.99663/1.99663/1.99917/1.99917/1.99917/2/2/2
    - ColorOverLife: color 0.79/0.79/0.79 alpha 1.5/1.19746/0.78611/0.44757/0.34222/0.31111/0.28/0.24889/0.21778/0.18667/0.15556/0.12444/0.09333/0.06222/0.03111/0
    - Acceleration: 0/-10/-400/0/-50/-200
  - emitter `Dust`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.5/2 s
    - Size: 65/0/0/200/0/0
    - SizeMultiplyLife: 0.2/0.2/0.2/0.6321/0.6321/0.6321/0.95723/0.95723/0.95723/1.19028/1.19028/1.19028/1.34617/1.34617/1.34617/1.43981/1.43981/1.43981/1.48612/1.48612/1.48612/1.5/1.5/1.5
    - Acceleration: 0/-10/-40/0/-50/-20
    - Color: color 0.75/0.75/0.75 alpha 0.65/1.25
  - emitter `Flecks`
    - Required: material `DebrisMaster`, subUV 2x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 15-75@0s
    - Lifetime: 0.25/2 s
    - Size: 0.5/1/0.5/3/5/3
    - Velocity: -150/-150/150/150/150/1100 radial 0
    - Acceleration: -
    - SizeMultiplyLife: 1/10/1/1.00187/9.92883/1.00168/1.00704/9.72612/1.00634/1.0149/9.40717/1.01342/1.02481/8.98732/1.02234/1.03616/8.48189/1.03256/1.04832/7.90619/1.04351/1.06066/7.27556/1.05462/1.07257/6.60532/1.06534/1.08341/5.91078/1.0751/1.09256/5.20728/1.08334/1.0994/4.51013/1.0895/1.10331/3.83466/1.09302/1.10366/3.19619/1.09333/1.09982/2.61005/1.08988/1.09118/2.09155/1.0821/1.0771/1.65602/1.06942/1.05697/1.31879/1.05129/1.03015/1.09517/1.02715/0.99823/0.99845/0.9981/0.97506/0.97506/0.97506/0.95238/0.95238/0.95238/0.92971/0.92971/0.92971/0.90703/0.90703/0.90703/0.88435/0.88435/0.88435/0.86168/0.86168/0.86168/0.839/0.839/0.839/0.81633/0.81633/0.81633/0.79365/0.79365/0.79365/0.77098/0.77098/0.77098/0.7483/0.7483/0.7483/0.72562/0.72562/0.72562/0.70295/0.70295/0.70295/0.68027/0.68027/0.68027/0.6576/0.6576/0.6576/0.63492/0.63492/0.63492/0.61224/0.61224/0.61224/0.58957/0.58957/0.58957/0.56689/0.56689/0.56689/0.54422/0.54422/0.54422/0.52154/0.52154/0.52154/0.49887/0.49887/0.49887/0.47619/0.47619/0.47619/0.45351/0.45351/0.45351/0.43084/0.43084/0.43084/0.40816/0.40816/0.40816/0.38549/0.38549/0.38549/0.36281/0.36281/0.36281/0.34014/0.34014/0.34014/0.31746/0.31746/0.31746/0.29478/0.29478/0.29478/0.27211/0.27211/0.27211/0.24943/0.24943/0.24943/0.22676/0.22676/0.22676/0.20408/0.20408/0.20408/0.18141/0.18141/0.18141/0.15873/0.15873/0.15873/0.13605/0.13605/0.13605/0.11338/0.11338/0.11338/0.0907/0.0907/0.0907/0.06803/0.06803/0.06803/0.04535/0.04535/0.04535/0.02268/0.02268/0.02268/0/0/0
    - Color: color 1/1/1 alpha 0.5/2
    - TypeData Gpu: 
  - emitter `SandKickBigRound`
    - Required: material `M_DustPuff`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.5/1 s
    - Size: 200/100/100/400/400/400
    - Color: color 0.52604/0.39687/0.3096 alpha 0.25/0.5
    - ColorOverLife: color 1/1/1 alpha 0.5/0.65691/0.6602/0.58675/0.51344/0.49944/0.49535/0.48748/0.47613/0.46165/0.44434/0.42452/0.40253/0.37868/0.35331/0.32672/0.29925/0.27122/0.24294/0.21474/0.18694/0.15987/0.13385/0.10921/0.08625/0.06531/0.04671/0.03076/0.01779/0.00813/0.00209/0
    - SizeMultiplyLife: 0.25/0.25/0/0.31354/0.66321/0.06705/0.40805/0.91764/0.18793/0.52252/1.04722/0.34413/0.6459/1.08588/0.51713/0.76717/1.06755/0.68842/0.87531/1.02618/0.83951/0.95928/0.99569/0.95188/1.01075/1.01075/1.01075/1.05376/1.05376/1.05376/1.09677/1.09677/1.09677/1.13978/1.13978/1.13978/1.1828/1.1828/1.1828/1.22581/1.22581/1.22581/1.26882/1.26882/1.26882/1.31183/1.31183/1.31183/1.35484/1.35484/1.35484/1.39785/1.39785/1.39785/1.44086/1.44086/1.44086/1.48387/1.48387/1.48387/1.52688/1.52688/1.52688/1.56989/1.56989/1.56989/1.6129/1.6129/1.6129/1.65591/1.65591/1.65591/1.69892/1.69892/1.69892/1.74194/1.74194/1.74194/1.78495/1.78495/1.78495/1.82796/1.82796/1.82796/1.87097/1.87097/1.87097/1.91398/1.91398/1.91398/1.95699/1.95699/1.95699/2/2/2
    - Acceleration: 0/0/-150/0/0/-300
    - Acceleration: -
    - Acceleration: -
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - Spawn: rate 0, bursts 1@0s
    - Spawn: rate 0, bursts 1@0s
    - Spawn: rate 0, bursts 5@0s
    - Spawn: rate 2, bursts -
  - material `MI_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_DustImpact` → E:\Tools\deagle-work\ron_fx\particles\T_dustImpact_16x4.png (512x128,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_DustFine` → E:\Tools\deagle-work\ron_fx\particles\T_GradientRound_A.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Noise_RVB.png (512x512,DXT1)
  - material `M_DustPuff` → E:\Tools\deagle-work\ron_fx\particles\T_FineParticle_DustAndDoplet.png (512x512,DXT5)
  - material `M_Smoke1` → E:\Tools\deagle-work\ron_fx\particles\T_Cloud_ABC.png (512x512,DXT1)
  - material `DebrisMaster` → E:\Tools\deagle-work\ron_fx\particles\Stones2x2.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\Gravel2x2_normal.png (512x512,BC5)

### RON_Drywall

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_BulletImpact_Plaster` | `M_BulletImpact_Drywall_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\t_airfield_concrete_a.png (1024²,BC7) |
| ImpactEffects_Pistol | `NS_BulletImpact_Plaster` | `M_BulletImpact_Drywall_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\t_airfield_concrete_a.png (1024²,BC7) |
| ShotgunImpactEffects | `NS_BulletImpact_Plaster` | `Decal_Impact_Inst_Drywall`  | E:\Tools\deagle-work\ron_fx\decals\Drywall2_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Drywall2_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Drywall_Exit` | `Decal_Impact_Inst_Drywall`  | E:\Tools\deagle-work\ron_fx\decals\Drywall2_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Drywall2_N.png (256²,BC5) |
| RicochetImpactEffects | `P_BulletImpact_Plaster` | `M_BulletImpact_Drywall_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\t_airfield_concrete_a.png (1024²,BC7) |

- **NS_BulletImpact_Plaster**: see above
- **NS_BulletImpact_Plaster**: see above
- **NS_BulletImpact_Plaster**: see above
- **P_VFX_Impact_Drywall_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Drywall_Exit`)
  - emitter `Smoke_Impact`
    - Required: material `MI_Impact_Smoke_05`, subUV 12x12, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.7/0.8 s
    - Size: 100/50/50/200/200/200
    - Velocity: -10/-10/20/10/10/500 radial 0
    - ColorOverLife: color 0.30208/0.30208/0.30208 alpha 1/0
    - SizeMultiplyLife: 0/0/0/0.67146/0.67146/0.67146/1.23806/1.23806/1.23806/1.70796/1.70796/1.70796/2.09032/2.09032/2.09032/2.39354/2.39354/2.39354/2.62655/2.62655/2.62655/2.79797/2.79797/2.79797/2.91656/2.91656/2.91656/2.99104/2.99104/2.99104/3.03011/3.03011/3.03011/3.04255/3.04255/3.04255/3.03701/3.03701/3.03701/3.02227/3.02227/3.02227/3.00704/3.00704/3.00704/3/3/3
    - Acceleration: -
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 20@0s
    - Lifetime: 0.1/0.2 s
    - Size: 5/1/1
    - Acceleration: -
    - SizeMultiplyLife: -
    - ColorOverLife: color 25/23.5784/16.125 alpha 1.5/0
    - Velocity: -50/-50/0/50/50/0 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 15@0s
    - Lifetime: 0.5/1 s
    - Size: 1/1/1.5
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Velocity: -50/-50/0/50/50/0 radial 0
    - Acceleration: -
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `Smoke_Impact`
    - Required: material `M_SmokeBall`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.2/0.3 s
    - Size: 85/85/85/100/100/100
    - Velocity: -10/-10/20/10/10/255 radial 0
    - ColorOverLife: color 0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.58854/0.26042/0.26042/0.26042 alpha 5/4.73545/4.4709/4.20635/3.9418/3.67725/3.4127/3.14815/2.8836/2.61905/2.3545/2.08995/1.8254/1.56085/1.2963/1.03175/0.7672/0.50265/0.2381/0.00936/0.00225/0.00589/0.01097/0.01739/0.02504/0.03381/0.04359/0.05428/0.06576/0.07792/0.09067/0.10388/0.11745/0.13128/0.14525/0.15925/0.17319/0.18694/0.2004/0.21346/0.22602/0.23796/0.24917/0.25956/0.269/0.27739/0.28463/0.2906/0.29519/0.29831/0.29983/0.29783/0.28656/0.26684/0.24045/0.2092/0.17488/0.1393/0.10425/0.07153/0.04295/0.0203/0.00539/0
    - SizeMultiplyLife: 0.2/0.2/0.2/0.52678/0.52678/0.52678/0.78501/0.78501/0.78501/0.98116/0.98116/0.98116/1.12237/1.12237/1.12237/1.21528/1.21528/1.21528/1.26687/1.26687/1.26687/1.28393/1.28393/1.28393/1.27333/1.27333/1.27333/1.24193/1.24193/1.24193/1.19655/1.19655/1.19655/1.14407/1.14407/1.14407/1.09132/1.09132/1.09132/1.04516/1.04516/1.04516/1.01246/1.01246/1.01246/1/1/1
    - Acceleration: -
    - Spawn: rate 0, bursts 25@0s
    - Spawn: rate 0, bursts 7@0s
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `MI_Impact_Smoke_05` → E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5), E:\Tools\deagle-work\ron_fx\particles\whisp_redux_2048_12x12.png (1024x1024,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_redux_2048_normal.png (256x256,BC5)
  - material `M_SmokeBall` → E:\Tools\deagle-work\ron_fx\particles\T_SmokeBall_01_8_8.png (1024x1024,DXT5)
- **P_BulletImpact_Plaster**: see above

### RON_Wallpaper

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_VFX_Impact_Wallpaper` | `M_BulletImpact_Drywall_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\t_airfield_concrete_a.png (1024²,BC7) |
| ImpactEffects_Pistol | `NS_VFX_Impact_Wallpaper` | `M_BulletImpact_Drywall_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\t_airfield_concrete_a.png (1024²,BC7) |
| ShotgunImpactEffects | `NS_VFX_Impact_Wallpaper` | `Decal_Impact_Inst_Wallpaper`  | E:\Tools\deagle-work\ron_fx\decals\Wallpaper_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Wallpaper_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Wallpaper_Exit` | `Decal_Impact_Inst_Wallpaper`  | E:\Tools\deagle-work\ron_fx\decals\Wallpaper_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Wallpaper_N.png (256²,BC5) |
| RicochetImpactEffects | `P_VFX_Impact_Wallpaper` | `M_BulletImpact_Drywall_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\t_airfield_concrete_a.png (1024²,BC7) |

- **NS_VFX_Impact_Wallpaper** (Niagara, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/Niagara/NS_VFX_Impact_Wallpaper`)
  - `BIG_SMOKE_1`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ThinSmoke_01` subUV [8.0, 4.0] align - | curves: Curve_6: 0.25 → max 1.5 → 1.5 (60 samples); Curve_7: 0.5 → max 0.5 → 0 (37 samples); Curve_6: 0.25 → max 1.5 → 1.5 (60 samples); Curve_7: 0.5 → max 0.5 → 0 (37 samples)
  - `DEBRIS_HIGH_SETTINGS_2`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `PaperBits` subUV [2.0, 2.0] align 0 | curves: Curve_3: 1 → max 1 → 0 (83 samples); Curve_3: 1 → max 1 → 0 (83 samples)
  - `DEBRIS_LOW_SETTINGS_5`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `PaperBits` subUV [2.0, 2.0] align 0 | curves: Curve_3: 1 → max 1 → 0 (83 samples); Curve_3: 1 → max 1 → 0 (83 samples)
  - `SMALL_SMOKE_0`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ThinSmoke_01` subUV [8.0, 4.0] align - | curves: Curve_6: 0.25 → max 1.5 → 1.5 (60 samples); Curve_7: 1.5 → max 1.5 → 0 (66 samples); Curve_6: 0.25 → max 1.5 → 1.5 (60 samples); Curve_7: 1.5 → max 1.5 → 0 (66 samples)
  - user params: User.Debris Color=[1.99676, 2.0, 1.79409, 1.0], User.Smoke Color=[0.79, 0.79, 0.79, 1.0]
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `PaperBits` → E:\Tools\deagle-work\ron_fx\particles\Stones2x2.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\Gravel2x2_normal.png (512x512,BC5), E:\Tools\deagle-work\ron_fx\particles\PaperShreds2x2.png (256x256,DXT5)
- **NS_VFX_Impact_Wallpaper**: see above
- **NS_VFX_Impact_Wallpaper**: see above
- **P_VFX_Impact_Wallpaper_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Wallpaper_Exit`)
  - emitter `Smoke_Impact`
    - Required: material `Whisps_trans2`, subUV 12x12, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.25/0.35 s
    - Size: 150/50/50/200/200/200
    - Velocity: -10/-10/20/10/10/500 radial 0
    - ColorOverLife: color 0.47917/0.47917/0.47917 alpha 0.1/0.32222/0.54444/0.76254/0.76092/0.71656/0.67392/0.63296/0.59366/0.55598/0.51989/0.48536/0.45234/0.42081/0.39074/0.36208/0.33482/0.3089/0.28431/0.261/0.23894/0.21811/0.19845/0.17995/0.16257/0.14628/0.13103/0.11681/0.10357/0.09128/0.0799/0.06941/0.05978/0.05095/0.04292/0.03563/0.02906/0.02317/0.01793/0.01331/0.00927/0.00577/0.0028/0.0003/-0.00174/-0.00337/-0.00462/-0.00553/-0.00611/-0.00642/-0.00648/-0.00633/-0.00599/-0.00551/-0.00492/-0.00424/-0.00352/-0.00279/-0.00208/-0.00142/-0.00085/-0.0004/-0.00011/0
    - SizeMultiplyLife: 0.5/0/0/0.5/0.5/0.5
    - Acceleration: -
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 25@0s
    - Lifetime: 0.1/1 s
    - Size: 1/1/1
    - Acceleration: -
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Velocity: -50/-50/0/50/50/0 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 50@0s
    - Lifetime: 0.03/0.05 s
    - Size: 5/5/1
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Acceleration: -
    - SizeMultiplyLife: -
    - Velocity: 0/0/50/0/0/25 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Spawn: rate 0, bursts 20@0s
    - Spawn: rate 0, bursts 25@0s
    - Spawn: rate 0, bursts 7@0s
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `Whisps_trans2` → E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5), E:\Tools\deagle-work\ron_fx\particles\whisp_redux_2048_12x12.png (1024x1024,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_redux_2048_normal.png (256x256,BC5)
- **P_VFX_Impact_Wallpaper** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Wallpaper`)
  - emitter `PaperBits`
    - Required: material `PaperBits`, subUV 2x2, localSpace 0, duration 0.1, loops 1
    - Spawn: rate 0, bursts 3-12@0s
    - Lifetime: 1/1.5 s
    - Size: 2/1/1/5/10/10
    - Velocity: -250/-250/200/250/250/500 radial 0
    - ColorOverLife: color 0.84/0.84/0.84 alpha 1/1/1/1/1/1/1/1/1/1/1/1/0.99055/0.66667/0.33333/0
    - Acceleration: -
    - TypeData Gpu: 
  - emitter `PaperBits`
    - Required: material `PaperBits`, subUV 2x2, localSpace 0, duration 0.1, loops 1
    - Spawn: rate 0, bursts 3-8@0s
    - Lifetime: 1/2 s
    - Size: 3/5/1/4/10/5
    - Velocity: -250/-250/150/250/250/1500 radial 0
    - ColorOverLife: color 1.99676/2/1.79409 alpha 1/1/1/1/1/1/1/1/1/1/1/1/0.99055/0.66667/0.33333/0
    - Acceleration: -
  - emitter `Dust`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.75/3 s
    - Size: 150/150/150/250/200/200
    - SizeMultiplyLife: 0.25/0.25/0.25/0.59415/0.59415/0.59415/0.87963/0.87963/0.87963/1.10852/1.10852/1.10852/1.28292/1.28292/1.28292/1.4049/1.4049/1.4049/1.47657/1.47657/1.47657/1.5/1.5/1.5
    - ColorOverLife: color 0.79/0.79/0.79 alpha 0.5/0
    - Acceleration: 0/-10/-100/0/-50/-50
  - emitter `BaseDust`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.15/0.4 s
    - Size: 25/150/150/75/200/200
    - SizeMultiplyLife: 0.75/0.75/0.75/1.06644/1.06644/1.06644/1.28049/1.28049/1.28049/1.41172/1.41172/1.41172/1.47971/1.47971/1.47971/1.50404/1.50404/1.50404/1.50427/1.50427/1.50427/1.5/1.5/1.5
    - ColorOverLife: color 0.79/0.79/0.79 alpha 1.5/1.02593/0.65959/0.38831/0.1994/0.08017/0.01793/0
    - Acceleration: 0/-10/-100/0/-50/-50
    - Spawn: rate 0, bursts 0@0s
    - Spawn: rate 0, bursts 25@0s
    - Spawn: rate 0, bursts 5@0s
    - Spawn: rate 0, bursts 5@0s
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `PaperBits` → E:\Tools\deagle-work\ron_fx\particles\Stones2x2.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\Gravel2x2_normal.png (512x512,BC5), E:\Tools\deagle-work\ron_fx\particles\PaperShreds2x2.png (256x256,DXT5)

## concrete

### RON_ConcreteSoft

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_Bullet_Impact_Concrete_Big` | `M_BulletImpact_Concrete_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet10.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\t_airfield_concrete_a.png (1024²,BC7) |
| ImpactEffects_Pistol | `NS_Bullet_Impact_Concrete_Big` | `M_BulletImpact_Concrete_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet10.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\t_airfield_concrete_a.png (1024²,BC7) |
| ShotgunImpactEffects | `NS_Bullet_Impact_Concrete_Big` | `Decal_Impact_Inst_Concrete_Soft`  | E:\Tools\deagle-work\ron_fx\decals\Concrete_Soft_BO.png (64²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Concrete_Soft_N.png (64²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Concrete_Soft_Exit` | `Decal_Impact_Inst_Concrete_Soft`  | E:\Tools\deagle-work\ron_fx\decals\Concrete_Soft_BO.png (64²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Concrete_Soft_N.png (64²,BC5) |
| RicochetImpactEffects | `P_Bullet_Impact_Concrete_Big` | `M_BulletImpact_Concrete_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet10.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\t_airfield_concrete_a.png (1024²,BC7) |

- **NS_Bullet_Impact_Concrete_Big** (Niagara, `/Game/ReadyOrNot/VFX/VFXImpacts/Niagara/NS_Bullet_Impact_Concrete_Big`)
  - `Base_Dust_20`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `MI_ParticleGeneric_ThinSmoke` subUV [8.0, 4.0] align 0 | curves: Curve_34: 0.75 → max 2 → 2 (60 samples); Curve_35: 1.25 → max 1.25 → 0 (60 samples); Curve_34: 0.75 → max 2 → 2 (60 samples); Curve_35: 1.25 → max 1.25 → 0 (60 samples)
  - `DEBRIS_HIGH_SETTINGS_4`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `DebrisMaster` subUV [2.0, 2.0] align - | curves: Vector2DCurve_17: 1 → max 1 → 0 (106 samples); Vector2DCurve_17: 1 → max 1 → 0 (106 samples)
  - `DEBRIS_LOW_SETTINGS_3`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `DebrisMaster` subUV [2.0, 2.0] align - | curves: Vector2DCurve_17: 1 → max 1 → 0 (106 samples); Vector2DCurve_17: 1 → max 1 → 0 (106 samples)
  - `Dust_19`: GPUComputeSim, localSpace 0, bounds -20..20 / -20..20 / -20..20 | Sprite mat `MI_ParticleGeneric_ThinSmoke` subUV [8.0, 4.0] align 0 | curves: Curve_34: 0.25 → max 3.5 → 3.5 (89 samples); Curve_35: 1 → max 1 → 0 (53 samples); Curve_34: 0.25 → max 3.5 → 3.5 (89 samples); Curve_35: 1 → max 1 → 0 (53 samples)
  - `SHOT_SPATTER_0`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `MI_ParticleGeneric_DebrisSpray` subUV [4.0, 1.0] align VelocityAligned | curves: Curve_17: 1 → max 1 → 0.05 (56 samples); Curve_17: 1 → max 1 → 0.05 (56 samples); Vector2DCurve_17: 0.35 → max 3.5 → 3.5 (182 samples); Vector2DCurve_17: 0.35 → max 3.5 → 3.5 (182 samples)
  - user params: User.Debris Color=[0.5, 0.5, 0.5, 1.0], User.Dust Color A=[0.55, 0.55, 0.55, 1.0], User.Dust Color B=[0.79, 0.79, 0.79, 1.0], User.Sparks color=[1000.0, 250.0, 50.0, 1.0]
  - material `MI_ParticleGeneric_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5)
  - material `MI_ParticleGeneric_ThinSmoke` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `DebrisMaster` → E:\Tools\deagle-work\ron_fx\particles\Stones2x2.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\Gravel2x2_normal.png (512x512,BC5)
- **NS_Bullet_Impact_Concrete_Big**: see above
- **NS_Bullet_Impact_Concrete_Big**: see above
- **P_VFX_Impact_Concrete_Soft_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Concrete_Soft_Exit`)
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 5@0s
    - Lifetime: 0.1 s
    - Size: 0.25/0.25/1/0.4/0.4/1
    - Acceleration: -
    - SizeMultiplyLife: -
    - ColorOverLife: color 25/23.5784/16.125 alpha 1.5/0
    - Velocity: -50/-50/100/50/50/118.099 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `Dust Shoot Out`
    - Required: material `whispOne_snowed`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1-2@0s
    - Lifetime: 0.2/0.5 s
    - Size: 100/100/100/150/150/150
    - Velocity: -10/-10/700/2/2/200 radial 0
    - ColorOverLife: color 0.79/0.79/0.79 alpha 0.5/0.7381/0.97619/0.81633/0.61224/0.40816/0.20408/0
    - Acceleration: 0/-10/20/0/-50/96
  - emitter `Fireball`
    - Required: material `MI_Impact_Smoke_01`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.05/0.075 s
    - Size: 85/85/85/100/100/100
    - Velocity: -10/-10/100/10/10/350 radial 0
    - ColorOverLife: color 25555/0.0458/0/0.26042/0.26042/0.26042 alpha 0.2/0.49867/0.39286/0.3
    - SizeMultiplyLife: 0.25/0.25/0.25/3/3/3
    - Acceleration: -
    - Spawn: rate 0, bursts 5@0s
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `whispOne_snowed` → E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
  - material `MI_Impact_Smoke_01` → E:\Tools\deagle-work\ron_fx\particles\Fire2c_2k.png (2048x2048,DXT5), E:\Tools\deagle-work\ron_fx\particles\RandomNormal2.png (32x32,B8G8R8A8), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
- **P_Bullet_Impact_Concrete_Big** (Cascade, `/Game/ReadyOrNot/VFX/VFXImpacts/EditV2/P_Bullet_Impact_Concrete_Big`)
  - emitter `SparkSpray`
    - Required: material `MI_SparkSpray`, subUV 4x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0.01s
    - Lifetime: 0.04/0.07 s
    - Size: 15/10/100/55/66/150
    - ColorOverLife: color 1400/250/50/1/0.1/0 alpha 0.75/0
    - SizeMultiplyLife: 0.25/0.25/0.25/1.5/1.5/1.5
  - emitter `BaseDust`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0.04s
    - Lifetime: 0.35/1 s
    - Size: 33/150/150/60/200/200
    - SizeMultiplyLife: 0.25/0.25/0.25/0.75018/0.75018/0.75018/1.1992/1.1992/1.1992/1.59928/1.59928/1.59928/1.95314/1.95314/1.95314/2.26308/2.26308/2.26308/2.53174/2.53174/2.53174/2.76151/2.76151/2.76151/2.95493/2.95493/2.95493/3.11449/3.11449/3.11449/3.24264/3.24264/3.24264/3.34194/3.34194/3.34194/3.4148/3.4148/3.4148/3.4638/3.4638/3.4638/3.49134/3.49134/3.49134/3.5/3.5/3.5
    - ColorOverLife: color 0.79/0.79/0.79 alpha 1/0.37519/0.07649/0
    - Acceleration: -100/-100/-10/100/100/0
  - emitter `ConcreteShardsHighRes`
    - Required: material `DebrisMaster`, subUV 2x2, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 5-25@0s
    - Lifetime: 2/4 s
    - Size: 1.5/2/0.03/7/7/0.25
    - Velocity: -250/-250/75/250/250/1500 radial 0
    - Color: color 0.5/0.5/0.5 alpha 1
    - SizeMultiplyLife: 1/1/1/0.95223/0.95174/0.9508/0.82719/0.82557/0.82244/0.65222/0.64931/0.64367/0.45467/0.4508/0.44328/0.26189/0.25785/0.25002/0.10122/0.09831/0.09267/0/0/0
    - Acceleration: -
    - TypeData Gpu: 
  - emitter `Dust`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.75/3 s
    - Size: 165/150/150/265/200/200
    - SizeMultiplyLife: 0.25/0.25/0.25/0.51482/0.51482/0.51482/0.69585/0.69585/0.69585/0.8172/0.8172/0.8172/0.90369/0.90369/0.90369/0.97976/0.97976/0.97976/1.06167/1.06167/1.06167/1.14072/1.14072/1.14072/1.21501/1.21501/1.21501/1.28333/1.28333/1.28333/1.34447/1.34447/1.34447/1.39723/1.39723/1.39723/1.44036/1.44036/1.44036/1.47268/1.47268/1.47268/1.49295/1.49295/1.49295/1.5/1.5/1.5
    - Acceleration: -100/-100/-10/100/100/0
    - Color: color 0.79/0.79/0.79 alpha 0.35/1
  - emitter `ShootSpatter`
    - Required: material `MI_DebrisSpray`, subUV 4x1, localSpace True, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.2/0.33 s
    - Size: 36/82.5/15/66/125/12
    - SizeMultiplyLife: 0.35/0.3/0/0.77297/0.75158/0.16526/1.19828/1.23991/0.35085/1.60136/1.74405/0.54007/1.95764/2.24307/0.71627/2.24255/2.71602/0.86277/2.43153/3.14198/0.96291/2.5/3.5/1
    - Velocity: 0/0/100 radial 0
    - Acceleration: 0/0/-700
    - Color: color 0.6/0.6/0.6 alpha 0.5/1
  - emitter `Flecks`
    - Required: material `DebrisMaster`, subUV 2x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 15-75@0s
    - Lifetime: 0.25/2 s
    - Size: 0.5/0.5/0.5/4/4/3
    - Velocity: -250/-250/100/250/250/1250 radial 0
    - Acceleration: -
    - SizeMultiplyLife: 1/10/1/1.00187/9.92883/1.00168/1.00704/9.72612/1.00634/1.0149/9.40717/1.01342/1.02481/8.98732/1.02234/1.03616/8.48189/1.03256/1.04832/7.90619/1.04351/1.06066/7.27556/1.05462/1.07257/6.60532/1.06534/1.08341/5.91078/1.0751/1.09256/5.20728/1.08334/1.0994/4.51013/1.0895/1.10331/3.83466/1.09302/1.10366/3.19619/1.09333/1.09982/2.61005/1.08988/1.09118/2.09155/1.0821/1.0771/1.65602/1.06942/1.05697/1.31879/1.05129/1.03015/1.09517/1.02715/0.99823/0.99845/0.9981/0.97506/0.97506/0.97506/0.95238/0.95238/0.95238/0.92971/0.92971/0.92971/0.90703/0.90703/0.90703/0.88435/0.88435/0.88435/0.86168/0.86168/0.86168/0.839/0.839/0.839/0.81633/0.81633/0.81633/0.79365/0.79365/0.79365/0.77098/0.77098/0.77098/0.7483/0.7483/0.7483/0.72562/0.72562/0.72562/0.70295/0.70295/0.70295/0.68027/0.68027/0.68027/0.6576/0.6576/0.6576/0.63492/0.63492/0.63492/0.61224/0.61224/0.61224/0.58957/0.58957/0.58957/0.56689/0.56689/0.56689/0.54422/0.54422/0.54422/0.52154/0.52154/0.52154/0.49887/0.49887/0.49887/0.47619/0.47619/0.47619/0.45351/0.45351/0.45351/0.43084/0.43084/0.43084/0.40816/0.40816/0.40816/0.38549/0.38549/0.38549/0.36281/0.36281/0.36281/0.34014/0.34014/0.34014/0.31746/0.31746/0.31746/0.29478/0.29478/0.29478/0.27211/0.27211/0.27211/0.24943/0.24943/0.24943/0.22676/0.22676/0.22676/0.20408/0.20408/0.20408/0.18141/0.18141/0.18141/0.15873/0.15873/0.15873/0.13605/0.13605/0.13605/0.11338/0.11338/0.11338/0.0907/0.0907/0.0907/0.06803/0.06803/0.06803/0.04535/0.04535/0.04535/0.02268/0.02268/0.02268/0/0/0
    - Color: color 0.4/0.4/0.4 alpha 1/2
    - TypeData Gpu: 
  - emitter `BaseDust`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace True, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0.03s
    - Lifetime: 0.5 s
    - Size: 30/150/150/50/200/200
    - SizeMultiplyLife: 0.75/0.75/0.75/1.29688/1.29688/1.29688/1.6612/1.6612/1.6612/1.87884/1.87884/1.87884/1.98566/1.98566/1.98566/2.01755/2.01755/2.01755/2.01037/2.01037/2.01037/2/2/2
    - ColorOverLife: color 0.5/0.5/0.5 alpha 1.25/0.56038/0.14131/0
  - emitter `SparkSpray`
    - Required: material `M_Particle_Additive`, subUV 4x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 3@0s
    - Lifetime: 0.03/0.06 s
    - Size: 5/5/5/35/35/35
    - ColorOverLife: color 1400/250/50/5/0.5/0 alpha 1/0
    - SizeMultiplyLife: 0.1/0.2/1/0.5/1.5/1
  - emitter `SmokeKick`
    - Required: material `MI_DustImpact`, subUV 16x2, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.25/1 s
    - Size: 75/165/150/125/220/200
    - Color: color 1/1/1 alpha 0.5/1
    - ColorOverLife: color 1/1/1 alpha 2.5/2.30423/2.0183/1.69405/1.38334/1.13802/1.00949/0.98765/0.97002/0.95238/0.93474/0.91711/0.89947/0.88183/0.8642/0.84656/0.82892/0.81129/0.79365/0.77601/0.75838/0.74074/0.7231/0.70547/0.68783/0.67019/0.65256/0.63492/0.61728/0.59965/0.58201/0.56437/0.54674/0.5291/0.51146/0.49383/0.47619/0.45855/0.44092/0.42328/0.40564/0.38801/0.37037/0.35273/0.3351/0.31746/0.29982/0.28219/0.26455/0.24691/0.22928/0.21164/0.194/0.17637/0.15873/0.14109/0.12346/0.10582/0.08818/0.07055/0.05291/0.03527/0.01764/0
    - SizeMultiplyLife: 0.25/0.5/0/0.38699/0.64867/0.11854/0.55178/0.81801/0.27805/0.6499/0.90988/0.39438/0.6849/0.92898/0.4624/0.72222/0.94512/0.53191/0.76094/0.95863/0.60158/0.8/0.96975/0.66997/0.83837/0.97871/0.73566/0.875/0.98573/0.79722/0.90885/0.99104/0.85323/0.93888/0.99488/0.90226/0.96405/0.99746/0.94287/0.98333/0.99902/0.97366/0.99565/0.99979/0.99316/1/1/1
    - Acceleration: 0/0/-3/0/0/-1
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
  - material `MI_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_DustImpact` → E:\Tools\deagle-work\ron_fx\particles\T_dustImpact_16x4.png (512x128,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_SparkSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_longSpark_02_8x2.png (128x256,DXT1)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_Particle_Additive` → E:\Tools\deagle-work\ron_fx\particles\T_longSpark_02_8x2.png (128x256,DXT1)
  - material `DebrisMaster` → E:\Tools\deagle-work\ron_fx\particles\Stones2x2.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\Gravel2x2_normal.png (512x512,BC5)

### RON_ConcreteStrong

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_Bullet_Impact_Concrete_Big` | `M_BulletImpact_Concrete_02`  | E:\Tools\deagle-work\ron_fx\decals\Bullet1.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Decal_Broken_FloorConcrete_B.png (512²,DXT1) |
| ImpactEffects_Pistol | `NS_Bullet_Impact_Concrete_Big` | `M_BulletImpact_Concrete_02`  | E:\Tools\deagle-work\ron_fx\decals\Bullet1.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Decal_Broken_FloorConcrete_B.png (512²,DXT1) |
| ShotgunImpactEffects | `NS_Bullet_Impact_Concrete_Big` | `Decal_Impact_Inst_Concrete_Hard`  | E:\Tools\deagle-work\ron_fx\decals\Concrete_Hard_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Concrete_Hard_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Concrete_Soft_Exit` | `Decal_Impact_Inst_Concrete_Hard`  | E:\Tools\deagle-work\ron_fx\decals\Concrete_Hard_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Concrete_Hard_N.png (256²,BC5) |
| RicochetImpactEffects | `P_Bullet_Impact_Concrete_Big` | `M_BulletImpact_Concrete_02`  | E:\Tools\deagle-work\ron_fx\decals\Bullet1.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Decal_Broken_FloorConcrete_B.png (512²,DXT1) |

- **NS_Bullet_Impact_Concrete_Big**: see above
- **NS_Bullet_Impact_Concrete_Big**: see above
- **NS_Bullet_Impact_Concrete_Big**: see above
- **P_VFX_Impact_Concrete_Soft_Exit**: see above
- **P_Bullet_Impact_Concrete_Big**: see above

## brick

### RON_Brick

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_BulletImpact_Brick` | `M_BulletImpact_Brick_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet3.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Decal_Broken_FloorConcrete_B.png (512²,DXT1) |
| ImpactEffects_Pistol | `NS_BulletImpact_Brick` | `M_BulletImpact_Brick_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet3.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Decal_Broken_FloorConcrete_B.png (512²,DXT1) |
| ShotgunImpactEffects | `NS_BulletImpact_Brick` | `Decal_Impact_Inst_Brick`  | E:\Tools\deagle-work\ron_fx\decals\Brick_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Brick_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Brick_Exit` | `Decal_Impact_Inst_Brick`  | E:\Tools\deagle-work\ron_fx\decals\Brick_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Brick_N.png (256²,BC5) |
| RicochetImpactEffects | `P_BulletImpact_Brick` | `M_BulletImpact_Brick_01`  | E:\Tools\deagle-work\ron_fx\decals\Bullet3.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Decal_Broken_FloorConcrete_B.png (512²,DXT1) |

- **NS_BulletImpact_Brick** (Niagara, `/Game/ReadyOrNot/VFX/VFXImpacts/Niagara/NS_BulletImpact_Brick`)
  - `DEBRIS_HIGH_SETTINGS_0`: GPUComputeSim, localSpace 0, bounds -25..25 / -25..25 / -25..25 | Sprite mat `DebrisMaster` subUV [2.0, 2.0] align 0 | curves: Curve_51: 1 → max 1 → 0 (80 samples); Curve_52: 0.25 → max 0.25 → 0 (20 samples); Curve_53: -0.25 → max 0 → 0 (20 samples); Curve_51: 1 → max 1 → 0 (80 samples)
  - `DEBRIS_LOW_SETTINGS_3`: GPUComputeSim, localSpace 0, bounds -25..25 / -25..25 / -25..25 | Sprite mat `DebrisMaster` subUV [2.0, 2.0] align 0 | curves: Curve_51: 1 → max 1 → 0 (80 samples); Curve_52: 0.25 → max 0.25 → 0 (20 samples); Curve_53: -0.25 → max 0 → 0 (20 samples); Curve_51: 1 → max 1 → 0 (80 samples)
  - `Dust_1`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ParticleGeneric_ThinSmoke` subUV [8.0, 4.0] align - | curves: Curve_34: 1 → max 1 → 0 (53 samples); Curve_35: 1 → max 1 → 0 (57 samples); Curve_34: 1 → max 1 → 0 (53 samples); Curve_35: 1 → max 1 → 0 (57 samples)
  - `SHOT_SPATTER_0`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ParticleGeneric_DebrisSpray` subUV [4.0, 1.0] align VelocityAligned | curves: Curve_17: 1 → max 1 → 0 (53 samples); Curve_17: 1 → max 1 → 0 (53 samples); Vector2DCurve_17: 0.35 → max 3.5 → 3.5 (182 samples); Vector2DCurve_17: 0.35 → max 3.5 → 3.5 (182 samples)
  - `Smoke_Kick_2`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ParticleGeneric_DustImpact` subUV [16.0, 2.0] align VelocityAligned | curves: Curve_17: 1.5 → max 1.5 → 0 (66 samples); Curve_17: 1.5 → max 1.5 → 0 (66 samples); Vector2DCurve_17: 0.5 → max 1.25 → 1.25 (82 samples); Vector2DCurve_17: 0.5 → max 1.25 → 1.25 (82 samples)
  - user params: User.Debris Color=[0.4, 0.4, 0.4, 1.0], User.Dust Color=[1.0, 0.75, 0.58, 1.0], User.Spatter Color=[0.4, 0.3, 0.23, 1.0]
  - material `MI_ParticleGeneric_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5)
  - material `MI_ParticleGeneric_DustImpact` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_dustImpact_16x4.png (512x128,DXT5)
  - material `MI_ParticleGeneric_ThinSmoke` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `DebrisMaster` → E:\Tools\deagle-work\ron_fx\particles\Stones2x2.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\Gravel2x2_normal.png (512x512,BC5)
- **NS_BulletImpact_Brick**: see above
- **NS_BulletImpact_Brick**: see above
- **P_VFX_Impact_Brick_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Brick_Exit`)
  - emitter `Dust Secondary`
    - Required: material `Whisps_trans3`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 2@0s
    - Lifetime: 0.1/0.4 s
    - Size: 100/100/100/200/150/150
    - Velocity: -250/-250/250/250/250/500 radial 0
    - ColorOverLife: color 0.23438/0.17024/0.11231/0.31771/0.25894/0.20519 alpha 1/1.23622/1.47244/1.70866/1.94488/2.1811/2.41732/2.65354/2.88976/3.12598/3.3622/3.59843/3.83465/3.9961/3.98266/3.96855/3.95379/3.93838/3.92236/3.90572/3.88849/3.87069/3.85233/3.83343/3.814/3.79406/3.77362/3.7527/3.73133/3.7095/3.68725/3.66458/3.64151/3.61806/3.59425/3.57008/3.54558/3.52076/3.49564/3.47024/3.44456/3.41863/3.39247/3.36608/3.33948/3.3127/3.28574/3.25863/3.23138/3.204/3.17651/3.14893/3.12127/3.09355/3.06579/3.03799/3.01019/2.98239/2.95461/2.92686/2.89917/2.87154/2.844/2.81656/2.78923/2.76204/2.73499/2.70811/2.68141/2.6549/2.62861/2.60255/2.57673/2.55117/2.52589/2.5009/2.47622/2.45187/2.42786/2.4042/2.38092/2.35803/2.33554/2.31347/2.29184/2.27067/2.24996/2.22974/2.21003/2.19083/2.17216/2.15405/2.1365/2.11953/2.10316/2.08741/2.07229/2.05781/2.044/2.03086/2.01842/2.00669/1.99429/1.96878/1.9283/1.87421/1.80787/1.73065/1.64392/1.54903/1.44734/1.34022/1.22904/1.11515/0.99992/0.88472/0.77089/0.65982/0.55285/0.45136/0.3567/0.27024/0.19335/0.12738/0.07369/0.03366/0.00864/0
    - SizeMultiplyLife: 0.5/0.5/0.5/0.77737/0.77737/0.77737/0.94589/0.94589/0.94589/1.02969/1.02969/1.02969/1.05289/1.05289/1.05289/1.03959/1.03959/1.03959/1.01392/1.01392/1.01392/1/1/1
    - Acceleration: 0/0/20/0/0/155
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 6@0s
    - Lifetime: 0.1/0.5 s
    - Size: 1/1/1.5
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Velocity: -50/-50/0/50/50/0 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `Dust`
    - Required: material `Whisps_trans1`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.25/0.3 s
    - Size: 100/100/100/200/150/150
    - Velocity: -50/-50/850/50/50/500 radial 0
    - ColorOverLife: color 0.38431/0.38431/0.38431/0.57647/0.50616/0.44677 alpha 2/3.81102/5.62205/7.43307/9.24409/11.0551/12.8661/14.6772/16.4882/18.2992/20.1102/21.9213/23.7323/24.9954/24.9693/24.9271/24.8691/24.7957/24.7073/24.6043/24.487/24.3557/24.211/24.0531/23.8824/23.6993/23.5042/23.2974/23.0794/22.8504/22.611/22.3614/22.102/21.8332/21.5554/21.2689/20.9741/20.6715/20.3613/20.0439/19.7198/19.3893/19.0527/18.7105/18.363/18.0106/17.6537/17.2926/16.9277/16.5594/16.1881/15.8141/15.4379/15.0597/14.6801/14.2992/13.9176/13.5355/13.1535/12.7717/12.3907/12.0108/11.6323/11.2557/10.8813/10.5095/10.1406/9.77508/9.41327/9.05556/8.7023/8.35388/8.01068/7.67306/7.3414/7.01608/6.69748/6.38595/6.08189/5.78566/5.49765/5.21822/4.94774/4.68661/4.43518/4.19383/3.96295/3.7429/3.53406/3.3368/3.15149/2.97852/2.81826/2.67108/2.53735/2.41746/2.31177/2.22067/2.14452/2.0837/2.03858/2.00954/1.99429/1.96878/1.9283/1.87421/1.80787/1.73065/1.64392/1.54903/1.44734/1.34022/1.22904/1.11515/0.99992/0.88472/0.77089/0.65982/0.55285/0.45136/0.3567/0.27024/0.19335/0.12738/0.07369/0.03366/0.00864/0
    - SizeMultiplyLife: 0.2/0.2/0.2/0.49122/0.49122/0.49122/0.70773/0.70773/0.70773/0.85725/0.85725/0.85725/0.94821/0.94821/0.94821/0.98853/0.98853/0.98853/0.98648/0.98648/0.98648/0.9501/0.9501/0.9501/0.88757/0.88757/0.88757/0.807/0.807/0.807/0.71652/0.71652/0.71652/0.62426/0.62426/0.62426/0.53836/0.53836/0.53836/0.46691/0.46691/0.46691/0.41813/0.41813/0.41813/0.4/0.4/0.4
    - Acceleration: 0/-50/20/0/0/-150
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 25@0s
    - Lifetime: 0.3/0.5 s
    - Size: 0.5/0.5/0.5/1.5/1.5/1
    - Acceleration: -
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Velocity: -50/-50/0/50/50/0 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 5@0s
    - Lifetime: 0.03/0.2 s
    - Size: 1/2/1
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Acceleration: -
    - SizeMultiplyLife: -
    - Velocity: 0/0/50/0/0/25 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Spawn: rate 0, bursts 25@0s
    - Spawn: rate 0, bursts 20@0s
    - Spawn: rate 0, bursts 25@0s
    - Spawn: rate 0, bursts 5@0s
    - Spawn: rate 0, bursts 5@0s
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `Whisps_trans1` → E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5)
  - material `Whisps_trans3` → E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
- **P_BulletImpact_Brick** (Cascade, `/Game/ReadyOrNot/VFX/VFXImpacts/P_BulletImpact_Brick`)
  - emitter `Dust`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.5/2 s
    - Size: 75/150/150/200/200/200
    - SizeMultiplyLife: 0.25/0.25/0.25/0.53466/0.53466/0.53466/0.75822/0.75822/0.75822/0.91972/0.91972/0.91972/1.02064/1.02064/1.02064/1.09944/1.09944/1.09944/1.17199/1.17199/1.17199/1.23802/1.23802/1.23802/1.29727/1.29727/1.29727/1.34947/1.34947/1.34947/1.39436/1.39436/1.39436/1.43169/1.43169/1.43169/1.46117/1.46117/1.46117/1.48257/1.48257/1.48257/1.49558/1.49558/1.49558/1.5/1.5/1.5
    - Acceleration: -20/-20/-60/20/20/-30
    - Color: color 0.66667/0.5/0.38667 alpha 0.75/1
  - emitter `Flecks`
    - Required: material `DebrisMaster`, subUV 2x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 5-50@0s
    - Lifetime: 0.25/2 s
    - Size: 0.75/0.75/0.5/3/3/3
    - Velocity: -250/-250/150/250/250/1200 radial 0
    - Acceleration: -
    - SizeMultiplyLife: 1/10/1/1.00187/9.92883/1.00168/1.00704/9.72612/1.00634/1.0149/9.40717/1.01342/1.02481/8.98732/1.02234/1.03616/8.48189/1.03256/1.04832/7.90619/1.04351/1.06066/7.27556/1.05462/1.07257/6.60532/1.06534/1.08341/5.91078/1.0751/1.09256/5.20728/1.08334/1.0994/4.51013/1.0895/1.10331/3.83466/1.09302/1.10366/3.19619/1.09333/1.09982/2.61005/1.08988/1.09118/2.09155/1.0821/1.0771/1.65602/1.06942/1.05697/1.31879/1.05129/1.03015/1.09517/1.02715/0.99823/0.99845/0.9981/0.97506/0.97506/0.97506/0.95238/0.95238/0.95238/0.92971/0.92971/0.92971/0.90703/0.90703/0.90703/0.88435/0.88435/0.88435/0.86168/0.86168/0.86168/0.839/0.839/0.839/0.81633/0.81633/0.81633/0.79365/0.79365/0.79365/0.77098/0.77098/0.77098/0.7483/0.7483/0.7483/0.72562/0.72562/0.72562/0.70295/0.70295/0.70295/0.68027/0.68027/0.68027/0.6576/0.6576/0.6576/0.63492/0.63492/0.63492/0.61224/0.61224/0.61224/0.58957/0.58957/0.58957/0.56689/0.56689/0.56689/0.54422/0.54422/0.54422/0.52154/0.52154/0.52154/0.49887/0.49887/0.49887/0.47619/0.47619/0.47619/0.45351/0.45351/0.45351/0.43084/0.43084/0.43084/0.40816/0.40816/0.40816/0.38549/0.38549/0.38549/0.36281/0.36281/0.36281/0.34014/0.34014/0.34014/0.31746/0.31746/0.31746/0.29478/0.29478/0.29478/0.27211/0.27211/0.27211/0.24943/0.24943/0.24943/0.22676/0.22676/0.22676/0.20408/0.20408/0.20408/0.18141/0.18141/0.18141/0.15873/0.15873/0.15873/0.13605/0.13605/0.13605/0.11338/0.11338/0.11338/0.0907/0.0907/0.0907/0.06803/0.06803/0.06803/0.04535/0.04535/0.04535/0.02268/0.02268/0.02268/0/0/0
    - Color: color 0.4/0.4/0.4 alpha 2
    - TypeData Gpu: 
  - emitter `BaseDust`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.25/1 s
    - Size: 50/150/150/75/200/200
    - SizeMultiplyLife: 0.5/0.5/0.5/0.8303/0.8303/0.8303/1.08006/1.08006/1.08006/1.26012/1.26012/1.26012/1.38131/1.38131/1.38131/1.45447/1.45447/1.45447/1.49042/1.49042/1.49042/1.5/1.5/1.5
    - Acceleration: 0/-10/-250/0/-50/-150
    - Color: color 1/0.75/0.58 alpha 1
  - emitter `Specks`
    - Required: material `M_ParticleEmberSimple`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 5-50@0s
    - Lifetime: 0.01/1.5 s
    - Size: 0.25/2/0.5/3/4/3
    - Velocity: -400/-400/250/400/400/1500 radial 0
    - Acceleration: -
    - SizeMultiplyLife: 1/10/1/1.00187/9.92883/1.00168/1.00704/9.72612/1.00634/1.0149/9.40717/1.01342/1.02481/8.98732/1.02234/1.03616/8.48189/1.03256/1.04832/7.90619/1.04351/1.06066/7.27556/1.05462/1.07257/6.60532/1.06534/1.08341/5.91078/1.0751/1.09256/5.20728/1.08334/1.0994/4.51013/1.0895/1.10331/3.83466/1.09302/1.10366/3.19619/1.09333/1.09982/2.61005/1.08988/1.09118/2.09155/1.0821/1.0771/1.65602/1.06942/1.05697/1.31879/1.05129/1.03015/1.09517/1.02715/0.99823/0.99845/0.9981/0.97506/0.97506/0.97506/0.95238/0.95238/0.95238/0.92971/0.92971/0.92971/0.90703/0.90703/0.90703/0.88435/0.88435/0.88435/0.86168/0.86168/0.86168/0.839/0.839/0.839/0.81633/0.81633/0.81633/0.79365/0.79365/0.79365/0.77098/0.77098/0.77098/0.7483/0.7483/0.7483/0.72562/0.72562/0.72562/0.70295/0.70295/0.70295/0.68027/0.68027/0.68027/0.6576/0.6576/0.6576/0.63492/0.63492/0.63492/0.61224/0.61224/0.61224/0.58957/0.58957/0.58957/0.56689/0.56689/0.56689/0.54422/0.54422/0.54422/0.52154/0.52154/0.52154/0.49887/0.49887/0.49887/0.47619/0.47619/0.47619/0.45351/0.45351/0.45351/0.43084/0.43084/0.43084/0.40816/0.40816/0.40816/0.38549/0.38549/0.38549/0.36281/0.36281/0.36281/0.34014/0.34014/0.34014/0.31746/0.31746/0.31746/0.29478/0.29478/0.29478/0.27211/0.27211/0.27211/0.24943/0.24943/0.24943/0.22676/0.22676/0.22676/0.20408/0.20408/0.20408/0.18141/0.18141/0.18141/0.15873/0.15873/0.15873/0.13605/0.13605/0.13605/0.11338/0.11338/0.11338/0.0907/0.0907/0.0907/0.06803/0.06803/0.06803/0.04535/0.04535/0.04535/0.02268/0.02268/0.02268/0/0/0
    - Color: color 1/1/1 alpha 0.1/1
    - TypeData Gpu: CameraMotionBlurAmount=1.0
  - emitter `None`
    - Required: material `M_Gravel`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 5-10@0s
    - Lifetime: 1/2 s
    - Size: 0.05/0.05/0.05/0.35/0.35/0.35
    - Velocity: -250/-250/500/250/250/100 radial 0
    - Acceleration: 0/0/-2000/0/0/-1000
    - SizeMultiplyLife: 1/1/1/0/0/0
    - Color: color 0.51042/0.51042/0.51042/0.19271/0.19271/0.19271 alpha 1
    - TypeData Mesh: Mesh=SM_RockDirt, bOverrideMaterial=True
  - emitter `None`
    - Required: material `M_Gravel`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 6-12@0s
    - Lifetime: 0.25/2 s
    - Size: 0.05/0.05/0.05/0.35/0.35/0.35
    - Velocity: -200/-200/300/200/200/1200 radial 0
    - Acceleration: 0/0/-2000/0/0/-1000
    - SizeMultiplyLife: 1/1/1/0/0/0
    - Color: color 0.20833/0.20833/0.20833/0.51042/0.51042/0.51042 alpha 1
    - TypeData Mesh: Mesh=SM_RockDirt, bOverrideMaterial=True
  - emitter `ShootSpatter`
    - Required: material `MI_DebrisSpray`, subUV 4x1, localSpace True, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1-2@0s
    - Lifetime: 0.2/0.4 s
    - Size: 20/20/20/40/40/40
    - SizeMultiplyLife: 0.6/0.3/0/1.02774/0.73463/0.16526/1.46757/1.17663/0.35085/1.89527/1.61179/0.54007/2.28659/2.02591/0.71627/2.6173/2.40479/0.86277/2.86318/2.73422/0.96291/3/3/1
    - Velocity: 0/0/25 radial 0
    - Color: color 0.78906/0.59531/0.4644 alpha 0.8/1
    - Acceleration: 0/0/-980/0/0/-480
  - emitter `SmokeKick`
    - Required: material `MI_DustImpact`, subUV 16x2, localSpace True, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1-1@0s
    - Lifetime: 0.5/1.25 s
    - Size: 100/100/100/250/250/250
    - Color: color 0.52604/0.39687/0.3096 alpha 0.75/1
    - SizeMultiplyLife: 0.5/0.25/0/0.51992/0.54861/0.16962/0.49572/0.87606/0.3694/0.52714/0.91711/0.44479/0.5691/0.94466/0.51239/0.61614/0.97648/0.57959/0.6666/1.0113/0.64536/0.71882/1.04785/0.70864/0.77112/1.08485/0.76839/0.82186/1.12104/0.82356/0.86935/1.15515/0.87311/0.91194/1.18591/0.91601/0.94796/1.21204/0.95117/0.97576/1.23228/0.9776/0.99364/1.24534/0.99421/1/1.25/1
    - Acceleration: 0/0/-400
    - Color: color 1/0.75/0.58 alpha 0.75/1
    - Color: color 1/0.75/0.58 alpha 0.35/0.65
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - Spawn: rate 0, bursts 1@0s
    - Spawn: rate 0, bursts 1@0s
    - Spawn: rate 2, bursts -
    - Spawn: rate 2, bursts -
  - material `MI_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_DustImpact` → E:\Tools\deagle-work\ron_fx\particles\T_dustImpact_16x4.png (512x128,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_Gravel` → E:\Tools\deagle-work\ron_fx\particles\T_modern_concrete_normal.png (512x512,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Wave_N.png (1024x1024,BC5)
  - material `M_ParticleEmberSimple` → E:\Tools\deagle-work\ron_fx\particles\T_Glow_A.png (512x512,DXT1)
  - material `DebrisMaster` → E:\Tools\deagle-work\ron_fx\particles\Stones2x2.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\Gravel2x2_normal.png (512x512,BC5)

## wood

### RON_Pine

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01`  | E:\Tools\deagle-work\ron_fx\decals\Wood_1_N.png (1024²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Wood_3_H.png (1024²,DXT5) |
| ImpactEffects_Pistol | `NS_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01`  | E:\Tools\deagle-work\ron_fx\decals\Wood_1_N.png (1024²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Wood_3_H.png (1024²,DXT5) |
| ShotgunImpactEffects | `NS_Bullet_Impact_Wood_Big_Shotgun` | `Decal_Impact_Inst_WoodSoft`  | E:\Tools\deagle-work\ron_fx\decals\WoodSoft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\WoodSoft_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Pine_Exit` | `Decal_Impact_Inst_WoodSoft`  | E:\Tools\deagle-work\ron_fx\decals\WoodSoft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\WoodSoft_N.png (256²,BC5) |
| RicochetImpactEffects | `P_Bullet_Impact_Wood_Medium` | `M_BulletImpact_Wood_01`  | E:\Tools\deagle-work\ron_fx\decals\Wood_1_N.png (1024²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Wood_3_H.png (1024²,DXT5) |

- **NS_Bullet_Impact_Wood_Big** (Niagara, `/Game/ReadyOrNot/VFX/VFXImpacts/Niagara/NS_Bullet_Impact_Wood_Big`)
  - `DEBRIS_11`: GPUComputeSim, localSpace 0, bounds -500..500 / -500..500 / -500..500 | Sprite mat `Shards_Mat` subUV [2.0, 2.0] align 0 | curves: Curve_16: -360 → max 0 → 0 (128 samples); Curve_17: 360 → max 360 → 0 (128 samples); Curve_16: -360 → max 0 → 0 (128 samples); Curve_17: 360 → max 360 → 0 (128 samples)
  - `DUST_13`: GPUComputeSim, localSpace 0, bounds -750..750 / -750..750 / -750..750 | Sprite mat `MI_ThinSmoke_01` subUV [8.0, 4.0] align 0 | curves: Curve_16: 0.25 → max 1.25059 → 1.25 (43 samples); Curve_17: 1.25 → max 1.25 → 0 (50 samples); Curve_16: 0.25 → max 1.25059 → 1.25 (43 samples); Curve_17: 1.25 → max 1.25 → 0 (50 samples)
  - `FLOATING_DIRT_17`: GPUComputeSim, localSpace 0, bounds -500..750 / -500..750 / -500..750 | Sprite mat `M_ParticleEmberSimple` subUV - align VelocityAligned | curves: Curve_16: 1 → max 1.10379 → 0 (68 samples); Curve_17: 0 → max 1 → 1 (57 samples); Curve_16: 1 → max 1.10379 → 0 (68 samples); Curve_17: 0 → max 1 → 1 (57 samples)
  - `SHOT_SPATTER_14`: GPUComputeSim, localSpace 0, bounds -750..750 / -750..750 / -750..750 | Sprite mat `MI_DebrisSpray` subUV [4.0, 1.0] align VelocityAligned | curves: Vector2DCurve_8: 0.1 → max 3 → 2.5 (172 samples); Vector2DCurve_8: 0.1 → max 3 → 2.5 (172 samples)
  - `SMOKE_KICK_16`: GPUComputeSim, localSpace 0, bounds -500..500 / -500..500 / -500..500 | Sprite mat `MI_DustImpact` subUV [16.0, 2.0] align VelocityAligned | curves: Curve_8: 1 → max 1 → 0 (50 samples); Curve_8: 1 → max 1 → 0 (50 samples); Vector2DCurve_8: 0.5 → max 1 → 1 (40 samples); Vector2DCurve_8: 0.5 → max 1 → 1 (40 samples)
  - user params: User.Debris Color=[0.45312, 0.35836, 0.27376, 1.0], User.Dust Color=[1.0, 0.82823, 0.75, 1.0], User.Spatter Color=[0.75, 0.66641, 0.6, 1.0]
  - material `MI_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_DustImpact` → E:\Tools\deagle-work\ron_fx\particles\T_dustImpact_16x4.png (512x128,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_ParticleEmberSimple` → E:\Tools\deagle-work\ron_fx\particles\T_Glow_A.png (512x512,DXT1)
  - material `Shards_Mat` → E:\Tools\deagle-work\ron_fx\particles\Shards.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\TilingNoise06_N.png (512x512,BC5)
- **NS_Bullet_Impact_Wood_Big**: see above
- **NS_Bullet_Impact_Wood_Big_Shotgun** (Niagara, `/Game/ReadyOrNot/VFX/VFXImpacts/Niagara/NS_Bullet_Impact_Wood_Big_Shotgun`)
  - `DEBRIS_11`: GPUComputeSim, localSpace 0, bounds -500..500 / -500..500 / -500..500 | Sprite mat `Shards_Mat` subUV [2.0, 2.0] align 0 | curves: Curve_2: -360 → max 0 → 0 (128 samples); Curve_3: 360 → max 360 → 0 (128 samples); Curve_2: -360 → max 0 → 0 (128 samples); Curve_3: 360 → max 360 → 0 (128 samples)
  - `DUST_13`: GPUComputeSim, localSpace 0, bounds -750..750 / -750..750 / -750..750 | Sprite mat `MI_ThinSmoke_01` subUV [8.0, 4.0] align 0 | curves: Curve_2: 0.25 → max 1.25059 → 1.25 (43 samples); Curve_3: 1.25 → max 1.25 → 0 (50 samples); Curve_2: 0.25 → max 1.25059 → 1.25 (43 samples); Curve_3: 1.25 → max 1.25 → 0 (50 samples)
  - `FLOATING_DIRT_17`: GPUComputeSim, localSpace 0, bounds -500..750 / -500..750 / -500..750 | Sprite mat `M_ParticleEmberSimple` subUV - align VelocityAligned | curves: Curve_2: 1 → max 1.10379 → 0 (68 samples); Curve_3: 0 → max 1 → 1 (57 samples); Curve_2: 1 → max 1.10379 → 0 (68 samples); Curve_3: 0 → max 1 → 1 (57 samples)
  - `SHOT_SPATTER_14`: GPUComputeSim, localSpace 0, bounds -750..750 / -750..750 / -750..750 | Sprite mat `MI_DebrisSpray` subUV [4.0, 1.0] align VelocityAligned | curves: Vector2DCurve_1: 0.1 → max 3 → 2.5 (172 samples); Vector2DCurve_1: 0.1 → max 3 → 2.5 (172 samples)
  - `SMOKE_KICK_16`: GPUComputeSim, localSpace 0, bounds -500..500 / -500..500 / -500..500 | Sprite mat `MI_DustImpact` subUV [16.0, 2.0] align VelocityAligned | curves: Curve_1: 1 → max 1 → 0 (50 samples); Curve_1: 1 → max 1 → 0 (50 samples); Vector2DCurve_1: 0.5 → max 1 → 1 (40 samples); Vector2DCurve_1: 0.5 → max 1 → 1 (40 samples)
  - user params: User.Debris Color=[0.45312, 0.35836, 0.27376, 1.0], User.Dust Color=[1.0, 0.82823, 0.75, 1.0], User.Spatter Color=[0.75, 0.66641, 0.6, 1.0]
  - material `MI_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_DustImpact` → E:\Tools\deagle-work\ron_fx\particles\T_dustImpact_16x4.png (512x128,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_ParticleEmberSimple` → E:\Tools\deagle-work\ron_fx\particles\T_Glow_A.png (512x512,DXT1)
  - material `Shards_Mat` → E:\Tools\deagle-work\ron_fx\particles\Shards.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\TilingNoise06_N.png (512x512,BC5)
- **P_VFX_Impact_Pine_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/Exit/P_VFX_Impact_Pine_Exit`)
  - emitter `Smokeball`
    - Required: material `MI_Impact_Smoke_04`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.8/1.1 s
    - Velocity: -15/-15/100/2/2/500 radial 0
    - Size: 45/45/45/75/75/75
    - ColorOverLife: color 0.57292/0.49991/0.3163/0.23958/0.2028/0.15348 alpha 0.5/0.58333/0.66667/0.75/0.83333/0.91667/0.99043/0.63046/0.36037/0.17487/0.05905/-0.00218/-0.02368/-0.02052/-0.00764/0
    - SizeMultiplyLife: 0.5/0.5/0.5/0.89947/0.89947/0.89947/1.3581/1.3581/1.3581/1.83192/1.83192/1.83192/2.27698/2.27698/2.27698/2.64932/2.64932/2.64932/2.90498/2.90498/2.90498/3/3/3
    - Acceleration: 0/0/20/0/50/25
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.1 s
    - Size: 0.5/0.5/1/0.75/0.4/1
    - Acceleration: -
    - SizeMultiplyLife: -
    - ColorOverLife: color 25/23.5784/16.125 alpha 1.5/0
    - Velocity: -50/-50/100/50/50/118.099 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `None`
    - Required: material `Squib_one`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.5/0.6 s
    - Size: 75/25.48/75/125/125/125
    - Velocity: 0/0/25/0/0/100 radial 0
    - ColorOverLife: color 0.63542/0.43623/0.2085/0.70833/0.56981/0.38407 alpha 0.25/1.91629/1.86747/1.82006/1.77403/1.72935/1.68601/1.64397/1.60322/1.56372/1.52545/1.48838/1.4525/1.41778/1.38418/1.3517/1.32029/1.28994/1.26062/1.23231/1.20498/1.17861/1.15317/1.12864/1.10498/1.08219/1.06022/1.03907/1.01869/0.99908/0.98019/0.96201/0.94452/0.92768/0.91148/0.89588/0.88086/0.8664/0.85248/0.83906/0.82612/0.81364/0.8016/0.78996/0.7787/0.76781/0.75724/0.74699/0.73701/0.7273/0.71782/0.70854/0.69945/0.69052/0.68172/0.67303/0.66442/0.65587/0.64735/0.63885/0.63032/0.62176/0.61313/0.6044/0.59554/0.58637/0.57689/0.56711/0.55705/0.54672/0.53614/0.52531/0.51427/0.50301/0.49156/0.47993/0.46814/0.4562/0.44412/0.43193/0.41963/0.40724/0.39478/0.38226/0.36969/0.3571/0.34449/0.33188/0.31929/0.30673/0.29421/0.28176/0.26938/0.25709/0.24491/0.23284/0.22091/0.20914/0.19752/0.18609/0.17486/0.16383/0.15303/0.14247/0.13216/0.12213/0.11238/0.10293/0.09379/0.08499/0.07653/0.06843/0.0607/0.05337/0.04644/0.03993/0.03385/0.02823/0.02307/0.01838/0.0142/0.01052/0.00737/0.00476/0.0027/0.00121/0.0003/0
    - SizeMultiplyLife: 0.5/0.5/0.5/0.83358/0.83351/0.83358/1.14704/1.14676/1.14704/1.42901/1.42845/1.42901/1.66811/1.66725/1.66811/1.85298/1.85185/1.85298/1.97224/1.97091/1.97224/2.01454/2.01313/2.01454
    - Acceleration: 0/0/20/0/50/25
  - emitter `Spattering`
    - Required: material `SpeckledNormal`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 5@0s
    - Lifetime: 0.25/0.5 s
    - Size: 40/40/40/50/50/50
    - Velocity: -10/-10/20/10/10/500 radial 0
    - ColorOverLife: color 0.28/0.23974/0.19948 alpha 2/2.10753/2.21505/2.32258/2.43011/2.53763/2.64516/2.75269/2.86022/2.96774/2.9755/2.91388/2.8238/2.70853/2.57129/2.4153/2.2438/2.05998/1.86708/1.66832/1.46691/1.26609/1.0691/0.87916/0.6995/0.53333/0.38387/0.25435/0.14798/0.06798/0.01758/0
    - SizeMultiplyLife: 0/0/0/0.44868/0.44868/0.44868/0.74449/0.74449/0.74449/0.91798/0.91798/0.91798/0.99971/0.99971/0.99971/1.02025/1.02025/1.02025/1.01016/1.01016/1.01016/1/1/1
    - Acceleration: -
    - Spawn: rate 0, bursts 7@0s
    - Spawn: rate 0, bursts 7@0s
    - Spawn: rate 0, bursts 7@0s
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `MI_Impact_Smoke_04` → E:\Tools\deagle-work\ron_fx\particles\Cloud04_8x8.png (1024x1024,DXT5), E:\Tools\deagle-work\ron_fx\particles\WispySmoke03b_8x8.png (1024x1024,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
  - material `SpeckledNormal` → E:\Tools\deagle-work\ron_fx\particles\Noise_Anisotropic2_height.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\Speckled_tile.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\Speckled_tile_2.png (520x520,DXT1), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5), E:\Tools\deagle-work\ron_fx\particles\TX_FX_SmokeCloud01.png (64x64,DXT5)
  - material `Squib_one` → E:\Tools\deagle-work\ron_fx\particles\Squib_one_1024_8x8.png (1024x1024,DXT5), E:\Tools\deagle-work\ron_fx\particles\Squib_one_normal.png (256x256,BC5)
- **P_Bullet_Impact_Wood_Medium** (Cascade, `/Game/ReadyOrNot/VFX/VFXImpacts/Edit/P_Bullet_Impact_Wood_Medium`)
  - emitter `WoodShardsHighRes`
    - Required: material `M_WoodShards`, subUV 2x2, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 3-5@0s
    - Lifetime: 1.5/2 s
    - Size: 3/6/6/5/15/15
    - Velocity: -150/-150/500/150/150/1200 radial 0
    - Color: color 0.16146/0.11946/0.08914 alpha 1
    - Acceleration: 0/0/-500/0/0/-700
    - SizeMultiplyLife: 0/0/0/0.35563/0.35545/0.35508/1.04215/1.04175/1.04096/1.48898/1.48884/1.48858/1.48861/1.48932/1.4907/1.47468/1.47622/1.47922/1.46061/1.46296/1.46753/1.44641/1.44954/1.45562/1.43208/1.43597/1.44351/1.41763/1.42224/1.4312/1.40306/1.40838/1.41869/1.38837/1.39437/1.40599/1.37358/1.38022/1.39311/1.35868/1.36594/1.38004/1.34367/1.35154/1.3668/1.32857/1.33701/1.35339/1.31337/1.32236/1.33981/1.29809/1.3076/1.32607/1.28271/1.29273/1.31218/1.26726/1.27776/1.29814/1.25172/1.26268/1.28395/1.23611/1.24751/1.26962/1.22044/1.23225/1.25516/1.20469/1.2169/1.24057/1.18889/1.20146/1.22585/1.17303/1.18595/1.21102/1.15711/1.17036/1.19607/1.14115/1.1547/1.18101/1.12514/1.13898/1.16584/1.10909/1.1232/1.15058/1.093/1.10736/1.13523/1.07688/1.09147/1.11978/1.06073/1.07553/1.10425/1.04455/1.05955/1.08864/1.02836/1.04353/1.07296/1.01215/1.02747/1.05721/0.99592/1.01139/1.0414/0.97969/0.99528/1.02553/0.96345/0.97915/1.00961/0.94722/0.96301/0.99363/0.93099/0.94685/0.97762/0.91476/0.93068/0.96157/0.89855/0.91451/0.94548/0.88236/0.89835/0.92937/0.86618/0.88219/0.91323/0.85003/0.86603/0.89708/0.83391/0.8499/0.88092/0.81782/0.83378/0.86475/0.80177/0.81769/0.84857/0.78576/0.80162/0.8324/0.76979/0.78559/0.81624/0.75387/0.76959/0.80009/0.73801/0.75364/0.78396/0.7222/0.73773/0.76785/0.70646/0.72187/0.75177/0.69078/0.70607/0.73573/0.67517/0.69032/0.71972/0.65963/0.67464/0.70376/0.64418/0.65903/0.68785/0.6288/0.64349/0.67199/0.61351/0.62803/0.65619/0.59831/0.61264/0.64045/0.5832/0.59735/0.62479/0.5682/0.58214/0.6092/0.55329/0.56703/0.59368/0.53849/0.55202/0.57826/0.52381/0.53711/0.56292/0.50923/0.52231/0.54768/0.49478/0.50762/0.53253/0.48045/0.49305/0.5175/0.46625/0.4786/0.50257/0.45217/0.46428/0.48776/0.43824/0.45008/0.47307/0.42444/0.43602/0.4585/0.41078/0.42211/0.44407/0.39728/0.40833/0.42977/0.38392/0.3947/0.41561/0.37072/0.38122/0.4016/0.35768/0.36791/0.38774/0.34481/0.35475/0.37404/0.3321/0.34176/0.36049/0.31956/0.32894/0.34712/0.30721/0.31629/0.33391/0.29503/0.30382/0.32088/0.28303/0.29154/0.30804/0.27123/0.27944/0.29538/0.25961/0.26754/0.28291/0.2482/0.25583/0.27064/0.23698/0.24432/0.25857/0.22597/0.23303/0.24671/0.21517/0.22194/0.23506/0.20458/0.21106/0.22363/0.19421/0.2004/0.21242/0.18406/0.18997/0.20144/0.17414/0.17977/0.19069/0.16445/0.1698/0.18018/0.15499/0.16006/0.16991/0.14576/0.15057/0.15989/0.13678/0.14132/0.15012/0.12805/0.13232/0.14062/0.11956/0.12358/0.13137/0.11133/0.1151/0.1224/0.10336/0.10687/0.11369/0.09565/0.09892/0.10527/0.08821/0.09124/0.09713/0.08103/0.08384/0.08928/0.07414/0.07672/0.08172/0.06752/0.06988/0.07447/0.06118/0.06333/0.06751/0.05513/0.05708/0.06087/0.04937/0.05113/0.05454/0.04391/0.04548/0.04853/0.03875/0.04014/0.04284/0.03389/0.03511/0.03749/0.02933/0.0304/0.03247/0.02509/0.02601/0.02779/0.02117/0.02195/0.02345/0.01756/0.01821/0.01947/0.01428/0.01481/0.01584/0.01133/0.01175/0.01257/0.00871/0.00903/0.00966/0.00642/0.00666/0.00713/0.00448/0.00465/0.00497/0.00288/0.00298/0.0032/0.00162/0.00169/0.00181/0.00072/0.00075/0.00081/0.00018/0.00019/0.0002/0/0/0
  - emitter `WoodShardsSmall`
    - Required: material `M_WoodShards`, subUV 2x2, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 2-5@0s
    - Lifetime: 0.2/0.5 s
    - Size: 3/6/6/5/5/5
    - Velocity: -100/-150/300/150/150/600 radial 0
    - Color: color 0.45312/0.35836/0.27376 alpha 1
    - Acceleration: 0/0/-500/0/0/-300
    - SizeMultiplyLife: 1/1/1/0.9675/0.96605/0.96323/0.87809/0.87325/0.86384/0.74384/0.73512/0.7182/0.57683/0.5652/0.54264/0.38914/0.37702/0.35352/0.19284/0.18411/0.16719/0/0/0
  - emitter `SmokeKickburst`
    - Required: material `M_Smoke1`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 3-5@0s
    - Lifetime: 0.5/0.2 s
    - Size: 150/151.433/150/250/250/250
    - Velocity: -300/-300/25/300/300/50 radial 0
    - Color: color 0.44792/0.37854/0.27995 alpha 0.1/0.25
    - ColorOverLife: color 0.70833/0.59892/0.48329/1/1/1 alpha 0/0.24555/0.29978/0.29788/0.2941/0.2886/0.28151/0.27297/0.26313/0.25211/0.24008/0.22715/0.21348/0.19921/0.18448/0.16943/0.1542/0.13893/0.12376/0.10884/0.0943/0.08028/0.06693/0.05439/0.0428/0.03229/0.02302/0.01511/0.00872/0.00397/0.00102/0
    - SizeMultiplyLife: 0.5/0.5/0.5/1/1/1
    - Acceleration: 0/0/-3/0/0/-1
  - emitter `WoodShardsHighRes`
    - Required: material `M_WoodShards`, subUV 2x2, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 5-15@0s
    - Lifetime: 1.5/3 s
    - Size: 2/3/3/3/5/5
    - Velocity: -1200/-1200/0/1200/1200/2000 radial 0
    - Color: color 0.16146/0.11946/0.08914 alpha 1
    - Acceleration: 0/0/-1000/0/0/-1200
    - SizeMultiplyLife: 0/0/0/0.35563/0.35545/0.35508/1.04215/1.04175/1.04096/1.48898/1.48884/1.48858/1.48861/1.48932/1.4907/1.47468/1.47622/1.47922/1.46061/1.46296/1.46753/1.44641/1.44954/1.45562/1.43208/1.43597/1.44351/1.41763/1.42224/1.4312/1.40306/1.40838/1.41869/1.38837/1.39437/1.40599/1.37358/1.38022/1.39311/1.35868/1.36594/1.38004/1.34367/1.35154/1.3668/1.32857/1.33701/1.35339/1.31337/1.32236/1.33981/1.29809/1.3076/1.32607/1.28271/1.29273/1.31218/1.26726/1.27776/1.29814/1.25172/1.26268/1.28395/1.23611/1.24751/1.26962/1.22044/1.23225/1.25516/1.20469/1.2169/1.24057/1.18889/1.20146/1.22585/1.17303/1.18595/1.21102/1.15711/1.17036/1.19607/1.14115/1.1547/1.18101/1.12514/1.13898/1.16584/1.10909/1.1232/1.15058/1.093/1.10736/1.13523/1.07688/1.09147/1.11978/1.06073/1.07553/1.10425/1.04455/1.05955/1.08864/1.02836/1.04353/1.07296/1.01215/1.02747/1.05721/0.99592/1.01139/1.0414/0.97969/0.99528/1.02553/0.96345/0.97915/1.00961/0.94722/0.96301/0.99363/0.93099/0.94685/0.97762/0.91476/0.93068/0.96157/0.89855/0.91451/0.94548/0.88236/0.89835/0.92937/0.86618/0.88219/0.91323/0.85003/0.86603/0.89708/0.83391/0.8499/0.88092/0.81782/0.83378/0.86475/0.80177/0.81769/0.84857/0.78576/0.80162/0.8324/0.76979/0.78559/0.81624/0.75387/0.76959/0.80009/0.73801/0.75364/0.78396/0.7222/0.73773/0.76785/0.70646/0.72187/0.75177/0.69078/0.70607/0.73573/0.67517/0.69032/0.71972/0.65963/0.67464/0.70376/0.64418/0.65903/0.68785/0.6288/0.64349/0.67199/0.61351/0.62803/0.65619/0.59831/0.61264/0.64045/0.5832/0.59735/0.62479/0.5682/0.58214/0.6092/0.55329/0.56703/0.59368/0.53849/0.55202/0.57826/0.52381/0.53711/0.56292/0.50923/0.52231/0.54768/0.49478/0.50762/0.53253/0.48045/0.49305/0.5175/0.46625/0.4786/0.50257/0.45217/0.46428/0.48776/0.43824/0.45008/0.47307/0.42444/0.43602/0.4585/0.41078/0.42211/0.44407/0.39728/0.40833/0.42977/0.38392/0.3947/0.41561/0.37072/0.38122/0.4016/0.35768/0.36791/0.38774/0.34481/0.35475/0.37404/0.3321/0.34176/0.36049/0.31956/0.32894/0.34712/0.30721/0.31629/0.33391/0.29503/0.30382/0.32088/0.28303/0.29154/0.30804/0.27123/0.27944/0.29538/0.25961/0.26754/0.28291/0.2482/0.25583/0.27064/0.23698/0.24432/0.25857/0.22597/0.23303/0.24671/0.21517/0.22194/0.23506/0.20458/0.21106/0.22363/0.19421/0.2004/0.21242/0.18406/0.18997/0.20144/0.17414/0.17977/0.19069/0.16445/0.1698/0.18018/0.15499/0.16006/0.16991/0.14576/0.15057/0.15989/0.13678/0.14132/0.15012/0.12805/0.13232/0.14062/0.11956/0.12358/0.13137/0.11133/0.1151/0.1224/0.10336/0.10687/0.11369/0.09565/0.09892/0.10527/0.08821/0.09124/0.09713/0.08103/0.08384/0.08928/0.07414/0.07672/0.08172/0.06752/0.06988/0.07447/0.06118/0.06333/0.06751/0.05513/0.05708/0.06087/0.04937/0.05113/0.05454/0.04391/0.04548/0.04853/0.03875/0.04014/0.04284/0.03389/0.03511/0.03749/0.02933/0.0304/0.03247/0.02509/0.02601/0.02779/0.02117/0.02195/0.02345/0.01756/0.01821/0.01947/0.01428/0.01481/0.01584/0.01133/0.01175/0.01257/0.00871/0.00903/0.00966/0.00642/0.00666/0.00713/0.00448/0.00465/0.00497/0.00288/0.00298/0.0032/0.00162/0.00169/0.00181/0.00072/0.00075/0.00081/0.00018/0.00019/0.0002/0/0/0
  - emitter `SmokeKickLinger`
    - Required: material `M_Smoke1`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1-4@0s
    - Lifetime: 1/1.5 s
    - Size: 150/150/150/200/200/200
    - Velocity: -100/-100/400/100/100/1000 radial 0
    - Color: color 0.52604/0.39687/0.3096 alpha 0.1/0.25
    - ColorOverLife: color 0.34375/0.327/0.28646/1/1/1 alpha 0.5/0
    - SizeMultiplyLife: 0.3/0/0/0.38359/0.91675/0.06855/0.45292/1.60065/0.13494/0.50999/2.07631/0.19959/0.55695/2.37052/0.26295/0.59583/2.50849/0.32545/0.62875/2.5165/0.38754/0.65778/2.42019/0.44965/0.68501/2.2455/0.51222/0.71251/2.01829/0.57569/0.74237/1.7644/0.64051/0.77667/1.5097/0.70709/0.81752/1.2801/0.7759/0.86695/1.1013/0.84736/0.92711/0.99949/0.92192/1/1/1
    - Acceleration: 0/0/-3/0/0/-1
  - emitter `BaseSmoke`
    - Required: material `whispOne_Master_amb_Inst`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 4-6@0s
    - Lifetime: 0.15/1 s
    - Size: 100/100/100/150/150/150
    - Velocity: -500/-500/0/500/500/500 radial 0
    - ColorOverLife: color 0.8902/0.71765/0.47843 alpha 0/0.26667/0.53333/0.79055/0.73333/0.66667/0.6/0.53333/0.46667/0.4/0.33333/0.26667/0.2/0.13333/0.06667/0
    - SizeMultiplyLife: 0.2/0.2/0.2/0.56068/0.56068/0.56068/0.91931/0.91931/0.91931/1.25641/1.25641/1.25641/1.5525/1.5525/1.5525/1.78811/1.78811/1.78811/1.94377/1.94377/1.94377/2/2/2
    - Acceleration: 0/-50/20/0/-500/96
  - emitter `ShootSmoke`
    - Required: material `Whisps_trans2`, subUV 12x12, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 2@0s
    - Lifetime: 0.5 s
    - Size: 100/100/100/150/150/150
    - Velocity: -25/25/300/25/25/1000 radial 0
    - ColorOverLife: color 0.8902/0.71765/0.47843 alpha 0/0.10714/0.13393/0.10714/0.08036/0.05357/0.02679/0
    - SizeMultiplyLife: 0/0/0/0.31636/0.26095/0.26095/0.5607/0.36243/0.36243/0.74153/0.34794/0.34794/0.86737/0.26096/0.26096/0.94672/0.14499/0.14499/0.9881/0.04351/0.04351/1/0/0
    - Acceleration: 0/-50/20/0/-500/96
  - emitter `ShootSpatter`
    - Required: material `SpeckledNormal_02`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 14@0s
    - Lifetime: 0.1/0.3 s
    - Size: 12.5/25/12.5/25/75/25
    - Velocity: -255/-255/100/255/255/500 radial 0
    - ColorOverLife: color 0.13542/0.10917/0.07278 alpha 0.2/0.48571/0.42017/0.33613/0.2521/0.16807/0.08403/0
    - SizeMultiplyLife: 0.5/0.5/0.5/0.78865/0.78865/0.78865/0.96156/0.96156/0.96156/1.04474/1.04474/1.04474/1.06417/1.06417/1.06417/1.04586/1.04586/1.04586/1.0158/1.0158/1.0158/1/1/1
    - Acceleration: 0/-50/-150/0/-500/-250
  - emitter `Dust`
    - Required: material `M_Smoke1`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 30-50@0s
    - Lifetime: 2/6 s
    - Size: 1/1/1/3/3/3
    - Velocity: -300/-300/100/300/300/600 radial 0
    - Color: color 0.34375/0.34375/0.34375/1/1/1 alpha 0.5/1
    - ColorOverLife: color 1/1/1/0/0/0 alpha 1/0
    - Acceleration: 0/0/-10/0/0/-5
  - emitter `Smoke`
    - Required: material `M_Smoke1`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.5/1 s
    - Size: 100/100/100/200/200/200
    - Velocity: -20/-20/250/20/20/400 radial 0
    - Color: color 1/1/1 alpha 255/5
    - ColorOverLife: color 0.94271/0.70254/0.45171/1/1/1 alpha 0/0.19655/0.28728/0.30267/0.27317/0.22924/0.20135/0.19938/0.19698/0.19292/0.18735/0.18042/0.1723/0.16313/0.15309/0.14232/0.13099/0.11925/0.10725/0.09516/0.08314/0.07133/0.0599/0.04901/0.03881/0.02946/0.02112/0.01394/0.00808/0.0037/0.00095/0
    - SizeMultiplyLife: 0.3/0.3/0.3/0.68472/0.7121/0.72823/0.92013/0.93383/0.94189/1/1/1
    - Acceleration: 0/0/-3/0/0/-1
  - emitter `ShardBurts`
    - Required: material `M_WoodShards`, subUV 2x2, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 5-10@0s
    - Lifetime: 0.05/0.1 s
    - Size: 5/30/5/10/40/10
    - Velocity: -100/-100/250/100/100/400 radial 0
    - ColorOverLife: color 0.22917/0.13404/0.06577/0.29688/0.26218/0.23812 alpha 1/0
    - SizeMultiplyLife: 0.77288/0.71232/0.50034/0.01583/0.00069/0.00069
  - emitter `SmokeBurst`
    - Required: material `M_Smoke1`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1-1@0s
    - Lifetime: 0.1/0.3 s
    - Size: 10/50/10/20/100/20
    - Velocity: -20/-20/200/20/20/500 radial 0
    - Color: color 0.5625/0.47372/0.38379 alpha 0.5/1
    - ColorOverLife: color 0.94271/0.70254/0.45171/0.4375/0.33553/0.22559 alpha 0/0.41003/0.9942/1.5666/1.94134/1.99778/1.98141/1.9499/1.90453/1.84658/1.77735/1.69809/1.61011/1.51473/1.41322/1.30688/1.197/1.08486/0.97175/0.85896/0.74778/0.6395/0.53542/0.43683/0.345/0.26124/0.18682/0.12304/0.07118/0.03253/0.00837/0
    - SizeMultiplyLife: 1.875/1.875/1.875/2.5/2.5/2.5
    - Acceleration: 0/0/-3/0/0/-1
  - emitter `SpallingDust`
    - Required: material `M_DustFine`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.5/1 s
    - Size: 10/15/15/15/25/25
    - Velocity: 0/0/0 radial 0
    - ColorOverLife: color 0.52083/0.45123/0.31738/0.74479/0.65132/0.42282 alpha 0/0.32258/0.64516/0.96088/1/1/1/1/1/1/1/1/1/1/1/1/0.96774/0.90323/0.83871/0.77419/0.70968/0.64516/0.58065/0.51613/0.45161/0.3871/0.32258/0.25806/0.19355/0.12903/0.06452/0
    - Acceleration: 0/0/-80/0/0/-40
    - SizeMultiplyLife: 1/1/1/2/7/2
  - emitter `ShootAngles`
    - Required: material `SpeckledNormal_02`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 5@0s
    - Lifetime: 0.1/0.12 s
    - Size: 12.5/25/12.5/25/75/25
    - Velocity: -800/-800/100/800/800/250 radial 0
    - ColorOverLife: color 0.13542/0.10917/0.07278 alpha 0.2/0.77143/0.67227/0.53782/0.40336/0.26891/0.13445/0
    - SizeMultiplyLife: 0.5/0.5/0.5/0.78865/0.78865/0.78865/0.96156/0.96156/0.96156/1.04474/1.04474/1.04474/1.06417/1.06417/1.06417/1.04586/1.04586/1.04586/1.0158/1.0158/1.0158/1/1/1
    - Acceleration: 0/-50/-150/0/-500/-250
  - emitter `SmokeKick`
    - Required: material `M_Smoke1`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1-1@0s
    - Lifetime: 1/1.5 s
    - Size: 150/150/150/200/200/200
    - Velocity: -100/-100/400/100/100/600 radial 0
    - Color: color 0.57292/0.48157/0.37598 alpha 0.1/0.25
    - ColorOverLife: color 0.34896/0.28465/0.22537/1/1/1 alpha 2/2.00511/1.7191/1.2714/0.79146/0.40871/0.25149/0.24882/0.24545/0.24006/0.23285/0.22401/0.21372/0.20218/0.18959/0.17613/0.16201/0.1474/0.1325/0.11751/0.10262/0.08801/0.07388/0.06043/0.04784/0.0363/0.02601/0.01717/0.00995/0.00455/0.00117/0
    - SizeMultiplyLife: 0/0/0/0.14331/0.64057/0.13414/0.3755/1.03933/0.36725/0.60695/1.25402/0.60375/0.74791/1.34433/0.7479/0.81625/1.35945/0.81625/0.88463/1.3735/0.88463/0.953/1.38756/0.953/1.02138/1.40161/1.02138/1.08975/1.41567/1.08975/1.15813/1.42972/1.15813/1.2265/1.44378/1.2265/1.29488/1.45783/1.29488/1.36325/1.47189/1.36325/1.43163/1.48594/1.43163/1.5/1.5/1.5
    - Acceleration: 0/0/-3/0/0/-1
  - emitter `SmokeKickRound`
    - Required: material `M_Smoke1`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1-1@0s
    - Lifetime: 1/1.5 s
    - Size: 150/150/150/200/200/200
    - Velocity: -100/-100/400/100/100/600 radial 0
    - Color: color 0.52604/0.39687/0.3096 alpha 0.1/0.25
    - ColorOverLife: color 0.42188/0.40132/0.35156/1/1/1 alpha 0/0.24138/0.27048/0.24994/0.24421/0.23007/0.20924/0.18341/0.15426/0.12351/0.09284/0.06394/0.03851/0.01825/0.00486/0
    - SizeMultiplyLife: 0/0/0/0.0604/0.38578/0.05302/0.14502/0.57388/0.13534/0.24201/0.62164/0.23352/0.33956/0.58642/0.33413/0.42583/0.52555/0.42373/0.48901/0.4964/0.48889/0.51519/0.51519/0.51519/0.53539/0.53539/0.53539/0.55559/0.55559/0.55559/0.57579/0.57579/0.57579/0.59599/0.59599/0.59599/0.61619/0.61619/0.61619/0.63639/0.63639/0.63639/0.65659/0.65659/0.65659/0.67679/0.67679/0.67679/0.69699/0.69699/0.69699/0.71719/0.71719/0.71719/0.73739/0.73739/0.73739/0.75759/0.75759/0.75759/0.77779/0.77779/0.77779/0.79799/0.79799/0.79799/0.8182/0.8182/0.8182/0.8384/0.8384/0.8384/0.8586/0.8586/0.8586/0.8788/0.8788/0.8788/0.899/0.899/0.899/0.9192/0.9192/0.9192/0.9394/0.9394/0.9394/0.9596/0.9596/0.9596/0.9798/0.9798/0.9798/1/1/1
    - Acceleration: 0/0/-3/0/0/-1
    - Acceleration: 0/0/-3/0/0/-1
    - Acceleration: 0/0/-10/0/0/-5
    - Color: color 0.34375/0.34375/0.34375/1/1/1 alpha 0.5/1
    - Color: color 1/1/1 alpha 255/5
    - ColorOverLife: color 1/1/1/0/0/0 alpha 1/0
    - ColorOverLife: color 0.94271/0.70254/0.45171/1/1/1 alpha 0/0.19655/0.28728/0.30267/0.27317/0.22924/0.20135/0.19938/0.19698/0.19292/0.18735/0.18042/0.1723/0.16313/0.15309/0.14232/0.13099/0.11925/0.10725/0.09516/0.08314/0.07133/0.0599/0.04901/0.03881/0.02946/0.02112/0.01394/0.00808/0.0037/0.00095/0
    - Lifetime: 0.3/0.4 s
    - Lifetime: 0.5/1 s
    - Lifetime: 2/6 s
    - Required: material `M_Smoke1`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Required: material `M_Smoke1`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Size: 100/100/100/200/200/200
    - Size: 1/1/1/3/3/3
    - SizeMultiplyLife: 0.3/0.3/0.3/0.68472/0.7121/0.72823/0.92013/0.93383/0.94189/1/1/1
    - Spawn: rate 0, bursts 1@0s
    - Spawn: rate 0, bursts 30-50@0s
    - Spawn: rate 0, bursts 1-1@0s
    - Velocity: -20/-20/250/20/20/400 radial 0
    - Velocity: -300/-300/100/300/300/600 radial 0
  - material `M_DustFine` → E:\Tools\deagle-work\ron_fx\particles\T_GradientRound_A.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Noise_RVB.png (512x512,DXT1)
  - material `M_Smoke1` → E:\Tools\deagle-work\ron_fx\particles\T_Cloud_ABC.png (512x512,DXT1)
  - material `M_WoodShards` → E:\Tools\deagle-work\ron_fx\particles\T_WoodShard_OAH.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_WoodShard_normal.png (512x512,BC5)
  - material `SpeckledNormal_02` → E:\Tools\deagle-work\ron_fx\particles\Noise_Anisotropic2_height.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5), E:\Tools\deagle-work\ron_fx\particles\Speckled_tile_2_more.png (520x520,DXT1), E:\Tools\deagle-work\ron_fx\particles\Speckled_tile_more.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\TX_FX_SmokeCloud01.png (64x64,DXT5)
  - material `Whisps_trans2` → E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5), E:\Tools\deagle-work\ron_fx\particles\whisp_redux_2048_12x12.png (1024x1024,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_redux_2048_normal.png (256x256,BC5)
  - material `whispOne_Master_amb_Inst` → E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)

### RON_Plywood

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01`  | E:\Tools\deagle-work\ron_fx\decals\Wood_1_N.png (1024²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Wood_3_H.png (1024²,DXT5) |
| ImpactEffects_Pistol | `NS_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01`  | E:\Tools\deagle-work\ron_fx\decals\Wood_1_N.png (1024²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Wood_3_H.png (1024²,DXT5) |
| ShotgunImpactEffects | `NS_Bullet_Impact_Wood_Big_Shotgun` | `Decal_Impact_Inst_WoodHard`  | E:\Tools\deagle-work\ron_fx\decals\WoodHard_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\WoodHard_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Plywood_Exit` | `Decal_Impact_Inst_WoodHard`  | E:\Tools\deagle-work\ron_fx\decals\WoodHard_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\WoodHard_N.png (256²,BC5) |
| RicochetImpactEffects | `P_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01`  | E:\Tools\deagle-work\ron_fx\decals\Wood_1_N.png (1024²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Wood_3_H.png (1024²,DXT5) |

- **NS_Bullet_Impact_Wood_Big**: see above
- **NS_Bullet_Impact_Wood_Big**: see above
- **NS_Bullet_Impact_Wood_Big_Shotgun**: see above
- **P_VFX_Impact_Plywood_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Plywood_Exit`)
  - emitter `Smokeball`
    - Required: material `M_SmokeBall`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.5/1 s
    - Size: 45/45/45/75/75/75
    - Velocity: -50/-50/100/50/50/500 radial 0
    - ColorOverLife: color 0.57292/0.44409/0.26832/0.44792/0.37915/0.28695 alpha 0/0.10169/0.20339/0.30508/0.40678/0.50847/0.61016/0.71186/0.81355/0.91525/1.01694/1.11864/1.22033/1.32202/1.42372/1.52541/1.62711/1.7288/1.83049/1.93219/1.89028/1.84216/1.79541/1.75001/1.70593/1.66316/1.62167/1.58145/1.54247/1.50471/1.46816/1.43279/1.39859/1.36552/1.33358/1.30274/1.27298/1.24428/1.21662/1.18997/1.16433/1.13967/1.11596/1.09319/1.07134/1.05039/1.03031/1.01109/0.9927/0.97513/0.95835/0.94235/0.9271/0.91259/0.89878/0.88567/0.87324/0.86145/0.8503/0.83975/0.8298/0.82041/0.81158/0.80327/0.79548/0.78817/0.78133/0.77493/0.76897/0.76341/0.75823/0.75342/0.74896/0.74482/0.74099/0.73744/0.73415/0.73111/0.72829/0.72567/0.72323/0.72095/0.71882/0.7168/0.71489/0.71305/0.71128/0.70954/0.70782/0.7061/0.70436/0.70257/0.70072/0.69879/0.69675/0.69459/0.69228/0.68981/0.68715/0.68428/0.68119/0.67785/0.67424/0.67034/0.66614/0.66161/0.65672/0.65147/0.64583/0.63977/0.63329/0.62635/0.61894/0.61104/0.60263/0.58921/0.55935/0.51538/0.46047/0.39781/0.3306/0.262/0.19522/0.13344/0.07984/0.03761/0.00993/0
    - SizeMultiplyLife: 1/1/1/1.18268/1.18268/1.18268/1.39481/1.39481/1.39481/1.6303/1.6303/1.6303/1.88282/1.88282/1.88282/2.14616/2.14616/2.14616/2.41406/2.41406/2.41406/2.68027/2.68027/2.68027/2.93856/2.93856/2.93856/3.18268/3.18268/3.18268/3.40637/3.40637/3.40637/3.60342/3.60342/3.60342/3.7675/3.7675/3.7675/3.89251/3.89251/3.89251/3.972/3.972/3.972/4/4/4
    - Acceleration: 0/0/20/0/50/25
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 3@0s
    - Lifetime: 0.1 s
    - Size: 0.25/0.25/1/0.4/0.4/1
    - Acceleration: -
    - SizeMultiplyLife: -
    - ColorOverLife: color 25/23.5784/16.125 alpha 1.5/0
    - Velocity: -50/-50/100/50/50/118.099 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `None`
    - Required: material `Squib_one`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.25/0.4 s
    - Size: 75/25.48/75/125/125/125
    - Velocity: 0/0/25/0/0/100 radial 0
    - ColorOverLife: color 0.63542/0.43623/0.2085/0.70833/0.56981/0.38407 alpha 0.25/1.91629/1.86747/1.82006/1.77403/1.72935/1.68601/1.64397/1.60322/1.56372/1.52545/1.48838/1.4525/1.41778/1.38418/1.3517/1.32029/1.28994/1.26062/1.23231/1.20498/1.17861/1.15317/1.12864/1.10498/1.08219/1.06022/1.03907/1.01869/0.99908/0.98019/0.96201/0.94452/0.92768/0.91148/0.89588/0.88086/0.8664/0.85248/0.83906/0.82612/0.81364/0.8016/0.78996/0.7787/0.76781/0.75724/0.74699/0.73701/0.7273/0.71782/0.70854/0.69945/0.69052/0.68172/0.67303/0.66442/0.65587/0.64735/0.63885/0.63032/0.62176/0.61313/0.6044/0.59554/0.58637/0.57689/0.56711/0.55705/0.54672/0.53614/0.52531/0.51427/0.50301/0.49156/0.47993/0.46814/0.4562/0.44412/0.43193/0.41963/0.40724/0.39478/0.38226/0.36969/0.3571/0.34449/0.33188/0.31929/0.30673/0.29421/0.28176/0.26938/0.25709/0.24491/0.23284/0.22091/0.20914/0.19752/0.18609/0.17486/0.16383/0.15303/0.14247/0.13216/0.12213/0.11238/0.10293/0.09379/0.08499/0.07653/0.06843/0.0607/0.05337/0.04644/0.03993/0.03385/0.02823/0.02307/0.01838/0.0142/0.01052/0.00737/0.00476/0.0027/0.00121/0.0003/0
    - SizeMultiplyLife: 1/1/1/1.31636/1.31636/1.31636/1.5607/1.5607/1.5607/1.74153/1.74153/1.74153/1.86737/1.86737/1.86737/1.94672/1.94672/1.94672/1.9881/1.9881/1.9881/2/2/2
    - Acceleration: 0/0/20/0/50/25
    - Spawn: rate 0, bursts 7@0s
    - Spawn: rate 0, bursts 7@0s
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `M_SmokeBall` → E:\Tools\deagle-work\ron_fx\particles\T_SmokeBall_01_8_8.png (1024x1024,DXT5)
  - material `Squib_one` → E:\Tools\deagle-work\ron_fx\particles\Squib_one_1024_8x8.png (1024x1024,DXT5), E:\Tools\deagle-work\ron_fx\particles\Squib_one_normal.png (256x256,BC5)
- **P_Bullet_Impact_Wood_Big** (Cascade, `/Game/ReadyOrNot/VFX/VFXImpacts/Edit/P_Bullet_Impact_Wood_Big`)
  - emitter `Specks`
    - Required: material `M_ParticleEmberSimple`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 3-75@0s
    - Lifetime: 0.25/2 s
    - Size: 0.25/2/0.5/1.5/4/3
    - Velocity: -400/-400/250/400/400/1200 radial 0
    - Acceleration: -
    - SizeMultiplyLife: 1/10/1/1.00187/9.92883/1.00168/1.00704/9.72612/1.00634/1.0149/9.40717/1.01342/1.02481/8.98732/1.02234/1.03616/8.48189/1.03256/1.04832/7.90619/1.04351/1.06066/7.27556/1.05462/1.07257/6.60532/1.06534/1.08341/5.91078/1.0751/1.09256/5.20728/1.08334/1.0994/4.51013/1.0895/1.10331/3.83466/1.09302/1.10366/3.19619/1.09333/1.09982/2.61005/1.08988/1.09118/2.09155/1.0821/1.0771/1.65602/1.06942/1.05697/1.31879/1.05129/1.03015/1.09517/1.02715/0.99823/0.99845/0.9981/0.97506/0.97506/0.97506/0.95238/0.95238/0.95238/0.92971/0.92971/0.92971/0.90703/0.90703/0.90703/0.88435/0.88435/0.88435/0.86168/0.86168/0.86168/0.839/0.839/0.839/0.81633/0.81633/0.81633/0.79365/0.79365/0.79365/0.77098/0.77098/0.77098/0.7483/0.7483/0.7483/0.72562/0.72562/0.72562/0.70295/0.70295/0.70295/0.68027/0.68027/0.68027/0.6576/0.6576/0.6576/0.63492/0.63492/0.63492/0.61224/0.61224/0.61224/0.58957/0.58957/0.58957/0.56689/0.56689/0.56689/0.54422/0.54422/0.54422/0.52154/0.52154/0.52154/0.49887/0.49887/0.49887/0.47619/0.47619/0.47619/0.45351/0.45351/0.45351/0.43084/0.43084/0.43084/0.40816/0.40816/0.40816/0.38549/0.38549/0.38549/0.36281/0.36281/0.36281/0.34014/0.34014/0.34014/0.31746/0.31746/0.31746/0.29478/0.29478/0.29478/0.27211/0.27211/0.27211/0.24943/0.24943/0.24943/0.22676/0.22676/0.22676/0.20408/0.20408/0.20408/0.18141/0.18141/0.18141/0.15873/0.15873/0.15873/0.13605/0.13605/0.13605/0.11338/0.11338/0.11338/0.0907/0.0907/0.0907/0.06803/0.06803/0.06803/0.04535/0.04535/0.04535/0.02268/0.02268/0.02268/0/0/0
    - Color: color 1/1/1 alpha 0.1/1
    - TypeData Gpu: CameraMotionBlurAmount=1.0
  - emitter `Dust`
    - Required: material `M_Smoke1`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 15@0s
    - Lifetime: 0.5/3 s
    - Size: 1/1/1/3/3/3
    - Velocity: -300/-300/100/300/300/1000 radial 0
    - Color: color 1/1/1 alpha 0.5/1
    - ColorOverLife: color 1/1/1/0/0/0 alpha 1/0
    - Acceleration: -100/-100/-490/100/100/-100
  - emitter `SmokeKick`
    - Required: material `MI_DustImpact`, subUV 16x2, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1-1@0s, 1@0.05s
    - Lifetime: 0.25/0.66 s
    - Size: 35/35/35/135/135/135
    - Color: color 1/1/1 alpha 0.15/0.35
    - ColorOverLife: color 1/0.82823/0.75 alpha 1/0.78552/0.66621/0.61749/0.6148/0.63355/0.64918/0.62903/0.60282/0.57661/0.5504/0.52419/0.49798/0.47177/0.44556/0.41935/0.39315/0.36694/0.34073/0.31452/0.28831/0.2621/0.23589/0.20968/0.18347/0.15726/0.13105/0.10484/0.07863/0.05242/0.02621/0
    - SizeMultiplyLife: 0.5/0.65/0/0.70183/0.8599/0.29926/0.79216/0.92419/0.48216/0.85787/0.95566/0.63113/0.91492/0.9774/0.77143/0.95991/0.991/0.88904/0.98941/0.99801/0.96991/1/1/1
    - Acceleration: 0/0/-3/0/0/-1
  - emitter `ShootSpatter`
    - Required: material `MI_DebrisSpray`, subUV 4x1, localSpace True, duration 1(def), loops 1
    - Spawn: rate 0, bursts 2-2@0s, 1-1@0.03s, 1@0.06s
    - Lifetime: 0.075/0.175 s
    - Size: 40/40/40/15/15/15/20/20/20/5/5/5
    - SizeMultiplyLife: 0.1/0.5/0/0.55545/0.8941/0.16526/1.06671/1.25753/0.35085/1.59206/1.58702/0.54007/2.08979/1.8793/0.71627/2.51817/2.13107/0.86277/2.83548/2.33906/0.96291/3/2.5/1
    - ColorOverLife: color 0.75/0.66641/0.6 alpha 1/0.64605/0.41501/0.33
    - Acceleration: 0/0/-9.8
  - emitter `BaseDust`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.33/1 s
    - Size: 60/150/150/120/200/200
    - SizeMultiplyLife: 0.25/0.25/0.25/0.60689/0.60689/0.60689/0.88575/0.88575/0.88575/1.04928/1.04928/1.04928/1.15849/1.15849/1.15849/1.23809/1.23809/1.23809/1.29199/1.29199/1.29199/1.32396/1.32396/1.32396/1.33786/1.33786/1.33786/1.3375/1.3375/1.3375/1.3267/1.3267/1.3267/1.30929/1.30929/1.30929/1.28909/1.28909/1.28909/1.26992/1.26992/1.26992/1.25563/1.25563/1.25563/1.25/1.25/1.25
    - ColorOverLife: color 1/0.82823/0.75 alpha 1.25/0.79933/0.38998/0.29073/0.22264/0.16611/0.12006/0.08341/0.05509/0.03404/0.01917/0.0094/0.00368/0.00092/5e-05/0
    - Acceleration: -10/-10/0/10/10/0
  - emitter `WoodShardsHighRes`
    - Required: material `Shards_Mat`, subUV 2x2, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts -
    - Lifetime: 2/4 s
    - Size: 0.5/3/3/5/8/8
    - Color: color 0.45312/0.35836/0.27376 alpha 1
    - Acceleration: -
    - SizeMultiplyLife: 1/1/1/0.76136/0.77299/0.79555/0.54724/0.56339/0.59472/0.36209/0.37759/0.40767/0.21035/0.22198/0.24454/0.09646/0.10292/0.11545/0.02486/0.0268/0.03056/0/0/0
    - TypeData Gpu: CameraMotionBlurAmount=100.0, bClearExistingParticlesOnInit=True
  - emitter `WoodShardsHighRes`
    - Required: material `Shards_Mat`, subUV 2x2, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts -
    - Lifetime: 2/4 s
    - Size: 0.5/5/7.8/4/8/8
    - Color: color 0.45312/0.35836/0.27376 alpha 1
    - Acceleration: -
    - SizeMultiplyLife: 1.5/1.5/1.5/1.23365/1.24528/1.26784/0.9481/0.96426/0.99559/0.66529/0.6808/0.71088/0.40714/0.41877/0.44133/0.19559/0.20205/0.21459/0.05256/0.0545/0.05827/0/0/0
    - TypeData Gpu: CameraMotionBlurAmount=100.0, bClearExistingParticlesOnInit=True
  - emitter `WoodShardsSmall`
    - Required: material `Shards_Mat`, subUV 2x2, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts -
    - Lifetime: 0.1/1 s
    - Size: 0.35/2/6/6/8/15
    - Velocity: -500/-500/-150/500/500/500 radial 0
    - Color: color 0.45312/0.35836/0.27376 alpha 1
    - Acceleration: -
    - SizeMultiplyLife: 1/1/1/0.97514/0.9732/0.96944/0.90354/0.89708/0.88455/0.78965/0.77802/0.75546/0.63791/0.62241/0.59233/0.45276/0.43661/0.40528/0.23864/0.22701/0.20445/0/0/0
    - TypeData Gpu: CameraMotionBlurAmount=100.0, bClearExistingParticlesOnInit=True
    - Acceleration: -
    - Acceleration: -
    - Acceleration: -
    - Acceleration: -
    - Acceleration: -
    - Acceleration: -
    - Color: color 0.45312/0.35836/0.27376 alpha 1
    - Color: color 0.45312/0.35836/0.27376 alpha 1
    - Color: color 0.45312/0.35836/0.27376 alpha 1
    - Color: color 0.45312/0.35836/0.27376 alpha 1
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - Spawn: rate 0, bursts 5@0s
  - material `MI_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_DustImpact` → E:\Tools\deagle-work\ron_fx\particles\T_dustImpact_16x4.png (512x128,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_ParticleEmberSimple` → E:\Tools\deagle-work\ron_fx\particles\T_Glow_A.png (512x512,DXT1)
  - material `M_Smoke1` → E:\Tools\deagle-work\ron_fx\particles\T_Cloud_ABC.png (512x512,DXT1)
  - material `Shards_Mat` → E:\Tools\deagle-work\ron_fx\particles\Shards.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\TilingNoise06_N.png (512x512,BC5)

### RON_Mahogany

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01`  | E:\Tools\deagle-work\ron_fx\decals\Wood_1_N.png (1024²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Wood_3_H.png (1024²,DXT5) |
| ImpactEffects_Pistol | `NS_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01`  | E:\Tools\deagle-work\ron_fx\decals\Wood_1_N.png (1024²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Wood_3_H.png (1024²,DXT5) |
| ShotgunImpactEffects | `NS_Bullet_Impact_Wood_Big` | `Decal_Impact_Inst_WoodHard`  | E:\Tools\deagle-work\ron_fx\decals\WoodHard_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\WoodHard_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Mahogany_Exit` | `Decal_Impact_Inst_WoodHard`  | E:\Tools\deagle-work\ron_fx\decals\WoodHard_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\WoodHard_N.png (256²,BC5) |
| RicochetImpactEffects | `P_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01`  | E:\Tools\deagle-work\ron_fx\decals\Wood_1_N.png (1024²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Wood_3_H.png (1024²,DXT5) |

- **NS_Bullet_Impact_Wood_Big**: see above
- **NS_Bullet_Impact_Wood_Big**: see above
- **NS_Bullet_Impact_Wood_Big**: see above
- **P_VFX_Impact_Mahogany_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Mahogany_Exit`)
  - emitter `WoodBits`
    - Required: material `Splinters2x2_Mat_Inst`, subUV 2x2, localSpace 0, duration 0.1, loops 1
    - Spawn: rate 0, bursts 2@0s
    - Lifetime: 0.5/1 s
    - Size: 10/2/2/3/10/10
    - Velocity: -155/-155/15/155/155/750 radial 0
    - ColorOverLife: color 0.6/0.51373/0.42745 alpha 1/1/1/1/1/1/1/1/1/1/1/1/0.99055/0.66667/0.33333/0
    - Acceleration: -
    - TypeData Gpu: 
  - emitter `Spattering`
    - Required: material `SpeckledNormal`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.1/0.15 s
    - Size: 40/40/40/50/50/50
    - Velocity: -10/-10/20/10/10/500 radial 0
    - ColorOverLife: color 0.28/0.23974/0.19948 alpha 2/2.10753/2.21505/2.32258/2.43011/2.53763/2.64516/2.75269/2.86022/2.96774/2.9755/2.91388/2.8238/2.70853/2.57129/2.4153/2.2438/2.05998/1.86708/1.66832/1.46691/1.26609/1.0691/0.87916/0.6995/0.53333/0.38387/0.25435/0.14798/0.06798/0.01758/0
    - SizeMultiplyLife: 0/0/0/0.28143/0.28143/0.28143/0.61828/0.61828/0.61828/0.99973/0.99973/0.99973/1.41447/1.41447/1.41447/1.85149/1.85149/1.85149/2.29963/2.29963/2.29963/2.74779/2.74779/2.74779/3.18486/3.18486/3.18486/3.59972/3.59972/3.59972/3.98122/3.98122/3.98122/4.31834/4.31834/4.31834/4.59981/4.59981/4.59981/4.81474/4.81474/4.81474/4.95169/4.95169/4.95169/5/5/5
    - Acceleration: -
  - emitter `None`
    - Required: material `Squib_one`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.25/0.5 s
    - Size: 75/25.48/75/125/125/125
    - Velocity: 0/0/210/0/0/255 radial 0
    - ColorOverLife: color 0.63542/0.43623/0.2085/0.70833/0.56981/0.38407 alpha 0.25/1.91629/1.86747/1.82006/1.77403/1.72935/1.68601/1.64397/1.60322/1.56372/1.52545/1.48838/1.4525/1.41778/1.38418/1.3517/1.32029/1.28994/1.26062/1.23231/1.20498/1.17861/1.15317/1.12864/1.10498/1.08219/1.06022/1.03907/1.01869/0.99908/0.98019/0.96201/0.94452/0.92768/0.91148/0.89588/0.88086/0.8664/0.85248/0.83906/0.82612/0.81364/0.8016/0.78996/0.7787/0.76781/0.75724/0.74699/0.73701/0.7273/0.71782/0.70854/0.69945/0.69052/0.68172/0.67303/0.66442/0.65587/0.64735/0.63885/0.63032/0.62176/0.61313/0.6044/0.59554/0.58637/0.57689/0.56711/0.55705/0.54672/0.53614/0.52531/0.51427/0.50301/0.49156/0.47993/0.46814/0.4562/0.44412/0.43193/0.41963/0.40724/0.39478/0.38226/0.36969/0.3571/0.34449/0.33188/0.31929/0.30673/0.29421/0.28176/0.26938/0.25709/0.24491/0.23284/0.22091/0.20914/0.19752/0.18609/0.17486/0.16383/0.15303/0.14247/0.13216/0.12213/0.11238/0.10293/0.09379/0.08499/0.07653/0.06843/0.0607/0.05337/0.04644/0.03993/0.03385/0.02823/0.02307/0.01838/0.0142/0.01052/0.00737/0.00476/0.0027/0.00121/0.0003/0
    - SizeMultiplyLife: 0.5/0.5/0.5/0.83358/0.83351/0.83358/1.14704/1.14676/1.14704/1.42901/1.42845/1.42901/1.66811/1.66725/1.66811/1.85298/1.85185/1.85298/1.97224/1.97091/1.97224/2.01454/2.01313/2.01454
    - Acceleration: 0/0/20/0/50/25
    - Spawn: rate 0, bursts 7@0s
    - Spawn: rate 0, bursts 7@0s
  - material `Splinters2x2_Mat_Inst` → E:\Tools\deagle-work\ron_fx\particles\Stones2x2.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\Gravel2x2_normal.png (512x512,BC5), E:\Tools\deagle-work\ron_fx\particles\Wood_Splinters2x2.png (1024x1024,DXT5)
  - material `SpeckledNormal` → E:\Tools\deagle-work\ron_fx\particles\Noise_Anisotropic2_height.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\Speckled_tile.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\Speckled_tile_2.png (520x520,DXT1), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5), E:\Tools\deagle-work\ron_fx\particles\TX_FX_SmokeCloud01.png (64x64,DXT5)
  - material `Squib_one` → E:\Tools\deagle-work\ron_fx\particles\Squib_one_1024_8x8.png (1024x1024,DXT5), E:\Tools\deagle-work\ron_fx\particles\Squib_one_normal.png (256x256,BC5)
- **P_Bullet_Impact_Wood_Big**: see above

### RON_Treewood

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01`  | E:\Tools\deagle-work\ron_fx\decals\Wood_1_N.png (1024²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Wood_3_H.png (1024²,DXT5) |
| ImpactEffects_Pistol | `NS_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01`  | E:\Tools\deagle-work\ron_fx\decals\Wood_1_N.png (1024²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Wood_3_H.png (1024²,DXT5) |
| ShotgunImpactEffects | `NS_Bullet_Impact_Wood_Big_Shotgun` | `Decal_Impact_Inst_WoodSoft`  | E:\Tools\deagle-work\ron_fx\decals\WoodSoft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\WoodSoft_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Tree_Exit` | `Decal_Impact_Inst_WoodSoft`  | E:\Tools\deagle-work\ron_fx\decals\WoodSoft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\WoodSoft_N.png (256²,BC5) |
| RicochetImpactEffects | `P_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01`  | E:\Tools\deagle-work\ron_fx\decals\Wood_1_N.png (1024²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Wood_3_H.png (1024²,DXT5) |

- **NS_Bullet_Impact_Wood_Big**: see above
- **NS_Bullet_Impact_Wood_Big**: see above
- **NS_Bullet_Impact_Wood_Big_Shotgun**: see above
- **P_VFX_Impact_Tree_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Tree_Exit`)
  - emitter `None`
    - Required: material `Smoke2_M`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.25/0.5 s
    - Size: 75/25.48/75/125/125/125
    - Velocity: 0/0/25/0/0/100 radial 0
    - ColorOverLife: color 0.63542/0.43623/0.2085/5/2/1 alpha 1/0.67067/0.50262/0.45479/0.4848/0.55103/0.61151/0.62436/0.55974/0.46575/0.3613/0.25546/0.15735/0.07599/0.02057/0
    - SizeMultiplyLife: 0.2/0.2/0.2/0.50527/0.50527/0.50527/0.72104/0.72104/0.72104/0.86281/0.86281/0.86281/0.94609/0.94609/0.94609/0.98638/0.98638/0.98638/0.99918/0.99918/0.99918/1/1/1
    - Acceleration: 0/0/20/0/50/25
  - emitter `Smokeballhit`
    - Required: material `M_SmokeBall`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.1/0.2 s
    - Size: 25/25/45/50/50/50
    - Velocity: -55/-55/0/55/55/300 radial 0
    - ColorOverLife: color 1/0.87004/0.69271/0.44792/0.37915/0.28695 alpha 0.5/0.7381/0.97619/0.81499/0.55134/0.28634/0.08192/0
    - SizeMultiplyLife: 0.5/1.5/1.5/0.69732/1.64191/1.64191/0.83471/1.63645/1.63645/0.92296/1.52936/1.52936/0.97283/1.36642/1.36642/0.99511/1.19338/1.19338/1.00057/1.05598/1.05598/1/1/1
    - Acceleration: 0/0/20/0/50/25
    - Spawn: rate 0, bursts 7@0s
    - Spawn: rate 0, bursts 7@0s
  - material `Smoke2_M` → E:\Tools\deagle-work\ron_fx\particles\Smoke1b_2K.png (2048x2048,DXT5)
  - material `M_SmokeBall` → E:\Tools\deagle-work\ron_fx\particles\T_SmokeBall_01_8_8.png (1024x1024,DXT5)
- **P_Bullet_Impact_Wood_Big**: see above

## metal

### RON_Steel

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_Bullet_Impact_Metal_Big` | `M_BulletImpact_Metal`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |
| ImpactEffects_Pistol | `NS_Bullet_Impact_Metal_Big_Shotgun` | `M_BulletImpact_Metal`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |
| ShotgunImpactEffects | `NS_Bullet_Impact_Metal_Big_Shotgun` | `Decal_Impact_Inst_Metal_Hard`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Hard_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Hard_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Steel_Exit` | `Decal_Impact_Inst_Metal_Hard`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Hard_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Hard_N.png (256²,BC5) |
| RicochetImpactEffects | `P_Bullet_Impact_Metal_Big` | `M_BulletImpact_Metal`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |

- **NS_Bullet_Impact_Metal_Big** (Niagara, `/Game/ReadyOrNot/VFX/VFXImpacts/Niagara/NS_Bullet_Impact_Metal_Big`)
  - `Core_Sparks_2`: GPUComputeSim, localSpace 0, bounds -25..25 / -25..25 / -25..25 | Sprite mat `MI_ParticleGeneric_Additive` subUV [4.0, 2.0] align VelocityAligned | curves: Curve_0: 0 → max 1 → 1 (57 samples); Curve_4: 0 → max 1 → 1 (57 samples); Curve_0: 0 → max 1 → 1 (57 samples); Curve_4: 0 → max 1 → 1 (57 samples)
  - `DEBRIS_HIGH_SETTINGS_0`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `M_MetalShards` subUV [2.0, 2.0] align VelocityAligned | curves: Curve_0: 1 → max 1.01662 → 0 (95 samples); Curve_4: 1 → max 1.01662 → 0 (95 samples); Curve_0: 1 → max 1.01662 → 0 (95 samples); Curve_4: 1 → max 1.01662 → 0 (95 samples)
  - `DEBRIS_LOW_SETTINGS_5`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `M_MetalShards` subUV [2.0, 2.0] align VelocityAligned | curves: Curve_0: 1 → max 1.01662 → 0 (95 samples); Curve_4: 1 → max 1.01662 → 0 (95 samples); Curve_0: 1 → max 1.01662 → 0 (95 samples); Curve_4: 1 → max 1.01662 → 0 (95 samples)
  - `Dust_15`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `MI_ParticleGeneric_MuzzlePuffFront` subUV [8.0, 4.0] align 0 | curves: Curve_0: 0.25 → max 2.5 → 2.5 (85 samples); Curve_1: 1 → max 1 → 0 (43 samples); Curve_8: 0.25 → max 2.5 → 2.5 (85 samples); Curve_9: 1 → max 1 → 0 (43 samples)
  - `Flash_13`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `MI_ParticleGeneric_Sparks_02` subUV [2.0, 2.0] align - | curves: Curve_0: 0.75 → max 0.75 → 0 (30 samples); Curve_4: 0.75 → max 0.75 → 0 (30 samples); Curve_0: 0.75 → max 0.75 → 0 (30 samples); Curve_4: 0.75 → max 0.75 → 0 (30 samples)
  - `MAIN_SPARKS_3`: GPUComputeSim, localSpace 0, bounds -25..25 / -25..25 / -25..25 | Sprite mat `MI_ParticleGeneric_Additive` subUV [4.0, 2.0] align VelocityAligned | curves: Curve_0: 0 → max 1 → 1 (57 samples); Curve_4: 0 → max 1 → 1 (57 samples); Curve_0: 0 → max 1 → 1 (57 samples); Curve_4: 0 → max 1 → 1 (57 samples)
  - `SECONDARY_SPARKS_4`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `MI_ParticleGeneric_Sparks_01` subUV [4.0, 4.0] align 0 | curves: Curve_0: 0 → max 0.29741 → 0 (30 samples); Curve_4: 0 → max 0.29741 → 0 (30 samples); Curve_0: 0 → max 0.29741 → 0 (30 samples); Curve_4: 0 → max 0.29741 → 0 (30 samples)
  - user params: User.Debris Color=[0.875, 0.875, 0.875, 1.0], User.Dust Color=[1.0, 1.0, 1.0, 1.0], User.Sparks Color A=[13909.3125, 828.87189, 71.0325, 1.0], User.Sparks Color B=[225.85823, 66.30975, 28.413, 1.0]
  - material `MI_ParticleGeneric_Additive` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_longSpark_02_8x2.png (128x256,DXT1)
  - material `MI_ParticleGeneric_MuzzlePuffFront` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_muzzlePuff_Front_01.png (1024x512,DXT5)
  - material `MI_ParticleGeneric_Sparks_01` → E:\Tools\deagle-work\ron_fx\particles\T_VFX_Sparks_01_B.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5)
  - material `MI_ParticleGeneric_Sparks_02` → E:\Tools\deagle-work\ron_fx\particles\T_VFX_Sparks_03_B.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5)
  - material `M_MetalShards` → E:\Tools\deagle-work\ron_fx\particles\T_WoodShard_OAH.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_WoodShard_normal.png (512x512,BC5)
- **NS_Bullet_Impact_Metal_Big_Shotgun** (Niagara, `/Game/ReadyOrNot/VFX/VFXImpacts/Niagara/NS_Bullet_Impact_Metal_Big_Shotgun`)
  - `Core_Sparks_2`: GPUComputeSim, localSpace 0, bounds -25..25 / -25..25 / -25..25 | Sprite mat `MI_ParticleGeneric_Additive` subUV [4.0, 2.0] align VelocityAligned | curves: Curve_3: 0 → max 1 → 1 (57 samples); Curve_3: 0 → max 1 → 1 (57 samples); VectorCurve_3: 5 → max 5 → 1 (294 samples); VectorCurve_3: 5 → max 5 → 1 (294 samples)
  - `DEBRIS_HIGH_SETTINGS_0`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `M_MetalShards` subUV [2.0, 2.0] align VelocityAligned | curves: Curve_3: 1 → max 1.01662 → 0 (95 samples); Curve_3: 1 → max 1.01662 → 0 (95 samples)
  - `DEBRIS_LOW_SETTINGS_5`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `M_MetalShards` subUV [2.0, 2.0] align VelocityAligned | curves: Curve_3: 1 → max 1.01662 → 0 (95 samples); Curve_3: 1 → max 1.01662 → 0 (95 samples)
  - `Flash_13`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `MI_ParticleGeneric_Sparks_02` subUV [2.0, 2.0] align - | curves: Curve_3: 0.75 → max 0.75 → 0 (30 samples); Curve_3: 0.75 → max 0.75 → 0 (30 samples)
  - `MAIN_SPARKS_3`: GPUComputeSim, localSpace 0, bounds -25..25 / -25..25 / -25..25 | Sprite mat `MI_ParticleGeneric_Additive` subUV [4.0, 2.0] align VelocityAligned | curves: Curve_3: 0 → max 1 → 1 (57 samples); Curve_3: 0 → max 1 → 1 (57 samples); Vector2DCurve_3: 0.1 → max 0.99887 → 0.75 (106 samples); Vector2DCurve_3: 0.1 → max 0.99887 → 0.75 (106 samples)
  - `SECONDARY_SPARKS_4`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `MI_ParticleGeneric_Sparks_01` subUV [4.0, 4.0] align 0 | curves: Curve_3: 0 → max 0.29741 → 0 (30 samples); Curve_3: 0 → max 0.29741 → 0 (30 samples)
  - user params: User.Debris Color=[0.875, 0.875, 0.875, 1.0], User.Dust Color=[1.0, 1.0, 1.0, 1.0], User.Sparks Color A=[13909.3125, 828.87189, 71.0325, 1.0], User.Sparks Color B=[225.85823, 66.30975, 28.413, 1.0]
  - material `MI_ParticleGeneric_Additive` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_longSpark_02_8x2.png (128x256,DXT1)
  - material `MI_ParticleGeneric_Sparks_01` → E:\Tools\deagle-work\ron_fx\particles\T_VFX_Sparks_01_B.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5)
  - material `MI_ParticleGeneric_Sparks_02` → E:\Tools\deagle-work\ron_fx\particles\T_VFX_Sparks_03_B.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5)
  - material `M_MetalShards` → E:\Tools\deagle-work\ron_fx\particles\T_WoodShard_OAH.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_WoodShard_normal.png (512x512,BC5)
- **NS_Bullet_Impact_Metal_Big_Shotgun**: see above
- **P_VFX_Impact_Steel_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/Exit/P_VFX_Impact_Steel_Exit`)
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 15@0s
    - Lifetime: 0.35/0.5 s
    - Size: 1/1/1.5
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Velocity: -50/-50/0/50/50/0 radial 0
    - Acceleration: -
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `Smoke_Impact`
    - Required: material `M_SmokeBall`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.5/0.65 s
    - Size: 85/85/85/100/100/100
    - Velocity: -10/-10/20/10/10/255 radial 0
    - ColorOverLife: color 0.58854/0.58854/0.58854/0.26042/0.26042/0.26042 alpha 0.5/0.5/0.5/0.47205/0.39113/0.31649/0.24045/0
    - SizeMultiplyLife: 0.2/0.2/0.2/0.52678/0.52678/0.52678/0.78501/0.78501/0.78501/0.98116/0.98116/0.98116/1.12237/1.12237/1.12237/1.21528/1.21528/1.21528/1.26687/1.26687/1.26687/1.28393/1.28393/1.28393/1.27333/1.27333/1.27333/1.24193/1.24193/1.24193/1.19655/1.19655/1.19655/1.14407/1.14407/1.14407/1.09132/1.09132/1.09132/1.04516/1.04516/1.04516/1.01246/1.01246/1.01246/1/1/1
    - Acceleration: -
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 50@0s
    - Lifetime: 0.03/0.05 s
    - Size: 1/2/1
    - ColorOverLife: color 50/19/0.14464/15/5.45346/1.99438 alpha 1/0
    - Acceleration: -
    - SizeMultiplyLife: 2/2/1/2.1811/2.1811/1/2.3622/2.3622/1/2.54331/2.54331/1/2.72441/2.72441/1/2.90551/2.90551/1/3.08661/3.08661/1/3.26772/3.26772/1/3.44882/3.44882/1/3.62992/3.62992/1/3.81102/3.81102/1/3.99213/3.99213/1/4.17323/4.17323/1/4.35433/4.35433/1/4.53543/4.53543/1/4.71654/4.71654/1/4.89764/4.89764/1/5.07874/5.07874/1/5.25984/5.25984/1/5.44094/5.44094/1/5.62205/5.62205/1/5.80315/5.80315/1/5.98425/5.98425/1/6.16535/6.16535/1/6.34646/6.34646/1/6.52756/6.52756/1/6.70866/6.70866/1/6.88976/6.88976/1/7.07087/7.07087/1/7.25197/7.25197/1/7.43307/7.43307/1/7.61417/7.61417/1/7.79528/7.79528/1/7.97638/7.97638/1/8.15748/8.15748/1/8.33858/8.33858/1/8.51968/8.51968/1/8.70079/8.70079/1/8.88189/8.88189/1/9.06299/9.06299/1/9.24409/9.24409/1/9.4252/9.4252/1/9.6063/9.6063/1/9.7874/9.7874/1/9.9685/9.9685/1/10.1496/10.1496/1/10.3307/10.3307/1/10.5118/10.5118/1/10.6929/10.6929/1/10.874/10.874/1/11.0551/11.0551/1/11.2362/11.2362/1/11.4173/11.4173/1/11.5984/11.5984/1/11.7795/11.7795/1/11.9606/11.9606/1/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2
    - Velocity: 0/0/50/0/0/25 radial 0
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 8@0s
    - Lifetime: 0.03/0.4 s
    - Size: 1/2/1
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Acceleration: -
    - SizeMultiplyLife: -
    - Velocity: 0/0/50/0/0/25 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 55@0s
    - Lifetime: 0.1/0.5 s
    - Size: 3/3/3
    - Acceleration: -
    - ColorOverLife: color 0.89062/0.11809/0.08264/0.95312/0.33233/0.27537 alpha 1/0
    - Velocity: -50/-50/0/50/50/0 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `Bits`
    - Required: material `Shards_Mat`, subUV 2x2, localSpace 0, duration 0.1, loops 1
    - Spawn: rate 0, bursts 5@0s
    - Lifetime: 1/3 s
    - Size: 2/1/1/3/3/3
    - Velocity: 0/0/0/150/150/250 radial 0
    - ColorOverLife: color 0.84/0.84/0.84 alpha 1/1/1/1/1/1/1/1/1/1/1/1/0.99055/0.66667/0.33333/0
    - Acceleration: -
    - TypeData Gpu: 
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Spawn: rate 0, bursts 20@0s
    - Spawn: rate 0, bursts 25@0s
    - Spawn: rate 0, bursts 25@0s
    - Spawn: rate 0, bursts 20@0s
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `Shards_Mat` → E:\Tools\deagle-work\ron_fx\particles\Shards.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\TilingNoise06_N.png (512x512,BC5)
  - material `M_SmokeBall` → E:\Tools\deagle-work\ron_fx\particles\T_SmokeBall_01_8_8.png (1024x1024,DXT5)
- **P_Bullet_Impact_Metal_Big** (Cascade, `/Game/ReadyOrNot/VFX/VFXImpacts/EditV2/P_Bullet_Impact_Metal_Big`)
  - emitter `SparkFlash`
    - Required: material `MI_SparkSpray`, subUV 4x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 0@0s
    - Lifetime: 0.015/0.03 s
    - Size: 25/25/25/45/45/45
    - SizeMultiplyLife: 0.25/0.25/0.25/1/1/1
    - Color: color 200/30/0.4 alpha 0.1/1
  - emitter `Sparks_02`
    - Required: material `MI_Sparks_02`, subUV 2x2, localSpace True, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.1/0.2 s
    - Size: 10/100/100/55/150/150
    - Velocity: -500/-500/0/500/500/500 radial 0
    - ColorOverLife: color 13909.3/828.872/71.0325 alpha 0.75/0.6/0.45/0.30354/0.275/0.25/0.225/0.2/0.175/0.15/0.125/0.1/0.075/0.05/0.025/0
  - emitter `FastSparks`
    - Required: material `M_Particle_Additive`, subUV 4x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts -
    - Lifetime: 0.05/0.15 s
    - Acceleration: -100/-100/-980/100/100/-980
    - Size: 0.5/0.5/0.5/2/2/2
    - SizeMultiplyLife: 0.1/0.5/0/0.19524/0.60582/0.10582/0.29048/0.71164/0.21164/0.38571/0.81746/0.31746/0.48095/0.92328/0.42328/0.57619/1.0291/0.5291/0.67143/1.13492/0.63492/0.76667/1.24074/0.74074/0.8619/1.34656/0.84656/0.95714/1.45238/0.95238/0.9923/1.4923/1/0.97829/1.47829/1/0.96429/1.46429/1/0.95028/1.45028/1/0.93627/1.43627/1/0.92227/1.42227/1/0.90826/1.40826/1/0.89426/1.39426/1/0.88025/1.38025/1/0.86625/1.36625/1/0.85224/1.35224/1/0.83824/1.33824/1/0.82423/1.32423/1/0.81022/1.31022/1/0.79622/1.29622/1/0.78221/1.28221/1/0.76821/1.26821/1/0.7542/1.2542/1/0.7402/1.2402/1/0.72619/1.22619/1/0.71218/1.21218/1/0.69818/1.19818/1/0.68417/1.18417/1/0.67017/1.17017/1/0.65616/1.15616/1/0.64216/1.14216/1/0.62815/1.12815/1/0.61415/1.11415/1/0.60014/1.10014/1/0.58613/1.08613/1/0.57213/1.07213/1/0.55812/1.05812/1/0.54412/1.04412/1/0.53011/1.03011/1/0.51611/1.01611/1/0.5021/1.0021/1/0.4881/0.9881/1/0.47409/0.97409/1/0.46008/0.96008/1/0.44608/0.94608/1/0.43207/0.93207/1/0.41807/0.91807/1/0.40406/0.90406/1/0.39006/0.89006/1/0.37605/0.87605/1/0.36204/0.86204/1/0.34804/0.84804/1/0.33403/0.83403/1/0.32003/0.82003/1/0.30602/0.80602/1/0.29202/0.79202/1/0.27801/0.77801/1/0.26401/0.76401/1/0.25/0.75/1
    - Color: color 500/75/1 alpha 0.5/1
  - emitter `SmokeKick`
    - Required: material `MI_DustImpact`, subUV 16x2, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 2@0s
    - Lifetime: 0.125/0.4 s
    - Size: 45/45/45/100/100/100
    - Color: color 1/1/1 alpha 0.25/0.5
    - ColorOverLife: color 1/1/1 alpha 1.5/1.13072/0.72118/0.50414/0.48387/0.46595/0.44803/0.43011/0.41219/0.39427/0.37634/0.35842/0.3405/0.32258/0.30466/0.28674/0.26882/0.2509/0.23297/0.21505/0.19713/0.17921/0.16129/0.14337/0.12545/0.10753/0.08961/0.07168/0.05376/0.03584/0.01792/0
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - Acceleration: 0/0/-3/0/0/-1
  - emitter `MetalBits_HighRes`
    - Required: material `M_MetalShards`, subUV 2x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 3-25@0s
    - Lifetime: 2/4 s
    - Size: 0.5/1/3/4/7/7
    - Velocity: -800/-800/0/800/800/100 radial 0
    - Acceleration: -
    - SizeMultiplyLife: 1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/0.95013/0.95013/0.95013/0.79365/0.79365/0.79365/0.63492/0.63492/0.63492/0.47619/0.47619/0.47619/0.31746/0.31746/0.31746/0.15873/0.15873/0.15873/0/0/0
    - Color: color 0.22917/0.22917/0.22917/0.875/0.875/0.875 alpha 1
    - TypeData Gpu: 
  - emitter `TinySparks`
    - Required: material `M_Particle_Additive`, subUV 4x2, localSpace 0, duration 0.075, loops 1
    - Spawn: rate 400/600, bursts 0@0s
    - Lifetime: 0.05/0.3 s
    - Acceleration: -100/-100/-980/100/100/-980
    - Size: 0.5/0.5/0.5/1/1/1
    - Color: color 500/75/1 alpha 0.5/1
  - emitter `Sparks_01`
    - Required: material `MI_Sparks_01`, subUV 4x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0.05s
    - Lifetime: 0.3/0.5 s
    - Size: 25/100/100/150/150/150
    - Velocity: -250/-250/0/250/250/500 radial 0
    - ColorOverLife: color 222.549/66.3097/28.413 alpha 0/0.1/0.2/0.29646/0.275/0.25/0.225/0.2/0.175/0.15/0.125/0.1/0.075/0.05/0.025/0
    - Acceleration: 0/-50/20/0/-500/96
  - emitter `Sparks_SPatterB`
    - Required: material `MI_Sparks_01_Var1`, subUV 4x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.25/0.5 s
    - Size: 75/100/100/100/150/150
    - Velocity: 0/0/100/0/0/1000 radial 0
    - ColorOverLife: color 0/0/0/47.2441/8.0315/0/94.4882/16.063/0/141.732/24.0945/0/188.976/32.126/0/236.22/40.1575/0/283.465/48.189/0/330.709/56.2205/0/377.953/64.2519/0/425.197/72.2835/0/472.441/80.315/0/519.685/88.3465/0/566.929/96.3779/0/614.173/104.409/0/661.417/112.441/0/708.661/120.472/0/755.905/128.504/0/803.15/136.535/0/850.394/144.567/0/897.638/152.598/0/944.882/160.63/0/992.126/168.661/0/1039.37/176.693/0/1086.61/184.724/0/1133.86/192.756/0/1181.1/200.787/0/1228.35/208.819/0/1275.59/216.85/0/1322.83/224.882/0/1370.08/232.913/0/1417.32/240.945/0/1464.57/248.976/0/1496.06/254.331/0/1480.31/251.654/0/1464.57/248.976/0/1448.82/246.299/0/1433.07/243.622/0/1417.32/240.945/0/1401.57/238.268/0/1385.83/235.591/0/1370.08/232.913/0/1354.33/230.236/0/1338.58/227.559/0/1322.83/224.882/0/1307.09/222.205/0/1291.34/219.528/0/1275.59/216.85/0/1259.84/214.173/0/1244.09/211.496/0/1228.35/208.819/0/1212.6/206.142/0/1196.85/203.465/0/1181.1/200.787/0/1165.35/198.11/0/1149.61/195.433/0/1133.86/192.756/0/1118.11/190.079/0/1102.36/187.402/0/1086.61/184.724/0/1070.87/182.047/0/1055.12/179.37/0/1039.37/176.693/0/1023.62/174.016/0/1007.87/171.339/0/992.126/168.661/0/976.378/165.984/0/960.63/163.307/0/944.882/160.63/0/929.134/157.953/0/913.386/155.276/0/897.638/152.598/0/881.89/149.921/0/866.142/147.244/0/850.394/144.567/0/834.646/141.89/0/818.898/139.213/0/803.15/136.535/0/787.402/133.858/0/771.654/131.181/0/755.906/128.504/0/740.158/125.827/0/724.41/123.15/0/708.662/120.472/0/692.913/117.795/0/677.165/115.118/0/661.417/112.441/0/645.669/109.764/0/629.921/107.087/0/614.173/104.409/0/598.425/101.732/0/582.677/99.0551/0/566.929/96.378/0/551.181/93.7008/0/535.433/91.0237/0/519.685/88.3465/0/503.937/85.6693/0/488.189/82.9921/0/472.441/80.315/0/456.693/77.6378/0/440.945/74.9607/0/425.197/72.2835/0/409.449/69.6063/0/393.701/66.9291/0/377.953/64.252/0/362.205/61.5748/0/346.457/58.8977/0/330.709/56.2205/0/314.961/53.5433/0/299.213/50.8661/0/283.465/48.189/0/267.717/45.5118/0/251.969/42.8347/0/236.221/40.1575/0/220.473/37.4803/0/204.725/34.8032/0/188.977/32.126/0/173.228/29.4488/0/157.481/26.7717/0/141.732/24.0945/0/125.984/21.4173/0/110.236/18.7402/0/94.4883/16.063/0/78.7405/13.3859/0/62.9923/10.7087/0/47.2441/8.03151/0/31.4963/5.35437/0/15.7482/2.67719/0/0/0/0 alpha 0/0.17143/0.28571/0.22857/0.17143/0.11429/0.05714/0
  - emitter `SlowSparks`
    - Required: material `M_Particle_Additive`, subUV 4x2, localSpace 0, duration 0.025, loops 1
    - Spawn: rate 0, bursts 0@0s
    - Lifetime: 0.01/0.4 s
    - Color: color 500/75/1 alpha 0.5/1
    - Size: 1/1/1/4/4/4
    - SizeMultiplyLife: 0.1/0.5/0/0.19524/0.55291/0.10582/0.29048/0.60582/0.21164/0.38571/0.65873/0.31746/0.48095/0.71164/0.42328/0.57619/0.76455/0.5291/0.67143/0.81746/0.63492/0.76667/0.87037/0.74074/0.8619/0.92328/0.84656/0.95714/0.97619/0.95238/0.9923/0.99743/0.99486/0.97829/0.99276/0.98553/0.96429/0.9881/0.97619/0.95028/0.98343/0.96685/0.93627/0.97876/0.95752/0.92227/0.97409/0.94818/0.90826/0.96942/0.93884/0.89426/0.96475/0.92951/0.88025/0.96008/0.92017/0.86625/0.95542/0.91083/0.85224/0.95075/0.90149/0.83824/0.94608/0.89216/0.82423/0.94141/0.88282/0.81022/0.93674/0.87348/0.79622/0.93207/0.86415/0.78221/0.9274/0.85481/0.76821/0.92274/0.84547/0.7542/0.91807/0.83613/0.7402/0.9134/0.8268/0.72619/0.90873/0.81746/0.71218/0.90406/0.80812/0.69818/0.89939/0.79879/0.68417/0.89472/0.78945/0.67017/0.89006/0.78011/0.65616/0.88539/0.77077/0.64216/0.88072/0.76144/0.62815/0.87605/0.7521/0.61415/0.87138/0.74276/0.60014/0.86671/0.73343/0.58613/0.86204/0.72409/0.57213/0.85738/0.71475/0.55812/0.85271/0.70542/0.54412/0.84804/0.69608/0.53011/0.84337/0.68674/0.51611/0.8387/0.6774/0.5021/0.83403/0.66807/0.4881/0.82937/0.65873/0.47409/0.8247/0.64939/0.46008/0.82003/0.64006/0.44608/0.81536/0.63072/0.43207/0.81069/0.62138/0.41807/0.80602/0.61204/0.40406/0.80135/0.60271/0.39006/0.79669/0.59337/0.37605/0.79202/0.58403/0.36204/0.78735/0.5747/0.34804/0.78268/0.56536/0.33403/0.77801/0.55602/0.32003/0.77334/0.54669/0.30602/0.76867/0.53735/0.29202/0.76401/0.52801/0.27801/0.75934/0.51867/0.26401/0.75467/0.50934/0.25/0.75/0.5
    - Acceleration: -100/-100/-980/100/100/-980
  - emitter `BaseDust`
    - Required: material `MI_Muzzle_Puff_Front`, subUV 8x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 2@0s
    - Lifetime: 0.15/1.75 s
    - Size: 35/150/150/125/200/200
    - SizeMultiplyLife: 0.25/0.25/0.25/0.40446/0.40446/0.40446/0.62922/0.62922/0.62922/0.88675/0.88675/0.88675/1.1395/1.1395/1.1395/1.34994/1.34994/1.34994/1.48067/1.48067/1.48067/1.52902/1.52902/1.52902/1.56959/1.56959/1.56959/1.60909/1.60909/1.60909/1.64753/1.64753/1.64753/1.68494/1.68494/1.68494/1.72131/1.72131/1.72131/1.75667/1.75667/1.75667/1.79102/1.79102/1.79102/1.82439/1.82439/1.82439/1.85677/1.85677/1.85677/1.88818/1.88818/1.88818/1.91864/1.91864/1.91864/1.94816/1.94816/1.94816/1.97674/1.97674/1.97674/2.00441/2.00441/2.00441/2.03117/2.03117/2.03117/2.05704/2.05704/2.05704/2.08203/2.08203/2.08203/2.10615/2.10615/2.10615/2.12941/2.12941/2.12941/2.15182/2.15182/2.15182/2.17341/2.17341/2.17341/2.19417/2.19417/2.19417/2.21412/2.21412/2.21412/2.23328/2.23328/2.23328/2.25166/2.25166/2.25166/2.26927/2.26927/2.26927/2.28612/2.28612/2.28612/2.30222/2.30222/2.30222/2.31759/2.31759/2.31759/2.33223/2.33223/2.33223/2.34617/2.34617/2.34617/2.35941/2.35941/2.35941/2.37196/2.37196/2.37196/2.38385/2.38385/2.38385/2.39507/2.39507/2.39507/2.40564/2.40564/2.40564/2.41558/2.41558/2.41558/2.4249/2.4249/2.4249/2.4336/2.4336/2.4336/2.44171/2.44171/2.44171/2.44923/2.44923/2.44923/2.45617/2.45617/2.45617/2.46255/2.46255/2.46255/2.46839/2.46839/2.46839/2.47368/2.47368/2.47368/2.47846/2.47846/2.47846/2.48271/2.48271/2.48271/2.48647/2.48647/2.48647/2.48974/2.48974/2.48974/2.49254/2.49254/2.49254/2.49487/2.49487/2.49487/2.49675/2.49675/2.49675/2.49819/2.49819/2.49819/2.4992/2.4992/2.4992/2.4998/2.4998/2.4998/2.5/2.5/2.5
    - ColorOverLife: color 1/1/1 alpha 1/0.76776/0.50608/0.3058/0.24444/0.22222/0.2/0.17778/0.15556/0.13333/0.11111/0.08889/0.06667/0.04444/0.02222/0
    - Acceleration: -40/-40/-40/40/40/40
    - Spawn: rate 0, bursts 5@0s
  - material `MI_DustImpact` → E:\Tools\deagle-work\ron_fx\particles\T_dustImpact_16x4.png (512x128,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_Muzzle_Puff_Front` → E:\Tools\deagle-work\ron_fx\particles\T_muzzlePuff_Front_01.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_SparkSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_longSpark_02_8x2.png (128x256,DXT1)
  - material `M_MetalShards` → E:\Tools\deagle-work\ron_fx\particles\T_WoodShard_OAH.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_WoodShard_normal.png (512x512,BC5)
  - material `M_Particle_Additive` → E:\Tools\deagle-work\ron_fx\particles\T_longSpark_02_8x2.png (128x256,DXT1)
  - material `MI_Sparks_01` → E:\Tools\deagle-work\ron_fx\particles\T_VFX_Sparks_01_B.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
  - material `MI_Sparks_01_Var1` → E:\Tools\deagle-work\ron_fx\particles\T_VFX_Sparks_01_B.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
  - material `MI_Sparks_02` → E:\Tools\deagle-work\ron_fx\particles\T_VFX_Sparks_03_B.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)

### RON_Aluminium

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_Bullet_Impact_Metal_Big` | `M_BulletImpact_Metal`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |
| ImpactEffects_Pistol | `NS_Bullet_Impact_Metal_Big` | `M_BulletImpact_Metal`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |
| ShotgunImpactEffects | `NS_Bullet_Impact_Metal_Big_Shotgun` | `M_BulletImpact_Metal`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Aluminium_Exit` | `M_BulletImpact_Metal`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |
| RicochetImpactEffects | `P_Bullet_Impact_Metal_Big` | `M_BulletImpact_Metal`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |

- **NS_Bullet_Impact_Metal_Big**: see above
- **NS_Bullet_Impact_Metal_Big**: see above
- **NS_Bullet_Impact_Metal_Big_Shotgun**: see above
- **P_VFX_Impact_Aluminium_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/Exit/P_VFX_Impact_Aluminium_Exit`)
  - emitter `Fireball`
    - Required: material `MI_Impact_Smoke_01`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.1/0.15 s
    - Size: 85/85/85/100/100/100
    - Velocity: -10/-10/100/10/10/350 radial 0
    - ColorOverLife: color 25555/0.0458/0/0.26042/0.26042/0.26042 alpha 0.2/0.49867/0.39286/0.3
    - SizeMultiplyLife: 0.25/0.25/0.25/3/3/3
    - Acceleration: -
  - emitter `Smoke_Impact`
    - Required: material `MI_Impact_Smoke_04`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.3/0.5 s
    - Size: 85/85/85/100/100/100
    - Velocity: -10/-10/100/10/10/350 radial 0
    - ColorOverLife: color 0.58854/0.58854/0.58854/0.26042/0.26042/0.26042 alpha 2/1.7619/1.52381/1.36669/0.99828/0.54571/0.16196/0
    - SizeMultiplyLife: 0.1/0.1/0.1/8/8/8
    - Acceleration: -
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 5@0s
    - Lifetime: 0.03/0.2 s
    - Size: 1/2/1
    - ColorOverLife: color 1500/500/500/150/5.45346/1.99438 alpha 1/0
    - Acceleration: -
    - SizeMultiplyLife: 2/2/1/2.1811/2.1811/1/2.3622/2.3622/1/2.54331/2.54331/1/2.72441/2.72441/1/2.90551/2.90551/1/3.08661/3.08661/1/3.26772/3.26772/1/3.44882/3.44882/1/3.62992/3.62992/1/3.81102/3.81102/1/3.99213/3.99213/1/4.17323/4.17323/1/4.35433/4.35433/1/4.53543/4.53543/1/4.71654/4.71654/1/4.89764/4.89764/1/5.07874/5.07874/1/5.25984/5.25984/1/5.44094/5.44094/1/5.62205/5.62205/1/5.80315/5.80315/1/5.98425/5.98425/1/6.16535/6.16535/1/6.34646/6.34646/1/6.52756/6.52756/1/6.70866/6.70866/1/6.88976/6.88976/1/7.07087/7.07087/1/7.25197/7.25197/1/7.43307/7.43307/1/7.61417/7.61417/1/7.79528/7.79528/1/7.97638/7.97638/1/8.15748/8.15748/1/8.33858/8.33858/1/8.51968/8.51968/1/8.70079/8.70079/1/8.88189/8.88189/1/9.06299/9.06299/1/9.24409/9.24409/1/9.4252/9.4252/1/9.6063/9.6063/1/9.7874/9.7874/1/9.9685/9.9685/1/10.1496/10.1496/1/10.3307/10.3307/1/10.5118/10.5118/1/10.6929/10.6929/1/10.874/10.874/1/11.0551/11.0551/1/11.2362/11.2362/1/11.4173/11.4173/1/11.5984/11.5984/1/11.7795/11.7795/1/11.9606/11.9606/1/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2/2
    - Velocity: 0/0/50/0/0/25 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 3@0s
    - Lifetime: 0.3/0.35 s
    - Size: 1/8/1.5
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Velocity: -50/-50/0/50/50/0 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Spawn: rate 0, bursts 20@0s
    - Spawn: rate 0, bursts 25@0s
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `MI_Impact_Smoke_01` → E:\Tools\deagle-work\ron_fx\particles\Fire2c_2k.png (2048x2048,DXT5), E:\Tools\deagle-work\ron_fx\particles\RandomNormal2.png (32x32,B8G8R8A8), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
  - material `MI_Impact_Smoke_04` → E:\Tools\deagle-work\ron_fx\particles\Cloud04_8x8.png (1024x1024,DXT5), E:\Tools\deagle-work\ron_fx\particles\WispySmoke03b_8x8.png (1024x1024,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
- **P_Bullet_Impact_Metal_Big**: see above

### RON_Galvanized

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_Bullet_Impact_Metal_Big` | `M_BulletImpact_Metal`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |
| ImpactEffects_Pistol | `NS_Bullet_Impact_Metal_Big` | `M_BulletImpact_Metal`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |
| ShotgunImpactEffects | `NS_Bullet_Impact_Metal_Big_Shotgun` | `Decal_Impact_Inst_Metal_Galvanized`  | E:\Tools\deagle-work\ron_fx\decals\Galvanized_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Galvanized_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Galvanized_Exit` | `Decal_Impact_Inst_Metal_Galvanized`  | E:\Tools\deagle-work\ron_fx\decals\Galvanized_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Galvanized_N.png (256²,BC5) |
| RicochetImpactEffects | `P_Bullet_Impact_Metal_Big` | `M_BulletImpact_Metal`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |

- **NS_Bullet_Impact_Metal_Big**: see above
- **NS_Bullet_Impact_Metal_Big**: see above
- **NS_Bullet_Impact_Metal_Big_Shotgun**: see above
- **P_VFX_Impact_Galvanized_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Galvanized_Exit`)
  - emitter `Smoke_expand`
    - Required: material `Whisps_additive`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.4/0.5 s
    - Size: 50/250/250/300/600/100
    - Velocity: -50/-50/800/50/50/200 radial 0
    - ColorOverLife: color 0.835/0.835/0.835 alpha 0.43995/0.5416/0.78438/0.97828/0.98788/0.93842/0.85895/0.75676/0.63918/0.51354/0.38713/0.26729/0.16136/0.07659/0.02043/0
    - SizeMultiplyLife: 0/0/0/0.36048/0.36048/0.36048/0.74329/0.74329/0.74329/1.12008/1.12008/1.12008/1.4625/1.4625/1.4625/1.74219/1.74219/1.74219/1.93081/1.93081/1.93081/2/2/2
    - Acceleration: 0/250/20/0/-250/96.2069
  - emitter `Spark burst`
    - Required: material `M_Flare`, subUV 1x1, localSpace True, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.02/0.05 s
    - Size: 15/0/0/30/0/0
    - SizeMultiplyLife: 0.1/0.1/0.1/0.5663/0.5663/0.5663/0.9627/0.9627/0.9627/1.33265/1.33265/1.33265/1.68234/1.68234/1.68234/1.97633/1.97633/1.97633/2.17892/2.17892/2.17892/2.25441/2.25441/2.25441
    - ColorOverLife: color 250/150/100 alpha 1.5/0
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 5@0s
    - Lifetime: 0.1/0.15 s
    - Size: 2/2/2
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Acceleration: -
    - SizeMultiplyLife: -
    - TypeData Gpu: bClearExistingParticlesOnInit=True
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Spawn: rate 0, bursts 20@0s
    - Spawn: rate 0, bursts 7@0s
  - material `M_Flare` → E:\Tools\deagle-work\ron_fx\particles\Flare_star.png (64x64,DXT1)
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `Whisps_additive` → E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
- **P_Bullet_Impact_Metal_Big**: see above

### RON_Tin

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_VFX_Impact_Tin` | `M_BulletImpact_Metal`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |
| ImpactEffects_Pistol | `NS_VFX_Impact_Tin` | `M_BulletImpact_Metal`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |
| ShotgunImpactEffects | `NS_VFX_Impact_Tin_Shotgun` | `Decal_Impact_Inst_Metal_Tin`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Tin_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Tin_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Tin` | `Decal_Impact_Inst_Metal_Tin`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Tin_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Tin_N.png (256²,BC5) |
| RicochetImpactEffects | `P_VFX_Impact_Tin` | `M_BulletImpact_Metal`  | E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |

- **NS_VFX_Impact_Tin** (Niagara, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/Niagara/NS_VFX_Impact_Tin`)
  - `DEBRIS_HIGH_SETTINGS_0`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `Shards_Mat` subUV [2.0, 2.0] align 0 | curves: Curve_3: 90 → max 90 → 0 (126 samples); Curve_4: -90 → max 0 → 0 (125 samples); Curve_5: 1 → max 1 → 0 (102 samples); Curve_3: 90 → max 90 → 0 (126 samples)
  - `DEBRIS_LOW_SETTINGS_3`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `Shards_Mat` subUV [2.0, 2.0] align 0 | curves: Curve_3: 90 → max 90 → 0 (126 samples); Curve_4: -90 → max 0 → 0 (125 samples); Curve_5: 1 → max 1 → 0 (102 samples); Curve_3: 90 → max 90 → 0 (126 samples)
  - `Smoke_11`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ThinSmoke_01` subUV [8.0, 4.0] align 0 | curves: Curve_2: 0.75 → max 1.5 → 1.5 (45 samples); Curve_3: 1 → max 1 → 0 (53 samples); Curve_2: 0.75 → max 1.5 → 1.5 (45 samples); Curve_3: 1 → max 1 → 0 (53 samples)
  - `Sparks_13`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `M_Radial_Gradient` subUV - align VelocityAligned | curves: Curve_3: 0 → max 1 → 1 (57 samples); Curve_4: 1 → max 1 → 0 (57 samples); Curve_5: 1 → max 1 → 0 (57 samples); Curve_3: 0 → max 1 → 1 (57 samples)
  - user params: User.Debris Color=[1.0, 1.0, 1.0, 1.0], User.Smoke Color=[0.815, 0.815, 0.815, 1.0], User.Sparks Color=[50.0, 0.53968, 0.0, 1.0]
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `Shards_Mat` → E:\Tools\deagle-work\ron_fx\particles\Shards.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\TilingNoise06_N.png (512x512,BC5)
- **NS_VFX_Impact_Tin**: see above
- **NS_VFX_Impact_Tin_Shotgun** (Niagara, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/Niagara/NS_VFX_Impact_Tin_Shotgun`)
  - `DEBRIS_HIGH_SETTINGS_0`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `Shards_Mat` subUV [2.0, 2.0] align 0 | curves: Curve_6: 90 → max 90 → 0 (126 samples); Curve_7: -90 → max 0 → 0 (125 samples); Curve_8: 1 → max 1 → 0 (102 samples); Curve_6: 90 → max 90 → 0 (126 samples)
  - `DEBRIS_LOW_SETTINGS_3`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `Shards_Mat` subUV [2.0, 2.0] align 0 | curves: Curve_6: 90 → max 90 → 0 (126 samples); Curve_7: -90 → max 0 → 0 (125 samples); Curve_8: 1 → max 1 → 0 (102 samples); Curve_6: 90 → max 90 → 0 (126 samples)
  - `Smoke_11`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ThinSmoke_01` subUV [8.0, 4.0] align 0 | curves: Curve_6: 0.75 → max 1.5 → 1.5 (45 samples); Curve_7: 1 → max 1 → 0 (53 samples); Curve_6: 0.75 → max 1.5 → 1.5 (45 samples); Curve_7: 1 → max 1 → 0 (53 samples)
  - `Sparks_13`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `M_Radial_Gradient` subUV - align VelocityAligned | curves: Curve_6: 0 → max 1 → 1 (57 samples); Curve_7: 1 → max 1 → 0 (57 samples); Curve_8: 1 → max 1 → 0 (57 samples); Curve_6: 0 → max 1 → 1 (57 samples)
  - user params: User.Debris Color=[1.0, 1.0, 1.0, 1.0], User.Smoke Color=[0.815, 0.815, 0.815, 1.0], User.Sparks Color=[50.0, 0.53968, 0.0, 1.0]
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `Shards_Mat` → E:\Tools\deagle-work\ron_fx\particles\Shards.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\TilingNoise06_N.png (512x512,BC5)
- **P_VFX_Impact_Tin** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Tin`)
  - emitter `SmokeKick`
    - Required: material `MI_DustImpact`, subUV 16x2, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.25/0.5 s
    - Size: 60/60/60/125/125/125
    - Color: color 1/1/1 alpha 0.5/1
    - ColorOverLife: color 1/1/1 alpha 1.5/1.13072/0.72118/0.50414/0.48387/0.46595/0.44803/0.43011/0.41219/0.39427/0.37634/0.35842/0.3405/0.32258/0.30466/0.28674/0.26882/0.2509/0.23297/0.21505/0.19713/0.17921/0.16129/0.14337/0.12545/0.10753/0.08961/0.07168/0.05376/0.03584/0.01792/0
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - Acceleration: 0/0/-3/0/0/-1
  - emitter `Sparks_02`
    - Required: material `MI_Sparks_02`, subUV 2x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.1/0.125 s
    - Size: 50/100/100/80/150/150
    - Velocity: -500/-500/0/500/500/500 radial 0
    - ColorOverLife: color 13909.3/828.872/71.0325 alpha 0.75/0.6/0.45/0.30354/0.275/0.25/0.225/0.2/0.175/0.15/0.125/0.1/0.075/0.05/0.025/0
  - emitter `Bits`
    - Required: material `Shards_Mat`, subUV 2x2, localSpace 0, duration 0.1, loops 1
    - Spawn: rate 0, bursts 5@0s
    - Lifetime: 0.5/1 s
    - Size: 1/1/1/4/3/3
    - Velocity: 0/0/0/150/150/350 radial 0
    - ColorOverLife: color 0.84/0.84/0.84 alpha 1/1/1/1/1/1/1/1/1/1/1/1/0.99055/0.66667/0.33333/0
    - Acceleration: -
    - TypeData Gpu: 
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 25@0s
    - Lifetime: 0.03/0.4 s
    - Size: 1/2/1
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Acceleration: -
    - SizeMultiplyLife: -
    - Velocity: 0/0/50/0/0/25 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `EmberBig_HighRes`
    - Required: material `M_Particle_Additive`, subUV 4x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 2@0s
    - Lifetime: 0.025/0.1 s
    - Size: 3/3/3/4/4/4
    - Acceleration: -
    - SizeMultiplyLife: 0.5/1/1/0.25/0.5/1
    - Color: color 200/30/0.4 alpha 0.1/1
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 15@0s
    - Lifetime: 1/2 s
    - Size: 1/1/1.5
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Velocity: -50/-50/0/50/50/0 radial 0
    - Acceleration: -
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `EmberBig_HighRes`
    - Required: material `M_Particle_Additive`, subUV 4x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 0@0s
    - Lifetime: 0.01/1 s
    - Size: 2/2/2/3/3/3
    - Acceleration: -
    - SizeMultiplyLife: 1/1/1/0.25/0.5/1
    - Color: color 200/30/0.4 alpha 0.1/1
  - emitter `EmberBig_HighRes`
    - Required: material `MI_SparkSpray`, subUV 4x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 0@0s
    - Lifetime: 0.015/0.05 s
    - Velocity: 300/300/300/-300/-300/-300/0/0/0/0/0/0 radial 0
    - Size: 50/50/50/75/75/75
    - SizeMultiplyLife: 0.25/0.25/0.25/1/1/1
    - Color: color 200/30/0.4 alpha 0.1/1
  - emitter `Sparks_SPatter`
    - Required: material `MI_Sparks_01_Var1`, subUV 4x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.1/0.4 s
    - Size: 25/25/25/50/50/50
    - ColorOverLife: color 300/51/1/15/1/0 alpha 1/0
    - Acceleration: 0/-50/20/0/-500/96
    - SizeMultiplyLife: 0.25/0.5/1/0.54715/0.73195/1/0.8569/0.92153/1/1/1/1
  - emitter `BaseDust`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.15/0.5 s
    - Size: 25/150/150/125/200/200
    - SizeMultiplyLife: 0.75/0.75/0.75/1.06644/1.06644/1.06644/1.28049/1.28049/1.28049/1.41172/1.41172/1.41172/1.47971/1.47971/1.47971/1.50404/1.50404/1.50404/1.50427/1.50427/1.50427/1.5/1.5/1.5
    - ColorOverLife: color 1/1/1 alpha 1/0.55363/0.25873/0.08511/0.0026/-0.01896/-0.00977/0
    - Acceleration: 0/-10/0/0/-50/0
  - emitter `Sparks_02`
    - Required: material `MI_Sparks_05`, subUV 3x3, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.1 s
    - Size: 50/50/100/100/100/150
    - SizeMultiplyLife: 0.5/1/1/1/1.5/1
    - ColorOverLife: color 500/50/25 alpha 1/1/0.89286/0.71429/0.53571/0.35714/0.17857/0
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Spawn: rate 0, bursts 20@0s
    - Spawn: rate 0, bursts 25@0s
    - Spawn: rate 0, bursts 5@0s
  - material `MI_DustImpact` → E:\Tools\deagle-work\ron_fx\particles\T_dustImpact_16x4.png (512x128,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_SparkSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_longSpark_02_8x2.png (128x256,DXT1)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_Particle_Additive` → E:\Tools\deagle-work\ron_fx\particles\T_longSpark_02_8x2.png (128x256,DXT1)
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `Shards_Mat` → E:\Tools\deagle-work\ron_fx\particles\Shards.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\TilingNoise06_N.png (512x512,BC5)
  - material `MI_Sparks_01_Var1` → E:\Tools\deagle-work\ron_fx\particles\T_VFX_Sparks_01_B.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
  - material `MI_Sparks_02` → E:\Tools\deagle-work\ron_fx\particles\T_VFX_Sparks_03_B.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
  - material `MI_Sparks_05` → E:\Tools\deagle-work\ron_fx\particles\T_VFX_Sparks_05_B.png (1024x1024,DXT1), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
- **P_VFX_Impact_Tin**: see above

## glass

### RON_Glass_Plate

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_BulletImpact_Glass` | `None`  | - |
| ImpactEffects_Pistol | `NS_BulletImpact_Glass` | `None`  | - |
| ShotgunImpactEffects | `NS_BulletImpact_Glass` | `None`  | - |
| ExitImpactEffects | `P_VFX_Impact_Glass_Plate_Exit` | `Decal_Impact_Inst_Glass`  | E:\Tools\deagle-work\ron_fx\decals\Glass_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Hard_N.png (256²,BC5) |
| RicochetImpactEffects | `NS_BulletImpact_Glass` | `None`  | - |

- **NS_BulletImpact_Glass** (Niagara, `/Game/ReadyOrNot/VFX/VFXImpacts/Niagara/NS_BulletImpact_Glass`)
  - `Base_Dust_8`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ThinSmoke_01` subUV [8.0, 4.0] align 0 | curves: Curve_34: 0.05 → max 5 → 5 (80 samples); Curve_35: 0.75 → max 0.75 → 0 (45 samples); Curve_34: 0.05 → max 5 → 5 (80 samples); Curve_35: 0.75 → max 0.75 → 0 (45 samples)
  - `DEBRIS_HIGH_SETTINGS_0`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `M_Glass_Chunks` subUV [3.0, 3.0] align VelocityAligned | curves: Curve_51: 1.25 → max 1.25 → 0 (79 samples); Curve_52: 360 → max 360 → 0 (128 samples); Curve_53: -360 → max 0 → 0 (128 samples); Curve_51: 1.25 → max 1.25 → 0 (79 samples)
  - `DEBRIS_LOW_SETTINGS_3`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `M_Glass_Chunks` subUV [3.0, 3.0] align VelocityAligned | curves: Curve_54: 1.25 → max 1.25 → 0 (79 samples); Curve_55: 360 → max 360 → 0 (128 samples); Curve_56: -360 → max 0 → 0 (128 samples); Curve_54: 1.25 → max 1.25 → 0 (79 samples)
  - `Shot_Spatter_7`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ParticleGeneric_DebrisSpray` subUV [4.0, 1.0] align VelocityAligned | curves: Curve_17: 1 → max 1 → 0.05 (56 samples); Curve_17: 1 → max 1 → 0.05 (56 samples); Vector2DCurve_17: 0.3 → max 2.5 → 2.5 (162 samples); Vector2DCurve_17: 0.3 → max 2.5 → 2.5 (162 samples)
  - user params: User.Debris Color=[1.0, 1.0, 1.0, 1.0], User.Smoke Color=[2.0, 2.0, 2.0, 1.0], User.Spatter Color=[0.8, 0.8, 0.8, 1.0]
  - material `MI_ParticleGeneric_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_Glass_Chunks` → E:\Tools\deagle-work\ron_fx\particles\T_Glass_Chunks_3x3_Mask.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_Glass_Chunks_3x3_Normal.png (512x512,BC5)
- **NS_BulletImpact_Glass**: see above
- **NS_BulletImpact_Glass**: see above
- **P_VFX_Impact_Glass_Plate_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Glass_Plate_Exit`)
  - emitter `GlassBits`
    - Required: material `Shards_Mat`, subUV 2x2, localSpace 0, duration 0.1, loops 1
    - Spawn: rate 0, bursts 8@0s
    - Lifetime: 0.5/1 s
    - Size: 2/2/2/3/3/3
    - Velocity: -50/-50/0/50/50/1000 radial 0
    - ColorOverLife: color 0.84/0.84/0.84 alpha 1/1/1/1/1/1/1/1/1/1/1/1/0.99055/0.66667/0.33333/0
    - Acceleration: -
    - TypeData Gpu: 
  - emitter `Smoke_expand`
    - Required: material `Whisps_additive`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.25/0.275 s
    - Size: 50/250/250/300/600/100
    - Velocity: -50/-50/150/50/50/500 radial 0
    - ColorOverLife: color 0.835/0.835/0.835 alpha 1/1.00515/1.08097/1.20882/1.36946/1.54399/1.71337/1.85857/1.9606/2.00046/1.87887/1.51752/1.03078/0.53691/0.15429/0
    - SizeMultiplyLife: 0/0/0/0.30507/0.30507/0.30507/0.54503/0.54503/0.54503/0.72649/0.72649/0.72649/0.85609/0.85609/0.85609/0.94045/0.94045/0.94045/0.98622/0.98622/0.98622/1/1/1
    - Acceleration: 0/-50/20/0/-500/96.2069
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 5@0s
    - Lifetime: 0.03/0.02 s
    - Size: 10/10/10
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Acceleration: -
    - SizeMultiplyLife: -
    - Velocity: 0/0/50/0/0/25 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
    - ColorOverLife: color 200/139.86/83.6/10/5.45346/1.99438 alpha 1/0
    - Spawn: rate 0, bursts 5@0s
    - Spawn: rate 0, bursts 20@0s
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `Shards_Mat` → E:\Tools\deagle-work\ron_fx\particles\Shards.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\TilingNoise06_N.png (512x512,BC5)
  - material `Whisps_additive` → E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
- **NS_BulletImpact_Glass**: see above

### RON_Glass_Windshield

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_BulletImpact_Glass` | `None` DecalMesh `BP_ImpactMesh_Windshield` | - |
| ImpactEffects_Pistol | `NS_BulletImpact_Glass` | `None` DecalMesh `BP_ImpactMesh_Windshield` | - |
| ShotgunImpactEffects | `NS_BulletImpact_Glass` | `None` DecalMesh `BP_ImpactMesh_Windshield` | - |
| ExitImpactEffects | `P_VFX_Impact_Glass_Plate_Exit` | `Decal_Impact_Inst_Windshield`  | E:\Tools\deagle-work\ron_fx\decals\Glass_Windshield_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Metal_Soft_N.png (256²,BC5) |
| RicochetImpactEffects | `NS_BulletImpact_Glass` | `None` DecalMesh `BP_ImpactMesh_Windshield` | - |

- **NS_BulletImpact_Glass**: see above
- **NS_BulletImpact_Glass**: see above
- **NS_BulletImpact_Glass**: see above
- **P_VFX_Impact_Glass_Plate_Exit**: see above
- **NS_BulletImpact_Glass**: see above

### RON_Bulletproof_Glass

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_BulletImpact_Bulletproof_Glass` | `None` DecalMesh `BP_ImpactMesh_Glass` | - |
| ImpactEffects_Pistol | `NS_BulletImpact_Bulletproof_Glass` | `None` DecalMesh `BP_ImpactMesh_Glass` | - |
| ShotgunImpactEffects | `NS_BulletImpact_Bulletproof_Glass` | `None` DecalMesh `BP_ImpactMesh_Glass` | - |
| RicochetImpactEffects | `NS_BulletImpact_Glass` | `None` DecalMesh `BP_ImpactMesh_Glass` | - |

- **NS_BulletImpact_Bulletproof_Glass** (Niagara, `/Game/ReadyOrNot/VFX/VFXImpacts/Niagara/NS_BulletImpact_Bulletproof_Glass`)
  - `DUST_4`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ThinSmoke_01` subUV [8.0, 4.0] align 0 | curves: Curve_6: 0.05 → max 5 → 5 (99 samples); Curve_7: 0.75 → max 0.75 → 0 (45 samples); Curve_6: 0.05 → max 5 → 5 (99 samples); Curve_7: 0.75 → max 0.75 → 0 (45 samples)
  - `GLASS_BITS_HIGH_SETTINGS_2`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `M_Glass_Chunks` subUV [3.0, 3.0] align 0 | curves: -
  - `GLASS_BITS_LOW_SETTINGS_3`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `M_Glass_Chunks` subUV [3.0, 3.0] align 0 | curves: -
  - `SHOT_SPATTER_3`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ParticleGeneric_DebrisSpray` subUV [4.0, 1.0] align VelocityAligned | curves: Curve_3: 1 → max 1 → 0.05 (56 samples); Curve_3: 1 → max 1 → 0.05 (56 samples); Vector2DCurve_3: 0.3 → max 2.5 → 2.5 (162 samples); Vector2DCurve_3: 0.3 → max 2.5 → 2.5 (162 samples)
  - user params: User.Main Color=[1.0, 1.0, 1.0, 1.0]
  - material `MI_ParticleGeneric_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_Glass_Chunks` → E:\Tools\deagle-work\ron_fx\particles\T_Glass_Chunks_3x3_Mask.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_Glass_Chunks_3x3_Normal.png (512x512,BC5)
- **NS_BulletImpact_Bulletproof_Glass**: see above
- **NS_BulletImpact_Bulletproof_Glass**: see above
- **NS_BulletImpact_Glass**: see above

## flesh

### RON_Flesh

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_Blood_Smoke_Impact_Red_Static` | `Blood_Carpet_02_Inst` mesh-paint `MeshPaint_Bullethole_MASK` | E:\Tools\deagle-work\ron_fx\decals\Decal_BloodCarpet_02_Mask.png (512²,DXT1) |
| ImpactEffects_Pistol | `NS_Blood_Smoke_Impact_Red_Static` | `Blood_Carpet_02_Inst` mesh-paint `MeshPaint_Bullethole_MASK` | E:\Tools\deagle-work\ron_fx\decals\Decal_BloodCarpet_02_Mask.png (512²,DXT1) |
| ShotgunImpactEffects | `NS_Blood_Smoke_Impact_Red` | `Decals_Body_M_Inst_7`  | - |
| ExitImpactEffects | `P_VFX_Impact_Flesh_Exit` | `Blood_05_Inst` mesh-paint `MeshPaint_Bullethole_MASK` | Decal_BloodSpray_05_Mask (engine texture, not exported) |
| RicochetImpactEffects | `Blood_Smoke_Impact_Red` | `Blood_Carpet_02_Inst` mesh-paint `MeshPaint_Bullethole_MASK` | E:\Tools\deagle-work\ron_fx\decals\Decal_BloodCarpet_02_Mask.png (512²,DXT1) |

- **NS_Blood_Smoke_Impact_Red_Static** (Niagara, `/Game/ReadyOrNot/Assets/Particles/Impacts/Blood/Niagara/NS_Blood_Smoke_Impact_Red_Static`)
  - `BLOOD_DUST_5`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `MI_ThinSmoke_01` subUV [8.0, 4.0] align - | curves: Curve_2: 0.15 → max 2 → 2 (54 samples); Curve_3: 1 → max 1 → 0 (47 samples); Curve_2: 0.15 → max 2 → 2 (54 samples); Curve_3: 1 → max 1 → 0 (47 samples)
  - `BLOOD_DUST_IMPACT_7`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `MI_DustImpact` subUV [16.0, 2.0] align VelocityAligned | curves: Curve_1: 2 → max 2 → 0 (65 samples); Curve_1: 2 → max 2 → 0 (65 samples); Vector2DCurve_1: 0.2 → max 1.34501 → 1.325 (66 samples); Vector2DCurve_1: 0.2 → max 1.34501 → 1.325 (66 samples)
  - `SPRITE_DISSOLVE_2`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `MI_Liquid_Splash_2` subUV - align VelocityAligned | curves: Curve_4: 0 → max 1 → 1 (42 samples); Curve_5: 0 → max 1 → 1 (57 samples); Curve_6: 0.2 → max 1 → 1 (47 samples); Curve_7: 0 → max 1 → 1 (71 samples)
  - user params: User.Blood Color A=[0.2, 0.0, 0.0, 1.0], User.Blood Color B=[0.5, 0.05, 0.05, 1.0], User.Ground Blood Duration=1.0, User.Ground Blood Scale=1.0
  - material `MI_Liquid_Splash_2` → E:\Tools\deagle-work\ron_fx\particles\T_Blood_Splatter_07.png (1024x1024,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Splatter_07_N.png (1024x1024,BC5)
  - material `MI_DustImpact` → E:\Tools\deagle-work\ron_fx\particles\T_dustImpact_16x4.png (512x128,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
- **NS_Blood_Smoke_Impact_Red_Static**: see above
- **NS_Blood_Smoke_Impact_Red** (Niagara, `/Game/ReadyOrNot/Assets/Particles/Impacts/Blood/Niagara/NS_Blood_Smoke_Impact_Red`)
  - `BLOOD_DRIPS_4`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `MIC_BloodClip_Splat_2x2` subUV [2.0, 2.0] align VelocityAligned | curves: Curve_184: 1 → max 1 → 0.05 (52 samples); Curve_185: 1 → max 1 → 0 (57 samples); Curve_184: 1 → max 1 → 0.05 (52 samples); Curve_185: 1 → max 1 → 0 (57 samples)
  - `BLOOD_DUST_5`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `MI_ParticleGeneric_ThinSmoke` subUV [8.0, 4.0] align - | curves: Curve_184: 0.15 → max 2 → 2 (54 samples); Curve_185: 1 → max 1 → 0 (47 samples); Curve_184: 0.15 → max 2 → 2 (54 samples); Curve_185: 1 → max 1 → 0 (47 samples)
  - `BLOOD_DUST_IMPACT_7`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `MI_ParticleGeneric_DustImpact` subUV [16.0, 2.0] align VelocityAligned | curves: Curve_92: 2 → max 2 → 0 (65 samples); Curve_92: 2 → max 2 → 0 (65 samples); Vector2DCurve_92: 0.2 → max 1.34501 → 1.325 (66 samples); Vector2DCurve_92: 0.2 → max 1.34501 → 1.325 (66 samples)
  - `FLESH_PIECES_COLLISON_3`: 0, localSpace 0, bounds -50..50 / -50..50 / 0..50 | Mesh mat `None` subUV - align - | curves: Curve_95: 1 → max 1 → 0 (81 samples); Curve_95: 1 → max 1 → 0 (81 samples)
  - `GROUND_BLOOD_6`: 0, localSpace 0, bounds -50..50 / -50..50 / 0..50 | Sprite mat `MI_Liquid_Splash_3` subUV - align CustomAlignment | curves: Curve_218: 0 → max 1 → 1 (57 samples); Curve_219: 0.3 → max 1 → 1 (46 samples); Curve_216: 0 → max 1 → 1 (57 samples); Curve_217: 0.3 → max 1 → 1 (46 samples)
  - `MESH_DISSOLVE_LONG_DISTANCE_0`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Mesh mat `None` subUV - align - | curves: Curve_204: 0.1 → max 1 → 1 (50 samples); Curve_205: 0 → max 1 → 1 (71 samples); Curve_204: 0.1 → max 1 → 1 (50 samples); Curve_205: 0 → max 1 → 1 (71 samples)
  - `MESH_DISSOLVE_SHORT_DISTANCE_3`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Mesh mat `None` subUV - align - | curves: Curve_126: 0.1 → max 1 → 1 (50 samples); Curve_127: 0 → max 1 → 1 (71 samples); Curve_126: 0.1 → max 1 → 1 (50 samples); Curve_127: 0 → max 1 → 1 (71 samples)
  - `SPRITE_DISSOLVE_2`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / 0..150 | Sprite mat `MI_Liquid_Splash_2` subUV - align VelocityAligned | curves: Curve_368: 0 → max 1 → 1 (42 samples); Curve_369: 0 → max 1 → 1 (57 samples); Curve_370: 0.2 → max 1 → 1 (47 samples); Curve_371: 0 → max 1 → 1 (71 samples)
  - user params: User.Blood Color A=[0.1, 0.0, 0.0, 1.0], User.Blood Color B=[0.2, 0.0, 0.0, 1.0], User.Ground Blood Duration=1.0, User.Ground Blood Scale=1.0
  - material `MIC_BloodClip_Splat_2x2` → E:\Tools\deagle-work\ron_fx\particles\T_Blood_Clip_A.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Clip_N.png (128x128,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Reflection.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_SubUV_2x2_01.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_SubUV_2x2_01_N.png (128x128,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Noise_N_02.png (512x512,BC5)
  - material `MI_Liquid_Splash_1` → E:\Tools\deagle-work\ron_fx\particles\T_Blood_Splatter_07.png (1024x1024,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Splatter_07_N.png (1024x1024,BC5)
  - material `MI_Liquid_Splash_2` → E:\Tools\deagle-work\ron_fx\particles\T_Blood_Splatter_07.png (1024x1024,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Splatter_07_N.png (1024x1024,BC5)
  - material `MI_Liquid_Splash_3` → E:\Tools\deagle-work\ron_fx\particles\Decal_Blood_Splat_16_BLUR.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\Decal_Blood_Splat_16_N.png (512x512,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Splatter_07.png (1024x1024,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Splatter_07_N.png (1024x1024,BC5)
  - material `MI_ParticleGeneric_DustImpact` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_dustImpact_16x4.png (512x128,DXT5)
  - material `MI_ParticleGeneric_ThinSmoke` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
- **P_VFX_Impact_Flesh_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Flesh_Exit`)
  - emitter `Blood_Collider`
    - Required: material `MIC_GibChunk_Red_01`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 3/5 s
    - Size: 0.1/0.1/0.1/0.2/0.2/0.2
    - Velocity: 2/-150/50/2555/150/350 radial 0
    - Acceleration: 0/0/-1000/0/0/-600
    - SizeMultiplyLife: 0.8/0.8/0.8/0.80737/0.80737/0.80737/0.81475/0.81475/0.81475/0.82212/0.82212/0.82212/0.82949/0.82949/0.82949/0.83687/0.83687/0.83687/0.84424/0.84424/0.84424/0.85161/0.85161/0.85161/0.85899/0.85899/0.85899/0.86636/0.86636/0.86636/0.87373/0.87373/0.87373/0.88111/0.88111/0.88111/0.88848/0.88848/0.88848/0.89585/0.89585/0.89585/0.90323/0.90323/0.90323/0.9106/0.9106/0.9106/0.91797/0.91797/0.91797/0.92535/0.92535/0.92535/0.93272/0.93272/0.93272/0.94009/0.94009/0.94009/0.94747/0.94747/0.94747/0.95484/0.95484/0.95484/0.96221/0.96221/0.96221/0.96959/0.96959/0.96959/0.97696/0.97696/0.97696/0.98433/0.98433/0.98433/0.99171/0.99171/0.99171/0.99908/0.99908/0.99908/0.77419/0.77419/0.77419/0.51613/0.51613/0.51613/0.25806/0.25806/0.25806/0/0/0
    - TypeData Mesh: Mesh=SM_GibChunk_01, bOverrideMaterial=True, bCollisionsConsiderPartilceSize=False
  - emitter `GoreSpawn`
    - Required: material `MIC_BloodClip_Splat_2x2`, subUV 2x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.15/0.2 s
    - Color: color 0.025/0/0 alpha 1
    - Size: 20/25/25/25/12/25
    - Velocity: -10/-10/0/10/10/0 radial 0
    - SizeMultiplyLife: 1/1/0/1.16623/1/0.05541/1.5948/1/0.19827/2.18078/1/0.39359/2.81922/1/0.60641/3.4052/1/0.80173/3.83377/1/0.94459/4/1/1
  - emitter `Strands`
    - Required: material `MIC_BloodClip_Slash_A`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 2@0s
    - Lifetime: 0.1/0.5 s
    - Color: color 0.21/0/0 alpha 1
    - Size: 34/25/25/150/75/50
    - Velocity: -10/-10/0/255/10/0 radial 0
    - SizeMultiplyLife: 0/0/0/0.05541/0.05541/0.05541/0.19827/0.19827/0.19827/0.39359/0.39359/0.39359/0.60641/0.60641/0.60641/0.80173/0.80173/0.80173/0.94459/0.94459/0.94459/1/1/1
    - Acceleration: 0/0/0/0/0/-960
  - emitter `Core`
    - Required: material `MIC_BloodClip_Splat_2x2`, subUV 2x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.15/0.1 s
    - Color: color 0.025/0/0 alpha 1
    - Size: 20/25/25/25/12/25
    - Velocity: -10/-10/0/10/10/0 radial 0
    - SizeMultiplyLife: 1/1/0/1.16623/1/0.05541/1.5948/1/0.19827/2.18078/1/0.39359/2.81922/1/0.60641/3.4052/1/0.80173/3.83377/1/0.94459/4/1/1
    - Size: 35/45/25/75/50/25
    - Size: 12/25/25/20/12/25
    - Size: 12/25/25/20/12/25
  - material `MIC_BloodClip_Slash_A` → E:\Tools\deagle-work\ron_fx\particles\T_Blood_Clip_A.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Clip_N.png (128x128,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Reflection.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Slash_A.png (128x64,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Slash_N.png (128x64,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Noise_N_02.png (512x512,BC5)
  - material `MIC_BloodClip_Splat_2x2` → E:\Tools\deagle-work\ron_fx\particles\T_Blood_Clip_A.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Clip_N.png (128x128,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Reflection.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_SubUV_2x2_01.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_SubUV_2x2_01_N.png (128x128,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Noise_N_02.png (512x512,BC5)
  - material `MIC_GibChunk_Red_01` → E:\Tools\deagle-work\ron_fx\particles\T_Gib_Red_C.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Gib_Red_N.png (128x128,BC5)
- **Blood_Smoke_Impact_Red** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Impacts/Blood/Blood_Smoke_Impact_Red`)
  - emitter `Trail`
    - Required: material `MuzzleFlash_Master_Inst`, subUV 11x11, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 1, bursts 4@0s
    - Lifetime: 1/3 s
    - Size: 20/250/250/56/600/600
    - Velocity: 25/5/-5/50/-5/5 radial 0
    - ColorOverLife: color 0.33854/0.33854/0.33854 alpha 5/4.79708/4.28567/3.60927/2.91137/2.33548/2.02461/1.99704/1.98995/1.97956/1.96598/1.94934/1.92978/1.9074/1.88235/1.85475/1.82472/1.79239/1.75788/1.72133/1.68285/1.64258/1.60064/1.55716/1.51225/1.46606/1.4187/1.3703/1.32099/1.27089/1.22014/1.16884/1.11714/1.06516/1.01302/0.96085/0.90877/0.85692/0.80542/0.75439/0.70396/0.65426/0.60541/0.55754/0.51078/0.46524/0.42107/0.37838/0.33729/0.29795/0.26046/0.22497/0.19158/0.16044/0.13167/0.10539/0.08172/0.0608/0.04276/0.02771/0.01578/0.0071/0.0018/0
    - SizeMultiplyLife: 1/1/1/1.2048/1.2048/1.20884/1.43946/1.43946/1.4564/1.68012/1.68012/1.70542/1.90283/1.90283/1.91855/2.09567/2.09567/2.07735/2.31115/2.31115/2.26658/2.54998/2.54998/2.49129/2.8013/2.8013/2.73839/3.05427/3.05427/2.99484/3.29806/3.29806/3.24762/3.52185/3.52185/3.48369/3.71473/3.71473/3.68996/3.86599/3.86599/3.85352/3.96459/3.96459/3.96109/4/4/4
    - Acceleration: -100/-50/10/100/-100/100
  - emitter `Blood_Spatter`
    - Required: material `MIC_BloodClip_Splat_2x2`, subUV 2x2, localSpace 0, duration 2.0, loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.05/0.2 s
    - Color: color 0.1/0/0 alpha 1
    - Size: 6/0/0/11/0/0
    - SizeMultiplyLife: 0.15/1/1/0.67602/1/1/1.10501/1/1/1.44285/1/1/1.69539/1/1/1.86852/1/1/1.9681/1/1/2/1/1
    - Acceleration: 0/0/-450
  - emitter `Directional_01`
    - Required: material `M_VA_Blood_Horizontal`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 2@0s, 1@0.065s
    - Lifetime: 0.35/0.75 s
    - Size: 0.05/0.05/0.05/0.1/0.1/0.1
    - Acceleration: 0/0/-980
    - SizeMultiplyLife: 0.35/0.2/0.2/0.53599/0.24491/0.24491/0.71769/0.36896/0.36896/0.89412/0.5567/0.5567/1.06429/0.79266/0.79266/1.22722/1.06139/1.06139/1.38192/1.34742/1.34742/1.5274/1.63529/1.63529/1.66268/1.90955/1.90955/1.78677/2.15473/2.15473/1.89869/2.35537/2.35537/1.99761/2.49703/2.49703/2.08796/2.60206/2.60206/2.17569/2.708/2.708/2.26088/2.81511/2.81511/2.34357/2.92327/2.92327/2.42378/3.03234/3.03234/2.50155/3.14219/3.14219/2.57691/3.25269/3.25269/2.6499/3.3637/3.3637/2.72054/3.4751/3.4751/2.78887/3.58675/3.58675/2.85492/3.69851/3.69851/2.91873/3.81026/3.81026/2.98032/3.92187/3.92187/3.03972/4.03319/4.03319/3.09698/4.14411/4.14411/3.15212/4.25448/4.25448/3.20518/4.36417/4.36417/3.25618/4.47306/4.47306/3.30516/4.58101/4.58101/3.35216/4.68788/4.68788/3.3972/4.79355/4.79355/3.44031/4.89788/4.89788/3.48154/5.00075/5.00075/3.5209/5.10201/5.10201/3.55844/5.20154/5.20154/3.59419/5.2992/5.2992/3.62818/5.39486/5.39486/3.66043/5.4884/5.4884/3.69099/5.57967/5.57967/3.71989/5.66854/5.66854/3.74715/5.75489/5.75489/3.77281/5.83857/5.83857/3.79691/5.91947/5.91947/3.81947/5.99744/5.99744/3.84053/6.07235/6.07235/3.86012/6.14408/6.14408/3.87827/6.21248/6.21248/3.89501/6.27743/6.27743/3.91039/6.33879/6.33879/3.92442/6.39644/6.39644/3.93714/6.45023/6.45023/3.94859/6.50004/6.50004/3.95879/6.54574/6.54574/3.96778/6.58719/6.58719/3.9756/6.62426/6.62426/3.98226/6.65682/6.65682/3.98782/6.68473/6.68473/3.99229/6.70786/6.70786/3.99571/6.72609/6.72609/3.99811/6.73928/6.73928/3.99953/6.74729/6.74729/4/6.75/6.75
    - Velocity: -50/0/0/75/0/0 radial 0
    - ColorOverLife: color 0.15/0/0 alpha 1
    - TypeData Mesh: Mesh=VA_Dir_Liquid
  - emitter `Blood_Spatter`
    - Required: material `MIC_BloodClip_Splat_2x2`, subUV 2x2, localSpace 0, duration 3.0, loops 1
    - Spawn: rate 100/400/97.3753/389.501/94.7507/379.003/92.126/368.504/89.5013/358.005/86.8766/347.507/84.252/337.008/81.6273/326.509/79.0026/316.01/76.3779/305.512/73.7533/295.013/71.1286/284.514/68.5039/274.016/65.8793/263.517/63.2546/253.018/60.6299/242.52/58.0052/232.021/55.3806/221.522/52.7559/211.024/50.1312/200.525/47.5066/190.026/44.8819/179.528/42.2572/169.029/39.6326/158.53/37.0079/148.031/34.3832/137.533/31.7585/127.034/29.1339/116.535/26.5092/106.037/23.8845/95.5381/21.2598/85.0394/18.6352/74.5407/16.0105/64.042/13.3858/53.5433/10.7612/43.0446/8.13649/32.546/5.51182/22.0473/2.88714/11.5486/0.26247/1.04988/1.77165/5.31495/3.74015/11.2205/5.70866/17.126/7.67716/23.0315/9.64566/28.937/11.6142/34.8425/13.5827/40.748/15.5512/46.6535/17.5197/52.559/19.4882/58.4646/21.4567/64.3701/23.4252/70.2756/24.9344/74.8688/24.6063/74.2126/24.2782/73.5564/23.9501/72.9003/23.6221/72.2441/23.294/71.5879/22.9659/70.9318/22.6378/70.2756/22.3097/69.6194/21.9816/68.9633/21.6535/68.3071/21.3255/67.6509/20.9974/66.9947/20.6693/66.3386/20.3412/65.6824/20.0131/65.0263/19.685/64.3701/19.357/63.7139/19.0289/63.0577/18.7008/62.4016/18.3727/61.7454/18.0446/61.0892/17.7165/60.4331/17.3884/59.7769/17.0604/59.1207/16.7323/58.4646/16.4042/57.8084/16.0761/57.1522/15.748/56.4961/15.42/55.8399/15.0919/55.1837/14.7638/54.5276/14.4357/53.8714/14.1076/53.2152/13.7795/52.5591/13.4514/51.9029/13.1234/51.2467/12.7953/50.5906/12.4672/49.9344/12.1391/49.2782/11.811/48.6221/11.4829/47.9659/11.1549/47.3097/10.8268/46.6536/10.4987/45.9974/10.1706/45.3412/9.84252/44.685/9.51444/44.0289/9.18635/43.3727/8.85827/42.7165/8.53019/42.0604/8.2021/41.4042/7.87402/40.748/7.54593/40.0919/7.21785/39.4357/6.88976/38.7795/6.56168/38.1234/6.2336/37.4672/5.90551/36.811/5.57743/36.1549/5.24935/35.4987/4.92126/34.8425/4.59318/34.1864/4.26509/33.5302/3.93701/32.874/3.60893/32.2178/3.28084/31.5617/2.95276/30.9055/2.62468/30.2493/2.29659/29.5932/1.9685/28.937/1.64042/28.2808/1.31233/27.6247/0.98425/26.9685/0.65617/26.3123/0.32808/25.6562/0/25, bursts -
    - Lifetime: 0.25/0.5 s
    - Color: color 0.2/0/0 alpha 1
    - Size: 1.5/1.5/1.5/3/3/3
    - SizeMultiplyLife: 0.25/0.5/0.25/0.27646/0.65873/0.27646/0.30291/0.81746/0.30291/0.32937/0.97619/0.32937/0.35582/1.13492/0.35582/0.38228/1.29365/0.38228/0.40873/1.45238/0.40873/0.43519/1.61111/0.43519/0.46164/1.76984/0.46164/0.4881/1.92857/0.4881/0.50087/2.00139/0.50017/0.50625/2.01/0.50125/0.51656/2.02649/0.50331/0.5316/2.05055/0.50632/0.55117/2.08187/0.51023/0.57509/2.12014/0.51502/0.60315/2.16504/0.52063/0.63516/2.21626/0.52703/0.67093/2.27349/0.53419/0.71026/2.33642/0.54205/0.75295/2.40473/0.55059/0.79882/2.47811/0.55976/0.84765/2.55624/0.56953/0.89927/2.63883/0.57985/0.95347/2.72555/0.59069/1.01005/2.81608/0.60201/1.06883/2.91013/0.61377/1.12961/3.00737/0.62592/1.19219/3.1075/0.63844/1.25637/3.21019/0.65127/1.32197/3.31515/0.66439/1.38878/3.42205/0.67776/1.45661/3.53058/0.69132/1.52527/3.64043/0.70505/1.59456/3.7513/0.71891/1.66428/3.86285/0.73286/1.73425/3.97479/0.74685/1.80425/4.0868/0.76085/1.87411/4.19857/0.77482/1.94362/4.30979/0.78872/2.01258/4.42013/0.80252/2.08081/4.5293/0.81616/2.14811/4.63698/0.82962/2.21428/4.74285/0.84286/2.27913/4.8466/0.85583/2.34245/4.94792/0.86849/2.40407/5.04651/0.88081/2.46377/5.14203/0.89275/2.52137/5.23419/0.90427/2.57667/5.32267/0.91533/2.62947/5.40716/0.9259/2.67959/5.48734/0.93592/2.72682/5.56291/0.94536/2.77096/5.63354/0.95419/2.81184/5.69894/0.96237/2.84923/5.75877/0.96985/2.88297/5.81275/0.97659/2.91283/5.86053/0.98257/2.93865/5.90183/0.98773/2.96021/5.93633/0.99204/2.97731/5.9637/0.99546/2.98978/5.98365/0.99796/2.99741/5.99585/0.99948/3/6/1
    - Acceleration: 0/0/-980
  - emitter `None`
    - Required: material `MI_Gore_4`, subUV 2x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 6@0s
    - Lifetime: 0.5/3 s
    - Size: 0.03/0.03/0.03/0.125/0.125/0.125
    - Velocity: 5/-150/-150/500/150/150 radial 0
    - Acceleration: 0/0/-980
    - SizeMultiplyLife: 1/1/1/0/0/0
    - Color: color 0.02/0.01/0.01/0.3/0.25/0.25 alpha 1
    - TypeData Mesh: Mesh=SM_FXMesh_GorePiece
  - emitter `Flecks`
    - Required: material `M_SolidDrop_Basic`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0/650/0/0, bursts 75@0s
    - Lifetime: 0.5/4 s
    - Size: 1.5/1.5/1.5/4.5/4.5/4.5
    - Acceleration: -
    - SizeMultiplyLife: 1/10/1/1.00187/9.92883/1.00168/1.00704/9.72612/1.00634/1.0149/9.40717/1.01342/1.02481/8.98732/1.02234/1.03616/8.48189/1.03256/1.04832/7.90619/1.04351/1.06066/7.27556/1.05462/1.07257/6.60532/1.06534/1.08341/5.91078/1.0751/1.09256/5.20728/1.08334/1.0994/4.51013/1.0895/1.10331/3.83466/1.09302/1.10366/3.19619/1.09333/1.09982/2.61005/1.08988/1.09118/2.09155/1.0821/1.0771/1.65602/1.06942/1.05697/1.31879/1.05129/1.03015/1.09517/1.02715/0.99823/0.99845/0.9981/0.97506/0.97506/0.97506/0.95238/0.95238/0.95238/0.92971/0.92971/0.92971/0.90703/0.90703/0.90703/0.88435/0.88435/0.88435/0.86168/0.86168/0.86168/0.839/0.839/0.839/0.81633/0.81633/0.81633/0.79365/0.79365/0.79365/0.77098/0.77098/0.77098/0.7483/0.7483/0.7483/0.72562/0.72562/0.72562/0.70295/0.70295/0.70295/0.68027/0.68027/0.68027/0.6576/0.6576/0.6576/0.63492/0.63492/0.63492/0.61224/0.61224/0.61224/0.58957/0.58957/0.58957/0.56689/0.56689/0.56689/0.54422/0.54422/0.54422/0.52154/0.52154/0.52154/0.49887/0.49887/0.49887/0.47619/0.47619/0.47619/0.45351/0.45351/0.45351/0.43084/0.43084/0.43084/0.40816/0.40816/0.40816/0.38549/0.38549/0.38549/0.36281/0.36281/0.36281/0.34014/0.34014/0.34014/0.31746/0.31746/0.31746/0.29478/0.29478/0.29478/0.27211/0.27211/0.27211/0.24943/0.24943/0.24943/0.22676/0.22676/0.22676/0.20408/0.20408/0.20408/0.18141/0.18141/0.18141/0.15873/0.15873/0.15873/0.13605/0.13605/0.13605/0.11338/0.11338/0.11338/0.0907/0.0907/0.0907/0.06803/0.06803/0.06803/0.04535/0.04535/0.04535/0.02268/0.02268/0.02268/0/0/0
    - Color: color 0.15/0/0 alpha 0.5/1
    - TypeData Gpu: 
  - emitter `BaseDust`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 2@0s
    - Lifetime: 0.5/1.5 s
    - Size: 25/150/150/100/200/200
    - SizeMultiplyLife: 0.15/0.15/0.15/0.65305/0.65305/0.65305/1.00516/1.00516/1.00516/1.24992/1.24992/1.24992/1.43785/1.43785/1.43785/1.59166/1.59166/1.59166/1.71483/1.71483/1.71483/1.81068/1.81068/1.81068/1.88262/1.88262/1.88262/1.93402/1.93402/1.93402/1.96824/1.96824/1.96824/1.98868/1.98868/1.98868/1.99869/1.99869/1.99869/2.00167/2.00167/2.00167/2.00098/2.00098/2.00098/2/2/2
    - ColorOverLife: color 0.5/0/0/0.5/0.25/0.25 alpha 1/0.88973/0.68103/0.45387/0.28823/0.24668/0.23719/0.2277/0.21822/0.20873/0.19924/0.18975/0.18027/0.17078/0.16129/0.1518/0.14232/0.13283/0.12334/0.11385/0.10436/0.09488/0.08539/0.0759/0.06641/0.05693/0.04744/0.03795/0.02846/0.01898/0.00949/0
    - Acceleration: -100/-100/-400/100/100/-150
  - emitter `ClothFlecksB`
    - Required: material `M_WoodShards`, subUV 2x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 7@0s
    - Lifetime: 0.5/3 s
    - Size: 0.05/0.05/0.05/0.5/0.5/0.5
    - Velocity: 5/-400/-400/1200/400/400 radial 0
    - Acceleration: -200/-200/-850/200/200/-350
    - SizeMultiplyLife: 1/1/1/0/0/0
    - Color: color 0.02/0.01/0.01/0.3/0.25/0.25 alpha 1
    - TypeData Mesh: Mesh=FXMesh_Paper, bOverrideMaterial=True
  - emitter `ClothFlecks`
    - Required: material `M_WoodShards`, subUV 2x2, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 15@0s
    - Lifetime: 0.25/0.75 s
    - Size: 0.05/0.05/0.05/0.5/0.5/0.5
    - Velocity: 0/-50/-50/750/50/50 radial 0
    - Acceleration: -100/-100/-900/100/100/-250
    - SizeMultiplyLife: 1/1/1/0/0/0
    - Color: color 0.1/0.01/0.01/0.4/0.1/0.1 alpha 1
    - TypeData Mesh: Mesh=FXMesh_Paper, bOverrideMaterial=True
  - emitter `SmokeKick`
    - Required: material `MI_DustImpact`, subUV 16x2, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1-1@0s
    - Lifetime: 0.25/1 s
    - Size: 50/50/50/175/175/175
    - Color: color 1/1/1 alpha 0.75/1.25
    - ColorOverLife: color 0.5/0/0/0.5/0.2/0.2 alpha 2/1.45746/0.98732/0.64939/0.5022/0.45977/0.41379/0.36782/0.32184/0.27586/0.22989/0.18391/0.13793/0.09195/0.04598/0
    - SizeMultiplyLife: 0.2/0.4/0/0.34998/0.57464/0.11854/0.53881/0.79208/0.27805/0.64987/0.90984/0.39438/0.6849/0.92898/0.4624/0.72222/0.94512/0.53191/0.76094/0.95863/0.60158/0.8/0.96975/0.66997/0.83837/0.97871/0.73566/0.875/0.98573/0.79722/0.90885/0.99104/0.85323/0.93888/0.99488/0.90226/0.96405/0.99746/0.94287/0.98333/0.99902/0.97366/0.99565/0.99979/0.99316/1/1/1
    - Acceleration: 0/0/-3/0/0/-1
  - emitter `ShootSpatter`
    - Required: material `MI_DebrisSpray`, subUV 4x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1-3@0s
    - Lifetime: 0.075/0.25 s
    - Size: 5/5/5/25/25/25
    - SizeMultiplyLife: 0.5/1/0/0.96987/1.47109/0.16526/1.34724/1.82589/0.35085/1.64363/2.08282/0.54007/1.87057/2.26028/0.71627/2.0396/2.37668/0.86277/2.16223/2.45045/0.96291/2.25/2.5/1
    - ColorOverLife: color 0.15/0.05/0.05 alpha 0.75/0.45217/0.24623/0.11519/0.04205/0.0098/0.00145/0
    - Acceleration: 0/0/-980
  - emitter `Omni_Base`
    - Required: material `M_VA_Blood_Directional`, subUV 1x1, localSpace 0, duration 0.1, loops 1
    - Spawn: rate 0, bursts -
    - Lifetime: 0.75/2 s
    - Size: 0.25/0.25/0.25/0.45/0.45/0.45
    - Acceleration: 0/0/-980
    - SizeMultiplyLife: 0.25/0.25/0.25/0.56787/0.56787/0.56787/0.93251/0.93251/0.93251/1.33551/1.33551/1.33551/1.76803/1.76803/1.76803/2.22151/2.22151/2.22151/2.68723/2.68723/2.68723/3.15654/3.15654/3.15654/3.62076/3.62076/3.62076/4.07124/4.07124/4.07124/4.49928/4.49928/4.49928/4.89628/4.89628/4.89628/5.25345/5.25345/5.25345/5.56231/5.56231/5.56231/5.81393/5.81393/5.81393/6/6/6
    - Velocity: 200/-5/-5/375/5/5 radial 0
    - ColorOverLife: color 0.15/0/0 alpha 1
    - TypeData Mesh: Mesh=VA_Omni_Liquid
    - Acceleration: -200/-50/75/200/-100/300
    - Acceleration: 0/0/-980
    - Acceleration: 0/0/-980
    - ColorOverLife: color 0.1/0/0 alpha 1/0
    - ColorOverLife: color 0.1/0/0 alpha 1/0
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - Spawn: rate 0, bursts -
    - Spawn: rate 2, bursts -
    - Spawn: rate 0, bursts 5@0s
    - Spawn: rate 2, bursts -
    - Spawn: rate 2, bursts -
    - Velocity: 250/-750/-750/1500/750/750 radial 0
    - Velocity: 250/-750/-750/1500/750/750 radial 0
    - Velocity: 250/-750/-750/1500/750/750 radial 0
    - Velocity: 250/-750/-750/1500/750/750 radial 0
    - Velocity: 250/-750/-750/1500/750/750 radial 0
    - Velocity: 250/-750/-750/1500/750/750 radial 0
  - material `M_SolidDrop_Basic` → E:\Tools\deagle-work\ron_fx\particles\T_mud_drop.png (256x256,DXT1)
  - material `MIC_BloodClip_Splat_2x2` → E:\Tools\deagle-work\ron_fx\particles\T_Blood_Clip_A.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Clip_N.png (128x128,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Reflection.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_SubUV_2x2_01.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_SubUV_2x2_01_N.png (128x128,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Noise_N_02.png (512x512,BC5)
  - material `MI_Gore_4` → E:\Tools\deagle-work\ron_fx\particles\T_DefaultNoSRGB_D.png (1024x1024,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Default_A.png (1024x1024,BC4), E:\Tools\deagle-work\ron_fx\particles\T_Default_D.png (1024x1024,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Default_N.png (1024x1024,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Default_ORM.png (1024x1024,DXT1), E:\Tools\deagle-work\ron_fx\particles\gore_decay2_low_DefaultMaterial_BaseColor.png (1024x1024,DXT1), E:\Tools\deagle-work\ron_fx\particles\gore_decay2_low_DefaultMaterial_Normal.png (1024x1024,BC5), E:\Tools\deagle-work\ron_fx\particles\gore_decay2_low_DefaultMaterial_OcclusionRoughnessMetallic.png (1024x1024,DXT1), E:\Tools\deagle-work\ron_fx\particles\CorpsePile_01_M_PROXY_Corpses_01_Roughness.png (1024x1024,BC4), E:\Tools\deagle-work\ron_fx\particles\t_meancut_parts_low_BaseColor.png (1024x1024,DXT1), E:\Tools\deagle-work\ron_fx\particles\t_meancut_parts_low_Normal.png (1024x1024,BC5), E:\Tools\deagle-work\ron_fx\particles\t_meancut_parts_low_OcclusionRoughnessMetallic.png (1024x1024,DXT1), E:\Tools\deagle-work\ron_fx\particles\TX_YuccaLeaves_01a_RMA.png (256x256,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_OfficeStationery_ColorMask.png (256x256,BC4)
  - material `MI_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_DustImpact` → E:\Tools\deagle-work\ron_fx\particles\T_dustImpact_16x4.png (512x128,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_WoodShards` → E:\Tools\deagle-work\ron_fx\particles\T_WoodShard_OAH.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_WoodShard_normal.png (512x512,BC5)
  - material `MuzzleFlash_Master_Inst` → E:\Tools\deagle-work\ron_fx\particles\MuzzleSmokeOne_1024_11x11.png (1024x1024,DXT5)
  - material `M_VA_Blood_Directional` → E:\Tools\deagle-work\ron_fx\particles\VA_Dir_Liquid_Pos.png (1024x450,B8G8R8A8)
  - material `M_VA_Blood_Horizontal` → E:\Tools\deagle-work\ron_fx\particles\VA_Horizontal_Liquid_Pos.png (1024x560,B8G8R8A8)

## cloth

### RON_Fabric_Thin

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Fabric`  | E:\Tools\deagle-work\ron_fx\decals\Cardboard_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Fabric_BO.png (256²,DXT5) |
| ImpactEffects_Pistol | `NS_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Fabric`  | E:\Tools\deagle-work\ron_fx\decals\Cardboard_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Fabric_BO.png (256²,DXT5) |
| ShotgunImpactEffects | `NS_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Fabric`  | E:\Tools\deagle-work\ron_fx\decals\Cardboard_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Fabric_BO.png (256²,DXT5) |
| ExitImpactEffects | `P_VFX_Impact_FabricThin_Exit` | `Decal_Impact_Inst_Fabric`  | E:\Tools\deagle-work\ron_fx\decals\Cardboard_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Fabric_BO.png (256²,DXT5) |
| RicochetImpactEffects | `P_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Fabric`  | E:\Tools\deagle-work\ron_fx\decals\Cardboard_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Fabric_BO.png (256²,DXT5) |

- **NS_Bullet_Impact_Fabric_Big** (Niagara, `/Game/ReadyOrNot/VFX/VFXImpacts/Niagara/NS_Bullet_Impact_Fabric_Big`)
  - `Debris_5`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `M_ParticleEmberSimple` subUV - align 0 | curves: Curve_48: 1 → max 1 → 0 (90 samples); Curve_49: 5 → max 5 → 0 (88 samples); Curve_50: -5 → max 0 → 0 (89 samples); Curve_48: 1 → max 1 → 0 (90 samples)
  - `Feathers_6`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_Feathers_01` subUV [5.0, 5.0] align 0 | curves: Curve_32: 0.75 → max 1.5 → 1.5 (40 samples); Curve_33: 2 → max 2 → 0 (79 samples); Curve_32: 0.75 → max 1.5 → 1.5 (40 samples); Curve_33: 2 → max 2 → 0 (79 samples)
  - `Shot_Spatter_3`: GPUComputeSim, localSpace True, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_DebrisSpray` subUV [4.0, 1.0] align VelocityAligned | curves: Curve_16: 1 → max 1 → 0 (53 samples); Curve_16: 1 → max 1 → 0 (53 samples); Vector2DCurve_16: 0.3 → max 2.5 → 2 (156 samples); Vector2DCurve_16: 0.3 → max 2.5 → 2 (156 samples)
  - `Smoke_Kick_2`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ThinSmoke_01` subUV [8.0, 4.0] align 0 | curves: Curve_48: 0.15 → max 1 → 1 (34 samples); Curve_49: 0.5 → max 0.5 → 0 (25 samples); Curve_50: 1 → max 1 → 0 (48 samples); Curve_48: 0.15 → max 1 → 1 (34 samples)
  - `SPARKS_0`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_Sparks_05` subUV [3.0, 3.0] align VelocityAligned | curves: Curve_16: 1 → max 1 → 0 (64 samples); Curve_16: 1 → max 1 → 0 (64 samples); Vector2DCurve_16: 0.5 → max 1.5 → 1.5 (74 samples); Vector2DCurve_16: 0.5 → max 1.5 → 1.5 (74 samples)
  - user params: User.Debris Color=[0.375, 0.375, 0.375, 1.0], User.Feathers Color=[0.25, 0.25, 0.25, 1.0], User.Smoke Color=[1.0, 1.0, 1.0, 1.0], User.Sparks Color=[500.0, 50.0, 25.0, 1.0], User.Spatter Color=[100.0, 100.0, 100.0, 1.0]
  - material `MI_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_ParticleEmberSimple` → E:\Tools\deagle-work\ron_fx\particles\T_Glow_A.png (512x512,DXT1)
  - material `MI_Feathers_01` → E:\Tools\deagle-work\ron_fx\particles\T_VFX_Feathers_01_B.png (2048x2048,DXT1), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
  - material `MI_Sparks_05` → E:\Tools\deagle-work\ron_fx\particles\T_VFX_Sparks_05_B.png (1024x1024,DXT1), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
- **NS_Bullet_Impact_Fabric_Big**: see above
- **NS_Bullet_Impact_Fabric_Big**: see above
- **P_VFX_Impact_FabricThin_Exit** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/Exit/P_VFX_Impact_FabricThin_Exit`)
  - emitter `Smoke_Main`
    - Required: material `Whisps_trans5`, subUV 12x12, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1-1@0s
    - Lifetime: 0.45/0.5 s
    - Size: 50/50/50/50/150/150
    - Velocity: 0/0/500/0/0/250 radial 0
    - ColorOverLife: color 0.30054/0.2705/0.27889 alpha 0.5/0
    - SizeMultiplyLife: 0.05/0.1/0.1/0.78381/0.83317/0.83317/1.34119/1.38876/1.38876/1.73994/1.78473/1.78473/1.99946/2.04069/2.04069/2.138/2.17504/2.17504/2.1746/2.207/2.207/2.12779/2.15529/2.15529/2.01639/2.03889/2.03889/1.8591/1.8767/1.8767/1.67462/1.68758/1.68758/1.48171/1.49048/1.49048/1.29909/1.30429/1.30429/1.14541/1.14784/1.14784/1.03958/1.04022/1.04022/1/1/1
    - Acceleration: 0/-50/20/0/-400/96.2069
  - emitter `FabricBits`
    - Required: material `FabricChaffs`, subUV 2x2, localSpace 0, duration 0.1, loops 1
    - Spawn: rate 0, bursts 5@0s
    - Lifetime: 0.5/1.25 s
    - Size: 1/1/1/3/3/3
    - Velocity: -2/-2/500/2/2/500 radial 0
    - ColorOverLife: color 0.84/0.84/0.84 alpha 1/1/1/1/1/1/1/1/1/1/1/1/0.99055/0.66667/0.33333/0
    - Acceleration: -
    - TypeData Gpu: 
  - emitter `Fire`
    - Required: material `Mat_Fire`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 1, bursts 5@0s
    - Lifetime: 0.02/0.05 s
    - Size: 10/5/25
    - ColorOverLife: color 0.99648/0.40358/0.40358/0.23438/0.07219/0.04219 alpha 1/0/0.99666/0.0287/0.98705/0.06138/0.97172/0.09761/0.95121/0.13697/0.92608/0.17905/0.89688/0.22343/0.86416/0.26968/0.82846/0.31739/0.79034/0.36613/0.75034/0.41549/0.70902/0.46505/0.66693/0.51438/0.62462/0.56306/0.58264/0.61068/0.54154/0.65681/0.50188/0.70104/0.46419/0.74294/0.42904/0.78209/0.39697/0.81809/0.36852/0.85049/0.34427/0.87889/0.32477/0.90284/0.31056/0.92195/0.30218/0.93579/0.29877/0.94012/0.27042/0.85483/0.21339/0.67647/0.14275/0.45341/0.07359/0.23405/0.02098/0.06679/0/0
    - SizeMultiplyLife: 0/0/0/5/5/5
  - emitter `Trail`
    - Required: material `MI_Muzzle_Smoke_03`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 1, bursts 1-1@0s
    - Lifetime: 0.15/0.25 s
    - Size: 5/0/0
    - Velocity: 0/0/55 radial 0
    - ColorOverLife: color 0.28125/0.22547/0.1969 alpha 25/24.9407/24.7683/24.4906/24.1151/23.6495/23.1015/22.4788/21.7889/21.0396/20.2386/19.3934/18.5117/17.6013/16.6697/15.7247/14.7738/13.8247/12.8852/11.9628/11.0652/10.2/9.37507/8.59788/7.87614/7.21751/6.62965/6.12022/5.69688/5.36728/5.13909/5.01997/4.99394/4.96222/4.9035/4.81967/4.71264/4.58431/4.43657/4.27134/4.09051/3.89599/3.68967/3.47346/3.24925/3.01896/2.78448/2.54771/2.31056/2.07492/1.8427/1.6158/1.39612/1.18556/0.98603/0.79942/0.62763/0.47258/0.33615/0.22025/0.12679/0.05766/0.01476/0
    - SizeMultiplyLife: 0/0/0/0.39324/0.39324/0.38471/0.83774/0.83774/0.82302/1.3221/1.3221/1.30329/1.83454/1.83454/1.81346/2.3635/2.3635/2.34173/2.8973/2.8973/2.87614/3.42433/3.42433/3.40482/3.93293/3.93293/3.91587/4.41149/4.41149/4.39739/4.84833/4.84833/4.83744/5.2319/5.2319/5.22423/5.55039/5.55039/5.54568/5.79243/5.79243/5.79017/5.946/5.946/5.94539/6/6/6
    - Acceleration: -100/-50/10/100/-100/100
    - Acceleration: -200/-50/75/200/-100/300
    - Spawn: rate 0, bursts 5@0s
    - Spawn: rate 0, bursts 20@0s
  - material `Mat_Fire` → E:\Tools\deagle-work\ron_fx\particles\FlameBig_sheet.png (2048x2048,DXT1)
  - material `FabricChaffs` → E:\Tools\deagle-work\ron_fx\particles\Stones2x2.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\Fabric.png (1024x1024,DXT5), E:\Tools\deagle-work\ron_fx\particles\Gravel2x2_normal.png (512x512,BC5)
  - material `Whisps_trans5` → E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5), E:\Tools\deagle-work\ron_fx\particles\whisp_redux_2048_12x12.png (1024x1024,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_redux_2048_normal.png (256x256,BC5)
  - material `MI_Muzzle_Smoke_03` → E:\Tools\deagle-work\ron_fx\particles\MuzzleSmokeOne_1024_11x11.png (1024x1024,DXT5)
- **P_Bullet_Impact_Fabric_Big** (Cascade, `/Game/ReadyOrNot/VFX/VFXImpacts/Edit/P_Bullet_Impact_Fabric_Big`)
  - emitter `Sparks_02`
    - Required: material `MI_Sparks_05`, subUV 3x3, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.1 s
    - Size: 50/50/100/100/100/150
    - SizeMultiplyLife: 0.5/1/1/1/1.5/1
    - ColorOverLife: color 500/50/25 alpha 1/1/0.89286/0.71429/0.53571/0.35714/0.17857/0
  - emitter `EmberBig_HighRes`
    - Required: material `M_ParticleEmberSimple`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 7-25@0s
    - Lifetime: 1/3 s
    - Size: 0.5/1/0.5/8/6/5
    - Acceleration: 150/150/-800/-150/-150/-400
    - Velocity: -350/-350/75/350/350/1259 radial 0
    - SizeMultiplyLife: 1/1/1/1.02702/1.02905/1.02433/1.07708/1.08287/1.0694/1.10395/1.11176/1.0936/1.06158/1.06621/1.05544/0.95238/0.95238/0.95238/0.85714/0.85714/0.85714/0.7619/0.7619/0.7619/0.66667/0.66667/0.66667/0.57143/0.57143/0.57143/0.47619/0.47619/0.47619/0.38095/0.38095/0.38095/0.28571/0.28571/0.28571/0.19048/0.19048/0.19048/0.09524/0.09524/0.09524/0/0/0
    - Color: color 0.375/0.375/0.375 alpha 1
  - emitter `SmokeKickRound`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.5/1.75 s
    - Size: 150/150/150/300/300/300
    - Velocity: -100/-100/200/100/100/600 radial 0
    - ColorOverLife: color 0.5/0.5/0.5/0.76667/0.76667/0.76667/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1 alpha 0.5/0.16667/0.05831/0.00523/-0.0151/-0.01474/-0.00569/0
    - SizeMultiplyLife: 0.15/0/0/0.24168/0.38539/0.05389/0.31512/0.57261/0.13814/0.37361/0.61946/0.23834/0.42043/0.58373/0.34006/0.45884/0.52323/0.42887/0.49214/0.49574/0.49033/0.52359/0.52416/0.52416/0.55473/0.55595/0.55595/0.58554/0.58729/0.58729/0.61592/0.6181/0.6181/0.64579/0.64829/0.64829/0.67505/0.67778/0.67778/0.70362/0.7065/0.7065/0.73141/0.73436/0.73436/0.75834/0.76129/0.76129/0.78431/0.7872/0.7872/0.80925/0.81202/0.81202/0.83306/0.83567/0.83567/0.85565/0.85807/0.85807/0.87694/0.87913/0.87913/0.89684/0.89878/0.89878/0.91525/0.91693/0.91693/0.9321/0.93351/0.93351/0.94729/0.94844/0.94844/0.96075/0.96163/0.96163/0.97237/0.97302/0.97302/0.98208/0.98252/0.98252/0.98978/0.99004/0.99004/0.9954/0.99552/0.99552/0.99883/0.99886/0.99886/1/1/1
    - Acceleration: -40/-40/-40/40/40/40
  - emitter `SmokeKickRound`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1-1@0.03s
    - Lifetime: 0.125/0.2 s
    - Size: 45/150/150/65/300/300
    - Velocity: -100/-100/750/100/100/1000 radial 0
    - ColorOverLife: color 0.5/0.5/0.5/0.76667/0.76667/0.76667/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1 alpha 1/0.78571/0.57143/0.35714/0.21429/0.14286/0.07143/0
    - SizeMultiplyLife: 0.025/0/0/0.2241/0.92053/0.07354/0.40101/1.61466/0.15343/0.55688/2.10542/0.23799/0.69307/2.41795/0.32553/0.81076/2.57586/0.41434/0.91126/2.60381/0.50274/0.99582/2.52582/0.58902/1.0657/2.36622/0.6715/1.12217/2.14925/0.74848/1.16649/1.89913/0.81827/1.19992/1.64012/0.87917/1.22371/1.3965/0.92948/1.23916/1.19238/0.96753/1.24749/1.05228/0.99157/1.25/1/1
    - Acceleration: -40/-40/-500/40/40/-200
  - emitter `Feathers`
    - Required: material `MI_Feathers_01`, subUV 5x5, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1-3@0s
    - Lifetime: 0.5/3 s
    - Size: 65/65/65/115/115/115
    - ColorOverLife: color 0.25/0.25/0.25 alpha 2/0
    - SizeMultiplyLife: 0.75/0.75/1/1.14058/0.94445/1/1.40362/1.30555/1/1.5/1.5/1
    - Acceleration: 0/0/-425
  - emitter `ShootSpatter`
    - Required: material `MI_DebrisSpray`, subUV 4x1, localSpace True, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1-1@0.03s, 1@0.06s
    - Lifetime: 0.1/0.2 s
    - Size: 25/25/25/75/75/75
    - SizeMultiplyLife: 0.3/0.25/0/0.71666/0.63199/0.16526/1.12792/0.93828/0.35085/1.51655/1.18788/0.54007/1.8653/1.39982/0.71627/2.15696/1.59314/0.86277/2.37427/1.78686/0.96291/2.5/2/1
    - ColorOverLife: color 100/100/100 alpha 1/0.38025/0.08187/0
    - Acceleration: 0/0/-980
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
  - material `MI_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_ParticleEmberSimple` → E:\Tools\deagle-work\ron_fx\particles\T_Glow_A.png (512x512,DXT1)
  - material `MI_Feathers_01` → E:\Tools\deagle-work\ron_fx\particles\T_VFX_Feathers_01_B.png (2048x2048,DXT1), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
  - material `MI_Sparks_05` → E:\Tools\deagle-work\ron_fx\particles\T_VFX_Sparks_05_B.png (1024x1024,DXT1), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)

### RON_Fabric_Carpet

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Carpet`  | E:\Tools\deagle-work\ron_fx\decals\Asphalt_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Carpet_BO.png (256²,DXT5) |
| ImpactEffects_Pistol | `NS_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Carpet`  | E:\Tools\deagle-work\ron_fx\decals\Asphalt_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Carpet_BO.png (256²,DXT5) |
| ShotgunImpactEffects | `NS_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Carpet`  | E:\Tools\deagle-work\ron_fx\decals\Asphalt_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Carpet_BO.png (256²,DXT5) |
| ExitImpactEffects | `P_VFX_Impact_FabricThin_Exit` | `Decal_Impact_Inst_Carpet`  | E:\Tools\deagle-work\ron_fx\decals\Asphalt_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Carpet_BO.png (256²,DXT5) |
| RicochetImpactEffects | `P_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Carpet`  | E:\Tools\deagle-work\ron_fx\decals\Asphalt_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Carpet_BO.png (256²,DXT5) |

- **NS_Bullet_Impact_Fabric_Big**: see above
- **NS_Bullet_Impact_Fabric_Big**: see above
- **NS_Bullet_Impact_Fabric_Big**: see above
- **P_VFX_Impact_FabricThin_Exit**: see above
- **P_Bullet_Impact_Fabric_Big**: see above

### RON_Fabric_Stuffing

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Stuffing`  | E:\Tools\deagle-work\ron_fx\decals\Cardboard_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Stuffing_BO.png (256²,DXT5) |
| ImpactEffects_Pistol | `NS_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Stuffing`  | E:\Tools\deagle-work\ron_fx\decals\Cardboard_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Stuffing_BO.png (256²,DXT5) |
| ShotgunImpactEffects | `NS_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Stuffing`  | E:\Tools\deagle-work\ron_fx\decals\Cardboard_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Stuffing_BO.png (256²,DXT5) |
| ExitImpactEffects | `P_VFX_Impact_FabricThin_Exit` | `Decal_Impact_Inst_Stuffing`  | E:\Tools\deagle-work\ron_fx\decals\Cardboard_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Stuffing_BO.png (256²,DXT5) |
| RicochetImpactEffects | `P_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Stuffing`  | E:\Tools\deagle-work\ron_fx\decals\Cardboard_N.png (256²,BC5)<br>E:\Tools\deagle-work\ron_fx\decals\Stuffing_BO.png (256²,DXT5) |

- **NS_Bullet_Impact_Fabric_Big**: see above
- **NS_Bullet_Impact_Fabric_Big**: see above
- **NS_Bullet_Impact_Fabric_Big**: see above
- **P_VFX_Impact_FabricThin_Exit**: see above
- **P_Bullet_Impact_Fabric_Big**: see above

## dirt

### RON_Dirt

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_BulletImpact_Dirt` | `M_BulletImpact_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Plaster_1.png (512²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Training_Landscape_Dirt_Albedo.png (2048²,DXT1) |
| ImpactEffects_Pistol | `NS_BulletImpact_Dirt` | `M_BulletImpact_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Plaster_1.png (512²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Training_Landscape_Dirt_Albedo.png (2048²,DXT1) |
| ShotgunImpactEffects | `NS_BulletImpact_Dirt` | `Decal_Impact_Inst_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Dirt_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Dirt_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Dirt` | `Decal_Impact_Inst_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Dirt_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Dirt_N.png (256²,BC5) |
| RicochetImpactEffects | `P_BulletImpact_Dirt` | `M_BulletImpact_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Plaster_1.png (512²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Training_Landscape_Dirt_Albedo.png (2048²,DXT1) |

- **NS_BulletImpact_Dirt** (Niagara, `/Game/ReadyOrNot/VFX/VFXImpacts/Niagara/NS_BulletImpact_Dirt`)
  - `DIRT_2`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `M_SandSplashSmall` subUV - align 0 | curves: Curve_24: 1 → max 1 → 0 (42 samples); Curve_25: -1 → max 0 → 0 (35 samples); Curve_26: 1 → max 1 → 0 (70 samples); Curve_24: 1 → max 1 → 0 (42 samples)
  - `Dust_10`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ParticleGeneric_ThinSmoke` subUV [8.0, 4.0] align 0 | curves: Curve_16: 0.2 → max 1.25 → 1.25 (35 samples); Curve_17: 0.75 → max 0.75 → 0 (45 samples); Curve_16: 0.2 → max 1.25 → 1.25 (35 samples); Curve_17: 0.75 → max 0.75 → 0 (45 samples)
  - `Shot_Spatter_11`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `MI_ParticleGeneric_DebrisSpray` subUV [4.0, 1.0] align VelocityAligned | curves: Curve_24: 1 → max 1 → 0 (57 samples); Curve_25: 1 → max 1 → 0.33 (41 samples); Curve_26: 0 → max 1 → 1 (57 samples); Curve_24: 1 → max 1 → 0 (57 samples)
  - user params: User.Dirt Color=[0.97917, 0.66713, 0.28604, 1.0], User.Dust Color=[0.52, 0.39, 0.31, 1.0], User.Spatter Color=[0.75, 0.66641, 1.0, 1.0]
  - material `MI_ParticleGeneric_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5)
  - material `MI_ParticleGeneric_ThinSmoke` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_SandSplashSmall` → E:\Tools\deagle-work\ron_fx\particles\T_Bullet_ConcreteImpact_BaseHRA.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_Cloud_ABC.png (512x512,DXT1)
- **NS_BulletImpact_Dirt**: see above
- **NS_BulletImpact_Dirt**: see above
- **P_VFX_Impact_Dirt** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Dirt`)
  - emitter `Dust`
    - Required: material `Squib_one`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 2@0s
    - Lifetime: 0.3/0.5 s
    - Size: 100/250/250/250/350/350
    - Velocity: -50/-50/1800/50/50/20 radial 0
    - ColorOverLife: color 1/0.96369/0.915/0.88235/0.7098/0.47843 alpha 0/1.84277/1.22848/0.54087/0.32859/0.31987/0.31131/0.3029/0.29464/0.28653/0.27858/0.27077/0.2631/0.25558/0.2482/0.24097/0.23387/0.22692/0.2201/0.21342/0.20688/0.20046/0.19419/0.18804/0.18202/0.17613/0.17036/0.16473/0.15921/0.15382/0.14855/0.1434/0.13837/0.13346/0.12866/0.12398/0.11941/0.11495/0.11061/0.10637/0.10224/0.09821/0.0943/0.09048/0.08677/0.08316/0.07964/0.07623/0.07291/0.06969/0.06656/0.06353/0.06058/0.05773/0.05496/0.05229/0.04969/0.04719/0.04476/0.04242/0.04016/0.03797/0.03587/0.03384/0.03188/0.03/0.02819/0.02646/0.02479/0.02319/0.02165/0.02018/0.01878/0.01744/0.01616/0.01494/0.01377/0.01267/0.01162/0.01063/0.00968/0.00879/0.00795/0.00716/0.00642/0.00572/0.00507/0.00446/0.00389/0.00337/0.00288/0.00243/0.00202/0.00164/0.0013/0.00099/0.00071/0.00046/0.00023/4e-05/-0.00013/-0.00027/-0.0004/-0.0005/-0.00058/-0.00064/-0.00068/-0.00071/-0.00072/-0.00072/-0.00071/-0.00069/-0.00066/-0.00062/-0.00057/-0.00052/-0.00046/-0.00041/-0.00035/-0.00029/-0.00023/-0.00018/-0.00013/-9e-05/-5e-05/-2e-05/-1e-05/0
    - SizeMultiplyLife: 0.5/0.5/0.5/0.77737/0.77737/0.77737/0.94589/0.94589/0.94589/1.02969/1.02969/1.02969/1.05289/1.05289/1.05289/1.03959/1.03959/1.03959/1.01392/1.01392/1.01392/1/1/1
    - Acceleration: 0/-50/20/0/-500/96
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 20@0s
    - Lifetime: 0.1/0.35 s
    - Size: 0.4/0.4/0.4
    - Acceleration: -
    - SizeMultiplyLife: -
    - ColorOverLife: color 25/23.5784/16.125 alpha 1.5/0
    - Velocity: -50/-50/100/50/50/118.099 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `DirtBits`
    - Required: material `DebrisMaster`, subUV 2x2, localSpace 0, duration 0.1, loops 1
    - Spawn: rate 0, bursts 25@0s
    - Lifetime: 1/2 s
    - Size: 1/1/1/3/3/3
    - Velocity: -70/-70/350/70/70/750 radial 0
    - ColorOverLife: color 1/1/1 alpha 1/0
    - Acceleration: -
    - TypeData Gpu: CameraMotionBlurAmount=1.0, bClearExistingParticlesOnInit=True
  - emitter `Mesh Particle Smoke`
    - Required: material `Whisps_trans`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.2/0.5 s
    - Size: 5/150/50/35/300/100
    - Velocity: -50/-50/100/50/50/250 radial 0
    - ColorOverLife: color 0.30729/0.23405/0.18406 alpha 5/5.11107/5.45528/5.99721/6.70143/7.53251/8.45503/9.43356/10.4327/11.4169/12.3509/13.1988/13.9249/14.4941/14.8709/15.0199/14.8991/14.4662/13.7561/12.8142/11.686/10.4167/9.05209/7.638/6.21994/4.84335/3.55367/2.39638/1.41691/0.66073/0.17327/0
    - SizeMultiplyLife: 2/2/0.5/2.18268/2.14449/0.70185/2.39481/2.24961/0.96771/2.6303/2.31953/1.28634/2.88282/2.35876/1.64594/3.14616/2.37159/2.03507/3.41406/2.36245/2.44207/3.68027/2.33569/2.85539/3.93856/2.29569/3.26345/4.18268/2.24683/3.65467/4.40637/2.19348/4.01746/4.60342/2.14001/4.34029/4.7675/2.09082/4.61146/4.89251/2.05025/4.81961/4.972/2.02273/4.95283/5/2.01254/5
    - Acceleration: 0/0/20/0/0/25
  - emitter `None`
    - Required: material `Squib_two`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.5/0.7 s
    - Size: 300/250/250
    - Velocity: 50/50/300/50/50/500 radial 0
    - ColorOverLife: color 0.45833/0.36459/0.30317 alpha 0/0.99399/0.98646/0.98603/0.98979/0.99484/0.99826/0.99713/0.98854/0.96958/0.9442/0.91926/0.89477/0.87071/0.84709/0.8239/0.80113/0.77879/0.75687/0.73536/0.71426/0.69357/0.67328/0.65339/0.63389/0.61479/0.59607/0.57774/0.55978/0.5422/0.52499/0.50815/0.49168/0.47556/0.45979/0.44438/0.42932/0.4146/0.40021/0.38617/0.37246/0.35907/0.34601/0.33326/0.32084/0.30872/0.29691/0.28541/0.27421/0.2633/0.25269/0.24236/0.23232/0.22256/0.21307/0.20386/0.19491/0.18623/0.17781/0.16965/0.16174/0.15408/0.14666/0.13948/0.13254/0.12583/0.11936/0.1131/0.10707/0.10125/0.09565/0.09026/0.08507/0.08008/0.0753/0.0707/0.06629/0.06207/0.05803/0.05417/0.05048/0.04696/0.04361/0.04041/0.03738/0.0345/0.03177/0.02918/0.02674/0.02443/0.02226/0.02022/0.01831/0.01651/0.01484/0.01328/0.01183/0.01048/0.00924/0.0081/0.00705/0.00609/0.00522/0.00443/0.00372/0.00309/0.00252/0.00202/0.00159/0.00121/0.00089/0.00062/0.0004/0.00022/8e-05/-3e-05/-0.00011/-0.00015/-0.00018/-0.00018/-0.00017/-0.00015/-0.00012/-9e-05/-6e-05/-3e-05/-1e-05/0
    - SizeMultiplyLife: 0.1/0.1/0.1/0.26167/0.26167/0.26167/0.49204/0.49204/0.49204/0.6412/0.6412/0.6412/0.71814/0.71814/0.71814/0.80107/0.80107/0.80107/0.80872/0.80872/0.80872/0.81637/0.81637/0.81637/0.82402/0.82402/0.82402/0.83167/0.83167/0.83167/0.83933/0.83933/0.83933/0.84698/0.84698/0.84698/0.85463/0.85463/0.85463/0.86228/0.86228/0.86228/0.86993/0.86993/0.86993/0.87758/0.87758/0.87758/0.88523/0.88523/0.88523/0.89288/0.89288/0.89288/0.90054/0.90054/0.90054/0.90819/0.90819/0.90819/0.91584/0.91584/0.91584/0.92349/0.92349/0.92349/0.93114/0.93114/0.93114/0.93879/0.93879/0.93879/0.94644/0.94644/0.94644/0.95409/0.95409/0.95409/0.96174/0.96174/0.96174/0.9694/0.9694/0.9694/0.97705/0.97705/0.97705/0.9847/0.9847/0.9847/0.99235/0.99235/0.99235/1/1/1
    - Acceleration: 0/-50/20/0/-25/96
  - emitter `Rubble2`
    - Required: material `Mat_Rubble_MeshParticle`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 1, bursts 3@0s
    - Lifetime: 1/2 s
    - Size: 0.5/0.5/0.5/1.5/1.5/1.5
    - Velocity: -150/-150/150/150/150/750 radial 0
    - ColorOverLife: color 1/1/1 alpha 1/0
    - Acceleration: -
    - TypeData Mesh: Mesh=Aset_cement_rubble_S_rhlxZ_LOD0_Aset_cement_rubble_S_rhlxZ_2_LOD0
  - emitter `Dust`
    - Required: material `Smoke4_M`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.3/0.6 s
    - Size: 200/200/200/250/250/250
    - Velocity: 0/0/0/0/0/250 radial 0
    - ColorOverLife: color 2/2/2 alpha 1/0
    - SizeMultiplyLife: 0.7/0.5/0.5/0.97757/0.78865/0.78865/1.12191/0.96156/0.96156/1.16602/1.04474/1.04474/1.14289/1.06417/1.06417/1.08551/1.04586/1.04586/1.02688/1.0158/1.0158/1/1/1
    - Acceleration: 0/0/20/0/0/96
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 5@0s
    - Lifetime: 0.1 s
    - Size: 0.25/0.25/1/0.4/0.4/1
    - Acceleration: -
    - SizeMultiplyLife: -
    - ColorOverLife: color 25/23.5784/16.125 alpha 1.5/0
    - Velocity: -50/-50/100/50/50/118.099 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - emitter `Sparks`
    - Required: material `M_Radial_Gradient`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 8@0s
    - Lifetime: 0.05/0.3 s
    - Size: 0.75/0.5/0.5
    - Acceleration: -
    - SizeMultiplyLife: -
    - ColorOverLife: color 25/23.5784/16.125 alpha 1.5/0
    - Velocity: -50/-50/100/50/50/118.099 radial 0
    - TypeData Gpu: bClearExistingParticlesOnInit=True
  - material `Smoke4_M` → E:\Tools\deagle-work\ron_fx\particles\Smoke2a_2K.png (2048x2048,DXT5)
  - material `Mat_Rubble_MeshParticle` → E:\Tools\deagle-work\ron_fx\particles\cement_rubble_small_Grey_Chunk_rhlxZ_2K_Albedo.png (256x256,DXT1), E:\Tools\deagle-work\ron_fx\particles\cement_rubble_small_Grey_Chunk_rhlxZ_2K_Normal_LOD0.png (256x256,BC5), E:\Tools\deagle-work\ron_fx\particles\cement_rubble_small_Grey_Chunk_rhlxZ_2K_Roughness.png (32x32,DXT1)
  - material `M_Radial_Gradient` → no textures (procedural)
  - material `DebrisMaster` → E:\Tools\deagle-work\ron_fx\particles\Stones2x2.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\Gravel2x2_normal.png (512x512,BC5)
  - material `Squib_one` → E:\Tools\deagle-work\ron_fx\particles\Squib_one_1024_8x8.png (1024x1024,DXT5), E:\Tools\deagle-work\ron_fx\particles\Squib_one_normal.png (256x256,BC5)
  - material `Squib_two` → E:\Tools\deagle-work\ron_fx\particles\Squib_two_2048_8x8.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
  - material `Whisps_trans` → E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
- **P_BulletImpact_Dirt** (Cascade, `/Game/ReadyOrNot/VFX/VFXImpacts/P_BulletImpact_Dirt`)
  - emitter `ShootSpatter`
    - Required: material `MI_DebrisSpray`, subUV 4x1, localSpace True, duration 1(def), loops 1
    - Spawn: rate 0, bursts 2-2@0s, 1-1@0.03s, 1@0.06s
    - Lifetime: 0.075/0.175 s
    - Size: 40/40/40/15/15/15/20/20/20/5/5/5
    - SizeMultiplyLife: 0.1/0.5/0/0.55545/0.8941/0.16526/1.06671/1.25753/0.35085/1.59206/1.58702/0.54007/2.08979/1.8793/0.71627/2.51817/2.13107/0.86277/2.83548/2.33906/0.96291/3/2.5/1
    - ColorOverLife: color 0.75/0.66641/0.6 alpha 1/0.64605/0.41501/0.33
    - Acceleration: 0/0/-9.8
  - emitter `SmokeKickRound`
    - Required: material `M_DustPuff`, subUV 1x1, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1-1@0s
    - Lifetime: 2/3 s
    - Size: 150/150/150/300/300/300
    - Velocity: -100/-100/400/100/100/600 radial 0
    - Color: color 0.52604/0.39687/0.3096 alpha 0.1/0.25
    - ColorOverLife: color 0.83854/0.62766/0.40617/1/1/1 alpha 0/0.24138/0.27048/0.24994/0.24421/0.23007/0.20924/0.18341/0.15426/0.12351/0.09284/0.06394/0.03851/0.01825/0.00486/0
    - SizeMultiplyLife: 0/0/0/0.0604/0.38578/0.05302/0.14502/0.57388/0.13534/0.24201/0.62164/0.23352/0.33956/0.58642/0.33413/0.42583/0.52555/0.42373/0.48901/0.4964/0.48889/0.51519/0.51519/0.51519/0.53539/0.53539/0.53539/0.55559/0.55559/0.55559/0.57579/0.57579/0.57579/0.59599/0.59599/0.59599/0.61619/0.61619/0.61619/0.63639/0.63639/0.63639/0.65659/0.65659/0.65659/0.67679/0.67679/0.67679/0.69699/0.69699/0.69699/0.71719/0.71719/0.71719/0.73739/0.73739/0.73739/0.75759/0.75759/0.75759/0.77779/0.77779/0.77779/0.79799/0.79799/0.79799/0.8182/0.8182/0.8182/0.8384/0.8384/0.8384/0.8586/0.8586/0.8586/0.8788/0.8788/0.8788/0.899/0.899/0.899/0.9192/0.9192/0.9192/0.9394/0.9394/0.9394/0.9596/0.9596/0.9596/0.9798/0.9798/0.9798/1/1/1
    - Acceleration: 20/20/20/40/40/50
  - emitter `SandKickMicro`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1@0.05s
    - Lifetime: 0.5/1.75 s
    - Size: 100/50/50/300/300/300
    - Velocity: -25/-25/250/25/25/500 radial 0
    - Color: color 0.52604/0.39687/0.3096 alpha 0.35/0.65
    - ColorOverLife: color 0.15/0.132/0.12/0.375/0.333/0.3 alpha 0.75/0.25722/0.04528/0
    - SizeMultiplyLife: 0.2/0/0/0.35387/0.30189/0.0153/0.47635/0.3033/0.00609/0.56752/0.15739/-0.00932/0.62749/0.0173/-0.01266/0.65857/0.01732/0.01154/0.68397/0.07024/0.04683/0.71064/0.12802/0.08535/0.73838/0.19009/0.12673/0.76702/0.25588/0.17058/0.79637/0.32482/0.21655/0.82625/0.39637/0.26424/0.85646/0.46993/0.31329/0.88684/0.54496/0.36331/0.91718/0.62088/0.41392/0.94731/0.69712/0.46475/0.97705/0.77313/0.51542/1.0062/0.84833/0.56555/1.03459/0.92216/0.61477/1.06203/0.99406/0.66271/1.08833/1.06347/0.70898/1.11331/1.1298/0.7532/1.13679/1.19249/0.79499/1.15858/1.25098/0.83399/1.17849/1.30471/0.86981/1.19635/1.35311/0.90207/1.21197/1.39562/0.93041/1.22516/1.43167/0.95445/1.23574/1.4607/0.9738/1.24353/1.48214/0.98809/1.24835/1.49543/0.99695/1.25/1.5/1
    - Acceleration: -50/-50/-200/50/50/-100
  - emitter `None`
    - Required: material `M_SandSplashSmall`, subUV 1x1, localSpace 0, duration 0.3, loops 1
    - Spawn: rate 1000/250/5/0, bursts 15-50@0s
    - Lifetime: 1.5/3 s
    - Size: 0.1/0.1/0.1/7/7/7
    - Velocity: 200/200/1250/-200/-200/500/25/25/50/-25/-25/5 radial 0
    - ColorOverLife: color 0.97917/0.66713/0.28604/0.44271/0.30941/0.2006 alpha 1/0
    - SizeMultiplyLife: 1/1/1/0/0/0
    - Acceleration: -
    - TypeData Gpu: 
  - emitter `ShootSpatter`
    - Required: material `MI_DebrisSpray`, subUV 4x1, localSpace True, duration 0.35, loops 1
    - Spawn: rate 100/0, bursts -
    - Lifetime: 0.175/0.25 s
    - Size: 150/150/150/65/65/65/15/15/15/1/1/1
    - SizeMultiplyLife: 0.2/0.2/0/0.66853/1.07257/0.41434/1.01823/1.82504/0.81827/1.25/2.5/1
    - Acceleration: 0/0/-9.8
    - Color: color 0.075/0.066/0.06/0.75/0.666/0.6 alpha 1/0.5
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - SizeMultiplyLife: 0.4/0.6/0/0.60183/0.85799/0.29926/0.69536/0.93387/0.48216/0.7777/0.96367/0.63113/0.85958/0.98294/0.77143/0.93075/0.99392/0.88904/0.98098/0.99885/0.96991/1/1/1
    - Spawn: rate 2, bursts -
  - material `MI_DebrisSpray` → E:\Tools\deagle-work\ron_fx\particles\T_debrisSpray_4x1.png (512x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `M_DustPuff` → E:\Tools\deagle-work\ron_fx\particles\T_FineParticle_DustAndDoplet.png (512x512,DXT5)
  - material `M_SandSplashSmall` → E:\Tools\deagle-work\ron_fx\particles\T_Bullet_ConcreteImpact_BaseHRA.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_Cloud_ABC.png (512x512,DXT1)

### RON_Soil

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_VFX_Impact_Mud` | `M_BulletImpact_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Plaster_1.png (512²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Training_Landscape_Dirt_Albedo.png (2048²,DXT1) |
| ImpactEffects_Pistol | `NS_VFX_Impact_Mud` | `M_BulletImpact_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Plaster_1.png (512²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Training_Landscape_Dirt_Albedo.png (2048²,DXT1) |
| ShotgunImpactEffects | `NS_VFX_Impact_Mud` | `Decal_Impact_Inst_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Dirt_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Dirt_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Mud` | `Decal_Impact_Inst_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Dirt_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Dirt_N.png (256²,BC5) |
| RicochetImpactEffects | `P_VFX_Impact_Mud` | `M_BulletImpact_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Plaster_1.png (512²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Training_Landscape_Dirt_Albedo.png (2048²,DXT1) |

- **NS_VFX_Impact_Mud** (Niagara, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/Niagara/NS_VFX_Impact_Mud`)
  - `DEBRIS_HIGH_SETTINGS_0`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..200 | Sprite mat `DebrisMaster` subUV [2.0, 2.0] align VelocityAligned | curves: Curve_6: 1 → max 1 → 0 (82 samples); Curve_7: 1 → max 1 → 0 (102 samples); Curve_6: 1 → max 1 → 0 (82 samples); Curve_7: 1 → max 1 → 0 (102 samples)
  - `DEBRIS_LOW_SETTINGS_3`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..200 | Sprite mat `DebrisMaster` subUV [2.0, 2.0] align VelocityAligned | curves: Curve_6: 1 → max 1 → 0 (82 samples); Curve_7: 1 → max 1 → 0 (102 samples); Curve_6: 1 → max 1 → 0 (82 samples); Curve_7: 1 → max 1 → 0 (102 samples)
  - `Smoke_7`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..200 | Sprite mat `MI_ThinSmoke_01` subUV [8.0, 4.0] align - | curves: Curve_10: 0.025 → max 1 → 1 (53 samples); Curve_11: 0 → max 0.49988 → 0 (38 samples); Curve_9: 0.025 → max 1 → 1 (32 samples); Curve_10: 0.025 → max 1 → 1 (53 samples)
  - `Splash_9`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..200 | Sprite mat `MIC_BloodClip_A` subUV - align - | curves: Curve_6: 0 → max 1 → 1 (57 samples); Curve_7: 0.95 → max 0.95 → 0.35 (70 samples); Curve_6: 0 → max 1 → 1 (57 samples); Curve_7: 0.95 → max 0.95 → 0.35 (70 samples)
  - user params: User.Debris Color=[0.16146, 0.10407, 0.05466, 1.0], User.Smoke Color=[0.4, 0.3, 0.2, 1.0], User.Splash Color=[0.30729, 0.23308, 0.16485, 1.0]
  - material `MIC_BloodClip_A` → E:\Tools\deagle-work\ron_fx\particles\T_Blood_Clip_A.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Clip_N.png (128x128,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Reflection.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Noise_N_02.png (512x512,BC5)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `DebrisMaster` → E:\Tools\deagle-work\ron_fx\particles\Stones2x2.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\Gravel2x2_normal.png (512x512,BC5)
- **NS_VFX_Impact_Mud**: see above
- **NS_VFX_Impact_Mud**: see above
- **P_VFX_Impact_Mud** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Mud`)
  - emitter `SmokeKickRound`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.35/1.5 s
    - Size: 150/150/150/300/300/300
    - Velocity: -100/-100/200/100/100/400 radial 0
    - ColorOverLife: color 0.4/0.3/0.2/0.63389/0.47475/0.30996/0.84008/0.6312/0.41182/0.85238/0.65957/0.45707/0.86468/0.68794/0.50231/0.87698/0.71631/0.54756/0.88929/0.74468/0.5928/0.90159/0.77305/0.63805/0.91389/0.80142/0.68329/0.92619/0.82979/0.72853/0.93849/0.85815/0.77378/0.95079/0.88652/0.81902/0.9631/0.91489/0.86427/0.9754/0.94326/0.90951/0.9877/0.97163/0.95476/1/1/1 alpha 0.75/0.58871/0.42742/0.26898/0.22614/0.20145/0.17863/0.1576/0.13828/0.12061/0.10452/0.08992/0.07675/0.06494/0.0544/0.04506/0.03686/0.02972/0.02356/0.01832/0.01392/0.01028/0.00734/0.00502/0.00325/0.00195/0.00105/0.00048/0.00016/2e-05/-1e-05/0
    - SizeMultiplyLife: 0.025/0/0/0.12529/0.38539/0.05389/0.22037/0.57261/0.13814/0.30739/0.61946/0.23834/0.38348/0.58373/0.34006/0.44579/0.52323/0.42887/0.49144/0.49574/0.49033/0.52359/0.52416/0.52416/0.55473/0.55595/0.55595/0.58554/0.58729/0.58729/0.61592/0.6181/0.6181/0.64579/0.64829/0.64829/0.67505/0.67778/0.67778/0.70362/0.7065/0.7065/0.73141/0.73436/0.73436/0.75834/0.76129/0.76129/0.78431/0.7872/0.7872/0.80925/0.81202/0.81202/0.83306/0.83567/0.83567/0.85565/0.85807/0.85807/0.87694/0.87913/0.87913/0.89684/0.89878/0.89878/0.91525/0.91693/0.91693/0.9321/0.93351/0.93351/0.94729/0.94844/0.94844/0.96075/0.96163/0.96163/0.97237/0.97302/0.97302/0.98208/0.98252/0.98252/0.98978/0.99004/0.99004/0.9954/0.99552/0.99552/0.99883/0.99886/0.99886/1/1/1
    - Acceleration: -40/-40/-500/40/40/-200
  - emitter `SmokeKickRound`
    - Required: material `MI_ThinSmoke_01`, subUV 8x4, localSpace 0, duration 5.0, loops 1
    - Spawn: rate 0, bursts 1-1@0s
    - Lifetime: 0.1/0.15 s
    - Size: 45/150/150/65/300/300
    - Velocity: -100/-100/500/100/100/750 radial 0
    - ColorOverLife: color 0.4/0.3/0.2 alpha 1/0.78571/0.57143/0.35714/0.21429/0.14286/0.07143/0
    - SizeMultiplyLife: 0.025/0/0/0.2209/0.92053/0.07354/0.38886/1.61466/0.15343/0.53088/2.10542/0.23799/0.64921/2.41795/0.32553/0.74594/2.57586/0.41434/0.82326/2.60381/0.50274/0.8833/2.52582/0.58902/0.92822/2.36622/0.6715/0.96017/2.14925/0.74848/0.9813/1.89913/0.81827/0.99378/1.64012/0.87917/0.99972/1.3965/0.92948/1.00131/1.19238/0.96753/1.00069/1.05228/0.99157/1/1/1
    - Acceleration: -40/-40/-500/40/40/-200
  - emitter `Small_Burst`
    - Required: material `MIC_BloodClip_A`, subUV 1x1, localSpace True, duration 0.1, loops 1
    - Spawn: rate 0, bursts 2@0s
    - Lifetime: 0.15/0.25 s
    - Size: 35/35/35/75/75/75
    - Velocity: -25/-25/150/25/25/150 radial 0
    - Color: color 0.31771/0.22394/0.16213 alpha 1
    - SizeMultiplyLife: 0.3/0.3/0/0.35541/0.70522/0/0.49827/1.06903/0/0.69359/1.38381/0/0.90641/1.64193/0/1.10173/1.83576/0/1.24459/1.95766/0/1.3/2/0
    - Acceleration: 0/0/-100
  - emitter `Small`
    - Required: material `MIC_BloodClip_A`, subUV 1x1, localSpace True, duration 0.1, loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.2/0.35 s
    - Size: 35/35/35/55/55/55
    - Velocity: 0/0/250/0/0/400 radial 0
    - Color: color 0.30729/0.23308/0.16485 alpha 1
    - SizeMultiplyLife: 0.5/0.2/0/2/2/0
    - Acceleration: 0/0/-980
  - emitter `Bits`
    - Required: material `DebrisMaster`, subUV 2x2, localSpace 0, duration 0.1, loops 1
    - Spawn: rate 0, bursts 5@0s
    - Lifetime: 1/2 s
    - Size: 2/1/1/3/2/2
    - Velocity: -250/-250/100/250/250/400 radial 0
    - ColorOverLife: color 1/1/1 alpha 1/0
    - Acceleration: -
    - TypeData Gpu: CameraMotionBlurAmount=1.0, bClearExistingParticlesOnInit=True
  - emitter `Drops`
    - Required: material `mudmat`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 5@0s
    - Lifetime: 0.5/1.5 s
    - Size: 0.5/0.5/0.5/1/1/1
    - Velocity: -100/-100/150/100/100/750 radial 0
    - ColorOverLife: color 0.14583/0.11932/0.10606 alpha 1/0.99355/0.96491/0.90011/0.78518/0.60616/0.34909/0
    - Acceleration: -
    - TypeData Mesh: Mesh=mud
    - Spawn: rate 0, bursts 10@0s
    - Spawn: rate 0, bursts 50@0s
  - material `MIC_BloodClip_A` → E:\Tools\deagle-work\ron_fx\particles\T_Blood_Clip_A.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Clip_N.png (128x128,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Blood_Reflection.png (128x128,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Noise_N_02.png (512x512,BC5)
  - material `mudmat` → no textures (procedural)
  - material `MI_ThinSmoke_01` → E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_01_8x4.png (1024x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\T_thinSmoke_02_8x4.png (512x256,DXT5)
  - material `DebrisMaster` → E:\Tools\deagle-work\ron_fx\particles\Stones2x2.png (256x256,DXT5), E:\Tools\deagle-work\ron_fx\particles\Gravel2x2_normal.png (512x512,BC5)
- **P_VFX_Impact_Mud**: see above

### RON_Mud

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_VFX_Impact_Mud` | `M_BulletImpact_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Plaster_1.png (512²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Training_Landscape_Dirt_Albedo.png (2048²,DXT1) |
| ImpactEffects_Pistol | `NS_VFX_Impact_Mud` | `M_BulletImpact_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Plaster_1.png (512²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Training_Landscape_Dirt_Albedo.png (2048²,DXT1) |
| ShotgunImpactEffects | `NS_VFX_Impact_Mud` | `Decal_Impact_Inst_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Dirt_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Dirt_N.png (256²,BC5) |
| ExitImpactEffects | `P_VFX_Impact_Mud` | `Decal_Impact_Inst_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Dirt_BO.png (256²,DXT5)<br>E:\Tools\deagle-work\ron_fx\decals\Dirt_N.png (256²,BC5) |
| RicochetImpactEffects | `P_VFX_Impact_Mud` | `M_BulletImpact_Dirt`  | E:\Tools\deagle-work\ron_fx\decals\Bullet2.png (1024²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Plaster_1.png (512²,B8G8R8A8)<br>E:\Tools\deagle-work\ron_fx\decals\Training_Landscape_Dirt_Albedo.png (2048²,DXT1) |

- **NS_VFX_Impact_Mud**: see above
- **NS_VFX_Impact_Mud**: see above
- **NS_VFX_Impact_Mud**: see above
- **P_VFX_Impact_Mud**: see above
- **P_VFX_Impact_Mud**: see above

## water

### RON_Water

| impact BP | particle FX | decal material | decal textures |
|---|---|---|---|
| AllImpactEffects | `NS_BulletImpact_Water` | `None`  | - |
| ImpactEffects_Pistol | `P_BulletImpact_Water` | `None`  | - |
| ShotgunImpactEffects | `NS_BulletImpact_Water` | `None`  | - |
| ExitImpactEffects | `VFX_Impact_Water` | `None`  | - |
| RicochetImpactEffects | `P_BulletImpact_Water` | `None`  | - |

- **NS_BulletImpact_Water** (Niagara, `/Game/ReadyOrNot/VFX/VFXImpacts/Niagara/NS_BulletImpact_Water`)
  - `RIPPLES_7`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `M_WaterWave` subUV - align 0 | curves: Curve_33: 0 → max 1 → 1 (53 samples); Curve_34: 1 → max 1 → 0 (57 samples); Curve_35: 0.8 → max 1 → 1 (18 samples); Curve_33: 0 → max 1 → 1 (53 samples)
  - `SMALL_WATER_DRIPS_3`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `DefaultSpriteMaterial` subUV - align VelocityAligned | curves: Vector2DCurve_11: 1 → max 4.99083 → 0 (220 samples); Vector2DCurve_11: 1 → max 4.99083 → 0 (220 samples)
  - `STEAM_5`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `M_DropletMicro` subUV - align 0 | curves: Curve_22: 0.5 → max 3 → 3 (85 samples); Curve_23: 0.5 → max 0.5 → 0 (37 samples); Curve_22: 0.5 → max 3 → 3 (85 samples); Curve_23: 0.5 → max 0.5 → 0 (37 samples)
  - `WATER_DRIPS_2`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `M_AnimatedCaustics` subUV - align 0 | curves: Curve_22: 0.05 → max 1.5 → 1.5 (69 samples); Curve_23: 1 → max 1 → 0 (57 samples); Curve_22: 0.05 → max 1.5 → 1.5 (69 samples); Curve_23: 1 → max 1 → 0 (57 samples)
  - `WATER_SPLASH_6`: GPUComputeSim, localSpace 0, bounds -50..50 / -50..50 / -50..150 | Sprite mat `M_DropletMicro` subUV - align VelocityAligned | curves: Curve_11: 1 → max 1 → 0 (57 samples); Curve_11: 1 → max 1 → 0 (57 samples); Vector2DCurve_11: 0.25 → max 4.5 → 4.5 (190 samples); Vector2DCurve_11: 0.25 → max 4.5 → 4.5 (190 samples)
  - user params: User.Main Color=[1.0, 1.0, 1.0, 1.0]
  - material `M_AnimatedCaustics` → E:\Tools\deagle-work\ron_fx\particles\T_Caustics_A.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Cloud_ABC.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Smoke01.png (1024x1024,DXT5)
  - material `M_DropletMicro` → E:\Tools\deagle-work\ron_fx\particles\T_FineParticle_DustAndDoplet.png (512x512,DXT5)
  - material `M_WaterWave` → E:\Tools\deagle-work\ron_fx\particles\T_Ring02_N.png (512x512,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Ring_HeightMap.png (512x512,DXT1)
  - material `DefaultSpriteMaterial` → no textures (procedural)
- **P_BulletImpact_Water** (Cascade, `/Game/ReadyOrNot/VFX/VFXImpacts/P_BulletImpact_Water`)
  - emitter `RoundWaveDistortion`
    - Required: material `M_WaterWave`, subUV 1x1, localSpace 0, duration 0.3, loops 1
    - Spawn: rate 15, bursts 1@0s
    - Lifetime: 0.75/1.5 s
    - Size: 100/100/100/150/150/150
    - Velocity: 0/0/0 radial 0
    - ColorOverLife: color 1/1/1 alpha 1/0
    - SizeMultiplyLife: 0/0/0/0.26572/0.24124/0.24874/0.49037/0.45637/0.46679/0.67401/0.64138/0.65138/0.81673/0.79225/0.79975/0.91859/0.90499/0.90916/0.97965/0.97557/0.97682/1/1/1
  - emitter `None`
    - Required: material `M_AnimatedCaustics`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 5-11@0s
    - Lifetime: 0.5/0.75 s
    - Size: 25/25/25/50/50/50
    - Velocity: -75/-75/100/75/75/250 radial 0
    - ColorOverLife: color 1/1/1 alpha 1/0
    - Acceleration: 0/0/-800/0/0/-600
    - SizeMultiplyLife: 0.05/0.3/0.05/1.5/1.5/1.5
  - emitter `None`
    - Required: material `M_DropletMicro`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 3-5@0s
    - Lifetime: 0.35/0.75 s
    - Size: 15/15/15/25/25/25
    - Velocity: -100/-100/400/100/100/1000 radial 0
    - ColorOverLife: color 1/1/1 alpha 1/0
    - SizeMultiplyLife: 0.25/0.25/0.25/0.26587/0.4881/0.26587/0.28175/0.72619/0.28175/0.29762/0.96429/0.29762/0.31349/1.20238/0.31349/0.32937/1.44048/0.32937/0.34524/1.67857/0.34524/0.36111/1.91667/0.36111/0.37698/2.15476/0.37698/0.39286/2.39286/0.39286/0.40873/2.63095/0.40873/0.4246/2.86905/0.4246/0.44048/3.10714/0.44048/0.45635/3.34524/0.45635/0.47222/3.58333/0.47222/0.4881/3.82143/0.4881/0.50794/4.00265/0.50794/0.53968/4.01323/0.53968/0.57143/4.02381/0.57143/0.60317/4.03439/0.60317/0.63492/4.04497/0.63492/0.66667/4.05556/0.66667/0.69841/4.06614/0.69841/0.73016/4.07672/0.73016/0.7619/4.0873/0.7619/0.79365/4.09788/0.79365/0.8254/4.10847/0.8254/0.85714/4.11905/0.85714/0.88889/4.12963/0.88889/0.92063/4.14021/0.92063/0.95238/4.15079/0.95238/0.98413/4.16138/0.98413/1.01587/4.17196/1.01587/1.04762/4.18254/1.04762/1.07937/4.19312/1.07937/1.11111/4.2037/1.11111/1.14286/4.21429/1.14286/1.1746/4.22487/1.1746/1.20635/4.23545/1.20635/1.2381/4.24603/1.2381/1.26984/4.25661/1.26984/1.30159/4.2672/1.30159/1.33333/4.27778/1.33333/1.36508/4.28836/1.36508/1.39683/4.29894/1.39683/1.42857/4.30952/1.42857/1.46032/4.32011/1.46032/1.49206/4.33069/1.49206/1.52381/4.34127/1.52381/1.55556/4.35185/1.55556/1.5873/4.36243/1.5873/1.61905/4.37302/1.61905/1.65079/4.3836/1.65079/1.68254/4.39418/1.68254/1.71429/4.40476/1.71429/1.74603/4.41534/1.74603/1.77778/4.42593/1.77778/1.80952/4.43651/1.80952/1.84127/4.44709/1.84127/1.87302/4.45767/1.87302/1.90476/4.46825/1.90476/1.93651/4.47884/1.93651/1.96825/4.48942/1.96825/2/4.5/2
  - emitter `None`
    - Required: material `M_DustPuff`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1-3@0s
    - Lifetime: 0.15/0.35 s
    - Size: 35/35/35/50/50/50
    - Velocity: -150/-150/300/150/150/800 radial 0
    - ColorOverLife: color 1/1/1 alpha 0.75/0
    - SizeMultiplyLife: 0.25/0.25/0.25/0.26587/0.4881/0.26587/0.28175/0.72619/0.28175/0.29762/0.96429/0.29762/0.31349/1.20238/0.31349/0.32937/1.44048/0.32937/0.34524/1.67857/0.34524/0.36111/1.91667/0.36111/0.37698/2.15476/0.37698/0.39286/2.39286/0.39286/0.40873/2.63095/0.40873/0.4246/2.86905/0.4246/0.44048/3.10714/0.44048/0.45635/3.34524/0.45635/0.47222/3.58333/0.47222/0.4881/3.82143/0.4881/0.50529/4.00265/0.50529/0.52646/4.01323/0.52646/0.54762/4.02381/0.54762/0.56878/4.03439/0.56878/0.58995/4.04497/0.58995/0.61111/4.05556/0.61111/0.63228/4.06614/0.63228/0.65344/4.07672/0.65344/0.6746/4.0873/0.6746/0.69577/4.09788/0.69577/0.71693/4.10847/0.71693/0.7381/4.11905/0.7381/0.75926/4.12963/0.75926/0.78042/4.14021/0.78042/0.80159/4.15079/0.80159/0.82275/4.16138/0.82275/0.84392/4.17196/0.84392/0.86508/4.18254/0.86508/0.88624/4.19312/0.88624/0.90741/4.2037/0.90741/0.92857/4.21429/0.92857/0.94974/4.22487/0.94974/0.9709/4.23545/0.9709/0.99206/4.24603/0.99206/1.01323/4.25661/1.01323/1.03439/4.2672/1.03439/1.05556/4.27778/1.05556/1.07672/4.28836/1.07672/1.09788/4.29894/1.09788/1.11905/4.30952/1.11905/1.14021/4.32011/1.14021/1.16138/4.33069/1.16138/1.18254/4.34127/1.18254/1.2037/4.35185/1.2037/1.22487/4.36243/1.22487/1.24603/4.37302/1.24603/1.2672/4.3836/1.2672/1.28836/4.39418/1.28836/1.30952/4.40476/1.30952/1.33069/4.41534/1.33069/1.35185/4.42593/1.35185/1.37302/4.43651/1.37302/1.39418/4.44709/1.39418/1.41534/4.45767/1.41534/1.43651/4.46825/1.43651/1.45767/4.47884/1.45767/1.47884/4.48942/1.47884/1.5/4.5/1.5
  - emitter `None`
    - Required: material `M_DustFine`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 3-5@0s
    - Lifetime: 0.5/1 s
    - Size: 15/15/15/25/25/25
    - Velocity: -50/-50/150/50/50/500 radial 0
    - ColorOverLife: color 1/1/1 alpha 0.5/0
    - SizeMultiplyLife: 0.25/0.25/0.25/0.28226/0.47581/0.28226/0.31452/0.70161/0.31452/0.34677/0.92742/0.34677/0.37903/1.15323/0.37903/0.41129/1.37903/0.41129/0.44355/1.60484/0.44355/0.47581/1.83065/0.47581/0.51075/2.01075/0.51075/0.55376/2.05376/0.55376/0.59677/2.09677/0.59677/0.63978/2.13979/0.63978/0.6828/2.1828/0.6828/0.72581/2.22581/0.72581/0.76882/2.26882/0.76882/0.81183/2.31183/0.81183/0.85484/2.35484/0.85484/0.89785/2.39785/0.89785/0.94086/2.44086/0.94086/0.98387/2.48387/0.98387/1.02688/2.52688/1.02688/1.06989/2.56989/1.06989/1.1129/2.6129/1.1129/1.15591/2.65591/1.15591/1.19892/2.69892/1.19892/1.24194/2.74194/1.24194/1.28495/2.78495/1.28495/1.32796/2.82796/1.32796/1.37097/2.87097/1.37097/1.41398/2.91398/1.41398/1.45699/2.95699/1.45699/1.5/3/1.5
  - emitter `None`
    - Required: material `M_DropletMicro`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 3-5@0s
    - Lifetime: 0.75/1.5 s
    - Size: 25/25/25/40/40/40
    - Velocity: -100/-100/300/100/100/800 radial 0
    - ColorOverLife: color 1/1/1 alpha 0.5/0
    - SizeMultiplyLife: 0.5/0.5/0.5/0.51969/0.51969/0.51969/0.53937/0.53937/0.53937/0.55906/0.55906/0.55906/0.57874/0.57874/0.57874/0.59843/0.59843/0.59843/0.61811/0.61811/0.61811/0.6378/0.6378/0.6378/0.65748/0.65748/0.65748/0.67717/0.67717/0.67717/0.69685/0.69685/0.69685/0.71654/0.71654/0.71654/0.73622/0.73622/0.73622/0.75591/0.75591/0.75591/0.77559/0.77559/0.77559/0.79528/0.79528/0.79528/0.81496/0.81496/0.81496/0.83465/0.83465/0.83465/0.85433/0.85433/0.85433/0.87402/0.87402/0.87402/0.8937/0.8937/0.8937/0.91339/0.91339/0.91339/0.93307/0.93307/0.93307/0.95276/0.95276/0.95276/0.97244/0.97244/0.97244/0.99213/0.99213/0.99213/1.01181/1.01181/1.01181/1.0315/1.0315/1.0315/1.05118/1.05118/1.05118/1.07087/1.07087/1.07087/1.09055/1.09055/1.09055/1.11024/1.11024/1.11024/1.12992/1.12992/1.12992/1.14961/1.14961/1.14961/1.16929/1.16929/1.16929/1.18898/1.18898/1.18898/1.20866/1.20866/1.20866/1.22835/1.22835/1.22835/1.24803/1.24803/1.24803/1.26772/1.26772/1.26772/1.2874/1.2874/1.2874/1.30709/1.30709/1.30709/1.32677/1.32677/1.32677/1.34646/1.34646/1.34646/1.36614/1.36614/1.36614/1.38583/1.38583/1.38583/1.40551/1.40551/1.40551/1.4252/1.4252/1.4252/1.44488/1.44488/1.44488/1.46457/1.46457/1.46457/1.48425/1.48425/1.48425/1.50394/1.50394/1.50394/1.52362/1.52362/1.52362/1.54331/1.54331/1.54331/1.56299/1.56299/1.56299/1.58268/1.58268/1.58268/1.60236/1.60236/1.60236/1.62205/1.62205/1.62205/1.64173/1.64173/1.64173/1.66142/1.66142/1.66142/1.6811/1.6811/1.6811/1.70079/1.70079/1.70079/1.72047/1.72047/1.72047/1.74016/1.74016/1.74016/1.75984/1.75984/1.75984/1.77953/1.77953/1.77953/1.79921/1.79921/1.79921/1.8189/1.8189/1.8189/1.83858/1.83858/1.83858/1.85827/1.85827/1.85827/1.87795/1.87795/1.87795/1.89764/1.89764/1.89764/1.91732/1.91732/1.91732/1.93701/1.93701/1.93701/1.95669/1.95669/1.95669/1.97638/1.97638/1.97638/1.99606/1.99606/1.99606/2.01575/2.01575/2.01575/2.03543/2.03543/2.03543/2.05512/2.05512/2.05512/2.0748/2.0748/2.0748/2.09449/2.09449/2.09449/2.11417/2.11417/2.11417/2.13386/2.13386/2.13386/2.15354/2.15354/2.15354/2.17323/2.17323/2.17323/2.19291/2.19291/2.19291/2.2126/2.2126/2.2126/2.23228/2.23228/2.23228/2.25197/2.25197/2.25197/2.27165/2.27165/2.27165/2.29134/2.29134/2.29134/2.31102/2.31102/2.31102/2.33071/2.33071/2.33071/2.35039/2.35039/2.35039/2.37008/2.37008/2.37008/2.38976/2.38976/2.38976/2.40945/2.40945/2.40945/2.42913/2.42913/2.42913/2.44882/2.44882/2.44882/2.4685/2.4685/2.4685/2.48819/2.48819/2.48819/2.50787/2.50787/2.50787/2.52756/2.52756/2.52756/2.54724/2.54724/2.54724/2.56693/2.56693/2.56693/2.58661/2.58661/2.58661/2.6063/2.6063/2.6063/2.62598/2.62598/2.62598/2.64567/2.64567/2.64567/2.66535/2.66535/2.66535/2.68504/2.68504/2.68504/2.70472/2.70472/2.70472/2.72441/2.72441/2.72441/2.74409/2.74409/2.74409/2.76378/2.76378/2.76378/2.78346/2.78346/2.78346/2.80315/2.80315/2.80315/2.82283/2.82283/2.82283/2.84252/2.84252/2.84252/2.8622/2.8622/2.8622/2.88189/2.88189/2.88189/2.90157/2.90157/2.90157/2.92126/2.92126/2.92126/2.94094/2.94094/2.94094/2.96063/2.96063/2.96063/2.98031/2.98031/2.98031/1.5/3/1.5
    - Acceleration: 0/0/-700/0/0/-400
  - emitter `None`
    - Required: material `M_RoundDistortion`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 17-31@0s
    - Lifetime: 0.5/1 s
    - Size: 1/1/1/3/3/3
    - Velocity: -150/-150/50/150/150/500 radial 0
    - ColorOverLife: color 1/1/1 alpha 0.1
    - Acceleration: 0/0/-1500/0/0/-1000
    - SizeMultiplyLife: 1/1/1/1/1.31496/1/1/1.62992/1/1/1.94488/1/1/2.25984/1/1/2.5748/1/1/2.88976/1/1/3.20472/1/1/3.51969/1/1/3.83465/1/1/4.14961/1/1/4.46457/1/1/4.77953/1/1/4.97638/1/1/4.89764/1/1/4.8189/1/1/4.74016/1/1/4.66142/1/1/4.58268/1/1/4.50394/1/1/4.4252/1/1/4.34646/1/1/4.26772/1/1/4.18898/1/1/4.11024/1/1/4.0315/1/1/3.95276/1/1/3.87402/1/1/3.79528/1/1/3.71654/1/1/3.6378/1/1/3.55906/1/1/3.48031/1/1/3.40157/1/1/3.32283/1/1/3.24409/1/1/3.16535/1/1/3.08661/1/1/3.00787/1/1/2.92913/1/1/2.85039/1/1/2.77165/1/1/2.69291/1/1/2.61417/1/1/2.53543/1/1/2.45669/1/1/2.37795/1/1/2.29921/1/1/2.22047/1/1/2.14173/1/1/2.06299/1/1/1.98425/1/1/1.90551/1/1/1.82677/1/1/1.74803/1/1/1.66929/1/1/1.59055/1/1/1.51181/1/1/1.43307/1/1/1.35433/1/1/1.27559/1/1/1.19685/1/1/1.11811/1/1/1.03937/1/0.99213/0.99213/0.99213/0.97638/0.97638/0.97638/0.96063/0.96063/0.96063/0.94488/0.94488/0.94488/0.92913/0.92913/0.92913/0.91339/0.91339/0.91339/0.89764/0.89764/0.89764/0.88189/0.88189/0.88189/0.86614/0.86614/0.86614/0.85039/0.85039/0.85039/0.83465/0.83465/0.83465/0.8189/0.8189/0.8189/0.80315/0.80315/0.80315/0.7874/0.7874/0.7874/0.77165/0.77165/0.77165/0.75591/0.75591/0.75591/0.74016/0.74016/0.74016/0.72441/0.72441/0.72441/0.70866/0.70866/0.70866/0.69291/0.69291/0.69291/0.67717/0.67717/0.67717/0.66142/0.66142/0.66142/0.64567/0.64567/0.64567/0.62992/0.62992/0.62992/0.61417/0.61417/0.61417/0.59843/0.59843/0.59843/0.58268/0.58268/0.58268/0.56693/0.56693/0.56693/0.55118/0.55118/0.55118/0.53543/0.53543/0.53543/0.51969/0.51969/0.51969/0.50394/0.50394/0.50394/0.48819/0.48819/0.48819/0.47244/0.47244/0.47244/0.45669/0.45669/0.45669/0.44094/0.44094/0.44094/0.4252/0.4252/0.4252/0.40945/0.40945/0.40945/0.3937/0.3937/0.3937/0.37795/0.37795/0.37795/0.3622/0.3622/0.3622/0.34646/0.34646/0.34646/0.33071/0.33071/0.33071/0.31496/0.31496/0.31496/0.29921/0.29921/0.29921/0.28346/0.28346/0.28346/0.26772/0.26772/0.26772/0.25197/0.25197/0.25197/0.23622/0.23622/0.23622/0.22047/0.22047/0.22047/0.20472/0.20472/0.20472/0.18898/0.18898/0.18898/0.17323/0.17323/0.17323/0.15748/0.15748/0.15748/0.14173/0.14173/0.14173/0.12598/0.12598/0.12598/0.11024/0.11024/0.11024/0.09449/0.09449/0.09449/0.07874/0.07874/0.07874/0.06299/0.06299/0.06299/0.04724/0.04724/0.04724/0.0315/0.0315/0.0315/0.01575/0.01575/0.01575/0/0/0
  - material `M_AnimatedCaustics` → E:\Tools\deagle-work\ron_fx\particles\T_Caustics_A.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Cloud_ABC.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Smoke01.png (1024x1024,DXT5)
  - material `M_DropletMicro` → E:\Tools\deagle-work\ron_fx\particles\T_FineParticle_DustAndDoplet.png (512x512,DXT5)
  - material `M_DustFine` → E:\Tools\deagle-work\ron_fx\particles\T_GradientRound_A.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_Noise_RVB.png (512x512,DXT1)
  - material `M_DustPuff` → E:\Tools\deagle-work\ron_fx\particles\T_FineParticle_DustAndDoplet.png (512x512,DXT5)
  - material `M_RoundDistortion` → E:\Tools\deagle-work\ron_fx\particles\T_SpericalNormal.png (512x512,BC5)
  - material `M_WaterWave` → E:\Tools\deagle-work\ron_fx\particles\T_Ring02_N.png (512x512,BC5), E:\Tools\deagle-work\ron_fx\particles\T_Ring_HeightMap.png (512x512,DXT1)
- **NS_BulletImpact_Water**: see above
- **VFX_Impact_Water** (Cascade, `/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/VFX_Impact_Water`)
  - emitter `Drops`
    - Required: material `M_raindrop2`, subUV 1x1, localSpace 0, duration 0.2, loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.1/1 s
    - Size: 5/10/10
    - Velocity: -35/-35/250/35/35/20 radial 0
    - ColorOverLife: color 1/1/1 alpha 2/1.93814/1.76665/1.50652/1.17877/0.80443/0.4045/0
    - Acceleration: -
  - emitter `Drops2`
    - Required: material `M_raindrop2`, subUV 1x1, localSpace 0, duration 0.2, loops 1
    - Spawn: rate 0, bursts 50@0s
    - Lifetime: 0.25/0.5 s
    - Size: 5/10/10
    - Velocity: -100/-100/50/100/100/800 radial 0
    - ColorOverLife: color 1/1/1 alpha 1/0
    - Acceleration: -
    - TypeData Gpu: 
  - emitter `Drops2`
    - Required: material `M_raindrop2`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 100@0s
    - Lifetime: 0.1/0.25 s
    - Size: 5/10/10
    - Velocity: -100/-100/250/100/100/1000 radial 0
    - ColorOverLife: color 1/1/1 alpha 1/0
    - Acceleration: -
    - TypeData Gpu: 
  - emitter `Powder`
    - Required: material `Whisps_trans`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 1/2 s
    - Size: 150/250/250/200/600/600
    - Velocity: -10/-10/100/10/10/500 radial 0
    - ColorOverLife: color 0.81177/0.81177/0.81177 alpha 0/0.45393/0.51517/0.50672/0.49828/0.48983/0.48138/0.47294/0.46449/0.45605/0.4476/0.43916/0.43071/0.42227/0.41382/0.40538/0.39693/0.38849/0.38004/0.3716/0.36315/0.3547/0.34626/0.33781/0.32937/0.32092/0.31248/0.30403/0.29559/0.28714/0.2787/0.27025/0.26181/0.25336/0.24492/0.23647/0.22802/0.21958/0.21113/0.20269/0.19424/0.1858/0.17735/0.16891/0.16046/0.15202/0.14357/0.13513/0.12668/0.11823/0.10979/0.10134/0.0929/0.08445/0.07601/0.06756/0.05912/0.05067/0.04223/0.03378/0.02534/0.01689/0.00845/0
    - SizeMultiplyLife: 0/0/0/0.11264/0.11264/0.11264/0.27776/0.27776/0.27776/0.4699/0.4699/0.4699/0.66364/0.66364/0.66364/0.83353/0.83353/0.83353/0.95413/0.95413/0.95413/1/1/1
    - Acceleration: -
    - Acceleration: 0/-10/20/0/-500/96
  - emitter `Powder`
    - Required: material `Whisps_trans`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 1@0s
    - Lifetime: 0.1/0.2 s
    - Size: 25/25/25/35/35/35
    - Velocity: -10/-10/100/10/10/500 radial 0
    - ColorOverLife: color 0.81177/0.81177/0.81177 alpha 5/13.6692/22.3385/24.8611/24.6606/24.4601/24.2596/24.0591/23.8586/23.6581/23.4576/23.2571/23.0566/22.8561/22.6556/22.4551/22.2547/22.0542/21.8537/21.6532/21.4527/21.2522/21.0517/20.8512/20.6507/20.4502/20.2497/20.0492/19.8487/19.6483/19.4478/19.2473/19.0468/18.8463/18.6458/18.4453/18.2448/18.0443/17.8438/17.6433/17.4428/17.2423/17.0419/16.8414/16.6409/16.4404/16.2399/16.0394/15.8389/15.6384/15.4379/15.2374/15.0369/14.8364/14.6359/14.4354/14.235/14.0345/13.834/13.6335/13.433/13.2325/13.032/12.8315/12.631/12.4305/12.23/12.0295/11.8291/11.6286/11.4281/11.2276/11.0271/10.8266/10.6261/10.4256/10.2251/10.0246/9.82413/9.62364/9.42314/9.22265/9.02216/8.82167/8.62117/8.42068/8.22019/8.0197/7.8192/7.61871/7.41822/7.21773/7.01723/6.81674/6.61625/6.41576/6.21527/6.01477/5.81428/5.61379/5.4133/5.2128/5.01231/4.81182/4.61133/4.41083/4.21034/4.00985/3.80935/3.60886/3.40837/3.20788/3.00739/2.80689/2.6064/2.40591/2.20542/2.00492/1.80443/1.60394/1.40345/1.20296/1.00246/0.80197/0.60148/0.40098/0.20049/0
    - SizeMultiplyLife: 0/0/0/0.11264/0.11264/0.11264/0.27776/0.27776/0.27776/0.4699/0.4699/0.4699/0.66364/0.66364/0.66364/0.83353/0.83353/0.83353/0.95413/0.95413/0.95413/1/1/1
    - Acceleration: -
    - Acceleration: 0/-10/20/0/-500/96
  - emitter `Drops`
    - Required: material `M_raindrop2`, subUV 1x1, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 50@0s
    - Lifetime: 0.25/10 s
    - Size: 5/10/10
    - Velocity: -100/-100/250/100/100/1250 radial 0
    - ColorOverLife: color 1/1/1 alpha 1/0.99355/0.96491/0.90011/0.78518/0.60616/0.34909/0
    - Acceleration: -
  - emitter `Spray`
    - Required: material `SpeckledEmissive`, subUV 8x8, localSpace 0, duration 1(def), loops 1
    - Spawn: rate 0, bursts 2@0s
    - Lifetime: 0.25/1.25 s
    - Size: 50/50/50/100/100/100
    - Velocity: -10/-10/590/10/10/1200 radial 0
    - ColorOverLife: color 15/15/15/1/1/1 alpha 0/0.39911/0.79821/0.92421/0.63439/0.28782/0.09063/0.08514/0.08339/0.08165/0.07994/0.07825/0.07658/0.07494/0.07332/0.07172/0.07015/0.0686/0.06707/0.06556/0.06407/0.0626/0.06116/0.05974/0.05833/0.05695/0.05559/0.05425/0.05294/0.05164/0.05036/0.0491/0.04787/0.04665/0.04545/0.04427/0.04311/0.04197/0.04085/0.03975/0.03867/0.0376/0.03656/0.03553/0.03452/0.03353/0.03256/0.0316/0.03067/0.02975/0.02885/0.02796/0.02709/0.02624/0.02541/0.02459/0.02379/0.023/0.02223/0.02148/0.02075/0.02003/0.01932/0.01863/0.01796/0.0173/0.01665/0.01602/0.01541/0.01481/0.01422/0.01365/0.0131/0.01255/0.01202/0.01151/0.01101/0.01052/0.01004/0.00958/0.00913/0.0087/0.00827/0.00786/0.00746/0.00708/0.0067/0.00634/0.00599/0.00565/0.00533/0.00501/0.00471/0.00441/0.00413/0.00386/0.0036/0.00335/0.00311/0.00288/0.00266/0.00245/0.00225/0.00206/0.00188/0.00171/0.00154/0.00139/0.00125/0.00111/0.00098/0.00087/0.00075/0.00065/0.00056/0.00047/0.00039/0.00032/0.00026/0.0002/0.00015/0.00011/8e-05/5e-05/3e-05/1e-05/0/0
    - SizeMultiplyLife: 0/0/0/0.11264/0.11264/0.11264/0.27776/0.27776/0.27776/0.4699/0.4699/0.4699/0.66364/0.66364/0.66364/0.83353/0.83353/0.83353/0.95413/0.95413/0.95413/1/1/1
    - Acceleration: -
    - Spawn: rate 0, bursts 25@0s
    - Spawn: rate 0, bursts 25@0s
    - Spawn: rate 0, bursts 2@0s
    - Spawn: rate 0, bursts 25@0s
    - Spawn: rate 0, bursts 25@0s
  - material `M_raindrop2` → E:\Tools\deagle-work\ron_fx\particles\T_raindrop.png (8x8,DXT1), E:\Tools\deagle-work\ron_fx\particles\T_raindrop_normal.png (256x256,BC5), E:\Tools\deagle-work\ron_fx\particles\TilingNoise06_N.png (512x512,BC5)
  - material `SpeckledEmissive` → E:\Tools\deagle-work\ron_fx\particles\Noise_Anisotropic2_height.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\Speckled_tile.png (512x512,DXT1), E:\Tools\deagle-work\ron_fx\particles\Speckled_tile_2.png (520x520,DXT1), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
  - material `Whisps_trans` → E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8.png (512x512,DXT5), E:\Tools\deagle-work\ron_fx\particles\whisp_One_512_8x8_Normal.png (256x256,BC5)
- **P_BulletImpact_Water**: see above

## Tracers

- Asset: `/Game/ReadyOrNot/VFX/VFXImpacts/P_BulletTracer` (Cascade). It is spawned by the native `BulletTracer` actor (TracerParticle, SmokeParticle components).
- Emitter `Tracer`, **ribbon** type (`ParticleModuleTypeDataRibbon`, MaxParticleInTrailCount 25), trail source = actor socket `TracerSource` (strength 100).
- Spawn: 1 particle per 25 cm travelled (SpawnPerUnit, UnitScalar 25), rate 0. Lifetime **0.1 s**. Start size 0.25–2 (random per axis, i.e. ribbon width 0.25–2 cm). Start velocity 0.
- Colour over life: **HDR (40, 7, 0) → (1, 0, 0)** (orange-red emissive ×40 fading to dim red; TimeScale 1.11). Alpha over life 1 → 0 (TimeScale 1.33).
- Acceleration: random −500..500 / −1500..1500 / −500..500 cm/s² in world space (wobble along the trail).
- Material `/Game/ReadyOrNot/VFX/Materials/M_Ribbon`: a base Material with **no texture references** (procedural gradient; expression graph not readable from cooked data). Nothing to export.
- Length vs camera distance: no field; length = trail of ≤25 segments × 25 cm (~6 m) that fades in 0.1 s. **Name-only:** BaseMagazineWeapon.BulletTracers (class array, not overridden).
- Which rounds: every gun has `bSpawnTracer = true` **and** `bNoSpawnTracerForFiringPlayer = true`, so you only see tracers fired by others, never your own. No per-ammo tracer flag in AmmoDataTable.

## Muzzle-side bits

- Shell casings: `BulletShell_C` (weapon `ShellClass`), `ShellParticle` component on the gun, `ShellBounceFMODAudio` sound. No dedicated ground-puff FX found; footstep-style puffs exist (`/Game/ReadyOrNot/VFX/VFXFootsteps/P_Walk_ConcreteDustPuff` etc.) and are used by Walk/RunImpactEffects (MaxLifespan 5 s, decals 6–12).
- Bullet whiz / crack: **sound only** (`BulletProjectile.BulletWizzSound`, MinimumDistanceForWizz 500 cm). No heat-distortion visual in the data.
- Gun heat haze: `HeatSmokeParticleComponent` on weapons (HeatPerShot 0.1, HeatMinimumTime 0.8 s on the G19); particle asset not decoded here.

## Surface → effect table (rifle / AllImpactEffects, every surface)

| surface | particle FX | decal |
|---|---|---|
| DefaultImpactFx | `NS_Bullet_Impact_Concrete_Big` | `Decal_Impact_Inst_Concrete` |
| RON_Aluminium | `NS_Bullet_Impact_Metal_Big` | `M_BulletImpact_Metal` |
| RON_Asphalt | `NS_BulletImpact_Asphalt` | `Decal_Impact_Inst_Asphalt` |
| RON_Brick | `NS_BulletImpact_Brick` | `M_BulletImpact_Brick_01` |
| RON_CarbonFibre | `NS_VFX_Impact_Kevlar` | `Blood_Carpet_02_Inst` |
| RON_Cardboard | `NS_BulletImpact_CardBoard` | `Decal_Impact_Inst_Cardboard` |
| RON_Ceramic | `NS_Bullet_Impact_Concrete_Big` | `M_BulletImpact_Ceramic_01` |
| RON_ConcreteSoft | `NS_Bullet_Impact_Concrete_Big` | `M_BulletImpact_Concrete_01` |
| RON_ConcreteStrong | `NS_Bullet_Impact_Concrete_Big` | `M_BulletImpact_Concrete_02` |
| RON_Dirt | `NS_BulletImpact_Dirt` | `M_BulletImpact_Dirt` |
| RON_Drywall | `NS_BulletImpact_Plaster` | `M_BulletImpact_Drywall_01` |
| RON_Electrical | `NS_Car_Impact` | `M_BulletImpact_Metal` |
| RON_EnergyShield | `NS_WeaponSmoke_M2011_Muzzle` | `M_BulletImpact_Drywall_01` |
| RON_Fabric_Carpet | `NS_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Carpet` |
| RON_Fabric_Stuffing | `NS_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Stuffing` |
| RON_Fabric_Thin | `NS_Bullet_Impact_Fabric_Big` | `Decal_Impact_Inst_Fabric` |
| RON_Flesh | `NS_Blood_Smoke_Impact_Red_Static` | `Blood_Carpet_02_Inst` |
| RON_Galvanized | `NS_Bullet_Impact_Metal_Big` | `M_BulletImpact_Metal` |
| RON_Glass_Plate | `NS_BulletImpact_Glass` | `None` |
| RON_Glass_Windshield | `NS_BulletImpact_Glass` | `None` |
| RON_Grass | `NS_BulletImpact_Grass` | `M_BulletImpact_Dirt` |
| RON_Gravel | `NS_BulletImpact_Gravel` | `M_BulletImpact_Concrete_01` |
| RON_Lava | `NS_VFX_Impact_Paper` | `Blue1` |
| RON_Lead | `NS_Bullet_Impact_Metal_Big` | `M_BulletImpact_Metal` |
| RON_Leaves | `NS_BulletImpact_Foliage` | `Decal_Impact_Inst_Dirt` |
| RON_Limestone | `NS_BulletImpact_Brick` | `Decal_Impact_Inst_Concrete_Hard` |
| RON_Mahogany | `NS_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01` |
| RON_Marble_Coated | `NS_Bullet_Impact_Concrete_Big` | `M_BulletImpact_Concrete_02` |
| RON_Marble_Thick | `NS_Bullet_Impact_Concrete_Big` | `M_BulletImpact_Concrete_02` |
| RON_Mud | `NS_VFX_Impact_Mud` | `M_BulletImpact_Dirt` |
| RON_Paper | `NS_BulletImpact_Paper` | `Decal_Impact_Inst_Paper` |
| RON_Pine | `NS_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01` |
| RON_Plaster | `NS_BulletImpact_Plaster` | `M_BulletImpact_Drywall_01` |
| RON_Plastic | `NS_VFX_Impact_Plastic` | `Decal_Impact_Inst_Plastic` |
| RON_Plywood | `NS_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01` |
| RON_Polystyrene | `NS_VFX_Impact_Polystyrene` | `Decal_Impact_Inst_Plastic` |
| RON_Powder | `NS_BulletImpact_Powder` | `M_BulletImpact_Plaster_01` |
| RON_Rock | `NS_Bullet_Impact_Concrete_Big` | `M_BulletImpact_Brick_01` |
| RON_Rubber | `NS_BulletImpact_Rubber` | `Decal_Impact_Inst_Rubber` |
| RON_Sand | `NS_BulletImpact_Sand` | `M_BulletImpact_Dirt` |
| RON_Soil | `NS_VFX_Impact_Mud` | `M_BulletImpact_Dirt` |
| RON_Steel | `NS_Bullet_Impact_Metal_Big` | `M_BulletImpact_Metal` |
| RON_Tin | `NS_VFX_Impact_Tin` | `M_BulletImpact_Metal` |
| RON_Treewood | `NS_Bullet_Impact_Wood_Big` | `M_BulletImpact_Wood_01` |
| RON_Wallpaper | `NS_VFX_Impact_Wallpaper` | `M_BulletImpact_Drywall_01` |
| RON_Water | `NS_BulletImpact_Water` | `None` |
| RON_Vehicle | `NS_Bullet_Impact_Metal_Big` | `M_BulletImpact_Metal` |
| RON_Bulletproof_Glass | `NS_BulletImpact_Bulletproof_Glass` | `None` |
