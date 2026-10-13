# How the gunshot sound works (plain words)

## The parts of a gunshot

What you hear = **source + echo + reverb**.

1. **Source:** the bang itself.
2. **Echo:** one bounce you can hear on its own, from far away (shout at a cliff: "hey..... hey").
3. **Reverb:** thousands of bounces blurred into one fading "ahhhh".

The **tail** is the part you hear after the bang: the fading ring from the reverb.
- Small room: short tail.
- Big hall: long tail.
- Open field: almost no tail.

Sound doesn't really turn a wall into a new speaker. It **bounces off** the wall, like a ball off a wall.
- **Metal** rings long because it bounces back almost all the sound and loses little on each hit.
- **Fabric** soaks sound up, especially the high, sharp part, so it dies fast and sounds muffled.
- A material shaking and making its own sound is real but tiny. It only matters for things like a bell or a thin metal sheet.

## How Ready or Not builds one shot

**Step 1: the dry shot**
- 1 bang, picked at random out of 4. All 4 come up about equally.
- Plus all 3 echoes: Small, Base and Large. All 3 play on every shot; they are **not** picked by room size.
- Inside or outside decides which set of 3 echoes plays. Inside and outside each have their own set.
- Each echo was recorded twice (A and B), and every shot flips a coin for each one.
- So one shot can sound **4 × 2 × 2 × 2 = 32 ways**.

**Step 2: the room**
- The map maker places boxes by hand for rooms and outdoor areas. Standing inside a room box = inside.
- Each box gets a room **type** picked by hand. Nothing is measured, so there is no size cutoff.
  - Indoor types: bathroom, small, mid, corridor, hall.
  - Outdoor types: street, forest, cave.
- The type turns on that room's reverb preset, which adds the tail.

**Step 3: material (optional)**
- The dev can also mark a room's material: concrete, wood, metal or fabric. Most rooms have none.
- A material adds a second tail on top of the room's tail. Room and material presets stack; they are not hand-made per combination.

## The equation

> **What you hear = Dry shot + (Amount × Tail)**

- **Dry shot:** bang + 3 echoes (step 1).
- **Amount:** how loud the tail is next to the shot. Indoor and cave 0.56, outdoor 0.18 (about 3x quieter).
- **Tail:**
  - **Length:** how long it rings.
  - **First bounce:** how soon the first wall bounce comes back.
  - **Muffle:** sound above this pitch (Hz) is cut from the tail. Lower = duller.

| Preset | Equation | Explanation |
|---|---|---|
| Bathroom | Dry + 0.56 × Tail(1.2 s, bounce 12 ms, muffle 8500) | Tiny hard room. Short, bright, instant bounce. |
| Small room | Dry + 0.56 × Tail(1.4 s, bounce 9 ms, muffle 4000) | Closet. Short and a bit dull. |
| Mid room | Dry + 0.56 × Tail(1.7 s, bounce 18 ms, muffle 8500) | Normal room. Medium ring, bright. |
| Corridor | Dry + 0.56 × Tail(2.0 s, bounce 24 ms, muffle 3600) | Long hallway. Longer ring, dull. |
| Hall | Dry + 0.56 × Tail(2.6 s, bounce 42 ms, muffle 4000) | Big room. Long ring, walls far away. |
| Cave | Dry + 0.56 × Tail(2.1 s, bounce 27 ms, muffle 2500) | Rock all around. Long, dull ring. |
| Street | Dry + 0.18 × Tail(3.2 s, bounce 52 ms, muffle 3600) | Buildings far away. Quiet but long rolling echo. |
| Forest | Dry + 0.18 × Tail(1.5 s, bounce 110 ms, muffle 1500) | Trees soak it up. Quiet, very dull, late bounce. |

Material layers, added as a second tail. Their Amount isn't pulled from the game files yet.

| Material | Adds | Explanation |
|---|---|---|
| Concrete | + Tail(1.5 s, muffle 4000) | Hard, clear ring. |
| Wood | + Tail(1.5 s, muffle 5000) | Warmer, brighter. |
| Metal | + Tail(3.5 s, muffle 3200) | Rings a long time. |
| Fabric | + Tail(1.5 s, muffle 1150) | Very muffled, soft. |

## How our mod copies it

- KCD2 can't run Ready or Not's sound engine.
- So we ran Ready or Not's own engine offline and recorded steps 1 + 2 for each place type: all 32 versions per place, 13 places.
- KCD2 has no room boxes. Every 0.25 s the mod casts rays around you to work out inside/outside and the place type.
- When you fire, it plays one of that place's 32 recordings. The console command `deagle_env` shows the current place.

Exact numbers and sources: `docs/ron_fmod_events.md`, `docs/mk5_sound_spec.md`.
