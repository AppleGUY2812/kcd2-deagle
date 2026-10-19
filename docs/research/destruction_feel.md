# Destruction feel: no stiff pieces, things that hang on (the user's requirements, 2026-10-18)

The user's fear: pre-fractured debris looks like stiff toy blocks. Their idea: some parts should stay "sort of
connected like a joint, like sinew". These are requirements for the destruction prototype (destruction_buildings.md),
judged on in-game video before anything is batched to the ~1,700 houses.

## 1. Nothing stiff
| stiff look | fix |
|---|---|
| same-size clean chunks | mixed sizes (a few big slabs, many small); noisy, jagged cut faces; wood split along the grain into splinters (grain-aligned Voronoi cells for beams) |
| everything breaks at once | progressive failure: supports give over 0.2-1 s, so a roof sags, cracks, then drops |
| rigid beams | bendable joints: CryPhysics structural joints have bend / twist limits (`maxTorqueBend`, `maxTorqueTwist`): a beam sags before it snaps |
| a chunk lands whole | multi-level breaking: a big chunk that hits hard breaks again into smaller ones (hierarchical fracture) |
| clean physics in empty air | secondary detail: crumbs and dust trailing off pieces, a dust puff on each impact, a settling cloud (which also hides the swap) |
| floaty or bouncy | mass and friction per material: stone heavy and dead, timber medium, thatch light and drifting; low restitution |

KCD2's houses are mostly timber, plaster and thatch: splintering wood and collapsing thatch hide stiffness better than
concrete would.

## 2. Parts that stay connected ("sinew")
The stage between broken and gone: crack -> sag -> dangle -> tear.

| technique | what it gives | how in KCD2 |
|---|---|---|
| weak hinge joints | a board or beam broken free at one end that swings from the other; a door on one hinge | physics joints with angle limits and damping, breaking only past a much higher force (the same joint system KCD2's ragdolls use) |
| rope / sinew links | splinters, thatch strands, rafters dangling between two pieces, stretching and swaying | CryEngine rope physics between two chunks, snapping at a set tension |
| stretchy skinned fibres | torn wood fibres filling the gap as two pieces part, then tearing | each chunk its own bone, a fibre strip skinned to both sides: it stretches as the pieces separate, hidden past a tear threshold (games use this for bent metal and wood) |

Cost: hinge joints are nearly free once the support graph exists; ropes and fibre strips are extra art and tuning, after
the basic prototype looks right.

## 3. How it's judged
- Prototype: one house, one grenade. The pass test includes "doesn't look stiff" and "something hangs on", recorded in
  game (60 fps, slow motion) and judged by the user frame by frame, as with the muzzle flash.
- First probe (destruction_buildings.md S0): do KCD2's joints still bend and break as the CryEngine docs say? That
  decides whether section 2 is cheap or hard.
