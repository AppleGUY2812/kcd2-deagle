# Ready or Not ballistics read-out

Values decoded from the user's Steam install (UE5.3, read-only). Raw data: `ron_ballistics.json` (RoN property names kept).
Every asset decoded byte-exact with the community Mappings.usmap (build 113804, 2026-09-04).

**Legend:** `-` = not set on that asset → equals the C++ default in the exe (**not read**). `0`/`F` = explicitly zero/false. Distances cm unless marked m; speeds cm/s.

## 1. Ammo (AmmoDataTable, 41 rows; every field stored per row)

Damage curve = `ExternalCurve` (distance m : damage). Curve x is assumed to be UE cm (divided by 100 here). That fits pistols (1.5–50 m), but rifle curves then run to 3000 m, so treat curve x units as unverified. Body multipliers H/U/L/A/Ha/Le/F = Head / UpperBody / LowerBody / Arm / Hand / Leg / Foot.

| row | caliber | variety | Damage | damage curve | pellets | spread | PenLevel | PenDist | DurabilityDmg | Ricochet | Spall dmg/r | ArmouredHits | Hits | Artery | Dismember | Wound | H/U/L/A/Ha/Le/F | ragdoll D/H/A/L/T | IgnoresArmour |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Default |  | JHP | 30 | Curve_Damage_Pistol_G19: 1.5m:70 23.949m:65 50m:25 | 1 | 0 | 2 | 400 | 1 | 1 | 10/50 | 0 | 0.2 | 0 | 0 | 35 | 2/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 12gaBuck | 12 Gauge | Buckshot | 30 | Curve_Damage_Shotgun_590: 25m:40 72.816m:37.5 151.124m:35 | 8 | 0.75/0.75 | 1 | 150 | 0.125 | 0.4 | 0/0 | 0.9 | 0.3 | 0.5 | 25 | 45 | 3/1/1/1/0.1/1/0.1 | 3000/3000/3000/3000/3000 | F |
| 12gaSlug | 12 Gauge | Slug | 30 | Curve_Damage_Shotgun_590_Slug: 25m:250 72.816m:95 146.115m:92.5 | 1 | 0 | 3 | 750 | 1 | 0.9 | 10/50 | 1 | 0.3 | 1 | 0.2 | 75 | 3/1/1/0.25/0.1/0.25/0.1 | 3000/3000/3000/3000/3000 | F |
| 12gaBeanbag | 12 Gauge | Beanbag | 107005 | - | 1 | 0 | 0 | 25 | 1 | 0 | 0/0 | 1 | 0.2 | 0 | 0 | 35 | 2/1/1/0.3/0.1/0.3/0.1 | 3000/10000/3000/3000/3000 | F |
| 12gaBreach | 12 Gauge | Breach | 30 | Curve_Damage_Shotgun_BSG: 25m:50 29.929m:9 | 1 | 0 | 0 | 0 | 1 | 0 | 0/0 | 0.5 | 0.2 | 0 | 0 | 35 | 2/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 9x17FMJ | 9x17mm | AP | 30 | Curve_Damage_Pistol_P250: 15m:70 50m:35 | 1 | 0 | 2 | 250 | 0.7 | 0 | 8/25 | 0.05 | 0.2 | 0.1 | 0 | 35 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 9x17JHP | 9x17mm | JHP | 30 | Curve_Damage_Pistol_P250: 15m:70 50m:35 | 1 | 0 | 0 | 80 | 0.5 | 0 | 4/25 | 0.05 | 0.2 | 0.05 | 0 | 35 | 6/1/1/0.7/0.1/0.7/0.1 | 3000/3000/3000/3000/3000 | F |
| 9x18FMJ | 9x18mm | AP | 30 | Curve_Damage_Pistol_Makarov: 1m:50 50m:40 | 1 | 0 | 3 | 225 | 0.7 | 0 | 10/25 | 0.075 | 0.2 | 0.1 | 0 | 35 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 9x19FMJ | 9x19mm | AP | 30 | Curve_Damage_Pistol_G19: 1.5m:70 23.949m:65 50m:25 | 1 | 0 | 2 | 250 | 0.7 | 0.1 | 10/60 | 0.075 | 0.2 | 0.05 | 0 | 35 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 9x19JHP | 9x19mm | JHP | 30 | Curve_Damage_Pistol_G19_HP: 1.5m:80 23.949m:75 50m:35 | 1 | 0 | 0 | 100 | 0.5 | 0 | 5/5 | 0.075 | 0.2 | 0.2 | 0 | 35 | 6/1/1/0.7/0.1/0.7/0.1 | 3000/3000/3000/3000/3000 | F |
| 10x25FMJ | 10x25mm | AP | 30 | Curve_Damage_Pistol_USP: 25m:90 75m:50 | 1 | 0 | 2 | 400 | 0.7 | 0.1 | 10/60 | 0.125 | 0.2 | 0.1 | 0 | 35 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 10x25JHP | 10x25mm | JHP | 30 | Curve_Damage_Pistol_USP_HP: 25m:100 75m:60 | 1 | 0 | 0 | 80 | 0.5 | 0 | 5/0 | 0.125 | 0.3 | 0.05 | 0 | 35 | 6/1/1/0.7/0.1/0.7/0.1 | 3000/3000/3000/3000/3000 | F |
| 45FMJ | .45 ACP | AP | 45 | Curve_Damage_Pistol_USP: 25m:90 75m:50 | 1 | 0 | 2 | 300 | 0.6 | 0 | 15/60 | 0.125 | 0.2 | 0.1 | 0 | 35 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 45JHP | .45 ACP | JHP | 30 | Curve_Damage_Pistol_USP_HP: 25m:100 75m:60 | 1 | 0 | 0 | 110 | 0.3 | 0 | 7.5/0 | 0.125 | 0.3 | 0.05 | 0 | 35 | 6/1/1/0.7/0.1/0.7/0.1 | 3000/3000/3000/3000/3000 | F |
| 45JHP_S | .45 ACP | JHP | 30 | Curve_Damage_Pistol_USP_HP: 25m:100 75m:60 | 1 | 0 | 0 | 110 | 0.3 | 0 | 7.5/0 | 0.125 | 0.3 | 0.05 | 0 | 35 | 5/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 357FMJ | .357 Magnum | AP | 30 | Curve_Damage_Rifle_M14: 100m:160 1500m:145 3000m:95 | 1 | 0 | 2 | 400 | 1 | 0 | 12.5/60 | 0.75 | 0.2 | 0.1 | 0 | 35 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 357JHP | .357 Magnum | JHP | 30 | Curve_Damage_Rifle_M14_HP: 100m:175 1500m:145 3000m:105 | 1 | 0 | 1 | 150 | 1 | 0 | 6.25/30 | 0.75 | 0.35 | 0.2 | 0 | 35 | 6/1/1/0.7/0.1/0.7/0.1 | 3000/3000/3000/3000/3000 | F |
| 57x28FMJ | 5.7x28mm | AP | 30 | Curve_Damage_Pistol_P250: 15m:70 50m:35 | 1 | 0 | 3 | 250 | 1 | 0.3 | 10/30 | 0.8 | 0.2 | 0.1 | 0 | 15 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 57x28JHP | 5.7x28mm | JHP | 30 | Curve_Damage_Pistol_P250_HP: 15m:80 50m:45 | 1 | 0 | 2 | 100 | 1 | 0.1 | 5/0 | 0.8 | 0.5 | 0.2 | 0 | 15 | 6/1/1/0.7/0.1/0.7/0.1 | 3000/3000/3000/3000/3000 | F |
| 545x39FMJ | 5.45x39mm | AP | 30 | Curve_Damage_Rifle_AKS74U: 69.325m:105 287.024m:90 337.232m:85 412.232m:85 | 1 | 0 | 4 | 550 | 1 | 0.3 | 15/100 | 0.8 | 0.2 | 0.1 | 0 | 37.5 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 556x45FMJ | 5.56x45mm | AP | 30 | Curve_Damage_Rifle_M4A1: 60m:122.5 277.699m:110 327.907m:102.5 402.907m:102.5 | 1 | 0 | 4 | 700 | 1 | 0.3 | 15/100 | 0.85 | 0.2 | 0.1 | 0 | 37.5 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 556x45JHP | 5.56x45mm | JHP | 30 | Curve_Damage_Rifle_M4A1_HP: 60m:132.5 277.699m:112.5 327.907m:107.5 402.907m:107.5 | 1 | 0 | 2 | 175 | 1 | 0.1 | 7.5/25 | 0.85 | 0.5 | 0.2 | 0 | 37.5 | 6/1/1/0.7/0.1/0.7/0.1 | 3000/3000/3000/3000/3000 | F |
| 300AACFMJ | .300 Blackout | AP | 30 | Curve_Damage_Rifle_SBR300: 60m:135 207.175m:115 327.907m:102.5 402.907m:102.5 | 1 | 0 | 4 | 550 | 1 | 0.5 | 15/100 | 0.9 | 0.2 | 0.1 | 0 | 37.5 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 300AACJHP | .300 Blackout | JHP | 30 | Curve_Damage_Rifle_SBR300_JHP: 60m:145 207.175m:130 327.907m:112.5 402.907m:112.5 | 1 | 0 | 3 | 185 | 1 | 0.1 | 7.5/0 | 0.9 | 0.6 | 0.2 | 0 | 37.5 | 6/1/1/0.7/0.1/0.7/0.1 | 3000/3000/3000/3000/3000 | F |
| 762x39FMJ | 7.62x39mm | AP | 30 | Curve_Damage_Rifle_SBR300: 60m:135 207.175m:115 327.907m:102.5 402.907m:102.5 | 1 | 0 | 4 | 700 | 1 | 0.3 | 15/100 | 0.9 | 0.2 | 0.1 | 0 | 42 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 762x39JHP | 7.62x39mm | JHP | 30 | Curve_Damage_Rifle_SBR300_JHP: 60m:145 207.175m:130 327.907m:112.5 402.907m:112.5 | 1 | 0 | 3 | 300 | 1 | 0.1 | 7.5/0 | 0.9 | 0.6 | 0.2 | 0 | 42 | 6/1/1/0.7/0.1/0.7/0.1 | 3000/3000/3000/3000/3000 | F |
| 762x51FMJ | 7.62x51mm | AP | 30 | Curve_Damage_Rifle_SA58: 75m:160 410.095m:145 460.095m:135 | 1 | 0 | 4 | 1250 | 1 | 0.7 | 20/120 | 1 | 0.2 | 1 | 0 | 50 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 762x51JHP | 7.62x51mm | JHP | 30 | Curve_Damage_Rifle_SA58_HP: 75m:180 410.095m:147.5 460.095m:135 | 1 | 0 | 3 | 600 | 1 | 0.5 | 10/0 | 1 | 0.6 | 1 | 0 | 50 | 6/1/1/0.7/0.1/0.7/0.1 | 3000/3000/3000/3000/3000 | F |
| 792x57FMJ | 7.92x57mm | AP | 45 | Curve_Damage_Rifle_SA58: 75m:160 410.095m:145 460.095m:135 | 1 | 0 | 4 | 400 | 1 | 1 | 15/70 | 1 | 1 | 0 | 0.95 | 35 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| TaserAmmo |  | Taser | 1 | - | 1 | 0 | 0 | 10 | 1 | 0 | 0/0 | 0 | 0 | 0 | 0 | 0 | 1/1/1/1/1/1/1 | 0/0/0/0/0 | F |
| PepperballAmmo | .68 Pepperball | Pepperball | 1 | - | 1 | 0 | 0 | 400 | 1 | 0 | 0/0 | 0 | 0 | 0 | 0 | 35 | 2/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | T |
| 12gaBuckSawnoff | 12 Gauge | Buckshot | 30 | Curve_Damage_Shotgun_Sawnoff: 4m:30 12m:15 30m:10 | 8 | 1/2 | 0 | 68 | 0.125 | 0 | 10/50 | 0.8 | 0.2 | 0 | 0 | 35 | 2/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 4.6x30FMJ | 4.6x30mm | AP | 30 | Curve_Damage_Pistol_P250: 15m:70 50m:35 | 1 | 0 | 3 | 350 | 1 | 0.3 | 10/60 | 0.8 | 0.3 | 0.1 | 0 | 15 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 4.6x30JHP | 4.6x30mm | JHP | 30 | Curve_Damage_Pistol_P250_HP: 15m:80 50m:45 | 1 | 0 | 2 | 135 | 1 | 0.1 | 5/0 | 0.8 | 0.5 | 0.2 | 0 | 20 | 6/1/1/0.7/0.1/0.7/0.1 | 3000/3000/3000/3000/3000 | F |
| 50AEJHP | .50 Action Express | JHP | 55 | Curve_Damage_Pistol_50AE_HP: 35m:110 85m:65 | 1 | 0 | 0 | 110 | 1 | 0 | 7.5/0 | 0.125 | 1 | 2 | 0 | 50 | 7/6/4.5/3/1.5/3.5/1.5 | 3000/3000/3000/3000/3000 | F |
| 50AEFMJ | .50 Action Express | AP | 50 | Curve_Damage_Pistol_50AE: 35m:100 85m:55 | 1 | 0 | 3 | 300 | 1 | 0 | 15/60 | 1 | 1 | 1 | 0 | 50 | 7/5/3/1.2/1/1.3/1 | 3000/3000/3000/3000/3000 | F |
| 40MM_Flash | 40x46mm | Flashbang | 30 | Curve_Damage_Shotgun_590: 25m:40 72.816m:37.5 151.124m:35 | 1 | 0.75/0.75 | 1 | 150 | 0.125 | 0.4 | 0/0 | 0.9 | 0.3 | 0.5 | 0 | 45 | 5/1/1/1/0.1/1/0.1 | 3000/3000/3000/3000/3000 | F |
| 40MM_Gas | 40x46mm | CS Gas | 30 | Curve_Damage_Shotgun_590: 25m:40 72.816m:37.5 151.124m:35 | 1 | 0.75/0.75 | 1 | 150 | 0.125 | 0.4 | 0/0 | 0.9 | 0.3 | 0.5 | 0 | 45 | 5/1/1/1/0.1/1/0.1 | 3000/3000/3000/3000/3000 | F |
| 12gaFlash | 12 Gauge | Flash Thunder | 0.1 | - | 1 | 0 | 0 | 25 | 1 | 0 | 0/0 | 1 | 0.2 | 0 | 0 | 0 | 2/1/1/0.3/0.1/0.3/0.1 | 3000/10000/3000/3000/3000 | F |
| 6.5x48FMJ | 6.5x48FMJ | AP | 30 | Curve_Damage_Rifle_65x48Creedmoor: 75m:140 397.468m:125 470m:115 | 1 | 0 | 4 | 700 | 1 | 0.3 | 15/100 | 0.85 | 0.2 | 0.1 | 0 | 37.5 | 6/1/1/0.3/0.1/0.3/0.1 | 3000/3000/3000/3000/3000 | F |
| 6.5x48JHP | 6.5x48JHP | JHP | 30 | Curve_Damage_Rifle_65x48Creedmoor_JHP: 75m:160 397.468m:130 470m:115 | 1 | 0 | 3 | 350 | 1 | 0.1 | 22.5/0 | 0.85 | 0.6 | 0.1 | 0 | 37.5 | 6/1/1/0.7/0.1/0.7/0.1 | 3000/3000/3000/3000/3000 | F |

