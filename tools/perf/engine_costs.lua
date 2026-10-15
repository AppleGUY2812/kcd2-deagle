-- tools/live.sh tools/perf/engine_costs.lua: times single engine calls in the running game (read-only), logged as
-- "[Deagle] perf". 2026-10-15 at the village: GetEntitiesInSphere 3 m 5.35 ms, a 40 m ray 0.015 ms, the room check
-- (21 rays) 0.3-0.6 ms.
-- read-only timing probe: the room check (EnvSample = all its rays), one ray, one GetEntitiesInSphere
local D = Deagle
local clock = System.GetCurrAsyncTime
local function ms(f, n) local a = clock() for _ = 1, n do f() end return (clock() - a) * 1000 / n end
local eye = System.GetViewCameraPos()
local pl = D:Player()
local rays = 0
local orig = Physics.RayWorldIntersection
Physics.RayWorldIntersection = function(...) rays = rays + 1 return orig(...) end
local tSample = ms(function() D:EnvSample() end, 10)
Physics.RayWorldIntersection = orig
local perSample = rays / 10
local tRay40 = ms(function() local h = {} Physics.RayWorldIntersection(eye, { x = 40, y = 0, z = 0 }, 1, ent_static + ent_terrain, pl.id, nil, h) end, 50)
local tRay40all = ms(function() local h = {} Physics.RayWorldIntersection(eye, { x = 40, y = 0, z = 0 }, 1, ent_all, pl.id, nil, h) end, 50)
local tSphere = ms(function() System.GetEntitiesInSphere(pl:GetWorldPos(), 3) end, 3)
local tClassify = ms(function() D:EnvClassify(D:EnvSample()) end, 5)
System.LogAlways(string.format("[Deagle] perf EnvSample %.2f ms (%.1f rays), +classify %.2f ms; one 40 m ray static %.3f ms, all %.3f ms; GetEntitiesInSphere 3 m %.2f ms; clock unit check %.6f",
	tSample, perSample, tClassify, tRay40, tRay40all, tSphere, clock()))
