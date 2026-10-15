# Brief (cloud session): research papers: the theoretical best for many bullets

The user: "research papers for stuff like this, we need to find the theoretical best." Read docs/ARCHITECTURE.md
("The bar", "Milestone 2", "Bullet design: work only when something happens") first: that design is what to test
against the literature. No code; write `docs/research/bullets_theory.md` on a branch `cloud/papers`.

## Questions
1. **Lower bound.** What must any simulator of N rounds/s with H hits/s against a static scene of n triangles and M
   moving bodies do at minimum? (Each round at least once at the shot, each hit once: Omega(N + H) per second, plus
   the query cost.) Is our event-driven design asymptotically optimal; where is it not?
2. **Ray shooting** (computational geometry): best known query time / space for ray shooting among triangles and over
   terrains (Agarwal, Matousek; de Berg et al.), and what practical BVHs reach (SAH: MacDonald and Booth 1990;
   Wald; Embree: Wald et al. 2014; Aila and Laine 2009; wide BVHs, compressed BVHs; incoherent ray performance).
   Coherent packets / ray streams / cone tracing for bursts.
3. **Moving bodies vs many paths**: kinetic data structures (Basch, Guibas, Hershberger), continuous collision
   detection and time of impact (Redon; Mirtich), swept volumes, broad phase (sweep and prune, Baraff; optimized
   spatial hashing, Teschner et al. 2003), which fits "few movers, very many known paths".
4. **Event scheduling**: discrete event simulation queues: calendar queue (Brown 1988), timing wheels (Varghese and
   Lauck 1987), ladder queue; best for 10^5 events/s with near-uniform times.
5. **Trajectories**: closed forms for drag (linear drag exact; quadratic drag: no general closed form, approximations,
   e.g. Parker 1977, and modern papers), and table / polynomial fits per cartridge with error bounds vs a step
   integrator (what error is acceptable: compare with ballistics.lua's 0.05 s steps).
6. **Parallel and GPU**: batched ray queries, work distribution, and when GPU ray tracing (DXR, OptiX) beats CPU for
   small batches with a 1-3 frame latency budget.
7. **Rendering many tracers / impacts / decals / lights**: instancing, GPU particles, clustered lighting (Olsson et al.
   2012), deferred decals and overdraw, light budgets: the theory and the published numbers.

## Output (docs/research/bullets_theory.md)
Per question: the best known result (complexity + practical constants), the paper (authors, year, link: arXiv, ACM,
author pages), what it means for our design (keep / change / add), and a confidence. Separate peer-reviewed papers,
industry talks (GDC, SIGGRAPH courses, HPG) and blogs. End with a ranked list of design changes the literature
supports, each with the expected gain. Plain words; under ~400 lines.