**Deagle-relevant:** `50AEFMJ` / `50AEJHP` (".50 Action Express") rows exist with curves `Curve_Damage_Pistol_50AE(_HP)`, but no player weapon in `Items/WeaponsRevised` uses them.

### All damage curves (distance m : damage)

- `Curve_Damage_Pistol_50AE`: 35m:100 85m:55
- `Curve_Damage_Pistol_50AE_HP`: 35m:110 85m:65
- `Curve_Damage_Pistol_G19`: 1.5m:70 23.949m:65 50m:25
- `Curve_Damage_Pistol_G19_HP`: 1.5m:80 23.949m:75 50m:35
- `Curve_Damage_Pistol_Makarov`: 1m:50 50m:40
- `Curve_Damage_Pistol_P250`: 15m:70 50m:35
- `Curve_Damage_Pistol_P250_HP`: 15m:80 50m:45
- `Curve_Damage_Pistol_USP`: 25m:90 75m:50
- `Curve_Damage_Pistol_USP_HP`: 25m:100 75m:60
- `Curve_Damage_Pistol_Uzi`: 1.5m:70 23.949m:65 50m:25
- `Curve_Damage_Rifle_65x48Creedmoor`: 75m:140 397.468m:125 470m:115
- `Curve_Damage_Rifle_65x48Creedmoor_JHP`: 75m:160 397.468m:130 470m:115
- `Curve_Damage_Rifle_AK102`: 60m:100 163.778m:87.5 300m:40
- `Curve_Damage_Rifle_AKS74U`: 69.325m:105 287.024m:90 337.232m:85 412.232m:85
- `Curve_Damage_Rifle_M14`: 100m:160 1500m:145 3000m:95
- `Curve_Damage_Rifle_M14_HP`: 100m:175 1500m:145 3000m:105
- `Curve_Damage_Rifle_M4A1`: 60m:122.5 277.699m:110 327.907m:102.5 402.907m:102.5
- `Curve_Damage_Rifle_M4A1_HP`: 60m:132.5 277.699m:112.5 327.907m:107.5 402.907m:107.5
- `Curve_Damage_Rifle_SA58`: 75m:160 410.095m:145 460.095m:135
- `Curve_Damage_Rifle_SA58_HP`: 75m:180 410.095m:147.5 460.095m:135
- `Curve_Damage_Rifle_SBR300`: 60m:135 207.175m:115 327.907m:102.5 402.907m:102.5
- `Curve_Damage_Rifle_SBR300_JHP`: 60m:145 207.175m:130 327.907m:112.5 402.907m:112.5
- `Curve_Damage_Shotgun_590`: 25m:40 72.816m:37.5 151.124m:35
- `Curve_Damage_Shotgun_590_Slug`: 25m:250 72.816m:95 146.115m:92.5
- `Curve_Damage_Shotgun_BSG`: 25m:50 29.929m:9
- `Curve_Damage_Shotgun_Sawnoff`: 4m:30 12m:15 30m:10

