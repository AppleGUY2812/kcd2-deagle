-- Impact effects (Claude 2): SpawnFx, per-hit variety, the impactLook test looks, deagle_fx, deagle_fxreload.
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U
local coneDir, log, try, unquote, vadd, vcross, vnorm, vscale = U.coneDir, U.log, U.try, U.unquote, U.vadd, U.vcross, U.vnorm, U.vscale

local PLUGIN_FX = { deagle_impacts = true, deagle_blood = true }
-- per-hit variety (user 2026-10-14: the sparks "look the same" every hit): the effect's axis tilts up to `tilt` deg at a
-- random heading, and `jets` extra `jet` emitters point along the surface at random headings, up to `lift` deg off it
local FX_VARY = {
	["deagle_impacts.metal_v2"] = { tilt = 20, jet = "deagle_impacts.metal_v2_jet", jets = { 1, 3 }, lift = 20 },
}
-- how long the plugin holds each effect (fxs' last field, game seconds): its own longest life + 0.15 s; others 2 s.
-- Generated: tools/fx_hold.py (tests/run_all.sh fails when it no longer matches the particle libraries)
local FX_HOLD = {
	["deagle_impacts.metal"] = 1.65,
	["deagle_impacts.stone"] = 1.95,
	["deagle_impacts.plaster"] = 1.55,
	["deagle_impacts.wood"] = 2.15,
	["deagle_impacts.soil"] = 1.55,
	["deagle_impacts.sand"] = 1.40,
	["deagle_impacts.water"] = 0.90,
	["deagle_impacts.straw"] = 1.75,
	["deagle_impacts.cloth"] = 1.95,
	["deagle_impacts.glass"] = 0.95,
	["deagle_impacts.stone_exit"] = 1.55,
	["deagle_impacts.plaster_exit"] = 1.15,
	["deagle_impacts.wood_exit"] = 2.15,
	["deagle_impacts.metal_exit"] = 2.15,
	["deagle_impacts.glass_exit"] = 0.95,
	["deagle_impacts.cloth_exit"] = 1.15,
	["deagle_impacts.ricochet_metal"] = 0.75,
	["deagle_impacts.ricochet_stone"] = 1.35,
	["deagle_impacts.water_v3"] = 1.70,
	["deagle_impacts.metal_v2"] = 1.05,
	["deagle_impacts.metal_v2_jet"] = 0.40,
	["deagle_blood.entry"] = 1.05,
	["deagle_blood.exit"] = 1.25,
	["deagle_blood.head"] = 1.55,
	["deagle_blood.armor_entry"] = 1.75,
}
-- m: no impact / blood effect further from the camera (x the debug zoom): a 1 m splash is ~4 px tall at 250 m
-- (1080p, 60 deg view), and rounds are dropped at 250 m anyway (cfg.range); far ricochets landed up to 1 km out
local FX_FAR = 250
local LOOK_NEW = { ["deagle_impacts.water"] = "deagle_impacts.water_v3", ["deagle_impacts.metal"] = "deagle_impacts.metal_v2" }
-- Impact effect budget. A CIWS at 75 rounds/s spawned ~300 impact effects a second and the game fell to 4 fps (engine
-- 200-250 ms a frame, back to normal within a second of stopping: their dust and smoke piling up in front of the
-- camera; deagle_stress 75 aim, 2026-10-15). So: (1) a hit next to a fresh effect of the same kind gets none (within
-- cfg.fxBudget.near m x the impact scale, younger than nearTime s of game time): at that rate it lands in the same
-- cloud; (2) a token bucket: bursts up to burst effects, then rate a second. Skips are counted and logged at most every
-- 2 s. Lights, holes and sounds aren't affected. A/B: deagle_set fxBudget.on 0
local FX_RECENT = 24
local FX_JET = {}
for _, v in pairs(FX_VARY) do FX_JET[v.jet] = true end
function Deagle:FxBudget(name, pos, scale)
	local B = self.cfg.fxBudget
	if not (B and B.on) then return true end
	local now = System.GetCurrTime()
	local st = self.fxBudgetState
	if not st or now < st.t then   -- first use, or the clock went back (a save load)
		st = { tokens = B.burst, t = now, ring = {}, i = 0, skips = 0, said = 0 }
		self.fxBudgetState = st
	end
	st.tokens = math.min(B.burst, st.tokens + (now - st.t) * B.rate)
	st.t = now
	local skip = st.tokens < 1
	if not skip and not FX_JET[name] then   -- a metal hit's own rays all start at its spot
		local near = B.near * (scale or 1)
		for k = 1, FX_RECENT do
			local r = st.ring[k]
			if r and r.name == name and now - r.t < B.nearTime then
				local dx, dy, dz = pos.x - r.x, pos.y - r.y, pos.z - r.z
				if dx * dx + dy * dy + dz * dz < near * near then skip = true break end
			end
		end
	end
	if skip then
		st.skips = st.skips + 1
		if now >= st.said then
			st.said = now + 2
			log(string.format("impact effects: %d skipped so far (budget %g a second, none within %g m of a fresh one)", st.skips, B.rate, B.near))
		end
		return false
	end
	st.tokens = st.tokens - 1
	st.i = st.i % FX_RECENT + 1
	local r = st.ring[st.i] or {}
	r.name, r.x, r.y, r.z, r.t = name, pos.x, pos.y, pos.z, now
	st.ring[st.i] = r
	return true
end

function Deagle:SpawnFx(name, pos, normal, scale)
	if not name then return end
	if self.cfg.impactLook == "new" and LOOK_NEW[name] then name = LOOK_NEW[name] end
	if name == self.cfg.muzzleFx then scale = self.cfg.muzzleFxScale end
	local lib = string.match(name, "^([%w_]+)%.")
	if PLUGIN_FX[lib] and pos then   -- an impact / blood effect too far to see isn't spawned (debug zoom sees further)
		local cp = System.GetViewCameraPos()
		local dx, dy, dz = pos.x - cp.x, pos.y - cp.y, pos.z - cp.z
		if dx * dx + dy * dy + dz * dz > (FX_FAR * (self.zoom or 1)) ^ 2 then return end
	end
	self:ImpactLight(name, pos, normal, scale)
	if PLUGIN_FX[lib] and pos and not self:FxBudget(name, pos, scale) then return end
	local vary = FX_VARY[name]
	if vary and pos and normal then
		local a = math.abs(normal.z) < 0.9 and { x = 0, y = 0, z = 1 } or { x = 1, y = 0, z = 0 }
		local u = vnorm(vcross(normal, a))
		local v = vcross(normal, u)
		for _ = 1, math.random(vary.jets[1], vary.jets[2]) do
			local h, e = math.random() * 2 * math.pi, math.rad(math.random() * vary.lift)
			local along = vadd(vscale(u, math.cos(h)), vscale(v, math.sin(h)))
			self:SpawnFx(vary.jet, pos, vadd(vscale(along, math.cos(e)), vscale(normal, math.sin(e))), scale)
		end
		normal = coneDir(normal, vary.tilt)
	end
	if self.cfg.impactSpawn == "plugin" and self.native and PLUGIN_FX[lib] and pos and normal then
		-- fxs <effect> <scale> <box length m along the normal> <pos> <dir>: drawn from its first frame
		self:Native(string.format("fxs %s %.3f %.3f %.3f %.3f %.3f %.4f %.4f %.4f %.2f", name, scale or 1.0, 0.3,
			pos.x, pos.y, pos.z, normal.x, normal.y, normal.z, FX_HOLD[name] or 2))
		return
	end
	try("fx " .. name, Particle.SpawnEffect, name, pos, normal, scale or 1.0)
end

-- Impact lights (the user 2026-10-15, for a CIWS: "impacts allow me to light up the entire environment"): sparks and
-- flashes light what's around them. A fixed pool of cfg.impactLight.max world lights (a BasicEntity each, its light in
-- slot 0, reused), never a light per hit: at 75 rounds/s with 0.05 s each about 4 burn at once. A hit while every
-- light burns gets none (counted; logged at most every 2 s). Sparks only (soil, wood, water, cloth give no light), x
-- the impact scale (energy). Game time, so F9 stretches them. Off: deagle_set impactLight.on 0
local IMPACT_LIGHT = { ["deagle_impacts.metal"] = 1, ["deagle_impacts.metal_v2"] = 1, ["deagle_impacts.metal_exit"] = 0.8,
	["deagle_impacts.ricochet_metal"] = 0.7, ["deagle_impacts.ricochet_stone"] = 0.35, ["deagle_impacts.stone"] = 0.25 }
