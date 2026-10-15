-- The gun (Claude 4): Shoot / Fire, ammo, reloads, aiming, inspect, fire modes, CS2 accuracy.
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U
local HOLDS_AIM, SHOTS, camBasis, coneDir, csDir, log, try, unquote = U.HOLDS_AIM, U.SHOTS, U.camBasis, U.coneDir, U.csDir, U.log, U.try, U.unquote
local vadd, vnorm, vscale = U.vadd, U.vnorm, U.vscale

-- one shot: ray, effects, ammo. Returns true when a round was fired.
function Deagle:Shoot(force)
	local s, c = self.state, self.cfg
	local p = self:Player()
	if not p or s.reloading then return false end
	if not self:Owns() then
		s.drawn = false
		self:SetAim(false)
		self:HideInHand()
		return false
	end
	local now = System.GetCurrTime()
	if not force and now - s.lastShot < c.interval - 0.005 then return false end
	if s.ammo <= 0 then
		local ron = self:RonSounds()
		if ron and ron.dry then self:RonHere(ron.dry, self:DryGain(ron.dry)) else self:PlayTrigger(c.dryTrigger) end
		self:ShowAmmo()
		return false
	end
	s.heat = math.max(0, (s.heat or 0) - (now - s.lastShot) * c.heatDecay)
	s.lastShot = now
	s.ammo = s.ammo - 1

	local pos = System.GetViewCameraPos()
	local w = self.gun.cs2 and self.cs2 and self.cs2[self.gun.cs2]
	local dir
	if w and w.inStand then                     -- CS2's accuracy, the gun's own numbers
		local inacc, spread = self:CsAccuracy(w)
		dir = csDir(System.GetViewCameraDir(), inacc, spread)
		s.accFire = s.accFire + w.inFire
	else
		dir = coneDir(System.GetViewCameraDir(), (s.aiming and c.spreadAim or c.spreadHip) + s.heat)
	end
	-- the bullet leaves along the barrel, which may still be kicked up / sideways from the last shot
	local kp, ky = self:GunKickNow()
	if kp ~= 0 or ky ~= 0 then
		local _, _, right, upv = camBasis()
		local k = c.physics.barrelAim
		dir = vnorm(vadd(dir, vadd(vscale(upv, math.rad(kp) * k), vscale(right, math.rad(ky) * k))))
	end
	s.heat = s.heat + c.heatPerShot

	if c.gunFx then self:PlayTrigger(c.shotTrigger) end
	self:ShotHeard(self:ShotPhysics())
	if c.gunFx then self:MuzzleFlash(System.GetViewCameraDir()) end
	self:Recoil()
	if c.gunFx then self:EjectCase() end

	local phys = self:ShotPhysics()
	if phys then                    -- a round that flies (speed, drop, drag) and its tracer
		self:LaunchRound(pos, dir, phys, c.gunFx and self:Tracer(pos, dir, phys) or nil)
	else                            -- a gun without cartridge data: an instant ray with its CS2 numbers
		local hits = {}
		local ok, n = pcall(Physics.RayWorldIntersection, pos, vscale(dir, c.range), 1, ent_all, p.id, nil, hits)
		if ok and n and n > 0 and hits[1] then
			self:ApplyHit(hits[1], dir)
		elseif not ok then
			log("$4raycast failed: " .. tostring(n))
		end
	end
	if s.ammo == 0 then self:ShowAmmo() end
	return true
end