## 2. Weapon-side ballistics (68 weapon CDOs, `Items/WeaponsRevised/<name>`)

Velocity = ProjectileMovementSpeed/100 (m/s). Weapon `Damage`/`DamageOverRange` are the weapon defaults; the loaded ammo row (`CurrentAmmoType`/`AmmunitionTypes`) carries the per-round values.

| weapon | name | cartridge | ammo rows | vel m/s | Damage | weapon curve | HitScan | BulletDrag | PenDist | FirstShotSpread | ADSSpread | VelSpread | Wobble | Tracer |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Primary_870mcs | 870mcs | 12 Gauge | 12gaBuck/12gaSlug/12gaBuckSawnoff | 253 | 55 | Curve_Damage_Shotgun_590 | T | 250 | 5 | 1 | 0.7 | 0 | - | T |
| Primary_AK102 | AK102 | 5.56mm x 45mm | 556x45FMJ/556x45JHP | 905.256 | 40 | Curve_Damage_Rifle_AK102 | T | 0 | 10 | 0 | 0 | 0 | - | T |
| Primary_AK103 | AK103 | 5.56mm x 45mm | 762x39FMJ/762x39JHP | 905.256 | 70 | - | T | 0 | - | 0 | 0.1 | - | - | T |
| Primary_AR18 | AR18 | 5.56mm x 45mm | 300AACFMJ/300AACJHP | 991 | 40 | Curve_Damage_Rifle_SBR300 | T | - | 30 | 0 | 0.1 | 0 | - | T |
| Primary_B1301 | B1301 | 12 Gauge | 12gaBuck/12gaSlug | 221 | 40 | Curve_Damage_Shotgun_590 | T | 500 | 5 | 1 | 0.7 | 0 | - | T |
| Primary_B1301_Entryman | B1301 Entryman | 12 Gauge | 12gaBuck/12gaSlug | 221 | 40 | Curve_Damage_Shotgun_590 | T | 500 | 5 | 1 | 0.7 | 0 | - | T |
| Primary_BCM_MK1 | MK1 | 5.56mm x 45mm | 556x45FMJ/556x45JHP | 920 | 50 | Curve_Damage_Rifle_M4A1 | T | - | 25 | 0 | 0 | 0 | - | T |
| Primary_BRN180 | ARN-180 | 5.56mm x 45mm | 300AACFMJ/300AACJHP | 991 | 40 | Curve_Damage_Rifle_SBR300 | T | - | 30 | 0 | 0.1 | 0 | - | T |
| Primary_Benelli_M4 | M1014 | 12 Gauge | 12gaBuck/12gaSlug/12gaBuckSawnoff | 221 | 40 | Curve_Damage_Shotgun_590 | T | 500 | 5 | 1 | 0.7 | 0 | - | T |
| Primary_F90 | F90 | 5.56mm x 45mm | 556x45FMJ/556x45JHP | 991 | 40 | Curve_Damage_Rifle_SBR300 | T | - | 30 | 0 | 0.1 | 0 | - | T |
| Primary_FAL | FAL | 7.62mm x 51mm | 762x51FMJ/762x51JHP | 840 | 70 | Curve_Damage_Rifle_SA58 | T | - | 50 | 0 | 0.5 | 0 | - | T |
| Primary_FAL_OSW | SA-58 OSW | 7.62mm x 51mm | 762x51FMJ/762x51JHP | 840 | 70 | Curve_Damage_Rifle_SA58 | T | - | 50 | 0 | 0.5 | 0 | - | T |
| Primary_G36C | G36C | 5.56mm x 45mm | 556x45FMJ/556x45JHP | 920 | 40 | Curve_Damage_Rifle_M4A1 | T | - | 25 | 0 | 0 | 0 | - | T |
| Primary_G3A3 | G36C | 5.56mm x 45mm | 762x51FMJ/762x51JHP | 870 | 50 | - | T | - | 13.5 | 0 | 0 | 0 | - | T |
| Primary_HK416 | GA416 | 5.56mm x 45mm | 556x45FMJ/556x45JHP | 870 | 42 | Curve_Damage_Rifle_M4A1 | T | - | 25 | 0 | 0 | 0 | - | T |
| Primary_LVAW | LVAR | 5.56mm x 45mm | 300AACFMJ/300AACJHP | 991 | 40 | Curve_Damage_Rifle_SBR300 | T | - | 30 | 0 | 0.1 | 0 | - | T |
| Primary_M14 | M14 | 7.62mm x 51mm | 762x51FMJ/762x51JHP | 850 | 85 | Curve_Damage_Rifle_M14 | T | - | 120 | 0 | 0.1 | 0 | - | - |
| Primary_M16A4 | M16A4 | 5.56mm x 45mm | 556x45FMJ/556x45JHP | 905.256 | 38 | - | T | 0 | 18 | 0 | 0 | 0 | - | T |
| Primary_M4A1 | M4A1 | 5.56mm x 45mm | 556x45FMJ/556x45JHP | 905.256 | 50 | Curve_Damage_Rifle_M4A1 | T | 0 | 25 | 1 | 0 | 0.02 | - | T |
| Primary_M4A1_D_Temp | M4A1 | 5.56mm x 45mm | 556x45FMJ/556x45JHP | 905.256 | 50 | Curve_Damage_Rifle_M4A1 | T | 0 | 25 | 1 | 0 | 0.02 | - | T |
| Primary_M76 | M76 | 9mm | 9x19FMJ/9x19JHP | 730 | 45 | - | T | 0 | - | - | 0.1 | - | - | T |
| Primary_MCX | MCX | 5.56mm x 45mm | 556x45FMJ/556x45JHP | 991 | 40 | Curve_Damage_Rifle_SBR300 | T | - | 30 | 0 | 0.1 | 0 | - | T |
| Primary_MK18 | MK18 | 5.56mm x 45mm | 556x45FMJ/556x45JHP | 905.256 | 50 | Curve_Damage_Rifle_M4A1 | T | 0 | 25 | 1 | 0 | 0 | - | T |
| Primary_MK18_Blue | MK18 Simmunition | 5.56mm x 45mm | 9x19JHP | 905.256 | 50 | Curve_Damage_Rifle_M4A1 | T | 0 | 0 | 1 | 0 | 0 | - | T |
| Primary_MP510 | MP5/10MM | 9mm | 10x25FMJ/10x25JHP | 400 | 30 | Curve_Damage_Pistol_USP | T | 0 | 10 | 0 | 0 | 0 | - | T |
| Primary_MP5A2 | MP5A2 | 9mm | 9x19FMJ/9x19JHP | 400 | 30 | Curve_Damage_Pistol_G19 | T | 0 | 10 | 0 | 0 | 0 | - | T |
| Primary_MP5A3 | MP5A3 | 9mm | 9x19FMJ/9x19JHP | 400 | 30 | Curve_Damage_Pistol_G19 | T | 0 | 10 | 0 | 0 | 0 | - | T |
| Primary_MP7 | MP7 | 9mm | 4.6x30FMJ/4.6x30JHP | 300 | 30 | Curve_Damage_Pistol_G19 | T | 0 | 10 | 0 | 0 | 0 | - | T |
| Primary_MP9 | MP9 | 9mm | 9x19FMJ/9x19JHP | 300 | 30 | Curve_Damage_Pistol_G19 | T | 0 | 10 | 0 | 0 | 0 | - | T |
| Primary_MPL | MPL | 9mm | 9x19FMJ/9x19JHP | 395.935 | 35 | - | T | 0 | - | - | 0.1 | - | - | T |
| Primary_MPX | MPX | 9mm | 9x19FMJ/9x19JHP | 300 | 30 | Curve_Damage_Pistol_G19 | T | 0 | 10 | 0 | 0 | 0 | - | T |
| Primary_P90 | P90 | 9mm | 57x28FMJ/57x28JHP | 650 | 1 | Curve_Damage_Pistol_Makarov | T | 0 | 14 | 0 | 0.01 | - | - | T |
| Primary_Pepperball_MLO | Pepperball Gun | Pepperball | PepperballAmmo | 100 | 1 | - | - | - | - | - | 0.1 | - | - | T |
| Primary_S590_Assault_v2 | 590S | 12 Gauge | 12gaBuck/12gaSlug/12gaBuckSawnoff | 253 | 100 | Curve_Damage_Shotgun_590 | T | 250 | 6 | 1 | - | 0 | - | T |
| Primary_S590_Beanbag_V2 | 590A Less Lethal | Beanbag | - | 80 | 5 | - | - | - | - | - | - | 0 | - | - |
| Primary_SBR | ARWC | .300 BLACKOUT | 556x45FMJ/556x45JHP | 905.256 | 50 | Curve_Damage_Rifle_SBR300 | T | 0 | 25 | 0 | 0 | 0 | - | - |
| Primary_SCARL_v2 | MK16 | 5.56mm x 45mm | 556x45FMJ/556x45JHP | 905.256 | 50 | Curve_Damage_Rifle_M4A1 | T | 0 | 25 | 1 | 0 | 0 | - | T |
| Primary_SLR47 | SLR47 | 5.56mm x 45mm | 762x39FMJ/762x39JHP | 905.256 | 40 | Curve_Damage_Rifle_SBR300 | T | 0 | 25 | 0 | 0 | 0 | - | T |
| Primary_SPC9 | SPC9 | 9mm | 9x19FMJ/9x19JHP | 300 | 30 | Curve_Damage_Pistol_G19 | T | 0 | 10 | 0 | 0 | 0 | - | T |
| Primary_SR16 | SR-16 | 5.56mm x 45mm | 556x45FMJ/556x45JHP | 905.256 | 50 | Curve_Damage_Rifle_M4A1 | T | 0 | 25 | 0 | 0 | 0 | - | - |
| Primary_Saiga12 | Saiga | Buckshot | 12gaBuck/12gaSlug/12gaBuckSawnoff | 905.256 | 80 | - | T | 0 | 1 | - | 0.1 | - | 1.80103e+13 | T |
| Primary_Superbnova | Supernova | 12 Gauge | 12gaBuck/12gaSlug/12gaBuckSawnoff | 253 | 55 | Curve_Damage_Shotgun_590 | T | 250 | 5 | 1 | 0.7 | 0 | - | T |
| Primary_TAC700 | TAC700 | Pepperball | PepperballAmmo | 100 | 1 | - | - | - | - | - | 0.1 | 0 | - | T |
| Primary_UMP45_v2 | UMP-45 | .45 | 45FMJ/45JHP/45JHP_S | 285 | 36 | Curve_Damage_Pistol_USP | T | 0 | 8 | 0 | 0 | 0 | - | T |
| Primary_UMP9 | UMP-9 | 9mm | 9x19FMJ/9x19JHP | 285 | 36 | Curve_Damage_Pistol_USP | T | 0 | 8 | 0 | 0 | 0 | - | T |
| Primary_VKS | Pepperball Gun | Pepperball | PepperballAmmo | 100 | 1 | - | - | - | - | - | 0.1 | 0 | - | T |
| Primary_VKS_V2 | VPL-25 | 5.56mm x 45mm | PepperballAmmo | 200 | 1 | - | - | - | - | 0 | 0 | 0 | - | T |
| Primary_W870LL | Beanbag Shotgun | Beanbag | 12gaBeanbag | 120 | 5 | - | - | - | - | 1 | - | 0 | - | T |
| Primary_WCQB | 870CQB | 12 Gauge | 12gaBuck/12gaSlug/12gaBuckSawnoff | 253 | 55 | Curve_Damage_Shotgun_590 | T | 250 | 5 | 1 | 0.7 | 0 | - | T |
| Secondary_FiveSeven_V2 | FiveSeven | FN 5.7x28mm | 57x28FMJ/57x28JHP | 716.28 | 25 | Curve_Damage_Pistol_P250 | T | 0 | 14 | 0 | 0 | 0 | - | T |
| Secondary_G19_Blue | G19 | 9mm | 9x19JHP | 374.9 | 30 | Curve_Damage_Pistol_P250 | T | - | - | 0 | 0.1 | 0 | - | T |
| Secondary_G19_D_Temp | G19 | 9mm | 9x19FMJ/9x19JHP | 374.9 | 30 | Curve_Damage_Pistol_P250 | T | - | - | 0 | 0.1 | 0 | - | T |
| Secondary_G19_V2 | G19 | 9mm | 9x19FMJ/9x19JHP | 374.9 | 30 | Curve_Damage_Pistol_P250 | T | - | - | 0 | 0.1 | 0 | - | T |
| Secondary_Kimber1911 | TLE 1911 | .45 | 45FMJ/45JHP | 253 | 45 | Curve_Damage_Pistol_USP | T | - | 8 | 0 | 0 | 0.02 | - | T |
| Secondary_M1911_V2 | M1911 | .45 | 45FMJ/45JHP | 374.9 | 35 | - | T | - | 2.5 | 0.2 | 0 | - | 1.80103e+13 | - |
| Secondary_M2011 | M2011 | .45 | 45FMJ/45JHP | 253 | 45 | Curve_Damage_Pistol_USP | T | - | 8 | 0 | 0 | 0.02 | - | T |
| Secondary_M45A1 | M45A1 | .45 | 45FMJ/45JHP | 253 | 45 | Curve_Damage_Pistol_USP | T | - | 8 | 0 | 0 | 0.02 | - | T |
| Secondary_M92A3 | B92X | 9mm | 9x19FMJ/9x19JHP | 374.9 | 30 | Curve_Damage_Pistol_G19 | T | - | 10 | 0 | 0 | 0 | - | T |
| Secondary_M92FS_V2 | M9A1 | 9mm | 9x19FMJ/9x19JHP | 374.9 | 30 | Curve_Damage_Pistol_P250 | T | - | 5 | 0 | 0 | 0 | - | T |
| Secondary_P229 | M11 Compact | 9mm | 9x19FMJ/9x19JHP | 374.9 | 30 | Curve_Damage_Pistol_P250 | T | - | - | 0 | 0.1 | 0 | - | T |
| Secondary_P250 | P250 | 9mm | 9x17FMJ/9x17JHP | 374.9 | - | Curve_Damage_Pistol_P250 | T | - | 2.5 | 0 | 0 | 0 | 1.80103e+13 | T |
| Secondary_P99 | G19 | .40 S&W | 9x19FMJ/9x19JHP | 374.9 | 40 | - | T | - | 5 | 0.05 | 0 | 0 | - | T |
| Secondary_PFC9 | PC19 | 9mm | 9x19FMJ/9x19JHP | 374.9 | 30 | Curve_Damage_Pistol_P250 | T | - | - | 0 | 0.1 | 0 | - | T |
| Secondary_Python_V2 | .357 Magnum | .357 Magnum | 357FMJ/357JHP | 457.2 | 65 | Curve_Damage_Rifle_M14 | T | - | 55 | 0 | 0 | 0 | - | T |
| Secondary_TCR | TPL | 5.56mm x 45mm | PepperballAmmo | 200 | 1 | - | - | - | - | 0 | 0 | 0 | - | T |
| Secondary_Taser_V2 | Taser | Taser | TaserAmmo | 1000 | - | - | - | 0 | - | - | - | 0 | - | T |
| Secondary_USP | USP45 | 9mm | 45FMJ/45JHP | 374.9 | - | Curve_Damage_Pistol_USP | T | - | 8 | 0 | 0 | 0 | 1.80103e+13 | T |
| Sniper_M24 | M24 | - | 762x51FMJ/762x51JHP | 780 | 80 | - | T | 0 | 75 | 0 | 0 | 0 | - | T |