local IMPACT_LIGHT_HUE = { 1.0, 0.55, 0.25 }   -- hot steel sparks: yellow-orange
function Deagle:ImpactLight(name, pos, normal, scale)
	local L, k = self.cfg.impactLight, IMPACT_LIGHT[name]
	if not (L and L.on and k and pos) then return end
	local now = System.GetCurrTime()
	self.impactLights = self.impactLights or {}
	local pool, slot = self.impactLights, nil
	for i = 1, L.max do
		if not pool[i] or now >= pool[i].till then slot = i break end
	end
	if not slot then
		self.impactLightSkips = (self.impactLightSkips or 0) + 1
		if now >= (self.impactLightSaid or 0) then
			self.impactLightSaid = now + 2
			log(string.format("impact lights: all %d burning, %d hits got none so far", L.max, self.impactLightSkips))
		end
		return
	end
	local s = pool[slot] or { till = 0 }
	pool[slot] = s
	local e = s.id and System.GetEntity(s.id)
	if not e then
		local ok, ne = pcall(System.SpawnEntity, { class = "BasicEntity", name = "deagle_impact_light_" .. slot, position = pos,
			flags = ENTITY_FLAG_NO_SAVE })
		if not (ok and ne) then return end
		e = ne
		e:SetFlags(ENTITY_FLAG_NO_SAVE, 0)   -- never into a save
		s.id = e.id
	end
	local i = L.intensity * k * (scale or 1)
	local col = { x = i * IMPACT_LIGHT_HUE[1], y = i * IMPACT_LIGHT_HUE[2], z = i * IMPACT_LIGHT_HUE[3] }
	e:SetWorldPos(normal and vadd(pos, vscale(normal, L.off)) or pos)   -- off the surface, so it lights it
	local ok = pcall(e.LoadLight, e, 0, {
		style = 0, deferred_light = 1, radius = L.radius, diffuse_color = col, specular_color = col,
		hdrdyn = 0, lifetime = 0, realtime = 1, dot3 = 1, cast_shadow = 0, attenuation_bulbsize = L.bulb,
		volumetric_fog = false, volumetric_multiplier_wh = 0,
		corona_scale = 0, corona_dist_size_factor = 1, corona_dist_intensity_factor = 1 })
	if not ok then return end
	s.till = now + L.time
	local id = e.id
	Script.SetTimer(math.max(1, L.time * 1000), function()   -- game time; a light lit again meanwhile keeps burning
		if System.GetCurrTime() >= s.till - 0.001 then
			local le = System.GetEntity(id)
			if le then pcall(le.FreeSlot, le, 0) end
		end
	end)
