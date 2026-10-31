-- Viewmodel (Claude 4): the first-person arms + gun entity and its animation states, worn gear hiding, the HUD,
-- showing / hiding the gun.
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U
local DRAWING, GAME_MAPS_OFF, HOLDS_AIM, HOLDS_TOGGLE, INSPECTS, REST, SHOTS, log = U.DRAWING, U.GAME_MAPS_OFF, U.HOLDS_AIM, U.HOLDS_TOGGLE, U.INSPECTS, U.REST, U.SHOTS, U.log
local LEAN_MAPS_OFF = U.LEAN_MAPS_OFF
-- a Ready or Not gun's aimed shots have the hip shot's one cue (the gunshot at the trigger)
local CUES_OF = { shootAds = "shoot", shootEmptyAds = "shootEmpty" }
-- the poses a Ready or Not gun rests in while lowered (LowReadyTick): blended in and out over cfg.lowReady.blend
local LOWERED = { lowUp = true, lowDown = true, lowUpEmpty = true, lowDownEmpty = true, sprint = true }
local SPRINTS = { sprintStart = true, sprint = true, sprintStop = true }
local matToAngles, try, vadd, vcross, vlen, vnorm, vscale = U.matToAngles, U.try, U.vadd, U.vcross, U.vlen, U.vnorm, U.vscale

-- ---------------------------------------------------------------------------
-- viewmodel entity
-- ---------------------------------------------------------------------------
-- Older builds let the viewmodel (and test probes) into saves; on load they come back without their character, drawn
-- as the engine's default pyramid (the big white triangle). Anything of ours still in the world before the viewmodel
-- exists is one of those.
function Deagle:RemoveStaleEntities()
	if not System.GetEntitiesByClass then return end
	local n = 0
	for _, e in pairs(System.GetEntitiesByClass("BasicEntity") or {}) do
		local ok, name = pcall(e.GetName, e)
		if ok and type(name) == "string" and name:find("^deagle_") and not name:find("^deagle_case_") and e.id ~= self.vmId then
			pcall(System.RemoveEntity, e.id); n = n + 1
		end
	end
	if n > 0 then log("removed " .. n .. " stale entities from the save") end
end

-- every hide / show of the gun: the SPOM pieces (world entities, skin.lua) go with it
function Deagle:VmSetHidden(vm, h)
	pcall(vm.Hide, vm, h)
	for _, id in ipairs(self.rigidIds or {}) do local e = System.GetEntity(id) if e then pcall(e.Hide, e, h) end end
end

function Deagle:VmEntity()
	local vm = self.vmId and System.GetEntity(self.vmId)
	if vm then return vm end
	local c = self.cfg.vm
	self:RemoveStaleEntities()
	vm = System.SpawnEntity({ class = "BasicEntity", name = "deagle_viewmodel", position = System.GetViewCameraPos(),
		flags = ENTITY_FLAG_NO_SAVE, properties = { object_Model = "" } })
	if not vm then log("$4viewmodel spawn failed") return nil end
	pcall(vm.SetFlags, vm, ENTITY_FLAG_NO_SAVE, 0)
	local ok, slot = pcall(vm.LoadCharacter, vm, 0, c.cdf)
	log("viewmodel character " .. c.cdf .. " -> " .. tostring(ok) .. " " .. tostring(slot))
	if not ok or slot == nil or slot == false or slot == -1 then
		local ok2, slot2 = pcall(vm.LoadCharacter, vm, 0, c.chr)
		local ok3 = pcall(vm.CreateSkinAttachment, vm, 0, "vm_mesh", c.skin)
		log("viewmodel fallback chr " .. tostring(ok2) .. " " .. tostring(slot2) .. " skin " .. tostring(ok3))
	end
	pcall(vm.SetViewDistRatio, vm, 255)
	self.vmId, self.vmRoundsShown, self.flashCardMtl, self.flashLights = vm.id, nil, {}, nil
	self:FlashMeshSetup(vm)
	vm.OnUpdate = function() Deagle:VmPlace() end    -- per-frame follow (entity update); VmTick timer as well
	pcall(vm.Activate, vm, 1)
	return vm
end

-- the rounds in the gun's magazine: one attachment per modelled round (vm_round_1 = the top one), so it shows what it holds
function Deagle:VmRounds(n)
	local vm = self.vmId and System.GetEntity(self.vmId)
	local r = self.gun.rounds
	if not vm or not r or self.vmRoundsShown == n then return end
	self.vmRoundsShown = n
	if r == true then pcall(vm.HideAttachment, vm, 0, "vm_rounds", n <= 0, n <= 0) return end   -- one piece
	for i = 1, r do pcall(vm.HideAttachment, vm, 0, "vm_round_" .. i, i > n, i > n) end
end