## 3. Armour

### Plate / soft materials (`ArmorRevised/ArmorMaterials/AM_*`, class ArmourMaterial)

| material | DamageReduction | MinDamageReduction | DurabilityEnabled | Durability | ArmourLevel | SpallingChance | MoveSpeedMod | MoveAccelMod |
|---|---|---|---|---|---|---|---|---|
| AM_Ceramic | - | 0.4 | T | 4 | 6 | - | - | - |
| AM_Kevlar | 0.5 | 0.5 | - | - | 3 | - | - | 0 |
| AM_Steel | 0.865 | 0.7 | - | - | 6 | 1 | - | -0.18 |

### Player vests & helmets (CDOs under `ArmorRevised/`)

| item | name | MaxArmourCoverage | ArmourCoverage | CarrierMaterial | Durability | DamageReduction | MoveSpeedMult | Heavy | weight kg | other |
|---|---|---|---|---|---|---|---|---|---|---|
| Armor_Heavy_V2 | HEAVY ARMOR | AC_FrontBackSides | - | - | - | - | 0.9 | T | - |  |
| Armor_Light_V2 | LIGHT ARMOR | AC_FrontBack | - | - | - | - | - | - | 1.13398 |  |
| Armor_NoArmor | NO ARMOR | - | - | 0 | - | - | 1.4 | - | - |  |
| Armor_StabVest | STAB VEST | - | - | - | - | - | 1.4 | - | 1.13398 |  |
| BP_BaseBodyArmor | - | - | - | - | - | - | - | - | - |  |
| BP_Helmet_Pumpkin | SPOOKY MASK | - | - | - | 1 | - | - | - | 4.5 |  |
| BaseBodyArmor | - | - | - | AM_Kevlar | - | - | - | - | - |  |
| Helmet_BallisticMask | BALLISTIC MASK | - | - | - | 2 | - | - | - | 4.5 |  |
| Helmet_GPNVG | GPNVGs | - | - | - | - | - | - | - | - |  |
| Helmet_Gasmask_V2 | GAS MASK | - | - | - | 1 | - | - | - | 1.3 | {"BlockAnyDamageFrom": ["PepperballDamageType", "CSGasDamageType", "PepperSprayDamageType"]} |
| Helmet_Glasses | SHOOTING GLASSES | - | - | - | - | 0 | - | - | 0.05 |  |
| Helmet_Goggles | ANTI-FLASH GOGGLES | - | - | - | 1 | - | - | - | 0.285 | {"MultiplyStunDamageFrom": {"FlashbangDamage_Far_C": 0.25}} |
| Helmet_NVG_V2 | NVGs | - | - | - | 1 | - | - | - | 0.8 |  |
| Helmet_NoAccessory | HELMET ONLY | - | - | - | 1 | - | - | - | - |  |
| Helmet_None | NO HELMET | - | - | - | - | 0 | - | - | - |  |
| Helmet_UpArmor | UPARMORING | - | - | - | 2 | 0.8 | - | - | 2 |  |
| Legacy_Armor_Light_V2 | LIGHT ARMOR | AC_FrontBack | - | - | - | - | - | - | 1.13398 |  |
| BP_Helmet_Mandible | BALLISTIC MASK | - | - | - | 2 | - | - | - | 4.5 |  |
| BP_Helmet_None | NO HELMET | - | - | - | - | 0 | - | - | - |  |
| BP_Helmet_Standard | HELMET ONLY | - | - | - | 1 | - | - | - | - |  |

