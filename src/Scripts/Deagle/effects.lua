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
