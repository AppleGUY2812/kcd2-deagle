# Cartridges and ammo data

Physics source for the bullet system (BALLISTICS.md, Phase 1-2): each round's numbers come from Escape from Tarkov's ammo
data, checked against real manufacturer data. Raw data: `docs/cartridges/tarkov_ammo.json` (all 208 Tarkov ammo types,
sorted by caliber then name), written by `tools/tarkov_ammo.py`.

**Data status (2026-10-12): read before trusting the Tarkov numbers.** tarkov.dev's GraphQL API answered every query
with HTTP 422 "GraphQL server unavailable" from 14:07 UTC until at least 14:30 UTC (its status page returned Cloudflare
523, origin down). The script's fallback therefore wrote the JSON from the SPT project's copy of the game's own item
templates (`sp-tarkov/server-csharp`, `SPT_Data/database/templates/items.json`, **game version 0.16.9.40743**), mapped
field by field with tarkov.dev's own mapping code (`tarkov-data-manager/modules/get-item-properties.mjs`). So the
numbers are the game's raw values, the same ones tarkov.dev serves, but as of 0.16.9: if BSG changed ammo since then
they can differ. When tarkov.dev is back: `python3 tools/tarkov_ammo.py --source api`, then
`python3 tools/tarkov_ammo.py --tables` and paste the output between the table markers below.

## Headline numbers

| Round | Real world (manufacturer) | Tarkov |
|---|---|---|
| **.50 AE** (Desert Eagle) | Hornady 9245, 300 gr XTP: 19.4 g at 450 m/s (6 in barrel) = **1,965 J, 8.74 kg m/s**, G1 0.120 | **Present, 4 types**: FMJ 19.4 g at 440 m/s = 1,878 J, pen 40, dmg 85; JHP dmg 147 pen 12; Hawk JSP 26.4 g at 465 m/s = 2,854 J; Copper Solid pen 33 |
| **9x19** (Glock 17) | Federal AE9AP, 124 gr FMJ: 8.04 g at 351 m/s (4 in) = **494 J, 2.82 kg m/s**, BC 0.15 | 9 types, 398-658 J; ball-type PSO 7.46 g at 340 m/s = 431 J, pen 10, dmg 59; M882 pen 18; AP 6.3 pen 30; PBP pen 39 |

The test barrels match the mod's guns: the Desert Eagle's standard barrel is 6 in like Hornady's test barrel; the
Glock 17's is about 4.5 in, Federal tested from 4 in.

## Check against BALLISTICS.md ("Decided": shove = momentum, penetration = energy)

| Round | BALLISTICS.md | Real reference | Difference | What BALLISTICS.md's pair implies |
|---|---|---|---|---|
| 9 mm | 520 J, 2.9 kg m/s | 494 J, 2.82 kg m/s (Federal AE9AP 124 gr FMJ, 4 in) | **+5 % energy, +3 % momentum** (vs Hornady 124 gr XTP 460 J / 2.72: +13 % / +7 %) | 125 gr at 1,177 fps: a hotter load or a longer barrel |
| .50 AE | 2,100 J, 9.1 kg m/s | 1,965 J, 8.74 kg m/s (Hornady 9245 300 gr XTP, 6 in) | **+7 % energy, +4 % momentum** | 304 gr at 1,514 fps |