Base vest `BaseBodyArmor` sets CarrierMaterial = AM_Kevlar. The plate (`ArmourMaterial`) and coverage (`ArmourCoverage`, enum EArmourCoverage: AC_FrontBack / AC_FrontBackSides…) are chosen in the loadout (`SavedLoadout.ArmourMaterial/ArmourCoverage`), not fixed per vest.

### Suspect armour (`DataTables/SuspectArmourDataTable`)

| row | helmet | ArmourLevel | Durability | DamageMultiplier | SpallingChance | MoveSpeedMult |
|---|---|---|---|---|---|---|
| ExampleArmour | 0 | 3 | 5 | 0 | 0 | 1 |
| ExampleHelmet | T | 5 | 5 | 0 | 0 | 1 |
| ExplosiveVest | 0 | 0 | 0 | 0.25 | 0 | 1 |
| CivExplosiveVest | 0 | 0 | 0 | 0.25 | 0 | 1 |
| Armour_L1 | 0 | 1 | 4 | 0.4 | 0 | 1 |
| Armour_L1NoMesh | 0 | 1 | 4 | 0.4 | 0 | 1 |
| Armour_L2 | 0 | 2 | 4 | 0.25 | 0 | 1 |
| Armour_L2NoMesh | 0 | 2 | 4 | 0.25 | 0 | 1 |
| Armour_3A | 0 | 3 | 5 | 0.2 | 0 | 1 |
| Armour_3AMesh | 0 | 3 | 5 | 0.2 | 0 | 1 |
| Armour_4 | 0 | 4 | 6 | 0.2 | 0 | 1 |
| Armour_4Mesh | 0 | 4 | 6 | 0.2 | 0 | 1 |
| Helmet_L3 | T | 3 | 5 | 0 | 0 | 1 |
| Armour_L2ToxicBackpack | 0 | 2 | 4 | 0.25 | 0 | 1 |