-- hip shots play the full shoot clip; aimed shots keep the ADS pose and add the recoil on top
function Deagle:Fire(force, burstRound)
	local s = self.state
	if self:VmDrawing() or self.vmHolstering then return end
	if s.reloading and (burstRound or 1) > 1 then return end   -- a burst's later rounds never cancel a reload
	if s.reloading then
		-- the new mag is in: the shot goes off the moment the gun is back up
		if s.magSeated then s.fireQueued = true return end
		-- before that, staged like Insurgency: Sandstorm: while the old mag is still in the gun a shot cancels the
		-- reload and the rounds stay; once it's out, the shot is the round in the chamber and the gun has no mag until
		-- the next reload. An empty gun has nothing to fire: the reload goes on.
		if s.ammo <= 0 then return end
		local now = System.GetCurrTime()
		local spareInView = now >= (s.reloadCancel or 0)    -- the spare is in the hand: the mags would fly in a blend
		if spareInView and not self.gun.mags then return end -- (no mag pieces to hide: only before that)
		local out = now >= (s.reloadOut or math.huge)
		self:CancelReload()
		if out and not self.cfg.cancelKeepsRounds then
			s.ammo, s.magOut = math.min(s.ammo, 1), true
		end
		if spareInView then self:VmMagsHidden(self.cfg.vm.cutBlend) end
	end
	local aimed, wasLast = s.aiming, s.ammo == 1
	if not self:Shoot(force) then return end
	self:VmRounds(self:MagRounds())          -- the next round went into the chamber
	local anims = self.cfg.vm.anims
	if burstRound and anims.shootBurst and not aimed then
		-- the burst clip shows every round: the first round starts it, the others only fire; a round that empties
		-- the gun cuts it with the last shot (slide locks back)
		if wasLast then self:VmPlay("shootEmpty", false)                 -- its cue is the gunshot
		else
			-- the burst clip's slide cycles are gun.burstGap apart: play it at the speed that lands them on the rounds
			if burstRound == 1 then self:VmPlay("shootBurst", false, (self.gun.burstGap or self.cfg.burstShotGap) / self.cfg.burstShotGap) end
			self:PlayCues("shoot")                                       -- every round's gunshot (the clip has none)
		end
	elseif aimed or (self:FireMode() ~= "semi" and anims.shootAdd) then
		-- rapid fire stacks recoil on the rest pose; a clip still playing (inspect, a reload's settle) is cut first
		if self:VmBusy() and not SHOTS[self.vmAnim.key] then self:VmPlay(self:VmRestKey(), true, 1.0) end
		self:VmRecoilAdd(wasLast)
	else
		self:VmPlay(wasLast and "shootEmpty" or "shoot", false)
	end
end

-- ---------------------------------------------------------------------------
-- inventory, ammo, reload, aim
-- ---------------------------------------------------------------------------
function Deagle:CountOf(id)
	local p = self:Player()
	local ok, n = pcall(function() return p.inventory:GetCountOfClass(id) end)
	return ok and (n or 0) or 0
end

function Deagle:Owns()
	return self:CountOf(self.cfg.itemId) > 0
end

-- SendInfoText queues messages, so clear the current one first and only call this on state changes.
function Deagle:Notify(text)
	if self:HudActive() and type(text) == "string" and text:find("^Desert Eagle  reloading") then return end
	pcall(Game.HideInfoText)
	try("info text", Game.SendInfoText, text, false)
end

function Deagle:ShowAmmo()
	local s = self.state
	if self:HudActive() then pcall(Game.HideInfoText) return end   -- the HUD shows it
	if not s.drawn then
		self:Notify("Desert Eagle  holstered")
		return
	end
	local spare = self.cfg.useAmmoItems and string.format("   (%d spare)", self:CountOf(self.cfg.ammoId)) or ""
	local tail = s.ammo == 0 and "   EMPTY - press R" or ""
	self:Notify(string.format("Desert Eagle  %d / %d%s%s", s.ammo, self.cfg.magSize, spare, tail))
end

function Deagle:Give(line)
	local p = self:Player()
	local rounds = tonumber(unquote(line)) or (self.cfg.useAmmoItems and 49 or 0)
	if not self:Owns() then try("give pistol", ItemUtils.CreateInvItem, p, self.cfg.itemId, 1) end
	if rounds > 0 then try("give ammo", ItemUtils.CreateInvItem, p, self.cfg.ammoId, rounds) end
	log("give: pistol " .. self:CountOf(self.cfg.itemId) .. ", cartridges " .. self:CountOf(self.cfg.ammoId))
end

-- A reload's clip says when its work is done (gun.times, tools/clip_times.py): the new mag seats (the ammo goes in),
-- then the gun comes back up (ready: the reload is over and a shot cuts the rest of the clip). Until the old mag leaves
-- the gun, a shot cancels the reload: the rounds you had stay, no new ones.
function Deagle:Reload()
	local s, c = self.state, self.cfg
	-- a round in the chamber stays there: a fresh mag on top of it makes magSize + 1 (Deagle 7+1, Glock 17+1)
	local capacity = c.magSize + (s.ammo > 0 and 1 or 0)
	if s.reloading or s.ammo >= capacity or self:VmDrawing() or self.vmHolstering then return end
	if c.useAmmoItems and self:CountOf(c.ammoId) <= 0 then
		self:Notify("No .50 AE cartridges")
		return
	end
	local key = s.ammo == 0 and "reloadEmpty" or "reload"
	local a = c.vm.anims[key]
	local len = a[2] / (a[3] or c.vm.fps) / self:Rate(key)
	local seat = math.min(self:ClipTime(key, "seat") or len, len)
	local ready = math.max(seat, math.min(self:ClipTime(key, "ready") or len, len))
	local now = System.GetCurrTime()
	s.reloading, s.magSeated = true, false
	s.reloadEnd = now + ready
	s.reloadCancel = now + (self:ClipTime(key, "cancel") or 0)
	s.reloadOut = now + (self:ClipTime(key, "out") or self:ClipTime(key, "cancel") or 0)
	s.reloadTarget = capacity
	local this = s.reloadEnd
	local function mine() return Deagle.state.reloading and Deagle.state.reloadEnd == this end
	self:Notify("Desert Eagle  reloading...")
	self:PlayTrigger(c.reloadTrigger)
	Script.SetTimer(math.floor(seat * 1000), function() if mine() then Deagle:SeatMag() end end)
	Script.SetTimer(math.floor(ready * 1000) + 20, function() self:FinishReload() end)
	self:VmPlay(key, false)
	self:UpdateAim()
	if key == "reloadEmpty" and self.gun.newMagAt then
		Script.SetTimer(math.floor(self.gun.newMagAt / self:Rate(key) * 1000), function()
			if mine() then Deagle:VmMagIn(); Deagle:VmRounds(Deagle.cfg.magSize) end   -- a full magazine in the hand
		end)
	end
end

-- the new magazine is in: its rounds count (taken from the inventory with useAmmoItems)
function Deagle:SeatMag()
	local s, c = self.state, self.cfg
	if s.magSeated then return end
	s.magSeated = true
	self:VmMagIn()
	local take = (s.reloadTarget or c.magSize) - s.ammo
	if c.useAmmoItems then
		take = math.min(take, self:CountOf(c.ammoId))
		if take > 0 then
			try("use ammo", function() self:Player().inventory:DeleteItemOfClass(c.ammoId, take) end)
		end
	end
	s.ammo = s.ammo + take
	self:VmRounds(self:MagRounds())
end

-- Called by the timer and polled from input, so a reload never gets stuck if timers are cleared.
function Deagle:FinishReload()
	local s = self.state
	if not s.reloading then return end
	if System.GetCurrTime() < (s.reloadEnd or 0) then return end
	self:SeatMag()
	s.reloading = false
	self:ShowAmmo()
	if s.fireQueued then                   -- pressed while the new mag went in and the gun came up
		s.fireQueued = false
		if self:FireMode() ~= "auto" or s.trigger then self:TriggerPress() end
	end
end

-- a shot while the old magazine is still in: no reload, the rounds in it stay
function Deagle:CancelReload()
	local s = self.state
	s.reloading, s.reloadEnd, s.fireQueued = false, nil, false
	self:VmRounds(self:MagRounds())
	self:ShowAmmo()
end

-- Aim input. Aiming (zoom + ADS pose) follows the button, except while a reload / draw / holster runs: those finish
-- first and hand over to ADS in their last moments. Hip shots and inspect are cut by it.
function Deagle:SetAim(on)
	self.state.aimHeld = on and self.cfg.vm.ads and self.cfg.vm.anims.ads ~= nil or false
	self:UpdateAim()
end

function Deagle:AimBlocked()
	if self.vmHolstering or not self.state.drawn then return true end
	local a = self.vmAnim
	return a and not a.loop and HOLDS_AIM[a.key] and System.GetCurrTime() < (a.ends or 0) - self.cfg.vm.adsLead or false
end

function Deagle:UpdateAim(blend)
	local s = self.state
	local blocked = self:AimBlocked()
	local want = (s.aimHeld and not blocked) and true or false
	if want ~= s.aiming then
		s.aiming = want
		self:EaseFov(want)
		if self.native then self:Native("aim " .. (want and "1" or "0")) end
		local ron = self:RonSounds()
		if ron and (want and ron.adsIn or not want and ron.adsOut) then self:RonHere(want and ron.adsIn or ron.adsOut) end
	end
	if self.vmShown and not blocked then
		self:VmPlay(self:VmRestKey(), true, 1.0, blend or (not self:VmBusy() and self.cfg.vm.adsBlend or nil))
	end
end

-- zoom in / out over adsBlend (smoothstep) instead of snapping
function Deagle:EaseFov(on)
	local s = self.state
	try("aim fov", function()
		if (on or self.zoom) and not s.baseFov then s.baseFov = System.GetViewCameraFov() end
		if not s.baseFov then return end
		local to = s.baseFov * (on and self.cfg.aimFovScale or 1) / (self.zoom or 1)
		local from, t0 = s.fovNow or s.baseFov, System.GetCurrTime()
		self.fovToken = (self.fovToken or 0) + 1
		local tok = self.fovToken
		local function step()
			if Deagle.fovToken ~= tok then return end
			local k = math.min(1, (System.GetCurrTime() - t0) / math.max(0.01, Deagle.cfg.vm.adsBlend))
			k = k * k * (3 - 2 * k)
			s.fovNow = from + (to - from) * k
			System.SetViewCameraFov(s.fovNow)
			if k < 1 then Script.SetTimer(0, step)
			elseif not on and not Deagle.zoom then s.baseFov, s.fovNow = nil, nil end   -- fully out: read the game's FOV again next time
		end
		step()
	end)
end

-- inspect only from the hip; an empty gun keeps its slide locked and shows an empty magazine (MWII-style)
function Deagle:Inspect()
	local s = self.state
	if s.aiming or s.aimHeld or not s.drawn or s.reloading or self:VmBusy() or self.vmHolstering then return end
	self:VmPlay(s.ammo == 0 and "inspectEmpty" or "inspect", false)
end

-- ---- fire modes: from the gun's CS2 weapon (burst / full auto); B cycles them, the HUD shows the current one
function Deagle:FireModes()
	return self.gun.modes or { "semi" }
end

function Deagle:FireMode()
	local m = self:FireModes()
	return m[self.state.modeIndex or 1] or m[1]
end

function Deagle:CycleFireMode()
	local m = self:FireModes()
	if #m < 2 then return end
	self.state.modeIndex = (self.state.modeIndex or 1) % #m + 1
	log("fire mode " .. self:FireMode())
end

function Deagle:TriggerPress()
	local s, c, mode = self.state, self.cfg, self:FireMode()
	if mode == "auto" then
		s.trigger = true
		self.autoToken = (self.autoToken or 0) + 1
		local tok = self.autoToken
		local function shot()
			if not (Deagle.state.trigger and Deagle.autoToken == tok and Deagle.state.drawn) then return end
			Deagle:Fire()
			if Deagle.state.ammo > 0 then Script.SetTimer(math.max(1, math.floor(Deagle.cfg.interval * 1000)), shot) end
		end
		shot()
	elseif mode == "burst" then
		local now = System.GetCurrTime()
		local gap = c.burstShotGap
		if now < (s.burstReady or 0) or now - s.lastShot < c.interval - 0.005 then return end
		s.burstReady = now + 2 * gap + c.burstInterval
		self:Fire(false, 1)
		for i = 1, 2 do
			Script.SetTimer(math.floor(i * gap * 1000), function() if Deagle.state.drawn then Deagle:Fire(true, i + 1) end end)
		end
	else
		self:Fire()
	end
end
-- CS2's accuracy, for guns with a CS2 profile (user: "like CS2", it was too hard to hit; Ready or Not's guns will
-- bring their own): CS:GO / CS2 GetInaccuracy with the gun's own numbers (guns/cs2.lua, radians). Inaccuracy standing
-- or crouched (KCD2 gives Lua no stance: crouched = the eye under cfg.accuracy.crouchEye; measured live 1.54 m
-- standing, 1.02 crouched), + moving (a ramp from 34 % to 95 % of the gun's max speed, so walking slowly costs nothing),
-- + in the air (jumping, falling), + what recent shots left: each shot adds inFire, gone to 10 % over the recovery
-- time (shorter crouched). Returns the inaccuracy and the base spread for this shot.
function Deagle:CsAccuracy(w)
	local s, a, p = self.state, self.cfg.accuracy, self:Player()
	local now = System.GetCurrTime()
	local eye = System.GetViewCameraPos().z - p:GetWorldPos().z
	s.crouched = eye < a.crouchEye + (s.crouched and a.crouchBand or -a.crouchBand)   -- a band so a bob can't flicker it
	local ok, v = pcall(p.GetVelocity, p)
	local speed = ok and v and math.sqrt(v.x * v.x + v.y * v.y) or 0
	local air = false
	pcall(function() air = p.actor:IsFlying() end)
	s.accFire = (s.accFire or 0) * math.exp(-math.log(10) * math.max(0, now - (s.accT or now)) / (s.crouched and w.recCrouch or w.recStand))
	s.accT = now
	local lo, hi = 0.34 * w.maxSpeed, 0.95 * w.maxSpeed
	local move = math.max(0, math.min(1, (speed - lo) / (hi - lo)))
	local k = a.scale
	return k * ((s.crouched and w.inCrouch or w.inStand) + move * w.inMove + (air and w.inJump or 0) + s.accFire), k * w.spread
end