**Mismatch, small.** Both BALLISTICS.md figures run a few percent high. The ratios the design uses barely move:
.50 AE / 9 mm momentum 3.14 (BALLISTICS.md) vs 3.10 (real), energy 4.04 vs 3.98. Suggested values: 9 mm 494 J /
2.8 kg m/s, .50 AE 1,965 J / 8.7 kg m/s. (Bullet-only momentum; the recoil formula's powder-gas term comes on top.)
The .50 BMG line (17,000 J / 38 kg m/s) wasn't checked.

## Tarkov ammo by caliber

Columns: game values unless marked. **Dmg** = flesh damage per projectile (x N = projectiles per shot). **Pen** =
penetration power. **Armor dmg %** = share of pen dealt to armor durability. **Obstacle pen %** = tarkov.dev's
`penetrationChance` (raw `PenetrationChanceObstacle`): chance to go through world objects. **BC** = the game's
`BallisticCoeficient` (same range as G1 BCs; the game doesn't say which drag model). **E J, p kg m/s** = derived here
from mass and speed (1/2 m v^2, m v). **Beats class** = derived: highest Tarkov armor class (1-6) this round penetrates
with at least 50 % chance per hit on new armor, using the formula in "Tarkov's armor model". Sorted by pen. Bleed
modifiers, accuracy / recoil modifiers, stamina burn, heat, misfire chances are in the JSON.

<!-- tables: python3 tools/tarkov_ammo.py --tables -->
### .50 AE (12.7x33) (Desert Eagle) - `Caliber127x33`, 4 types

| Ammo | Dmg | Pen | Armor dmg % | Speed m/s | Mass g | BC | Frag % | Ricochet % | Obstacle pen % | Tracer | E J | p kg m/s | Beats class |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|---|--:|--:|:-:|
| .50 AE FMJ | 85 | 40 | 50 | 440 | 19.4 | 0.131 | 1 | 8 | 31 | - | 1878 | 8.54 | 4 |
| .50 AE Copper Solid | 94 | 33 | 56 | 460 | 25.9 | 0.163 | 30 | 6 | 20 | - | 2740 | 11.91 | 3 |
| .50 AE Hawk JSP | 122 | 26 | 28 | 465 | 26.4 | 0.166 | 30 | 0.4 | 12 | - | 2854 | 12.28 | 2 |
| .50 AE JHP | 147 | 12 | 23 | 440 | 19.4 | 0.122 | 70 | 2 | 5 | - | 1878 | 8.54 | 1 |

### 9x19 Parabellum (Glock 17) - `Caliber9x19PARA`, 9 types

| Ammo | Dmg | Pen | Armor dmg % | Speed m/s | Mass g | BC | Frag % | Ricochet % | Obstacle pen % | Tracer | E J | p kg m/s | Beats class |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|---|--:|--:|:-:|
| 9x19mm PBP gzh | 44 | 39 | 55 | 560 | 4.1 | 0.051 | 5 | 20 | 40 | - | 643 | 2.3 | 4 |
| 9x19mm AP 6.3 | 52 | 30 | 48 | 392 | 5.18 | 0.121 | 5 | 20 | 40 | - | 398 | 2.03 | 3 |
| 9x19mm Pst gzh | 54 | 20 | 33 | 457 | 5.4 | 0.068 | 15 | 5 | 20 | - | 564 | 2.47 | 2 |
| 9x19mm FMJ M882 | 56 | 18 | 35 | 385 | 5.4 | 0.068 | 20 | 10 | 20 | - | 400 | 2.08 | 2 |
| 9x19mm Green Tracer | 58 | 14 | 33 | 365 | 6 | 0.105 | 15 | 5 | 20 | green | 400 | 2.19 | 1 |
| 9x19mm Luger CCI | 70 | 10 | 38 | 420 | 7.46 | 0.131 | 25 | 6.5 | 12 | - | 658 | 3.13 | 1 |
| 9x19mm PSO gzh | 59 | 10 | 32 | 340 | 7.46 | 0.131 | 25 | 6.5 | 17 | - | 431 | 2.54 | 1 |
| 9x19mm QuakeMaker | 85 | 8 | 22 | 290 | 11.99 | 0.15 | 10 | 10 | 10 | - | 504 | 3.48 | 1 |
| 9x19mm RIP | 102 | 2 | 11 | 381 | 5.97 | 0.075 | 100 | 0.2 | 2 | - | 433 | 2.27 | - |

### .357 Magnum (9x33R) - `Caliber9x33R`, 4 types

| Ammo | Dmg | Pen | Armor dmg % | Speed m/s | Mass g | BC | Frag % | Ricochet % | Obstacle pen % | Tracer | E J | p kg m/s | Beats class |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|---|--:|--:|:-:|
| .357 Magnum FMJ | 70 | 35 | 43 | 385 | 10.15 | 0.163 | 1 | 6.5 | 35 | - | 752 | 3.91 | 3 |
| .357 Magnum JHP | 88 | 24 | 28 | 425 | 10.3 | 0.155 | 60 | 5.5 | 30 | - | 930 | 4.38 | 2 |
| .357 Magnum HP | 99 | 18 | 20 | 481 | 10.3 | 0.124 | 60 | 2.5 | 15 | - | 1192 | 4.95 | 2 |
| .357 Magnum SP | 108 | 12 | 15 | 455 | 10.2 | 0.189 | 20 | 3 | 20 | - | 1056 | 4.64 | 1 |

### .45 ACP (11.43x23) - `Caliber1143x23ACP`, 5 types

| Ammo | Dmg | Pen | Armor dmg % | Speed m/s | Mass g | BC | Frag % | Ricochet % | Obstacle pen % | Tracer | E J | p kg m/s | Beats class |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|---|--:|--:|:-:|
| .45 ACP AP | 66 | 38 | 48 | 299 | 13.8 | 0.197 | 1 | 10 | 29 | - | 617 | 4.13 | 4 |
| .45 ACP Match FMJ (!) | 72 | 25 | 36 | 340 | 7.46 | 0.131 | 1 | 6.5 | 16 | - | 431 | 2.54 | 2 |
| .45 ACP Lasermatch FMJ | 76 | 19 | 37 | 290 | 15 | 0.161 | 1 | 6.5 | 16 | red | 631 | 4.35 | 2 |
| .45 ACP Hydra-Shok | 100 | 13 | 30 | 274 | 14.9 | 0.114 | 50 | 6.5 | 13 | - | 559 | 4.08 | 1 |
| .45 ACP RIP (!) | 130 | 3 | 12 | 293 | 7.46 | 0.131 | 100 | 0.2 | 2 | - | 320 | 2.19 | - |

(!) `.45 ACP Match FMJ`: mass/diameter/BC copied from 9x19 (real 230 gr = 14.9 g, 11.5 mm); `.45 ACP RIP`: mass/diameter/BC copied from 9x19

### 5.56x45 NATO - `Caliber556x45NATO`, 11 types

| Ammo | Dmg | Pen | Armor dmg % | Speed m/s | Mass g | BC | Frag % | Ricochet % | Obstacle pen % | Tracer | E J | p kg m/s | Beats class |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|---|--:|--:|:-:|
| 5.56x45mm SSA AP | 38 | 57 | 58 | 1013 | 3.37 | 0.196 | 30 | 48 | 70 | - | 1729 | 3.41 | 6 |
| 5.56x45mm M995 | 42 | 53 | 52 | 1013 | 3.37 | 0.196 | 42 | 36 | 60 | - | 1729 | 3.41 | 5 |
| 5.56x45mm M855A1 | 49 | 44 | 47 | 945 | 4 | 0.233 | 44 | 38 | 55 | - | 1786 | 3.78 | 4 |
| 5.56x45mm M856A1 | 52 | 38 | 44 | 940 | 4 | 0.269 | 42.8 | 38 | 55 | red | 1767 | 3.76 | 4 |
| 5.56x45mm MK 318 Mod 0 (SOST) | 53 | 33 | 39 | 902 | 4 | 0.269 | 35 | 35 | 52 | - | 1627 | 3.61 | 3 |
| 5.56x45mm M855 | 54 | 31 | 37 | 922 | 4 | 0.233 | 50 | 40 | 50 | - | 1700 | 3.69 | 3 |
| 5.56x45mm FMJ | 57 | 23 | 33 | 957 | 3.75 | 0.252 | 50 | 26 | 42.5 | - | 1717 | 3.59 | 2 |
| 5.56x45mm M856 | 60 | 18 | 26 | 874 | 4.15 | 0.241 | 32.8 | 38 | 55 | red | 1585 | 3.63 | 2 |
| 5.56x45mm MK 255 Mod 0 (RRLP) | 72 | 11 | 24 | 936 | 5 | 0.335 | 3 | 10 | 20 | - | 2190 | 4.68 | 1 |
| 5.56x45mm HP | 79 | 7 | 22 | 947 | 3.6 | 0.209 | 70 | 20 | 40 | - | 1614 | 3.41 | 1 |
| 5.56x45mm Warmageddon | 88 | 3 | 11 | 936 | 5 | 0.335 | 90 | 5 | 20 | - | 2190 | 4.68 | - |

(!) left out `Airsoft 6mm BB` (joke item tagged with this caliber)

### 7.62x39 - `Caliber762x39`, 9 types

| Ammo | Dmg | Pen | Armor dmg % | Speed m/s | Mass g | BC | Frag % | Ricochet % | Obstacle pen % | Tracer | E J | p kg m/s | Beats class |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|---|--:|--:|:-:|
| 7.62x39mm MAI AP | 53 | 58 | 76 | 875 | 7.9 | 0.28 | 5 | 43.5 | 65 | - | 3024 | 6.91 | 6 |
| 7.62x39mm BP gzh | 58 | 47 | 63 | 730 | 7.9 | 0.28 | 12 | 31.5 | 60 | - | 2105 | 5.77 | 5 |
| 7.62x39mm PP gzh | 59 | 41 | 57 | 732 | 7.9 | 0.28 | 15 | 33 | 55 | - | 2117 | 5.78 | 4 |
| 7.62x39mm PS gzh | 61 | 35 | 52 | 717 | 7.9 | 0.28 | 20 | 35 | 40 | - | 2031 | 5.66 | 3 |
| 7.62x39mm T-45M1 gzh | 65 | 30 | 46 | 720 | 7.5 | 0.227 | 12 | 35 | 37 | red | 1944 | 5.4 | 3 |
| 7.62x39mm US gzh | 56 | 29 | 42 | 301 | 12.55 | 0.379 | 7.5 | 35.8 | 52 | - | 569 | 3.78 | 3 |
| 7.62x39mm FMJ | 63 | 26 | 33 | 775 | 7.9 | 0.28 | 30 | 25 | 35 | - | 2372 | 6.12 | 2 |
| 7.62x39mm SP | 68 | 20 | 27 | 772 | 7.9 | 0.28 | 35 | 20 | 30 | - | 2354 | 6.1 | 2 |
| 7.62x39mm HP | 80 | 15 | 20 | 754 | 8 | 0.278 | 40 | 17.5 | 26 | - | 2274 | 6.03 | 1 |

### 7.62x51 NATO - `Caliber762x51`, 8 types

| Ammo | Dmg | Pen | Armor dmg % | Speed m/s | Mass g | BC | Frag % | Ricochet % | Obstacle pen % | Tracer | E J | p kg m/s | Beats class |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|---|--:|--:|:-:|
| 7.62x51mm M993 | 70 | 65 | 85 | 930 | 8.1 | 0.291 | 13 | 28 | 92 | - | 3503 | 7.53 | 6 |
| 7.62x51mm M80A1 | 73 | 60 | 79 | 899 | 8.4 | 0.335 | 14 | 25 | 90 | - | 3394 | 7.55 | 6 |
| 7.62x51mm M61 | 75 | 55 | 74 | 838 | 9.75 | 0.346 | 13 | 30 | 90 | - | 3423 | 8.17 | 5 |
| 7.62x51mm M80 | 80 | 43 | 67 | 838 | 9.5 | 0.335 | 17 | 38 | 75 | - | 3336 | 7.96 | 4 |
| 7.62x51mm M62 Tracer | 82 | 42 | 65 | 838 | 9.2 | 0.326 | 14 | 38 | 67 | green | 3230 | 7.71 | 4 |
| 7.62x51mm BCP FMJ | 83 | 37 | 56 | 800 | 9.4 | 0.402 | 20 | 40 | 50 | - | 3008 | 7.52 | 4 |
| 7.62x51mm TCW SP | 85 | 30 | 25 | 771 | 10.8 | 0.383 | 35 | 40 | 20 | - | 3210 | 8.33 | 3 |
| 7.62x51mm Ultra Nosler | 105 | 15 | 40 | 823 | 10.7 | 0.375 | 70 | 20 | 10 | - | 3624 | 8.81 | 1 |

### 12 gauge (12/70) - `Caliber12g`, 17 types

| Ammo | Dmg | Pen | Armor dmg % | Speed m/s | Mass g | BC | Frag % | Ricochet % | Obstacle pen % | Tracer | E J | p kg m/s | Beats class |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|---|--:|--:|:-:|
| 12/70 AP-20 armor-piercing slug | 164 | 37 | 65 | 510 | 28.3 | 0.383 | 3 | 10 | 36 | - | 3680 | 14.43 | 4 |
| 12/70 flechette | 25 x8 | 31 | 26 | 320 | 0.47 /pellet | 0.257 | 0 | 0 | 30 | - | 24 x8 = 193 | 0.15 x8 = 1.2 | 3 |
| 12/70 makeshift .50 BMG slug | 197 | 26 | 57 | 410 | 45 | 0.582 | 5 | 10 | 28 | - | 3782 | 18.45 | 2 |
| 12/70 Piranha | 25 x10 | 24 | 22 | 310 | 0.47 /pellet | 0.257 | 0 | 10 | 20 | - | 23 x10 = 226 | 0.15 x10 = 1.46 | 2 |
| 12/70 FTX Custom Lite slug | 183 | 20 | 50 | 480 | 19.4 | 0.188 | 10 | 10 | 28 | - | 2235 | 9.31 | 2 |
| 12/70 Poleva-6u slug | 150 | 20 | 50 | 430 | 33.5 | 0.057 | 15 | 10 | 20 | - | 3097 | 14.41 | 2 |
| 12/70 Dual Sabot slug | 85 x2 | 17 | 65 | 415 | 22.5 /pellet | 0.074 | 10 | 10 | 25 | - | 1938 x2 = 3875 | 9.34 x2 = 18.68 | 2 |
| 12/70 Poleva-3 slug | 140 | 17 | 40 | 410 | 28 | 0.116 | 20 | 10 | 17 | - | 2353 | 11.48 | 2 |
| 12/70 lead slug | 167 | 15 | 55 | 370 | 36.6 | 0.121 | 20 | 10 | 20 | - | 2505 | 13.54 | 1 |
| 12/70 Copper Sabot Premier HP slug | 206 | 14 | 46 | 442 | 28.35 | 0.099 | 38 | 10 | 22 | - | 2769 | 12.53 | 1 |
| 12/70 Grizzly 40 slug | 190 | 12 | 48 | 390 | 40 | 0.222 | 12 | 10 | 20 | - | 3042 | 15.6 | 1 |
| 12/70 SuperFormance HP slug | 220 | 5 | 12 | 594 | 19.5 | 0.2 | 39 | 10 | 5 | - | 3440 | 11.58 | - |
| 12/70 6.5mm Express buckshot | 35 x9 | 3 | 26 | 430 | 2.1 /pellet | 0.024 | 0 | 0 | 2 | - | 194 x9 = 1747 | 0.9 x9 = 8.13 | - |
| 12/70 7mm buckshot (!) | 39 x8 | 3 | 26 | 415 | 0.09 /pellet | 0.013 | 0 | 0 | 2 | - | 8 x8 = 62 | 0.04 x8 = 0.3 | - |
| 12/70 8.5mm Magnum buckshot | 50 x8 | 2 | 26 | 385 | 3.38 /pellet | 0.022 | 0 | 0 | 5 | - | 251 x8 = 2004 | 1.3 x8 = 10.41 | - |
| 12/70 RIP | 265 | 2 | 11 | 410 | 19.6 | 0.055 | 100 | 1 | 0 | - | 1647 | 8.04 | - |
| 12/70 5.25mm buckshot | 37 x8 | 1 | 15 | 330 | 0.74 /pellet | 0.025 | 0 | 0 | 2 | - | 40 x8 = 322 | 0.24 x8 = 1.95 | - |

(!) `12/70 7mm buckshot`: 0.09 g per 7 mm pellet is impossible (lead ball ~2.0 g)
<!-- /tables -->

Row counts: .50 AE 4, 9x19 9, .357 4, .45 ACP 5, 5.56x45 11 (+1 joke item left out), 7.62x39 9, 7.62x51 8, 12 gauge 17:
**67 rows** (of 208 in the JSON).

## Real-world reference (one row per caliber)

Factory loads, manufacturer pages read 2026-10-12. Energy and momentum computed from the listed mass and velocity;
where the maker prints energy, the computed value matches it within 1 J. G1 = the maker labels it G1; "type not
stated" = Federal gives a BC without naming the drag model.

| Caliber | Load | Mass g (gr) | Muzzle velocity m/s (fps) | Test barrel | Energy J (ft-lb) | Momentum kg m/s | G1 BC | Source |
|---|---|--:|--:|---|--:|--:|--:|---|
| .50 AE | Hornady Custom 9245, 300 gr XTP HP | 19.44 (300) | 449.6 (1,475) | 6 in | 1,965 (1,449) | 8.74 | 0.120 | https://www.hornady.com/ammunition/handgun/50-action-express-300-gr-xtp-hp |
| 9x19 | Federal American Eagle AE9AP, 124 gr FMJ | 8.04 (124) | 350.5 (1,150) | 4 in | 494 (364) | 2.82 | 0.15 (type not stated) | https://le.vistaoutdoor.com/ammunition/federal/handgun/details.aspx?id=93 |
| 9x19 (alt.) | Hornady Custom 90242, 124 gr XTP | 8.04 (124) | 338.3 (1,110) | 4 in | 460 (339) | 2.72 | 0.165 | https://www.hornady.com/ammunition/handgun/9mm-luger-124-gr-xtp |
| .357 Magnum | Hornady Custom 90562, 158 gr XTP | 10.24 (158) | 381.0 (1,250) | 8 in vented | 743 (548) | 3.90 | 0.206 | https://www.hornady.com/ammunition/handgun/357-mag-158-gr-xtp |
| .45 ACP | Federal American Eagle AE45A, 230 gr FMJ | 14.90 (230) | 271.3 (890) | 5 in | 548 (404) | 4.04 | 0.184 (1) | https://www.federalpremium.com/handgun/american-eagle/american-eagle-handgun/11-AE45A.html |
| 5.56x45 | Hornady Frontier FR200, 55 gr FMJ (M193) | 3.56 (55) | 987.6 (3,240) | 20 in | 1,738 (1,282) | 3.52 | 0.243 | https://www.hornady.com/ammunition/rifle/5.56-nato-55-gr-fmj-bt-frontier |
| 5.56x45 (62 gr) | Hornady Frontier FR260, 62 gr FMJ (M855 weight, but Frontier bullets have no steel core) | 4.02 (62) | 932.7 (3,060) | 20 in | 1,747 (1,289) | 3.75 | 0.274 | https://www.hornady.com/ammunition/rifle/5.56-nato-62-gr-fmj-frontier |
| 7.62x39 | Hornady Frontier FR420, 123 gr FMJ | 7.97 (123) | 713.2 (2,340) | 24 in | 2,027 (1,495) | 5.68 | 0.266 | https://www.hornady.com/ammunition/rifle/7.62x39-123-gr-fmj-frontier |
| 7.62x51 | Federal American Eagle AE308D, .308 Win 150 gr FMJ-BT (2) | 9.72 (150) | 859.5 (2,820) | not stated | 3,590 (2,648) | 8.35 | 0.408 (type not stated) | https://www.federalpremium.com/rifle/american-eagle/american-eagle-rifle/11-AE308D.html |
| 12 ga buckshot | Federal Power-Shok F127 00, 2-3/4 in, 9 x 00 buck | 3.50 (54) per pellet (3) | 403.9 (1,325) | not stated | 285 per pellet, 2,569 total (3) | 1.41 per pellet, 12.7 total | not published | https://www.federalpremium.com/shotshell/power-shok-buckshot/power-shok-buckshot---low-recoil/11-F127+00.html |
| 12 ga slug | Federal Power-Shok F127 RS, 1 oz rifled HP slug | 28.38 (438) | 490.7 (1,610) | not stated | 3,418 (2,521) | 13.93 | 0.110 (type not stated) | https://www.federalpremium.com/shotshell/power-shok-rifled-slug/11-F127+RS.html |

(1) Federal lists no BC for AE45A; this is Hornady's G1 for its 230 gr .451 FMJ-RN bullet (item 45177),
https://www.hornady.com/bullets/handgun/45-cal-451-230-gr-fmj-rn. (2) Federal's 7.62x51 NATO 149 gr XM80C page is gone
(404); AE308D is the commercial .308 Win counterpart of M80 ball. (3) Federal lists 9 pellets, lead at 11.35 g/cc, but
no pellet mass; derived from the standard 00 buck diameter of 0.33 in (8.38 mm). Cross-check for .357: Federal AE357A
158 gr JSP, 1,240 fps / 539 ft-lb from 4 in vented (731 J, 3.87 kg m/s),
https://www.federalpremium.com/handgun/american-eagle/american-eagle-handgun/11-AE357A.html.

### Tarkov vs real
- Close for ball / FMJ rounds (mass, speed, energy within about 10 %): .50 AE FMJ 1,878 J vs 1,965 J; 5.56 M855 4.0 g
  at 922 m/s (1,700 J) vs 62 gr at 933 m/s (1,747 J); 7.62x51 M80 9.5 g at 838 m/s (3,336 J) vs 150 gr at 860 m/s
  (3,590 J); .45 Hydra-Shok 14.9 g at 274 m/s (559 J) vs 548 J; .357 FMJ 752 J vs 743 J.
- Tarkov's 9 mm ball is weaker than real: PSO 431 J, M882 400 J vs 494 J (Federal 124 gr ball).
- Tarkov's 7.62x39 FMJ / SP / HP (754-775 m/s) are 6-9 % faster than Hornady's 24 in barrel figure (713 m/s); PS gzh
  (717 m/s) matches.
- Tarkov's 7.62x51 BCs (0.29-0.40) run lower than Federal's 0.408; .50 AE (0.122-0.166) and 7.62x39 (0.28) are close.
- Data errors in the game files, flagged (!) in the tables: .45 ACP Match FMJ and RIP reuse 9x19's mass (7.46 g),
  diameter (9 mm) and BC; 12/70 7mm buckshot has 0.09 g pellets. Use the real 230 gr / 14.9 g for .45.
- Hollow points in Tarkov trade pen for damage hard (.50 AE JHP dmg 147 pen 12 vs FMJ dmg 85 pen 40): a game-balance
  shape, not physics. Their energy is the same as the FMJ (both 19.4 g at 440 m/s).

## Tarkov's armor model (shape we may borrow)

Sources: EFT wiki "Ballistics" (https://escapefromtarkov.fandom.com/wiki/Ballistics, read through its MediaWiki API
2026-10-12), the tarkov.dev schema (https://github.com/the-hideout/tarkov-api/blob/main/schema-static.mjs), the game's
globals / item templates via SPT, and TarkovGunsmith's simulator, which reconstructs the formulas from in-game tests
(https://github.com/Xerxes-17/TarkovGunsmith/blob/main/BackEnd/WishGranter/Statics/Ballistics.cs). The formulas are a
community reconstruction, not BSG's published code.

- **Pieces and layers.** Each armor piece has a class 1-6, durability (current / max), a material, the body zones it
  covers (thorax, stomach, head top, nape, ...) and a blunt throughput. Vests are built from soft inserts plus hard
  plates, each its own layer with its own class and durability (schema: `ItemPropertiesArmor` with `armorSlots`,
  `ItemPropertiesArmorAttachment`). Unprotected areas (armpits) are weak points. Armor stops rounds outright (or not);
  it isn't a flat damage reduction.
- **Penetration vs class.** Rolled per layer from class, durability % (D, 0-100) and the round's pen. Angle doesn't
  matter (except helmet ricochets).
  - A = (121 - 5000 / (45 + 2 D)) x class / 10. New armor: A = 10.06 x class; 75 %: 9.54 x class; 50 %: 8.65 x
    class; 25 %: 6.84 x class.
  - pen >= A: chance = 1 - pen / (pen - 0.9 A) / 100, 90-99 %. A - 15 < pen < A: chance = 0.4 (A - pen - 15)^2 / 100,
    rising from 0 to 90 %. pen <= A - 15: 0. Rule of thumb: class N needs pen of about 10 N, and 15 below that nothing
    gets through.
- **After a penetration**, damage and pen are both multiplied by clamp(pen / (A + 12), 0.6, 1) (0-40 % loss, usually
  about 20 %), then the next layer rolls. **On a block**, blunt damage = blunt throughput x clamp(1 - 0.03 (A - pen),
  0.2, 1) x damage. Soft armor behind a plate cuts it further (TarkovGunsmith treats a plate block as zero blunt).
- **Durability loss per hit** is about pen x armor dmg % x material destructibility, scaled 0.6-1.1 on a block and
  0.5-0.9 on a penetration (TarkovGunsmith's regression; the wiki says about 10-15 % less when the round goes through),
  minimum 1 per hit, even per buckshot pellet. Lower durability % lowers A, so armor degrades into penetrability.
  Effective durability = durability / destructibility. Destructibility from the game globals (same as the wiki):
  Aramid 0.1875, UHMWPE 0.3375, Combined 0.375, Titan 0.4125, Aluminium 0.45, Armored steel 0.525, Ceramic 0.6,
  Glass 0.6 (TarkovGunsmith uses 0.55 for ceramic).
- **Ricochet by angle.** The wiki says only helmets ricochet. Armor items carry `RicochetParams` (x, y, z)
  (tarkov.dev: ricochetX / Y / Z), e.g. FORT Kiver-M helmet (0.8, 0.3, 70), Altyn face shield (0.7, 0.4, 70); gear that
  never ricochets has (0, 0, 80). In 0.16.9 the values are non-zero on helmets and their built-in inserts, face shields
  / masks, two visors and 12 plates (6B12, 6B13, Korund side: (0.8, 0.3, 80)); whether plates actually use them isn't
  confirmed. Reading (x, y, z) as max chance, min chance and the angle (degrees) where ricochets start fits the data
  but isn't verified. A
  ricochet counts as a block with almost no blunt damage and the minimum 1 durability loss (wiki). Separately each round
  has `ricochetChance` and an obstacle penetration chance for world surfaces; whether it penetrates or ricochets off an
  object depends on the object's material and the round.
- **Flight.** Rounds slow with drag (BC) and drop with gravity, and lose damage and pen as they slow (wiki; the exact
  falloff isn't in these sources).
- **What it gives us.** Example penetration chance per hit on new armor, from the formula:

| Round | Pen | Class 1 | 2 | 3 | 4 | 5 | 6 |
|---|--:|--:|--:|--:|--:|--:|--:|
| 9x19 PSO / Luger CCI | 10 | 89 | 10 | 0 | 0 | 0 | 0 |
| 9x19 FMJ M882 | 18 | 98 | 66 | 3 | 0 | 0 | 0 |
| 9x19 PBP | 39 | 99 | 98 | 97 | 76 | 5 | 0 |
| .45 ACP Lasermatch FMJ | 19 | 98 | 77 | 6 | 0 | 0 | 0 |
| .357 Magnum FMJ | 35 | 99 | 98 | 96 | 38 | 0 | 0 |
| .50 AE JHP | 12 | 96 | 19 | 0 | 0 | 0 | 0 |
| .50 AE Hawk JSP | 26 | 98 | 97 | 47 | 0 | 0 | 0 |
| .50 AE FMJ | 40 | 99 | 98 | 97 | 87 | 9 | 0 |
| 5.56x45 M855 | 31 | 99 | 98 | 92 | 13 | 0 | 0 |
| 7.62x39 PS | 35 | 99 | 98 | 96 | 38 | 0 | 0 |
| 7.62x51 M80 | 43 | 99 | 98 | 97 | 94 | 24 | 0 |
| 12/70 8.5 mm buckshot | 2 | 19 | 0 | 0 | 0 | 0 | 0 |

  The "Decided" rule (small rounds bounce off plate, .50 AE goes through) maps onto this directly: rate KCD2 plate at
  class 3-4 and 9 mm ball (pen 10-20) is stopped, while .50 AE FMJ (pen 40) gets through class 4 87 % of the time when
  new. Worn plate (50 %) lets 9 mm M882 through class 3 20 % of the time.

## Fields in tarkov_ammo.json
- Top level: `fetchedUtc`, `source`, `gameVersion` (SPT fallback only), `count`, `ammo[]`, `armorMaterials[]`.
- Each `ammo[]` entry: tarkov.dev's `Ammo` fields (`caliber`, `damage`, `armorDamage`, `penetrationPower`,
  `penetrationPowerDeviation`, `penetrationChance` (obstacles), `fragmentationChance`, `ricochetChance`, `initialSpeed`,
  `projectileCount`, `tracer`, `tracerColor` (meaningless when `tracer` is false), `ammoType`, `lightBleedModifier`,
  `heavyBleedModifier`, `accuracyModifier`, `recoilModifier`, `staminaBurnPerDamage`, `weight`, `stackMaxSize`) and
  `item { id, name, shortName, properties }`, where `properties` (`ItemPropertiesAmmo`) adds `bulletMassGrams`,
  `ballisticCoeficient` (sic), `bulletDiameterMilimeters` (sic), `durabilityBurnFactor`, `heatFactor`, `misfireChance`,
  `failureToFeedChance`.
- SPT fallback only: `sptExtra` keeps raw fields tarkov.dev drops: `SpeedRetardation`, `TracerDistance`,
  `PenetrationDamageMod`, fragment counts, `AmmoLifeTimeSec`, `ammoDist` / `ammoHear` (audible distance), explosion
  fields. Their exact meaning isn't verified.
- Caliber ids: .50 AE `Caliber127x33`, 9x19 `Caliber9x19PARA`, .357 `Caliber9x33R`, .45 `Caliber1143x23ACP`, 5.56
  `Caliber556x45NATO`, 7.62x39 `Caliber762x39`, 7.62x51 `Caliber762x51`, 12 ga `Caliber12g`.