end

function Deagle:FxTest(name)
	name = unquote(name)
	local dir = System.GetViewCameraDir()
	self:SpawnFx(name, vadd(System.GetViewCameraPos(), vscale(dir, 3)), dir, 1)
end
-- deagle_fxreload: the plugin re-reads the dev copy of the impacts library (build.sh --dev mirrors src/Libs/Particles) into
-- the live effects, so impact effects can be tuned without restarting the game. The engine swaps an effect's params
-- without a lock: never while impact emitters may still be alive (3 s after the last shot)
function Deagle:FxReload(lib)
	lib = (lib and lib ~= "") and lib or "deagle_impacts"
	if not self.native then log("$4fxreload needs the plugin") return end
	local since = System.GetCurrTime() - (self.state.lastShot or -100)
	if since < 3 then   -- effects of the last shot may still be alive: try again once they're gone (game time: paused waits)
		log(string.format("fxreload: in %.1f s (effects of the last shot may still be alive)", 3 - since))
		if not self.fxReloadQueued then
			self.fxReloadQueued = true
			Script.SetTimer(math.floor((3 - since) * 1000) + 100, function() self.fxReloadQueued = false; self:FxReload(lib) end)
		end
		return
	end
	self:Native("fxreload " .. self.devRoot .. "Libs/Particles/" .. lib .. ".xml " .. lib)
end
