-- Muzzle flash (Claude 4): the particle flash, the flash cards on the gun, the flash light.
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U
local log, try, vadd, vdot, vnorm, vscale, vsub = U.log, U.try, U.vadd, U.vdot, U.vnorm, U.vscale, U.vsub

-- The flash rides the gun: the plugin spawns it the way Particle.SpawnEffect does (a free emitter) and moves it onto
-- the muzzle right after placing the gun in every camera step, keeping its bounding box valid (the engine's move
-- empties it; see native/src/main.cpp), so it sits on the muzzle in every frame drawn. Lua sends the muzzle (position + barrel direction in the viewmodel's axes) at the
-- shot and every frame while it burns. Checked in game 2026-10-12 (slow motion, scripted turns, recorded): effects on
-- entities (bone attachments, viewmodel slots, a helper entity) drew on some shots only, never on the Deagle, and
-- trailed by a frame. flashMode "world" leaves the flash where the muzzle was instead. No fallback: without the plugin
-- the flash is an error in the log. Smoke stays in the world.
local FLASH_FOLLOW = 0.3   -- s of game time the flash entity keeps following the muzzle (the longest flash part lives 0.11)

function Deagle:MuzzleFlash(dir)
	local c = self.cfg
	local pos = self:MuzzlePos()
	local phys = self:ShotPhysics()
	local scale = (phys and c.physics.flashPerUnit * phys.blast or 1) * c.muzzleFxScale
	-- A bigger blast burns a little longer (the engine can't stretch a flash per shot, so two versions of it). Neither
	-- can be shorter than ~0.08 s: a new emitter is first drawn a frame or two late (tools/check_fx.py).
	local fx = (phys and phys.blast >= c.physics.largeFlashBlast) and c.muzzleFxLarge or c.muzzleFx
	if c.flashMode == "world" then
		try("fx", Particle.SpawnEffect, fx, pos, dir, scale)
	elseif c.flashMode == "mesh" and self.vmInFp and self.vmShown and self:FlashMeshShot() then
		local vm = self.vmId and System.GetEntity(self.vmId)
		local okM, lp, ld = false, nil, nil
		if vm then okM, lp, ld = pcall(self.MuzzleLocal, self, vm) end
		if okM and c.flashMesh.light.mode == "ron" then self:FlashMeshLight(vm, lp, ld, fx == c.muzzleFxLarge)
		elseif okM then
			local light = (fx == c.muzzleFxLarge) and c.muzzleLightLarge or c.muzzleLight
			self:Native(string.format("flashfx %s %.4f %.4f %.4f %.4f %.4f %.4f %.4f", light, scale, lp.x, lp.y, lp.z, ld.x, ld.y, ld.z))
			self.flashUntil = System.GetCurrTime() + FLASH_FOLLOW
		end
	elseif c.flashMode == "live" and self.flashArmed and self.vmInFp and self.vmShown then
		-- the live flash (armed when the gun came out, FlashArm): seen in the frame it's fired, for 2 frames, 1 under
		-- 40 fps (Ready or Not: 0.035-0.04 s); its light from a short per-shot emitter on the muzzle
		local sizes = c.flashLive
		local n = (self.frameDt or 0.016) > 1 / 40 and 1 or #sizes
		self:Native("flashshot " .. table.concat(sizes, " ", 1, n))
		local vm = self.vmId and System.GetEntity(self.vmId)
		local okM, lp, ld = false, nil, nil
		if vm then okM, lp, ld = pcall(self.MuzzleLocal, self, vm) end
		if okM then
			local light = (fx == c.muzzleFxLarge) and c.muzzleLightLarge or c.muzzleLight
			self:Native(string.format("flashfx %s %.4f %.4f %.4f %.4f %.4f %.4f %.4f", light, scale, lp.x, lp.y, lp.z, ld.x, ld.y, ld.z))
			self.flashUntil = System.GetCurrTime() + FLASH_FOLLOW
		end
	elseif self.vmInFp and self.vmShown then
		local vm = self.vmId and System.GetEntity(self.vmId)
		local okM, lp, ld = false, "no viewmodel", nil
		if vm then okM, lp, ld = pcall(self.MuzzleLocal, self, vm) end
		if not self.native then
			if not self.flashNoNativeSaid then   -- once: every shot would flood the log
				self.flashNoNativeSaid = true
				log("$4muzzle flash: the native plugin isn't running (deagle_native.asi): no flash on the gun")
			end
		elseif not okM then
			log("$4muzzle flash: no muzzle on the gun: " .. tostring(lp))
		else
			self:Native(string.format("flashfx %s %.4f %.4f %.4f %.4f %.4f %.4f %.4f", fx, scale, lp.x, lp.y, lp.z, ld.x, ld.y, ld.z))
			self.flashUntil = System.GetCurrTime() + FLASH_FOLLOW
		end
	end
	try("fx", Particle.SpawnEffect, c.muzzleSmokeFx, pos, dir, scale / 0.6)
end

-- the muzzle in the viewmodel entity's own axes: where the muzzle bone is and which way the barrel points (the bone's
-- +Y: the exporters keep the muzzle bone rigid on the barrel with +Y down the bore). GetBoneLocal turns a world
-- direction into the bone's axes, so the bone's +Y in the world is the y of each world axis turned into it.
function Deagle:MuzzleLocal(vm)
	local bone = self.gun.bones.muzzle
	local p, o = vm:GetBonePos(bone), vm:GetWorldPos()
	local X, Y, Z = vm:GetDirectionVector(0), vm:GetDirectionVector(1), vm:GetDirectionVector(2)
	local bx = vm:GetBoneLocal(bone, { x = 1, y = 0, z = 0 })
	local by = vm:GetBoneLocal(bone, { x = 0, y = 1, z = 0 })
	local bz = vm:GetBoneLocal(bone, { x = 0, y = 0, z = 1 })
	local d, r = { x = bx.y, y = by.y, z = bz.y }, vsub(p, o)
	return { x = vdot(r, X), y = vdot(r, Y), z = vdot(r, Z) }, vnorm({ x = vdot(d, X), y = vdot(d, Y), z = vdot(d, Z) })
end

-- The live flash (flashMode "live"): once the gun is out and placed, the plugin keeps one never-stopping flash emitter
-- on its muzzle at a size of nothing (native/src/main.cpp "the live flash"); a shot scales it up for the frames it's
-- seen. Armed again for another gun or blast size; let go when the gun goes away.
function Deagle:FlashArm()
	local c = self.cfg
	if c.flashMode ~= "live" or not self.native or not (self.vmShown and self.vmInFp) then return end
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm or System.GetCurrTime() - (self.vmShownAt or 0) < 0.2 then return end   -- the plugin has placed the gun
	local phys = self:ShotPhysics()
	local scale = (phys and c.physics.flashPerUnit * phys.blast or 1) * c.muzzleFxScale
	local fx = (phys and phys.blast >= c.physics.largeFlashBlast) and c.muzzleFxLargeLive or c.muzzleFxLive
	local key = tostring(self.vmId) .. fx .. string.format("%.3f", scale)
	if self.flashArmed == key then return end
	local ok, lp, ld = pcall(self.MuzzleLocal, self, vm)
	if not ok then return end
	self:Native(string.format("flasharm %s %.4f %.4f %.4f %.4f %.4f %.4f %.4f", fx, scale, lp.x, lp.y, lp.z, ld.x, ld.y, ld.z))
	self.flashArmed = key
end

function Deagle:FlashDisarm()
	if self.flashArmed then self:Native("flashdisarm") end
	self.flashArmed = nil
end

-- The mesh flash: every card of both looks and both sizes is a skin attachment in the viewmodel's .cdf, built on that
-- gun's skeleton with every vertex on the muzzle bone (tools/flash_skin.sh, tools/flash_cdf.py), named after the card.
-- A .cdf's attachments show at load, so all are hidden when the viewmodel is made and at every look switch; a shot shows
-- one. As bone attachments (a model on the muzzle bone) every card drew the first card's model in KCD2, made at load or
-- at run time, even an apple put on one (in game 2026-10-14); the gun's skins (gun, magazines, rounds) draw their own.
-- the cards of the current look: ours (FLASH_SHAPES variants, each also turned FLASH_TURNS - 1 quarter turns round the
-- barrel: the roll) or Ready or Not's (flashRon: variants x stages); cdfMtl: the material the .cdf gives them
local FLASH_SHAPES, FLASH_TURNS = 4, 4
function Deagle:FlashLook()
	local c, r, phys = self.cfg.flashMesh, self.cfg.flashRon, self:ShotPhysics()
	local large = phys and phys.blast >= self.cfg.physics.largeFlashBlast
	if c.look == "ron" then
		return { look = "ron", set = large and r.large or r.small, variants = r.variants, stages = r.stages, time = r.time,
			mtl = r.mtl, cdfMtl = "deagle_vm_flash_ron" }
	end
	return { look = "ours", set = large and c.large or c.small, variants = c.roll and FLASH_SHAPES * FLASH_TURNS or FLASH_SHAPES,
		stages = 1, time = c.time, mtl = c.mtl, cdfMtl = "deagle_vm_flash" }
end
-- every card in the .cdf (both looks, both sizes): the attachments hidden at setup
function Deagle:FlashCardNames()
	local c, r, out = self.cfg.flashMesh, self.cfg.flashRon, {}
	for _, set in ipairs({ c.large, c.small }) do
		for v = 1, FLASH_SHAPES * FLASH_TURNS do out[#out + 1] = set .. "_" .. v end
	end
	for _, set in ipairs({ r.large, r.small }) do
		for v = 1, r.variants do
			for st = 1, r.stages do out[#out + 1] = set .. "_" .. v .. "_" .. st end
		end
	end
	return out
end
function Deagle:FlashMeshSetup(vm)
	local L = self:FlashLook()
	for _, name in ipairs(self:FlashCardNames()) do pcall(vm.HideAttachment, vm, 0, name, true, true) end
	local m = { names = {}, shown = {}, set = L.set, look = L }
	for v = 1, L.variants do
		for st = 1, L.stages do
			m.names[(v - 1) * L.stages + st] = L.stages > 1 and (L.set .. "_" .. v .. "_" .. st) or (L.set .. "_" .. v)
		end
	end
	self.flashMesh = m
	self:FlashMeshMaterial(vm)
end
-- another look (deagle_flash ron / ours): every card hidden, the new look's names taken; the .cdf's attachments stay
function Deagle:FlashMeshRebuild()
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm then return end
	self:FlashMeshSetup(vm)
end
-- another strength (flashMesh.mtl / flashRon.mtl: _dim, _hot, ...) on the current look's cards; only a card whose
-- material differs from it is changed (the .cdf's own otherwise: flashCardMtl is reset with every new viewmodel)
function Deagle:FlashMeshMaterial(vm)
	local m = self.flashMesh
	if not m or not vm then return end
	local L = self:FlashLook()
	self.flashCardMtl = self.flashCardMtl or {}
	for _, name in ipairs(m.names) do
		if (self.flashCardMtl[name] or L.cdfMtl) ~= L.mtl then
			pcall(vm.ChangeAttachmentMaterial, vm, name, "objects/manmade/weapons/deagle/vm/" .. L.mtl)
			self.flashCardMtl[name] = L.mtl
		end
	end
	m.mtl = L.mtl
end
function Deagle:FlashMeshShow(i, on)
	local vm = self.vmId and System.GetEntity(self.vmId)
	local m = self.flashMesh
	if not vm or not m or not m.names[i] then return end
	pcall(vm.HideAttachment, vm, 0, m.names[i], not on, true)
	m.shown[i] = on or nil
end
-- the card for game time e after the shot (nil once the flash is over): its variant's stage that e is in
local function flashCard(m, e)
	local L = m.look
	if e >= L.time then return nil end
	return (m.variant - 1) * L.stages + math.min(L.stages, math.floor(e / L.time * L.stages) + 1)
end
-- at the shot: a random variant, its first card seen in this frame; FlashMeshTick moves through its stages, then hides it
function Deagle:FlashMeshShot()
	local m = self.flashMesh
	if not m then return false end
	if m.mtl ~= self:FlashLook().mtl then self:FlashMeshMaterial(self.vmId and System.GetEntity(self.vmId)) end
	for i in pairs(m.shown) do self:FlashMeshShow(i, false) end
	m.variant = math.random(m.look.variants)
	m.on, m.frame, m.t0 = true, System.GetFrameID(), System.GetCurrTime()
	m.cur = flashCard(m, (self.frameDt or 0.016) / 2) or flashCard(m, 0)
	self:FlashMeshShow(m.cur, true)
	return true
end
-- every viewmodel frame after the shot's own: the flash lasts flashMesh.time (flashRon.time) of game time, so F9 slow motion stretches
-- it (it blinked by in 1-2 frames there: user 2026-10-14). A frame shows it while the frame's middle is still inside
-- that time (60 fps: 2 frames, under 40 fps: 1).
-- The shot's own frame is told by its engine frame id, not the viewmodel tick: a shot comes before its frame's tick,
-- which counted as the next frame and hid variant a before it was ever drawn (traced in game 2026-10-14)
function Deagle:FlashMeshTick()
	local m = self.flashMesh
	if not m or not m.on or System.GetFrameID() == m.frame then return end
	local want = flashCard(m, System.GetCurrTime() - m.t0 + (self.frameDt or 0.016) / 2)
	if want ~= m.cur then
		if m.cur then self:FlashMeshShow(m.cur, false) end
		if want then self:FlashMeshShow(want, true) end
		m.cur = want
	end
	if not want then m.on = nil end
end
-- Ready or Not's flash lights (flashMesh.light.mode "ron"), from its WeaponFlash_Pistol_V3 (the Deagle) and V2 (the
-- Glock) ParticleModuleLight: a main light on the burst 25 cm (V2: 10 cm) ahead of the muzzle for 0.04 s, and a second
-- one 12 (5) cm ahead for 0.035 s at 0.43 of its brightness (brightness 15-50 against 70-80); red, its hue 38250 : 450 :
-- 200. Entity lights in the viewmodel's slots: on the shot's own frame, for that game time (F9 stretches them), never
-- in the volumetric fog. Falloff: KCD2's lights fall off with the square of the distance from their bulb (light.bulb m)
-- and then fade linearly to nothing over the last 20 % of their radius: a radius that ends where the light still shows
-- draws a ring ("i can see the cutoff which shouldnt happen in real life", the user 2026-10-14; the particle light it
-- replaces ended at 1.5 m on the Glock, sized with its flash). So the radius is LIGHT_RADIUS, far past where the light
-- fades below sight (intensity 20, bulb 0.3: 1/10000 of it by 30 m), and only the square law shapes it.
local LIGHT_RADIUS = 40
local RON_LIGHTS = {
	{ slot = 3, at = 0.25, atSmall = 0.10, time = 0.04, share = 1 },
	{ slot = 4, at = 0.12, atSmall = 0.05, time = 0.035, share = 0.43 },
}
-- The colour is what the light adds on screen in Ready or Not, not its raw hue: its emitter colour (38250 : 450 : 200)
-- is red, and Ready or Not's tonemapper turns it red-orange on what it lights; KCD2's kept it red, the whole yard red
-- (the user's screenshot, 2026-10-14). Measured on the user's Ready or Not clip (frames 229 -> 231, linear light,
-- surfaces away from the fireball): added light 1 : 0.25-0.29 : 0.09-0.21.
local RON_LIGHT_HUE = { 1.0, 0.27, 0.15 }
function Deagle:FlashMeshLight(vm, lp, ld, large)
	local L = self.cfg.flashMesh.light
	local now = System.GetCurrTime()
	local phys = self:ShotPhysics()
	-- set on the Deagle; a smaller cartridge's flash lights by its blast (the muzzle gas: Glock 0.48 of the Deagle)
	local blast = phys and phys.blast / L.refBlast or 1
	self.flashLights = self.flashLights or {}
	for _, l in ipairs(RON_LIGHTS) do
		local i = L.intensity * blast * l.share
		local col = { x = i * RON_LIGHT_HUE[1], y = i * RON_LIGHT_HUE[2], z = i * RON_LIGHT_HUE[3] }
		local ok = pcall(vm.LoadLight, vm, l.slot, {
			style = 0, deferred_light = 1, radius = LIGHT_RADIUS, diffuse_color = col, specular_color = col,
			hdrdyn = 0, lifetime = 0, realtime = 1, dot3 = 1, cast_shadow = 0, attenuation_bulbsize = L.bulb,
			volumetric_fog = false, volumetric_multiplier_wh = 0,
			corona_scale = 0, corona_dist_size_factor = 1, corona_dist_intensity_factor = 1 })
		if ok then
			pcall(vm.SetSlotPos, vm, l.slot, vadd(lp, vscale(ld, large and l.at or l.atSmall)))
			self.flashLights[l.slot] = now + l.time
		end
	end
end
-- every viewmodel frame: a flash light whose game time is up goes
function Deagle:FlashLightTick()
	if not self.flashLights then return end
	local vm = self.vmId and System.GetEntity(self.vmId)
	local now = System.GetCurrTime()
	for slot, t in pairs(self.flashLights) do
		if now >= t or not vm then
			if vm then pcall(vm.FreeSlot, vm, slot) end
			self.flashLights[slot] = nil
		end
	end
end

-- every frame while a flash burns: the muzzle moves with the shot animation; afterwards the plugin lets go
function Deagle:FlashFollow()
	if not self.flashUntil then return end
	local vm = self.vmId and System.GetEntity(self.vmId)
	if System.GetCurrTime() >= self.flashUntil or not vm then
		self.flashUntil = nil
		self:Native("flashoff")
		return
	end
	local ok, lp, ld = pcall(self.MuzzleLocal, self, vm)
	if not ok then log("$4muzzle flash: lost the muzzle: " .. tostring(lp)) self.flashUntil = nil self:Native("flashoff") return end
	self:Native(string.format("flashpos %.4f %.4f %.4f %.4f %.4f %.4f", lp.x, lp.y, lp.z, ld.x, ld.y, ld.z))
end

-- deagle_flash ron|ours: the flash cards' look (flashMesh.look), on the cards (flashMode mesh)
function Deagle:FlashLookCmd(arg)
	local look = tostring(arg or ""):lower():find("ron") and "ron" or "ours"
	if self.cfg.flashMode ~= "mesh" then self:Set("flashMode mesh") end
	self:Set("flashMesh.look " .. look)
	self:Notify("muzzle flash: " .. (look == "ron" and "Ready or Not's" or "ours"))
end
