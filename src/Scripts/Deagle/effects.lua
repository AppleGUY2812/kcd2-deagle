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
local LOOK_NEW = { ["deagle_impacts.water"] = "deagle_impacts.water_v3", ["deagle_impacts.metal"] = "deagle_impacts.metal_v2" }
function Deagle:SpawnFx(name, pos, normal, scale)
	if not name then return end
	if self.cfg.impactLook == "new" and LOOK_NEW[name] then name = LOOK_NEW[name] end
	if name == self.cfg.muzzleFx then scale = self.cfg.muzzleFxScale end
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
	local lib = string.match(name, "^([%w_]+)%.")
	if self.cfg.impactSpawn == "plugin" and self.native and PLUGIN_FX[lib] and pos and normal then
		-- fxs <effect> <scale> <box length m along the normal> <pos> <dir>: drawn from its first frame
		self:Native(string.format("fxs %s %.3f %.3f %.3f %.3f %.3f %.4f %.4f %.4f", name, scale or 1.0, 0.3,
			pos.x, pos.y, pos.z, normal.x, normal.y, normal.z))
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