## 4. Surfaces (`Info/PenetrationData`, struct MaterialPenetration per physical surface)

Surfaces not listed here are not overridden → all-default struct (defaults not read). Surface names ↔ SurfaceTypeN from `Config/DefaultEngine.ini`.

| surface | bIsPenetrable | PenetrationDensity | PenetrationMultiplier | ArmourLevel | bCanRicochet | RicochetChanceMult | SpallingChance |
|---|---|---|---|---|---|---|---|
| DefaultPenetrationData | T | - | - | - | - | - | - |
| RON_Aluminium | T | 2.5 | - | 1 | - | - | 0.2 |
| RON_Brick | T | 1.95 | - | - | - | - | - |
| RON_Cardboard | T | 0.5 | - | - | 0 | - | - |
| RON_Ceramic | T | - | - | - | - | - | - |
| RON_ConcreteSoft | T | 4.25 | - | 2 | - | - | - |
| RON_ConcreteStrong | T | 5.5 | - | 3 | - | - | 0.3 |
| RON_Dirt | - | - | - | - | 0 | - | - |
| RON_Drywall | T | 0.6 | - | - | 0 | - | - |
| RON_Fabric_Carpet | T | 0.5 | - | - | - | - | - |
| RON_Fabric_Stuffing | T | - | - | - | - | - | - |
| RON_Fabric_Thin | T | 0.5 | - | - | 0 | - | - |
| RON_Flesh | T | 3 | - | - | 0 | - | - |
| RON_Galvanized | T | 2 | - | 3 | - | 1.25 | 0.7 |
| RON_Glass_Plate | T | 0.6 | - | - | 0 | - | - |
| RON_Glass_Windshield | T | - | - | - | 0 | - | - |
| RON_Grass | - | - | - | - | 0 | - | - |
| RON_Lava | - | - | - | - | 0 | - | - |
| RON_Lead | T | - | - | - | - | - | - |
| RON_Leaves | T | 0.7 | - | - | 0 | - | - |
| RON_Limestone | T | - | - | - | - | - | - |
| RON_Mahogany | T | - | - | - | 0 | - | - |
| RON_Marble_Coated | T | 8 | - | 4 | - | 4 | 0.6 |
| RON_Marble_Thick | T | 8.5 | - | 5 | - | 4 | 0.9 |
| RON_Mud | - | - | - | - | 0 | - | - |
| RON_Paper | T | 0.25 | - | - | 0 | - | - |
| RON_Pine | T | - | - | - | - | - | - |
| RON_Plaster | T | 0.8 | - | - | - | - | - |
| RON_Plastic | T | 0.85 | - | - | - | - | - |
| RON_Plywood | T | - | - | - | 0 | - | - |
| RON_Polystyrene | T | - | - | - | - | - | - |
| RON_Powder | - | - | - | - | 0 | - | - |
| RON_Rock | T | 1.5 | - | - | - | - | - |
| RON_Rubber | T | - | - | - | 0 | - | - |
| RON_Sand | - | - | - | - | 0 | - | - |
| RON_Snow | - | - | - | - | 0 | - | - |
| RON_Steel | T | 8 | - | 4 | - | - | 0.2 |
| RON_Tin | T | - | - | - | - | - | - |
| RON_Treewood | T | - | - | - | 0 | - | - |
| RON_Wallpaper | T | 0.7 | - | - | - | - | - |
| RON_Water | T | - | - | - | 0 | - | - |
| RON_Vehicle | T | - | - | - | - | - | - |
| RON_Bulletproof_Glass | T | - | - | - | - | - | - |