-- A reload cancelled mid-swap: the spare is in the hand and the old mag is out, and a blend back to the rest pose would
-- fly both across the view. Both are hidden while the hands blend, then the gun's own mag shows again, seated.
function Deagle:VmMagsHidden(t)
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm or not self.gun.mags then return end
	local function show(on)
		pcall(vm.HideAttachment, vm, 0, "vm_mag2", not on, not on)
		local m1 = on and not self.state.magOut      -- a gun whose mag came out stays without one until the next reload
		pcall(vm.HideAttachment, vm, 0, "vm_mag1", not m1, not m1)
		self.vmRoundsShown = nil
		self:VmRounds(on and self:MagRounds() or 0)
	end
	show(false)
	self.magsToken = (self.magsToken or 0) + 1
	local tok = self.magsToken
	Script.SetTimer(math.floor((t + 0.05) * 1000), function() if Deagle.magsToken == tok then show(true) end end)
end

-- the gun's magazine is back (a reload after one was left out by a cancel)
function Deagle:VmMagIn()
	if not self.state.magOut then return end
	self.state.magOut = false
	local vm = self.vmId and System.GetEntity(self.vmId)
	if vm then pcall(vm.HideAttachment, vm, 0, "vm_mag1", false, false) end
end

-- rounds in the magazine: with any ammo, one of them is in the chamber
function Deagle:MagRounds()
	local a = self.state.ammo
	return a > 0 and a - 1 or 0
end

function Deagle:VmBone(name)
	local vm = self.vmShown and self.vmId and System.GetEntity(self.vmId)
	if not vm then return nil end
	local ok, p = pcall(vm.GetBonePos, vm, name)
	return ok and p or nil
end

-- rebuild the viewmodel entity from scratch
function Deagle:VmRespawn()
	if self.vmId then pcall(System.RemoveEntity, self.vmId) end
	self.vmId, self.vmInFp = nil, nil
	self:VmEntity()
	if self.vmShown then self.vmShown = false; self:VmShow() end
	log("viewmodel respawned")
end

