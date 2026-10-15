# Brief (cloud session): our own bullet ray tracer, milestone 2, testable without the game

For a Claude Code cloud session on the code-only mirror. Read docs/ARCHITECTURE.md (milestone 2, the GPU decision) and
docs/whgame_re/collision_copy.md (how KCD2's RayWorldIntersection decides a hit: terrain first, holes, two-sided
triangles, ray-flag parts only, surface mapping) and docs/whgame_re/rays.md.

## Build
`native/tracer/` (new): a C++20 library with no engine or Windows headers.
- Input: a scene of static parts (triangle meshes with per-triangle surface ids and a mapping table, boxes, cylinders,
  capsules, spheres, each with a world transform) and a heightfield (size, step, origin, height and surface callbacks,
  hole value) in tiles of 32-64 m that can be added and removed while running.
- Ray query: first hit along a segment (dist, point, normal, surface, part / triangle id) with exactly KCD2's rules
  from collision_copy.md, plus a "moving bodies" list of capsules / boxes tested analytically.
- Backend: Intel Embree 4 (Apache 2.0) if the cloud image can fetch and build it (CMake FetchContent); otherwise a
  plain BVH of its own, with the same interface, so Embree can be swapped in. Batch API: N rays in, N hits out, split
  over worker threads.
- No allocation per ray. Thread-safe queries while tiles are swapped (double-buffered scene).

## Tests
- Unit tests on synthetic scenes against a brute-force reference (every triangle tested): random rays, 1e-5 m.
- KCD2's rules: terrain hole cells pass, back faces hit, a non-ray part is ignored, surface mapping (id < 0 = part's
  own, table[id < count ? id : 0]).
- Benchmark: a synthetic town (terrain 512 x 512 m + 5,000 meshes of 200-5,000 triangles), 1 million random 7 m and
  50 m rays: rays/s on 1, 2, 4 threads.

## Rules
Commit on a branch `cloud/tracer`, small commits, `bash tests/run_all.sh` passing (add the C++ tests only behind a
compiler check, as in the native-sim brief). Open questions into `native/tracer/QUESTIONS.md`.