Surface ids: 1=Aluminium, 2=Asphalt, 3=Brick, 4=CarbonFibre, 5=Cardboard, 6=Ceramic, 7=ConcreteSoft, 8=ConcreteStrong, 9=Dirt, 10=Drywall, 11=Electrical, 12=EnergyShield, 13=Fabric_Carpet, 14=Fabric_Stuffing, 15=Fabric_Thin, 16=Flesh, 17=Galvanized, 18=Glass_Plate, 19=Glass_Windshield, 20=Grass, 21=Gravel, 22=Ice, 23=Lava, 24=Lead, 25=Leaves, 26=Limestone, 27=Mahogany, 28=Marble_Coated, 29=Marble_Thick, 30=Mud, 31=Oil, 32=Paper, 33=Pine, 34=Plaster, 35=Plastic, 36=Plywood, 37=Polystyrene, 38=Powder, 39=Rock, 40=Rubber, 41=Sand, 42=Snow, 43=Soil, 44=Steel, 45=Tin, 46=Treewood, 47=Wallpaper, 48=Water, 49=Shield, 50=Cloth, 51=Vehicle, 52=Bulletproof_Glass, 53=Leather

Impact FX naming: `Blueprints/Logic/ImpactEffects_<class>` (e.g. ImpactEffects_Pistol), `ExitImpactEffects`, `RicochetImpactEffects`, `AllImpactEffects` (projectile); armour hits use `P_VFX_Impact_<Ceramic|Steel|Galvanized|Concrete_Soft>`.