-- arm look: "mw" (original sleeves + knuckle gloves) or "medieval" (KCD2 linen sleeves + old leather gloves,
-- tiled from the game's own Textures/cloth set)
function Deagle:VmApplyArms()
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm or not self.gun.armsLooks then return end
	local mtl = "objects/manmade/weapons/deagle/vm/" .. (self.cfg.vm.arms == "mw" and "deagle_vm" or "deagle_vm_medieval")
	local ok = pcall(vm.ChangeAttachmentMaterial, vm, "vm_mesh", mtl)
	log("arms look " .. self.cfg.vm.arms .. " (" .. tostring(ok) .. ")")
end

-- ---------------------------------------------------------------------------
-- Henry's body: hidden in first person while the gun is up (his shadow stays), with his worn gear; depth of field
-- off meanwhile (it focuses far and blurs the gun at 0.4 m). Dialogue / cutscene cameras get everything back.
-- ---------------------------------------------------------------------------
local GEAR_BONES = { "Hips", "Spine", "Spine1", "Spine2", "Spine3", "Spine4", "LeftUpLeg", "RightUpLeg" }

function Deagle:HideGear(hide)
	self.hiddenGear = self.hiddenGear or {}
	if not hide then
		for id in pairs(self.hiddenGear) do local e = System.GetEntity(id) if e then pcall(e.Hide, e, 0) end end
		self.hiddenGear = {}
		return
	end
	local p = self:Player()
	if not p then return end
	local pts = {}
	for _, b in ipairs(GEAR_BONES) do local ok, v = pcall(p.GetBonePos, p, b) if ok and v then pts[#pts + 1] = v end end
	local wp = p:GetWorldPos()
	for _, e in ipairs(System.GetEntitiesInSphere({ x = wp.x, y = wp.y, z = wp.z + 1.0 }, 1.2) or {}) do
		if e.class == "PickableItem" and not self.hiddenGear[e.id] then
			local ep = e:GetWorldPos()
			for _, v in ipairs(pts) do
				if vlen(vadd(ep, vscale(v, -1))) < self.cfg.vm.gearDist then
					pcall(e.Hide, e, 1); self.hiddenGear[e.id] = true
					log("hid worn item " .. tostring(e:GetName()))
					break
				end
			end
		end
	end
end

function Deagle:SetBodyHidden(hide)
	local p = self:Player()
	if p and self.bodyHidden ~= hide and (self.cfg.vm.hideBody or not hide) then
		pcall(p.HideAllAttachments, p, 0, hide, false)
		pcall(p.HideAttachmentMaster, p, 0, hide)
		self.bodyHidden = hide
		log("body " .. (hide and "hidden" or "shown"))
	end
	self:HideGear(hide and self.cfg.vm.hideBody)
	self.gearRescan = hide and System.GetCurrTime() + 1 or nil
	if hide then
		if self.savedDof == nil then
			local ok, v = pcall(System.GetCVar, "r_DepthOfField")
			self.savedDof = ok and v or 2
		end
		pcall(System.SetCVar, "r_DepthOfField", 0)
	elseif self.savedDof ~= nil then
		pcall(System.SetCVar, "r_DepthOfField", self.savedDof)
		self.savedDof = nil
	end
end

-- ---------------------------------------------------------------------------
-- per frame: first-person guard, placement on the camera (the plugin does it without lag when linked), HUD
-- ---------------------------------------------------------------------------
function Deagle:VmFollow()
	local p = self:Player()
	if not p or not self.vmShown then return end
	local wasFp = self.vmInFp
	local now = System.GetCurrTime()
	local cp, f = System.GetViewCameraPos(), System.GetViewCameraDir()
	local ok, hp = pcall(p.GetBonePos, p, "Head")
	local fp = ok and hp and vlen(vadd(cp, vscale(hp, -1))) < self.cfg.vm.fpMaxDist
	if fp ~= self.vmInFp then
		self.vmInFp = fp
		local vm = self.vmId and System.GetEntity(self.vmId)
		if vm then self:VmSetHidden(vm, fp and 0 or 1) end
		self:SetBodyHidden(fp and true or false)
		log("first person " .. tostring(fp))
	end
	-- worn gear: found when first person starts (SetBodyHidden) and once more a second later (items attached late after
	-- a load), not every second: each search is one 5-9 ms entity query (a stutter a second, Claude 3's audit 2026-10-15)
	if fp and self.gearRescan and now >= self.gearRescan then self.gearRescan = nil; self:HideGear(self.cfg.vm.hideBody) end
	-- the plugin places the viewmodel on the final camera; Lua only does it without the plugin (or the first frame)
	if self.vmInFp and not (self.native and wasFp) then
		local vm = self.vmId and System.GetEntity(self.vmId)
		if vm then
			local okU, up = pcall(System.GetViewCameraUpDir)
			if not okU or not up then up = { x = 0, y = 0, z = 1 } end
			local r = vnorm(vcross(f, up))   -- full camera orientation incl. the head-sway roll
			local u = vcross(r, f)
			local o = self.cfg.vm.offset
			vm:SetWorldPos(vadd(cp, vadd(vscale(f, o.fwd), vadd(vscale(r, o.right), vscale(u, o.up)))))
			vm:SetWorldAngles(matToAngles({ { r.x, f.x, u.x }, { r.y, f.y, u.y }, { r.z, f.z, u.z } }))
		end
	end
	if self.vmInFp ~= wasFp then self:NativeSyncVm() end
end

function Deagle:VmPlace()
	self:VmFollow()
	self:CamRecoilTick()
	local now = System.GetCurrTime()
	if now ~= self.hudT then self.hudT = now; self:DrawHud() end
end

-- While the gun is out the mod owns our keys: KCD2's own maps on them ("combat": left click punches;
-- "draw_holster_torch": R takes the torch out) are held off every frame. Switching them off once at H wasn't enough:
-- a gun drawn another way (gun switch, save load, script reload) skipped it, and the game turns its maps back on
-- after some state changes (R still took the torch out, user 2026-10-12).
-- KCD2's combat lock-on is off while the gun is out (wh_cs_PlayerLockDisabled: "Disables player locking functionality";
-- the camera turned to an opponent and held on him). The player's own value comes back at the holster.
function Deagle:SetLockOff(off)
	off = off and self.cfg.vm.noLockOn
	if off and self.savedLock == nil then
		local ok, v = pcall(System.GetCVar, "wh_cs_PlayerLockDisabled")
		self.savedLock = ok and tonumber(v) or 0
		pcall(System.SetCVar, "wh_cs_PlayerLockDisabled", 1)
	elseif not off and self.savedLock ~= nil then
		pcall(System.SetCVar, "wh_cs_PlayerLockDisabled", self.savedLock)
		self.savedLock = nil
	end
end

function Deagle:HoldGameMapsOff()
	if not self.state.drawn then return end
	self:SetLockOff(true)   -- a gun drawn another way (save load, gun switch) as well; a Lua check after the first
	for _, map in ipairs(GAME_MAPS_OFF) do ActionMapManager.EnableActionMap(map, false) end
	if self:LeanOn() then   -- Q leans (KCD2's chat); turned back on at the holster only if we turned it off
		self.leanMapsOff = true
		for _, map in ipairs(LEAN_MAPS_OFF) do ActionMapManager.EnableActionMap(map, false) end
	end
end

function Deagle:VmTick(token)
	if token ~= self.vmToken or not self.vmShown then return end
	local now = System.GetCurrTime()
	if self.vmTickT and now > self.vmTickT then self.frameDt = math.min(0.1, now - self.vmTickT) end   -- a round's first step
	self.vmTickT = now
	self.vmFrame = (self.vmFrame or 0) + 1
	self:VmPlace()
	self:FlashArm()
	self:FlashMeshTick()
	self:CueTick()
	self:ReloadKeyTick()
	self:LowReadyTick()
	self:LeanTick()
	self:EnvTick()
	self:CaseTick()
	self:BarrelHeatTick()
	self:FlashLightTick()
	if self.MotionTick then self:MotionTick() end
	if self.RonLayersTick then self:RonLayersTick() end
	self:HoldGameMapsOff()
	if (self.zoom ~= nil) ~= (self.vmZoomHidden == true) then     -- the gun out of view while the debug zoom is on
		local vm = self.vmId and System.GetEntity(self.vmId)
		if vm then self:VmSetHidden(vm, self.zoom and 1 or 0) end
		self.vmZoomHidden = self.zoom ~= nil
	end
	self:FlashFollow()
	if self.vmHolstering and self:HolsterTick() then
		self:HolsterDone(self.vmHolster.tok)
		if token ~= self.vmToken or not self.vmShown then return end   -- hidden (or drawn again: a new tick runs)
	end
	local a = self.vmAnim
	if a and not a.loop and System.GetCurrTime() >= a.ends and not self.vmHolstering then self:VmReturn() end
	Script.SetTimer(0, function() Deagle:VmTick(token) end)
end

local function hudText(x, y, text, size, c)
	return pcall(System.DrawText, x, y, text, Deagle.cfg.hud.font, size, 0, c[1], c[2], c[3], c[4])
end

function Deagle:HudActive()
	return self.cfg.hud.enabled and self.hudOk
end

function Deagle:DrawHud()
	local h, s = self.cfg.hud, self.state
	if not (h.enabled and self.hudOk ~= false and s.drawn and self.vmInFp) then return end
	local ok, vx, vy, vw, vh = pcall(System.GetViewport)
	local W = (ok and type(vw) == "number" and vw > 0) and vw or 1920
	local H = (ok and type(vh) == "number" and vh > 0) and vh or 1080
	local x, y = h.x * W, h.y * H
	local col = s.ammo == 0 and h.empty or h.col
	local good = hudText(x, y, tostring(s.ammo), h.size * 1.6, col)
	if not good then
		self.hudOk = false
		log("$4HUD: System.DrawText unavailable, falling back to info text")
		return
	end
	self.hudOk = true
	hudText(x + 0.028 * W, y + 0.012 * H, "/ " .. self.cfg.magSize, h.size * 0.9, h.col)
	local label = (s.reloading and not s.magSeated) and "RELOADING" or string.upper(self.gun.short or self.gun.title or "DESERT EAGLE")
	if #self:FireModes() > 1 then label = label .. "   " .. string.upper(self:FireMode()) end
	hudText(x - 0.004 * W, y + 0.05 * H, label, h.size * 0.55, { h.col[1], h.col[2], h.col[3], 0.7 })
end

-- ---------------------------------------------------------------------------
-- viewmodel animation
-- ---------------------------------------------------------------------------
-- rest loop: aiming wins (a reload that ends while aiming lands in ADS); slide-locked poses only when the mag is
-- empty and no reload is about to refill it. A Ready or Not gun aims procedurally (the plugin's ronvm moves the whole
-- gun to its sights): its rest clip stays idle.
function Deagle:VmRestKey()
	local s = self.state
	local empty = s.ammo == 0 and not s.reloading
	local a = self.cfg.vm.anims
	if self.vmSprint then return "sprint" end
	if self.vmLow then
		local k = self.vmLow == "up" and "lowUp" or "lowDown"
		return empty and a[k .. "Empty"] and k .. "Empty" or k
	end
	if s.aiming and not self.gun.ronStats then return empty and "adsEmpty" or "ads" end
	return empty and "idleEmpty" or "idle"
end

-- a one-shot clip is over: back to the rest loop, or into ADS if aim was held through it
-- Instant: every one-shot clip ends exactly on its rest pose (and the reload's seated spare mag swaps for the
-- identical main one without a blend that would fly them past each other). A queued draw / holster goes first.
function Deagle:VmReturn(trimmed)
	local s = self.state
	if self.pendingToggle then self.pendingToggle = false; self:Toggle()
	elseif s.aimHeld ~= s.aiming then self:UpdateAim()
	else
		-- a trimmed clip ends a few mm off its rest pose: a short blend, unless it ends with the spare mag seated (the
		-- rest pose has the main one there and a blend would fly both: switch, the image is the same to 3 mm)
		local cur = self.vmAnim
		local swap = cur and self.gun.swapTail and self.gun.swapTail[cur.key]
		local rest = self:VmRestKey()
		if rest == "sprint" and not (cur and cur.key == "sprintStart") and self.cfg.vm.anims.sprintStart then
			self:VmPlay("sprintStart", false, 1.0, self.cfg.lowReady.blend)   -- still sprinting: down again
		elseif LOWERED[rest] and not (cur and cur.key == "sprintStart") then
			self:VmPlay(rest, true, 1.0, self.cfg.lowReady.blend)
		else
			self:VmPlay(rest, true, 1.0, (trimmed and not swap) and self.cfg.vm.trimBlend or 0)
		end
	end
end

function Deagle:VmBusy()   -- a one-shot clip (shot, reload, inspect, draw, holster) still playing
	local a = self.vmAnim
	return a and not a.loop and System.GetCurrTime() < (a.ends or 0)
end

function Deagle:VmDrawing()   -- a draw until the gun is up (its ready time): no shots, reloads or inspects before
	local a = self.vmAnim
	return a and DRAWING[a.key] and System.GetCurrTime() < (a.ready or a.ends or 0) or false
end

function Deagle:VmBusyWith(keys)
	return self:VmBusy() and keys[self.vmAnim.key] or false
end

-- blend for a clip start when the caller doesn't set one
function Deagle:VmBlendFor(key)
	local c, cur = self.cfg.vm, self.vmAnim
	if not cur then return c.blend end
	if LOWERED[cur.key] and key ~= "sprintStop" then return self.cfg.lowReady.blend end
	if self:VmBusy() then
		if INSPECTS[cur.key] then return c.inspectCutBlend end
		if SHOTS[cur.key] and SHOTS[key] then return c.refireBlend end
		return c.cutBlend
	end
	if (cur.key == "ads" or cur.key == "adsEmpty") and not REST[key] then return c.cutBlend end
	if SHOTS[key] and REST[cur.key] then return 0 end   -- the kick lands on the next frame (idle = the shot's first frame)
	return c.blend
end

-- Plays a clip on the base layer. One-shot clips play their sound cues and return to the rest loop by themselves.
-- The engine refuses a start of the clip already on the layer, so replaying one (inspect -> inspect) goes through idle.
function Deagle:VmPlay(key, loop, speed, blend)
	if not REST[key] then self:VmClearRecoil() end   -- a full-body clip replaces a recoil still playing on top
	local vm, c = self:VmEntity(), self.cfg.vm
	if not vm then return 0 end
	local a = c.anims[key]
	if key == "shoot" then
		self.vmShot = (self.vmShot or 0) + 1
		a = a[(self.vmShot - 1) % #a + 1]
	end
	local name, frames = a[1], a[2]
	local plain = (speed or 1.0) == 1.0     -- measured times only hold at the clip's own pace (not a fast holster)
	speed = (speed or 1.0) * self:Rate(key)
	blend = blend or self:VmBlendFor(key)
	local cur = self.vmAnim
	-- the reload ends with the spare mag seated and the main one parked off screen: switch to idle (identical image)
	-- before any blend, or the blend would fly both magazines across the view
	local tail = cur and self.gun.swapTail and self.gun.swapTail[cur.key] and self.gun.swapTail[cur.key] / self:Rate(cur.key)
	local swapped = tail and System.GetCurrTime() >= (cur.full or cur.ends or 0) - tail
	if (cur and cur.name == name and not REST[key]) or (swapped and blend > 0 and key ~= "idle") then
		pcall(vm.StartAnimation, vm, 0, c.anims.idle[1], c.layer, 0, 1.0, true)
	end
	local ok, started = pcall(vm.StartAnimation, vm, 0, name, c.layer, blend, speed, loop and true or false)
	if not (ok and started) and self.vmLastFail ~= name and not (self.vmAnim and self.vmAnim.name == name) then
		self.vmLastFail = name
		log("$4viewmodel anim did not start: " .. name .. " " .. tostring(started))
	end
	local len = frames / (a[3] or c.fps) / speed
	-- the clip ends where its hands have settled (gun.times trim): the rest of it is them holding still
	local stop = math.min(not loop and plain and self:ClipTime(key, "trim") or len, len)
	local trimmed = stop < len - 0.01
	local now = System.GetCurrTime()
	self.vmAnimToken = (self.vmAnimToken or 0) + 1
	local tok = self.vmAnimToken
	self.vmAnim = { key = key, name = name, loop = loop, ends = now + stop, full = now + len,
		ready = now + math.min(plain and self:ClipTime(key, "ready") or stop, stop) }
	if not loop then
		self:PlayCues(CUES_OF[key] or key, blend, speed)
		if HOLDS_AIM[key] and key ~= "reload" and key ~= "reloadSpeed" and stop > c.adsLead then   -- reloads: at their end (mag swap)
			Script.SetTimer(math.floor((stop - c.adsLead) * 1000), function()
				if Deagle.vmAnimToken == tok and Deagle.vmShown and Deagle.state.aimHeld then Deagle:UpdateAim(c.adsLead) end
			end)
		end
		Script.SetTimer(math.max(1, math.floor(stop * 1000) - 30), function()
			if Deagle.vmAnimToken == tok and Deagle.vmShown and not Deagle.vmHolstering then Deagle:VmReturn(trimmed) end
		end)
	end
	return len
end

-- A finished non-looping clip on the recoil layer holds its last frame. The empty-mag recoil ends with the slide
-- locked back, so that offset rode on top of every later clip (reload, idle) until the layer was cleared.
function Deagle:VmClearRecoil()
	self.vmRecoilToken = (self.vmRecoilToken or 0) + 1
	local vm = self.vmId and System.GetEntity(self.vmId)
	if vm then pcall(vm.StopAnimation, vm, 0, self.cfg.vm.recoilLayer) end
end

-- aimed shot: additive recoil on its own layer over the ADS pose, cleared when it ends
function Deagle:VmRecoilAdd(empty)
	local vm, c = self:VmEntity(), self.cfg.vm
	if not vm then return end
	local list = c.anims.shootAdd
	local a = empty and c.anims.shootEmptyAdd or list[((self.vmShot or 0) % #list) + 1]
	local r = self:Rate(empty and "shootEmptyAdd" or "shootAdd")
	local len = a[2] / (a[3] or c.fps) / r
	self.vmShot = (self.vmShot or 0) + 1
	self.vmRecoilToken = (self.vmRecoilToken or 0) + 1
	local tok = self.vmRecoilToken
	local now = System.GetCurrTime()
	local rapid = now < (self.vmRecoilEnds or 0)       -- the last kick is still on: crossfade instead of snapping to zero
	self.vmRecoilEnds = now + len
	pcall(vm.StartAnimation, vm, 0, a[1], c.recoilLayer, rapid and 0.04 or 0.0, r, false)
	self:PlayCues(empty and "shootEmpty" or "shoot")
	Script.SetTimer(math.floor(len * 1000), function()
		if Deagle.vmRecoilToken ~= tok then return end
		-- same frame: rest loop in (adsEmpty after the last round keeps the slide locked), recoil offset out
		if Deagle.vmShown and not Deagle.vmHolstering and not Deagle:VmBusy() then Deagle:VmPlay(Deagle:VmRestKey(), true, 1.0, 0) end
		Deagle:VmClearRecoil()
	end)
end

-- an empty gun's trigger pull: its dryFire clip (Ready or Not's fiveseven dryfire over the locked-slide idle, baked
-- whole: it starts and ends on idleEmpty), played like a shot; not over a reload, draw, holster or inspect
function Deagle:VmDryFire()
	if not (self.cfg.vm.anims.dryFire and self.vmShown) or self.vmHolstering then return end
	if self:VmBusy() and not SHOTS[self.vmAnim.key] and self.vmAnim.key ~= "dryFire" then return end
	self:VmPlay("dryFire", false)
end

-- ---------------------------------------------------------------------------
-- Ready or Not's low ready (cfg.lowReady): against walls and while sprinting
-- ---------------------------------------------------------------------------
-- the gun's length as Ready or Not measures it: its root bone to its muzzle + 5 cm (no attachments here); per viewmodel
function Deagle:GunLength()
	local g = self.gun
	if self.gunLen and self.gunLen.vm == self.vmId and self.gunLen.gun == g.id then return self.gunLen.len end
	local root, muzzle = g.bones.root and self:VmBone(g.bones.root), self:VmBone(g.bones.muzzle)
	if not (root and muzzle) then return nil end
	local len = vlen({ x = muzzle.x - root.x, y = muzzle.y - root.y, z = muzzle.z - root.z }) + 0.05
	self.gunLen = { vm = self.vmId, gun = g.id, len = len }
	return len
end

-- how far ahead of the eye (from start m in front of it) the first solid thing is, up to far m: a ring of rays of radius
-- r round the view line and its centre (a stand-in for Ready or Not's sphere sweep), past foliage and invisible blockers
-- like a round (FlightHit). math.huge when nothing.
local RING = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }
function Deagle:WallDistance(far, r, stopAt)
	local pos, f, right, up = U.camBasis()
	local L = self.cfg.lowReady
	local move = vscale(f, far)
	local best = math.huge
	for _, o in ipairs(RING) do
		local from = vadd(vadd(pos, vscale(f, L.start)), vadd(vscale(right, o[1] * r), vscale(up, o[2] * r)))
		local hit = self:FlightHit(from, move)
		if hit and hit.dist < best then best = hit.dist end
		if best <= stopAt then break end
	end
	return best
end

-- every viewmodel frame on a Ready or Not gun: is it lowered (a wall: "up" / "down"; a sprint), and how near the wall
-- (the plugin's pushback). A change plays the new pose now if the base layer rests; a clip running (reload, shot,
-- inspect, draw) finishes first and VmReturn lands in it.
function Deagle:LowReadyTick()
	local g = self.gun
	if not g.ronStats or not self.vmShown then return end
	local L, a, s = self.cfg.lowReady, self.cfg.vm.anims, self.state
	-- a gun with its own sprint clips has no procedural sprint pose (mparams sprint: a plugin with lowready; sent from
	-- here, since its capabilities arrive after MotionParams at a hello)
	-- and its recoil / offset numbers (the wall pushback needs them before the first shot, recoil.lua RonParams)
	if self.native and self.nativeCaps and self.nativeCaps.ronvm and self.ronParamsFor ~= g.id then self:RonParams(g) end
	local sp = a.sprint and 0 or 1
	if self.native and self.nativeCaps and self.nativeCaps.lowready and self.sprintSent ~= sp then
		self.sprintSent = sp
		self:Native("mparams sprint " .. sp)
	end
	local p = self:Player()
	if not p then return end
	local ok, v = pcall(p.GetVelocity, p)
	local speed = ok and type(v) == "table" and not self:Flying() and math.sqrt((v.x or 0) ^ 2 + (v.y or 0) ^ 2) or 0
	local crouched = self.motionIn and self.motionIn.crouched
	local sprint = a.sprint ~= nil and speed > (self.vmSprint and L.sprintOut or L.sprintIn) and not s.aimHeld and not crouched
	local low = nil
	local push = self.native and self.nativeCaps and self.nativeCaps.lowready
	local len = (a.lowUp or a.lowDown or push) and self:GunLength()   -- no poses and no pushback: no rays
	if len then
		local block, pushAt = len + L.rangeOffset, len + L.pushRange
		local d = self:WallDistance(math.max(block, pushAt), self.vmLow and L.radiusLow or L.radius, -1)
		if d <= block and (a.lowUp or a.lowDown) then
			local f = System.GetViewCameraDir()
			low = math.deg(math.asin(math.max(-1, math.min(1, f.z)))) > L.pitch and "up" or "down"
			if not a[low == "up" and "lowUp" or "lowDown"] then low = low == "up" and "down" or "up" end   -- the one it has
		end
		local k = math.max(0, math.min(1, (pushAt - d) / pushAt))
		if push and (self.wallSent == nil or math.abs(k - self.wallSent) > 0.01 or (k == 0 and self.wallSent ~= 0)) then
			self.wallSent = k
			self:Native(string.format("ronp wall %.3f", k))
		end
	end
	if sprint then low = nil end
	local was = self.vmSprint
	if sprint == (was or false) and low == self.vmLow then return end
	local lowWas = self.vmLow
	self.vmSprint, self.vmLow = sprint or nil, low
	if (low == nil) ~= (lowWas == nil) and s.aimHeld then self:UpdateAim() end   -- down at a wall: out of the aim; up: back in
	if self.vmHolstering or (self:VmBusy() and not SPRINTS[self.vmAnim.key]) then return end
	if sprint and not was then
		self:VmPlay(a.sprintStart and "sprintStart" or "sprint", not a.sprintStart, 1.0, self:VmBlendFor("sprintStart"))
	elseif was and not sprint and a.sprintStop then
		self:VmPlay("sprintStop", false)
	else
		self:VmPlay(self:VmRestKey(), true, 1.0, L.blend)
	end
end

-- ---------------------------------------------------------------------------
-- draw / holster
-- ---------------------------------------------------------------------------
-- frames ahead of the holster's end the gun hides: 1 if the playhead Lua reads is the one this frame renders, 2 if
-- the animation updates after the script timers (the frame renders one step further)
local HOLSTER_AHEAD = 2
-- The draw starts on its own first frame (blend 0): the gun was hidden (or is a new entity), and a blend began from the
-- layer's last pose, the rest pose (the idle gun) for its first frames (the user, 2026-10-19).
function Deagle:VmShow()
	self.vmShownAt = System.GetCurrTime()
	self.vmHolstering, self.vmHolster = false, nil
	self.vmSprint, self.vmLow = nil, nil
	local vm = self:VmEntity()
	if not vm then return end
	self.vmShown = true
	self:VmPlace()
	-- the first draw after a load is the gun's drawFirst if it has one (Ready or Not's draw_first_b: the chamber check)
	local first = not self.drawnOnce and self.cfg.vm.anims.drawFirst and self.state.ammo > 0
	self.drawnOnce = true
	self:VmPlay(self.state.ammo == 0 and "drawEmpty" or first and "drawFirst" or "draw", false, 1.0, 0)
	self:VmSetHidden(vm, 0)
	self.vmRoundsShown = nil
	self:VmRounds(self:MagRounds())
	if self.state.magOut and self.gun.mags then pcall(vm.HideAttachment, vm, 0, "vm_mag1", true, true) end
	self.vmToken = (self.vmToken or 0) + 1
	local token = self.vmToken
	Script.SetTimer(0, function() Deagle:VmTick(token) end)
	pcall(vm.SetFlags, vm, ENTITY_FLAG_CASTSHADOW, 2)   -- no floating-arm shadow; Henry's shadow stays
	self:SetBodyHidden(true)
	self:NativeSyncVm()
	local key = tostring(self.vmId) .. self.cfg.vm.arms
	if self.vmArmsApplied ~= key then
		self.vmArmsApplied = key
		self:VmApplyArms()
	end
end

function Deagle:VmHide()
	self.vmShown, self.vmZoomHidden = false, false
	self.vmSprint, self.vmLow = nil, nil
	if (self.wallSent or 0) ~= 0 then self.wallSent = 0; if self.native then self:Native("ronp wall 0") end end
	self:LeanStop()
	if self.LayersReset then self:LayersReset() end
	self:FlashDisarm()
	self:BarrelSmokeStop()
	self.vmToken = (self.vmToken or 0) + 1
	local vm = self.vmId and System.GetEntity(self.vmId)
	if vm then self:VmSetHidden(vm, 1) end
	self:SetBodyHidden(false)
end

function Deagle:ShowInHand()
	self:VmShow()
end

-- holster: play the reversed draw, then hide on its last frame. A finished clip on the base layer drops to the rest pose
-- (the idle gun), and a timer missed that frame: timers run on game time while the clip runs on its own clock (F9 slow
-- motion runs it ~1.5x faster), and frames / fps is one frame past its last key. The idle gun showed for a frame before
-- the hide (the user, 2026-10-19). So VmTick hides it by the clip's own playhead (HolsterTick); the timer only ends a
-- holster whose viewmodel stopped ticking.
function Deagle:HideInHand()
	if self.vmShown and not self.vmHolstering then
		self.vmHolstering = true
		local key = self.state.ammo == 0 and "holsterEmpty" or "holster"
		local blend = self:VmBlendFor(key)
		local len = self:VmPlay(key, false, self.cfg.vm.holsterSpeed, blend)
		local a, speed = self.cfg.vm.anims[key], self.cfg.vm.holsterSpeed * self:Rate(key)
		self.vmHolsterTok = (self.vmHolsterTok or 0) + 1
		local tok = self.vmHolsterTok
		self.vmHolster = { tok = tok, key = key, from = System.GetCurrTime(), blend = blend,
			span = math.max(1, a[2] - 1) / (a[3] or self.cfg.vm.fps) / speed }   -- s from its first key to its last
		Script.SetTimer(math.floor(len * 1000), function() Deagle:HolsterDone(tok) end)
		return
	end
	self.vmHolstering, self.vmHolster = false, nil
	self:VmHide()
	self:NativeSyncVm()
end

function Deagle:HolsterDone(tok)
	if not (self.vmHolstering and self.vmHolster and self.vmHolster.tok == tok) then return end
	self.vmHolstering, self.vmHolster = false, nil
	if not self.state.drawn then self:VmHide(); self:NativeSyncVm() end
	if self.pendingToggle then self.pendingToggle = false; self:Toggle() end   -- drawn again meanwhile
end

-- every viewmodel frame while holstering: true once the clip's playhead (GetAnimationTime, 0..1) would pass its end by
-- the next frame. One frame of the clip = the last frame's playhead step (its own clock), at least the game-time one.
function Deagle:HolsterTick()
	local h, a = self.vmHolster, self.vmAnim
	if not (h and self.vmHolstering and a and a.key == h.key) then return false end
	if System.GetCurrTime() - h.from < h.blend then return false end   -- the layer may still report the clip it leaves
	local vm = self.vmId and System.GetEntity(self.vmId)
	local ok, t = false, nil
	if vm then ok, t = pcall(vm.GetAnimationTime, vm, 0, self.cfg.vm.layer) end
	if not ok or type(t) ~= "number" then return h.t ~= nil end      -- read before, gone now: the clip is over
	if h.t and t < h.t then return true end                           -- back to the start: the clip is over
	local step = (self.frameDt or 1 / 60) / h.span
	if h.t then step = math.max(step, t - h.t) end
	h.t = t
	return t + HOLSTER_AHEAD * step >= 1
end

-- Draw / holster. Asked for during a draw, holster or reload, it waits for that to finish (no mid-way jumps).
function Deagle:Toggle()
	local s = self.state
	if self.vmShown and (self.vmHolstering or self:VmBusyWith(HOLDS_TOGGLE)) then
		self.pendingToggle = not self.pendingToggle
		return
	end
	if not s.drawn and not self:Owns() then
		self:Notify("You don't have a Desert Eagle")
		return
	end
	s.drawn = not s.drawn
	if s.drawn then
		local h = self:Player().human
		try("holster weapon", function() if h:IsWeaponDrawn() then h:HolsterWeapon() end end)
		self:ShowInHand()
	else
		self:HideInHand()
	end
	-- native maps on our keys stay off while the pistol is out: "combat" turns left click into a fist attack,
	-- "draw_holster_torch" takes the torch out on R (reload)
	for _, map in ipairs(GAME_MAPS_OFF) do try(map .. " map", ActionMapManager.EnableActionMap, map, not s.drawn) end
	if s.drawn and self:LeanOn() or not s.drawn and self.leanMapsOff then   -- Q / E lean: KCD2's chat maps, E's use
		self.leanMapsOff = s.drawn or nil
		for _, map in ipairs(LEAN_MAPS_OFF) do try(map .. " map", ActionMapManager.EnableActionMap, map, not s.drawn) end
	end
	if self:LeanOn() or not s.drawn then self:Native("nointeract " .. (s.drawn and "1" or "0")) end
	-- and R: the game's own no_torch filter while the gun is out (plugin; returning true from OnAction for
	-- toggle_torch didn't stop it: the game takes the torch out elsewhere too, in game 2026-10-12)
	self:Native("notorch " .. (s.drawn and "1" or "0"))
	self:SetLockOff(s.drawn)
	if not s.drawn then self:SetAim(false) end
	log(s.drawn and "drawn" or "holstered")
	self:ShowAmmo()
end