## 5. Body / health

- Health (CharacterHealthComponent, Resource/MaxResource): **BasePlayer 160**, **CyberneticsSuspect_V2 160** (LowResourceThreshold 0.35, bEnableIncapacitation T), **CyberneticsSwat_V2 160**, Civilian: not overridden (default, not read; incapacitation enabled). AI tables (e.g. AIDataTable_Hotel) list Health 160 for every row, bOverrideHealth F, and pick suspect vests from SuspectArmourDataTable rows (`Armour_3A`, `Armour_4`).
- Per-zone damage multipliers live on the **ammo row** (section 1: Head/UpperBody/LowerBody/Arm/Hand/Leg/Foot). Typical 9x19FMJ: 6/1/1/0.3/0.1/0.3/0.1.
- Limb health (`LimbHealthData` for Head/LeftArm/RightArm/LeftLeg/RightLeg: Health, MaxHealth, LowHealthThreshold, LimbDamageMultiplier, Tickets…): **names only**, no character overrides them → C++ defaults.
- Ragdoll impulse per hit: ammo row `*RagdollImpulseStrength` (mostly 3000) × weapon `RagdollImpulseMultiplier` / `BulletPhysicsImpulseMultiplier`; AP damage type DamageImpulse -800.
- Hit reactions / flinch / stagger: **names only** (`ReadyOrNotCharacterAnimData.HitReaction_<zone>`, `Flinches`, `Death_Bleedout_<zone>` are animation references; `IncapacitationHealthMultiplier`, `BoneSuppressionAmount`, `DismembermentDamageMap` not set in the decoded CDOs). No numeric stagger threshold found in data.

## 6. How a bullet is simulated (from data)

- Weapons are **`bHitScan = T`** (G19 and others) but also define `BulletProjectile = Projectile_Base_C`. Projectile_Base: BulletProjectile class, sphere collision r=10 with CCD, InitialLifeSpan 5 s, RequiredAngleToDeflect 30°, PercentageToDeflect 1, DeflectionAmount 1, HitAngleMultiplier 1, wizz min distance 500. Its `BulletProjectileMovementComponent` has **no overrides** (gravity scale, sub-stepping, MaxSimulationTimeStep = C++ defaults, not read).
- Most likely flow (inference from field names, native code not read): hitscan trace at fire → penetration per surface using ammo `PenetrationLevel`/`PenetrationDistance` against surface `PenetrationDensity`/`ArmourLevel`; ricochet if surface `bCanRicochet` and roll < ammo `RicochetChance` × surface `RicochetChanceMultiplier` and incidence angle past 30°; spall from `SpallingChance` × ammo `SpallingDamage/Radius`. Damage = ammo curve(distance) × zone multiplier, then armour: if ammo PenetrationLevel < armour level → blocked, reduced by ArmourMaterial DamageReduction (floor MinDamageReduction), durability loses `DurabilityDamage` per hit.
- Max range: no explicit field; curves are flat past their last key (5000 cm = 50 m for most pistols).

## Names only (no value in data)

LimbHealthData fields; BulletProjectileMovementComponent (ProjectileGravityScale, MaxSimulationTimeStep, MaxSimulationIterations); BulletProjectile Drag/SpeedLossMultiplierPerSurface/DamageLossMultiplierPerSurface/VelocityRequiredToRespawn (not overridden on Projectile_Base); flinch/stagger thresholds; ArmourCoverage per body part (only enum EArmourCoverage per vest); the C++ formulas.
