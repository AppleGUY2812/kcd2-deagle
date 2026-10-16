-- Sound (Claude 3): gunshot gain, Ready or Not surfaces and hit sounds, pain voices, the dry click, sound cues on
-- the clips, the room check (Env*), the plugin sound engine state.
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U
local SHOT_CUE, SURFACES, log, surfaceName, try, vadd, vscale = U.SHOT_CUE, U.SURFACES, U.log, U.surfaceName, U.try, U.vadd, U.vscale

-- How loud a gunshot plays: the file's own gain (tools/shot_loudness.py evens out how loud each sample was mastered)
-- times the gun's blast level against the loudest gun in the mod. Blast peak pressure grows with the cube root of the
-- muzzle-gas energy (Hopkinson-Cranz): +(20/3) log10 of the energy ratio in dB (9 mm vs .357 measured within 1 dB).
local function blastDb(phys) return (20 / 3) * math.log10(phys.gasEnergy) end
function Deagle:ShotGain(file, own)   -- own: the gain itself (Ready or Not's live engine: its mix sets the level)
	local g = own or (self.shotGain and self.shotGain[file])
	if not g then
		log("$4no loudness for " .. tostring(file) .. " (run tools/shot_loudness.py)")
		g = 1
	end
	local phys = self:ShotPhysics()
	if not phys then return g end
	local loudest = blastDb(phys)
	for _, other in pairs(self.guns) do
		local o = self:ShotPhysics(other)
		if o then loudest = math.max(loudest, blastDb(o)) end
	end
	return g * 10 ^ (self.cfg.physics.loudnessSpan * (blastDb(phys) - loudest) / 20)
end
-- the hit's sound: Ready or Not's Bullet Impact Surfaces event with its Surface index (RoN's physical surfaces,
-- docs/ron_fmod_events.md "Surface / Material numbers"; RoN's Soil / Mud / Leaves / Snow play nothing, so earth is Dirt
-- and plants are Grass). Picked by the user by ear from tools/sound/ron_sound_check.py (2026-10-13): stone is
-- ConcreteStrong (RoN's "Rock" and "Limestone" play its brick clatter), glass is Glass_Windshield, hay is Grass, bells
-- ring RoN's Tower Bell event.
local RON_SURFACE = {}
local function ronSurface(index, names) for _, n in ipairs(names) do RON_SURFACE[n] = index end end
ronSurface(8, { "mat_rock", "mat_rock_unwalk", "mat_rock_horse_ignore", "mat_default", "mat_invulnerable",
	"mat_stairs_stone" })                                                                                           -- ConcreteStrong
ronSurface(34, { "mat_plaster" })                                                                                   -- Plaster
ronSurface(6, { "mat_ceramics" })                                                                                   -- Ceramic
ronSurface(33, { "mat_wood", "mat_wood_unwalk", "mat_door_wood", "mat_stairs_wood" })                                 -- Pine
ronSurface(46, { "mat_wood_soft" })                                                                                 -- Treewood
ronSurface(36, { "mat_shield_wood" })                                                                               -- Plywood
ronSurface(5, { "mat_wicker" })                                                                                     -- Cardboard
ronSurface(44, { "mat_metal", "mat_super_cool_sparkly_type", "mat_door_metal", "mat_plate",
	"mat_chainmail", "mat_chainmail_plate", "mat_fabric_plate", "mat_fabric_chainmail", "mat_fabric_chainmail_plate",
	"mat_leather_chainmail" })                                                                                          -- Steel
ronSurface(9, { "mat_soil", "mat_soil_unwalk", "mat_mud", "mat_road", "mat_field", "mat_forest" })                  -- Dirt
ronSurface(21, { "mat_gravel" })                                                                                    -- Gravel
ronSurface(20, { "mat_grass", "mat_grassdry", "mat_thatch", "mat_organic_stuff" })                                  -- Grass
ronSurface(48, { "mat_water", "mat_mudwater", "mat_slowwater", "mat_rockwater", "mat_puddle" })                     -- Water
ronSurface(19, { "mat_glass" })                                                                                     -- Glass_Windshield
ronSurface(15, { "mat_fabric", "mat_fabric_items", "mat_sack" })                                                    -- Fabric_Thin
ronSurface(53, { "mat_leather" })                                                                                   -- Leather
ronSurface(16, { "mat_flesh" })                                                                                     -- Flesh
RON_SURFACE.mat_bells = "bell"                                                                                      -- Tower Bell event
-- Steel: a real plate hit instead of RoN's Steel (the user: "we need to change the steel impact sound its sounds like
-- ass"): plate A of Hickok45's Sunday Shoot-a-Round #351 (3 takes; ~22 m from his camera), its clank, early shimmer and
-- ring lifted from the recording (tools/sound/plate_rings.py, plate_sounds.py: files and vol), as loud next to RoN's MK V
-- shot as in the clip (the user's pick, pack 20 "B"). Played by the plugin like RoN's events (Falloff, travel time);
-- a plugin without "file:" sounds (deagle_native_caps) keeps RoN's Steel, ron.
-- Every surface that played RoN's Steel plays it: doors, plate and mail too (the user: "armor as a type of steel target
-- plate we have for hickock's sample").
-- near: full level within it (the plugin's Falloff), 6 m instead of impactNear: up close it was "a little too loud"
-- (the user, 2026-10-14, at the test wall 4 m out); vol halved with it, so from 6 m out it's the level the user
-- approved (plate_sounds.py: 3.37 at 3 m; 3.414 since its 653 Hz hum is in, same level over 2 kHz), at 4 m -3.5 dB, at 3 m and closer -6 dB.
local PLATE = { files = { "steel/plate_01.wav", "steel/plate_02.wav", "steel/plate_03.wav" }, vol = 1.707, near = 6, ron = 44 }
for n, v in pairs(RON_SURFACE) do if v == 44 then RON_SURFACE[n] = PLATE end end
for n, sf in pairs(SURFACES) do sf.ron = RON_SURFACE[n] end

-- An NPC our round hits says what KCD2's NPCs say when a blow lands in a fight: its "got hit" bark, in its own voice
-- (metarole SKIRMISH_HITTARGET_TARGET: soldiers' and bandits' SOLDIER_DOSTAL_ZASAH_V_COMBATU and a few named people's;
-- rpg/role.xml), asked for the way KCD2's DialogUtils.RequestPlayerMonologByMetarole asks. KCD2 has no hit grunts:
-- voices.xml's v_hit_passed / v_hit_stopped / v_attack triggers are empty. Townsfolk have no such line and stay quiet.
-- (The user: "do the npcs make pain sounds", 2026-10-14.) Not the dead; one per NPC every PAIN_GAP s, so rapid fire
-- doesn't cut each line off.
local PAIN_GAP = 2.5
function Deagle:PainVoice(e)
	local hp = 1
	pcall(function() hp = e.actor:GetHealth() end)
	if not hp or hp <= 0 then return end
	local now, key = System.GetCurrTime(), tostring(e.id)
	if not self.painT or (self.painN or 0) > 64 then self.painT, self.painN = {}, 0 end   -- forget old NPCs now and then
	local last = self.painT[key]
	if last and now >= last and now - last < PAIN_GAP then return end   -- now < last: a save load set the clock back
	if not last then self.painN = (self.painN or 0) + 1 end
	self.painT[key] = now
	try("pain voice", function()
		XGenAIModule.SendMessageToEntityData(e.this.id, "dialog:monologRequest",
			table.MakeFromType("dialog:monologRequest", { metarole = "SKIRMISH_HITTARGET_TARGET" }))
	end)
end

-- A hit's sound from where it happened: Ready or Not's own event, 3D in its engine (direction). what = a RoN Surface
-- index (Bullet Impact Surfaces), a RON_HIT or ARMOR_HIT key, or one of our recordings ({ files, vol, ron }).
-- These events don't fade with FMOD's own 3D distance: RoN's game code sets PropagateDistance (m along the sound's path)
-- and the event's volume follows it (impacts measured: -3 dB at 5 m, -22 dB at 10 m, silent from 20 m; ricochets carry
-- to ~45 m). KCD2 has no portal volumes, so the path is the straight line. The plugin works it out from wherever the
-- listener is (the free camera too) and holds the sound back by its travel time (343 m/s): "PropagateDistance=@".
-- KCD2's AI hears it too (enum_sound.bullet_hit at the spot).
local SOUND_SPEED = 343
local RON_IMPACT = "event:/Surfaces/Impacts/Bullet Impacts/Bullet Impact Surfaces"
local RON_HIT = {
	ricochet = "event:/Surfaces/Impacts/Bullet Impacts/Ricochets",
	bell = "event:/Surfaces/Impacts/Bullet Impacts/Bullet Impact Tower Bell",
}
-- Worn armour (ballistics ArmorHit; _block: stopped or turned it). Our armour stops rounds real armour doesn't, the way
-- a steel target plate does, so it rings like the plate; a round through it makes no ring you'd hear over the gunshot
-- (the user, 2026-10-14: "armor as a type of steel target plate", "car doors it just goes through so no noticeable
-- sound"), only the flesh hit after it.
local ARMOR_HIT = { armor_helmet_block = PLATE, armor_body_block = PLATE, armor_helmet = false, armor_body = false }
Deagle.ronHitEvents = { RON_IMPACT }
for _, ev in pairs(RON_HIT) do Deagle.ronHitEvents[#Deagle.ronHitEvents + 1] = ev end
-- The sound budget (docs/ARCHITECTURE.md step 4). A CIWS at 75 rounds/s made 150-290 hit sounds a second and the game
-- fell to 8-12 fps (engine 76-120 ms a frame), while the same fire with hit sounds off held 41-61 fps (deagle_stress 75
-- aim ab, 2026-10-15): every one is a Ready or Not event living 1-3 s, and its engine updates on the game's main thread.
-- At that rate single hits can't be told apart. Per category (cfg.soundBudget: hit, ricochet, casing):
-- (1) merge: a sound within near m of a fresh one (younger than nearTime s) of the same kind plays nothing;
-- (2) a token bucket: bursts up to burst, then rate a second;
-- (3) priority by distance: a sound farther than closeM from the listener needs reserve tokens to spare, so far hits
--     can't starve the ones next to you.
-- prio "keep" always plays (hits on people; your own gunshot and handling sounds don't come here at all). Skips are
-- counted per category and logged at most every 2 s. Like effects.lua's FxBudget (Claude 2). A/B: deagle_set
-- soundBudget.on 0. Returns true when the sound may play.
local SOUND_RECENT = 16
local SOUND_CATS = { "hit", "ricochet", "casing" }
function Deagle:SoundBudget(cat, kind, pos, prio)
	local B = self.cfg.soundBudget
	local C = B and B.on and B[cat]
	if not C or not pos or prio == "keep" then return true end
	local now = System.GetCurrTime()
	local all = self.soundBudgetState
	if not all or now < all.t then   -- first use, or the clock went back (a save load)
		all = { t = now, said = 0 }
		self.soundBudgetState = all
	end
	all.t = now
	local st = all[cat]
	if not st or now < st.t then st = { tokens = C.burst, t = now, ring = {}, i = 0, skips = 0 }; all[cat] = st end
	st.tokens = math.min(C.burst, st.tokens + (now - st.t) * C.rate)
	st.t = now
	local need = 1
	if (C.reserve or 0) > 0 then
		local cp = System.GetViewCameraPos()
		if (pos.x - cp.x) ^ 2 + (pos.y - cp.y) ^ 2 + (pos.z - cp.z) ^ 2 > B.closeM * B.closeM then need = 1 + C.reserve end
	end
	local skip = st.tokens < need
	if not skip then
		for k = 1, SOUND_RECENT do
			local r = st.ring[k]
			if r and r.kind == kind and now - r.t < C.nearTime
				and (pos.x - r.x) ^ 2 + (pos.y - r.y) ^ 2 + (pos.z - r.z) ^ 2 < C.near * C.near then skip = true break end
		end
	end
	if skip then
		st.skips = st.skips + 1
		if now >= all.said then
			all.said = now + 2
			local parts = {}
			for _, c in ipairs(SOUND_CATS) do if all[c] then parts[#parts + 1] = c .. " " .. all[c].skips end end
			log("sounds skipped so far (budget, docs/PERFORMANCE.md #36): " .. table.concat(parts, ", "))
		end
		return false
	end
	st.tokens = st.tokens - 1
	st.i = st.i % SOUND_RECENT + 1
	local r = st.ring[st.i] or {}
	r.kind, r.t, r.x, r.y, r.z = kind, now, pos.x, pos.y, pos.z
	st.ring[st.i] = r
	return true
end
function Deagle:HitSound(what, pos, scale, vol)
	local c = self.cfg
	-- hits on people (flesh, worn armour) always play; ricochets and the rest share their category's budget
	local keep = what == SURFACES.mat_flesh.ron or ARMOR_HIT[what] ~= nil
	if ARMOR_HIT[what] ~= nil then what = ARMOR_HIT[what] end
	if not self:SoundBudget(what == "ricochet" and "ricochet" or "hit", what, pos, keep and "keep" or nil) then return end
	return self:HitSoundPlay(what, pos, scale, vol)
end
-- the hit sound itself, past the budget (deagle_stress spawn hitsound times it raw)
function Deagle:HitSoundPlay(what, pos, scale, vol)
	local c = self.cfg
	if self.native and what then
		local ev, params
		-- Falloff: the plugin's distance law from the listener (the free camera too), instead of RoN's PropagateDistance
		local prop = "Occlusion=0 PropagateDistance=@ Falloff=" .. c.impactNear
		if type(what) == "table" then   -- our own recordings: { files, vol, ron = RoN's Surface for an older plugin }
			if self.nativeCaps and self.nativeCaps.files then
				ev, params, vol = "file:" .. what.files[math.random(1, #what.files)], "Falloff=" .. (what.near or c.impactNear), what.vol * (vol or 1)
			else what = what.ron end
		end
		if ev then
		elseif type(what) == "number" then ev, params = RON_IMPACT, string.format("Surface=%d %s", what, prop)
		else ev, params = RON_HIT[what], prop end
		if ev then self:Native(string.format("ron3 %.3f %.2f %.2f %.2f %s|%s", c.impactVolume * (vol or 1), pos.x, pos.y, pos.z, ev, params)) end
	end
	-- one AI sound per hit: an NPC hit plays armour + flesh (+ a ricochet) at one spot in one frame, each re-running the
	-- AI's hearing (performance audit 2026-10-15)
	if c.hearing.on then
		local f, last = System.GetFrameID(), self.aiHeard
		if not (last and last.f == f and (last.x - pos.x) ^ 2 + (last.y - pos.y) ^ 2 + (last.z - pos.z) ^ 2 < 1) then
			self.aiHeard = { f = f, x = pos.x, y = pos.y, z = pos.z }
			try("AI hears the hit", XGenAIModule.ProduceSound, enum_sound.bullet_hit, pos, c.hearing.hit * (scale or 1))
		end
	end
end

-- the gun's Ready or Not events while cfg.sounds is "ron" (nil: play the gun mod's own recordings)
function Deagle:RonSounds()
	return self.cfg.sounds == "ron" and self.gun and self.gun.ron or nil
end

-- where Henry's gun is heard from while the free camera is the listener (his head)
function Deagle:GunSoundPos()
	local p = self:Player()
	local ok, h = pcall(p.GetBonePos, p, "Head")
	if ok and type(h) == "table" then return h end
	local w = p:GetWorldPos()
	return { x = w.x, y = w.y, z = w.z + 1.6 }
end
-- a Ready or Not event at the player (handling, dry fire, aim): 3D events sit on the listener; gain x its volume. With
-- the free camera on, the listener is the camera: then it plays at Henry (it sat on the camera: the user, 2026-10-13)
function Deagle:RonHere(ev, gain)
	if not (self.native and ev) then return end
	local vol = self.cfg.vm.soundVol.ron * (gain or 1)
	if self.fc then
		local at = self:GunSoundPos()
		self:Native(string.format("ron3 %.3f %.2f %.2f %.2f %s|", vol, at.x, at.y, at.z, ev))
	else self:Native(string.format("ron %.3f %s|", vol, ev)) end
end
-- RoN mixes its dry-fire clicks 37-44 dB under the shot, too quiet to hear in KCD2 (the user, 2026-10-13): each one's
-- measured level (short-term, rendered alone in RoN's engine) is raised to cfg.vm.dryDb
local DRY_DB = { ["event:/Weapons/FN57/FN57 Dry Fire"] = -40.0, ["event:/Weapons/G19/G19 Dryfire Old"] = -46.7 }
function Deagle:DryGain(ev)
	local own = DRY_DB[ev]
	return own and 10 ^ ((self.cfg.vm.dryDb - own) / 20) or 1
end

-- sound cues of a clip, played by the plugin; cues of an interrupted clip are dropped (shots always play).
-- Cues follow the clip's own playhead (GetAnimationTime, 0..1 through the clip; checked every frame by CueTick), not a
-- timer: in F9 slow motion the viewmodel's animation runs ~1.5x faster than game time while timers run on game time, so
-- timed sounds fell behind the later they came (mag-in ~2 s, slide close ~5 s late, the user's recording 2026-10-12).
-- a gun's Ready or Not handling cues (guns/ron_handling.lua) as plain cues: each event starts early by its click time,
-- so the click lands on the clip's moment ("ron:<event>", or "ronfeet:<event>" at the player's feet)
function Deagle:RonCues(key)
	local rc = self.gun.ronCues and self.gun.ronCues[key]
	if not rc then return nil end
	local out, r, latency = {}, self:Rate(key), self.cfg.vm.cueLead or 0
	for _, c in ipairs(rc) do
		-- real seconds early (its click, then the output latency) in clip seconds: the clip runs Rate times faster
		local lead = ((c.start and 0 or (self.ronClick[c[2]] or 0) / 1000) + latency) * r
		out[#out + 1] = { math.max(0, c[1] - lead), (c.feet and "ronfeet:" or "ron:") .. c[2] }
	end
	return out
end
-- blend: s the clip takes to blend in (VmPlay), while the layer may still report the clip it blends out of; speed: its
-- playback rate
function Deagle:PlayCues(key, blend, speed)
	local g = self.gun
	local cues = (self:RonSounds() and g.ronCues and self:RonCues(key)) or g.cues[key] or ((key == "shoot" or key == "shootEmpty") and SHOT_CUE) or nil
	if not cues then return end
	local a = self.cfg.vm.anims[key]
	if key == "shoot" then a = a[((self.vmShot or 1) - 1) % #a + 1] end
	local span = math.max(1, a[2] - 1) / (a[3] or self.cfg.vm.fps)   -- clip-timeline seconds from first to last frame
	local list = {}
	for _, c in ipairs(cues) do
		local at = math.min(1, c[1] / span)
		if at <= 0.001 then self:CueFire(key, c[2]) else list[#list + 1] = { at = at, file = c[2] } end
	end
	table.sort(list, function(x, y) return x.at < y.at end)
	self.cuesPending = #list > 0 and { key = key, name = a[1], tok = self.vmAnimToken, list = list,
		from = System.GetCurrTime(), blend = blend or 0, speed = speed or 1, span = span } or nil
end
function Deagle:CueFire(key, file)
	local v = self.cfg.vm.soundVol
	local ev = file:match("^ron:(.+)$")
	if ev then return self:RonHere(ev) end
	ev = file:match("^ronfeet:(.+)$")
	if ev then
		local p = self:Player()
		local at = p and p:GetWorldPos()
		if at and self.native then self:Native(string.format("ron3 %.3f %.2f %.2f %.2f %s|", v.ron, at.x, at.y, at.z + 0.05, ev)) end
		return
	end
	if file == "shot" then
		if not (self.cfg.shotSound and self.cfg.gunFx) then return end
		local ron = self:RonSounds()
		if ron and ron.fire then
			if self.native then
				local vol, out = v.shot * self:ShotGain(nil, 1), self:EnvOutside() and 1 or 0
				if self.fc and ron.fire3p then   -- heard from the free camera: RoN's 3D third-person shot at Henry, fading with distance
					local at = self:GunSoundPos()
					self:Native(string.format("ron3 %.3f %.2f %.2f %.2f %s|IsOutside=%d Occlusion=0 PropagateDistance=@ Falloff=3", vol, at.x, at.y, at.z, ron.fire3p, out))
				else self:Native(string.format("ron %.3f %s|IsOutside=%d", vol, ron.fire, out)) end
			end
			return
		end
		local f = self:ShotFile()
		if f then self:Snd(f, v.shot * self:ShotGain(f)) end
	else self:Snd(file, v.foley) end
end
-- every viewmodel frame: the cues the playhead has passed
function Deagle:CueTick()
	local p = self.cuesPending
	if not p then return end
	if self.vmAnimToken ~= p.tok and p.key ~= "shoot" and p.key ~= "shootEmpty" then self.cuesPending = nil return end
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm then return end
	local layer = self.cfg.vm.layer
	-- the layer may report the clip it blends out of until the blend is over: meanwhile the playhead from game time
	-- (game seconds cover the blend: the clip's clock runs at least as fast, ~1.5x in slow motion). (KCD2's
	-- GetCurAnimation would say which clip it is, but returns nil even while one plays: measured 2026-10-13; waiting
	-- for it silenced every clip cue.)
	local since, t = System.GetCurrTime() - p.from, nil
	if since < p.blend then t = since * p.speed / p.span
	else
		local okT, tt = pcall(vm.GetAnimationTime, vm, 0, layer)
		if not okT or type(tt) ~= "number" then return end
		t = tt
	end
	-- a cue due before the next frame goes now: waiting for the frame after it made every sound up to a frame late
	local ahead = (self.frameDt or 1 / 60) * p.speed / p.span
	while p.list[1] and p.list[1].at <= t + ahead + 1e-4 do
		self:CueFire(p.key, table.remove(p.list, 1).file)
	end
	if not p.list[1] then self.cuesPending = nil end
end

function Deagle:Snd(file, vol)
	if self.native then self:Native(string.format("snd %s %.2f", file, vol or 1)) end
end

-- ---------------------------------------------------------------------------
-- where the player is (docs/ron_live_spec.md): guns with gun.ron fire Ready or Not's own event through its own engine
-- (the plugin runs it: native/src/ron.cpp). RoN's room sound comes from 8 room and 4 material snapshots whose strength
-- follows global parameters (ReverbSMALL ... ReverbMaterialFABRIC, 0..1; its map boxes set them and fade them across
-- doorways). KCD2 has no such boxes, so rays from the eye classify the place every cfg.env.period s, a class takes over
-- after 2 reads in a row agree, and its weights fade in over cfg.env.fade s.
-- Classes: out_forest out_street out_covered in_<small|mid|hall|corridor>[_<stone|wood|metal|fabric>] in_cave(_stone)
-- ---------------------------------------------------------------------------
local ENV_UP, ENV_RING = {}, {}
local envSurfKind
local ENV_SURF = {}   -- surface name -> kind, worked out once per name (up to 30 finds each)
local function envSurf(name)
	local k = ENV_SURF[name]
	if not k then k = envSurfKind(name); ENV_SURF[name] = k end
	return k
end
function envSurfKind(name)
	if name:find("^mat_rock") or name:find("stone") or name:find("brick") or name:find("plaster") or name:find("ceramic") then return "stone" end
	if name:find("wood") or name:find("bark") or name:find("tree") or name:find("wicker") then return "wood" end
	if name:find("metal") or name:find("chain") or name:find("plate") or name:find("bell") or name:find("iron") then return "metal" end
	if name:find("fabric") or name:find("cloth") or name:find("sack") or name:find("carpet") or name:find("leather") then return "fabric" end
	if name:find("vegetation") or name:find("bush") or name:find("canopy") or name:find("leaves") or name:find("thatch") then return "veg" end
	if name:find("soil") or name:find("grass") or name:find("sand") or name:find("gravel") or name:find("mud") then return "terrain" end
	return "other"
end
local ENV_MATS = { "stone", "wood", "metal", "fabric" }
-- one sample: up = { {dist, kind}... } (nil dist = nothing), ring = 16 of { dist, kind } (index i = i x 22.5 deg)
function Deagle:EnvClassify(sample)
	local c = self.cfg.env
	local roofN, roofStone = 0, false
	for _, u in ipairs(sample.up) do
		if u.dist and u.dist <= c.roof then
			roofN = roofN + 1
			roofStone = roofStone or u.kind == "stone" or u.kind == "terrain"
		end
	end
	local roof = roofN >= c.roofHits
	local near, dists, kinds = 0, {}, {}
	for _, r in ipairs(sample.ring) do
		if r.dist then
			kinds[r.kind] = (kinds[r.kind] or 0) + 1
			if r.dist <= c.ringNear then near = near + 1; dists[#dists + 1] = r.dist end
		end
	end
	local hits = 0
	for _, n in pairs(kinds) do hits = hits + n end
	local function share(k) return hits > 0 and (kinds[k] or 0) / hits or 0 end
	local info = { roofHits = roofN, ringHits = hits, ringNear = near, shares = {} }
	for _, m in ipairs(ENV_MATS) do info.shares[m] = share(m) end
	if roof and near >= c.ringInside then
		table.sort(dists)
		local d = dists[math.ceil(#dists / 2)] or 0
		info.median = d
		local function dist(i) local r = sample.ring[(i - 1) % 16 + 1]; return r.dist or math.huge end
		local corridor = false
		for i = 1, 8 do
			if dist(i) < c.corrNarrow and dist(i + 8) < c.corrNarrow and dist(i + 4) > c.corrWide and dist(i + 12) > c.corrWide then corridor = true end
		end
		if share("stone") > c.share and roofStone then return share("stone") > 0.8 and "in_cave_stone" or "in_cave", info end
		local size = corridor and "corridor" or d < c.small and "small" or d <= c.hall and "mid" or "hall"
		local mat, best = nil, c.share   -- the walls' material: the kind most ring rays hit, if it's over half of them
		for _, m in ipairs(ENV_MATS) do if share(m) > best then mat, best = m, share(m) end end
		return "in_" .. size .. (mat and ("_" .. mat) or ""), info
	end
	if roof then return "out_covered", info end
	local trunks = 0
	for _, r in ipairs(sample.ring) do if r.dist and r.kind == "wood" then trunks = trunks + 1 end end
	if share("veg") > c.vegShare or trunks >= c.trunks then return "out_forest", info end
	return "out_street", info
end
-- a class as Ready or Not's globals: the room's snapshot (and material's) at 1, every other at 0; outside = IsOutside
local ENV_ROOMS = { "BATHROOM", "SMALL", "CORRIDOR", "MID", "HALL", "STREET", "FOREST", "CAVE" }
local ENV_MAT_GLOBAL = { stone = "CONCRETE", wood = "WOOD", metal = "METAL", fabric = "FABRIC" }
local ENV_GLOBALS = {}
for _, r in ipairs(ENV_ROOMS) do ENV_GLOBALS[#ENV_GLOBALS + 1] = "Reverb" .. r end
for _, m in ipairs(ENV_MATS) do ENV_GLOBALS[#ENV_GLOBALS + 1] = "ReverbMaterial" .. ENV_MAT_GLOBAL[m] end
local ENV_MIX = {}   -- class -> { mix, outside }: EnvFade asks every frame
function Deagle:EnvMix(cls)
	local m = ENV_MIX[cls]
	if not m then m = { self:EnvMixOf(cls) }; ENV_MIX[cls] = m end
	return m[1], m[2]
end
function Deagle:EnvMixOf(cls)
	local mix = {}
	for _, g in ipairs(ENV_GLOBALS) do mix[g] = 0 end
	local room, mat
	if cls == "out_street" then room = "STREET"
	elseif cls == "out_forest" then room = "FOREST"
	elseif cls == "out_covered" then room, mat = "MID", "stone"   -- a roof with open sides: a mid room's ring off it
	else
		local size, m = cls:match("^in_(%a+)_?(%a*)$")
		room, mat = size and size:upper(), m ~= "" and m or nil
	end
	if room then mix["Reverb" .. room] = 1 end
	if mat then mix["ReverbMaterial" .. ENV_MAT_GLOBAL[mat]] = 1 end
	return mix, cls:find("^out_") ~= nil
end
function Deagle:EnvOutside()
	return self.envClass == nil or self.envClass:find("^out_") ~= nil
end
-- rays from the eye: static things and terrain, not the player
function Deagle:EnvSample()
	local c, eye, pl = self.cfg.env, System.GetViewCameraPos(), self:Player()
	local function cast(dir, len)   -- the first thing a sound would bounce off: on through foliage, helpers, see-through hulls
		local from, done = eye, 0
		for _ = 1, 8 do
			local hits = {}
			local ok, n = pcall(Physics.RayWorldIntersection, from, vscale(dir, len - done), 1, ent_static + ent_terrain, pl and pl.id, nil, hits)
			if not (ok and n and n > 0 and hits[1]) then break end
			local name = surfaceName(hits[1])
			local sf = SURFACES[name]
			if not (sf and sf.fx == "pass") then return { dist = done + hits[1].dist, kind = envSurf(name) } end
			done = done + hits[1].dist + 0.01
			if done >= len then break end
			from = vadd(eye, vscale(dir, done))
		end
		return { dist = nil }
	end
	if #ENV_UP == 0 then
		local a = math.rad(c.upCone)
		ENV_UP[1] = { x = 0, y = 0, z = 1 }
		for k = 0, 3 do local h = k * math.pi / 2; ENV_UP[#ENV_UP + 1] = { x = math.sin(a) * math.cos(h), y = math.sin(a) * math.sin(h), z = math.cos(a) } end
		for k = 0, 15 do local h = k * math.pi / 8; ENV_RING[k + 1] = { x = math.cos(h), y = math.sin(h), z = 0 } end
	end
	local sample = { up = {}, ring = {} }
	for i, d in ipairs(ENV_UP) do sample.up[i] = cast(d, c.upLen) end
	for i, d in ipairs(ENV_RING) do sample.ring[i] = cast(d, c.ringLen) end
	return sample
end
-- every viewmodel frame: a new read every period s; the class changes after 2 agreeing reads; the weights fade to it
local ENV_STILL = 0.5   -- m the eye may move and still count as the last read's place
function Deagle:EnvTick()
	local c = self.cfg.env
	if not self:RonSounds() then return end   -- only Ready or Not's sounds use the place
	local now = System.GetCurrTime()
	-- now < envT: a save loaded while the game ran set the clock back (322 s once, Claude 4 2026-10-14): check at once
	if c.on and (not self.envT or now < self.envT or now - self.envT >= c.period) then
		self.envT = now
		-- the rays point the same way in the world every time, so standing still reads the same place: the last read
		-- counts again, without its 21+ rays (performance audit 2026-10-15)
		local eye, e0, cls = System.GetViewCameraPos(), self.envEye, nil
		if e0 and self.envLastCls and (eye.x - e0.x) ^ 2 + (eye.y - e0.y) ^ 2 + (eye.z - e0.z) ^ 2 < ENV_STILL ^ 2 then
			cls = self.envLastCls
		else
			local info
			cls, info = self:EnvClassify(self:EnvSample())
			self.envInfo, self.envEye, self.envLastCls = info, { x = eye.x, y = eye.y, z = eye.z }, cls
		end
		if cls == self.envClass then self.envPending = nil
		elseif cls == self.envPending or not self.envClass then self.envClass, self.envPending = cls, nil
		else self.envPending = cls end
	end
	self:EnvFade(now)
end
-- move every global toward the class's value at 1 / fade per second and send the ones that moved; the first class
-- after a load is set at once (nothing to fade from)
function Deagle:EnvFade(now)
	if not self.envClass or not self.native then return end
	local cur, first = self.envMixNow, self.envMixNow == nil
	local dt = math.max(0, now - (self.envFadeT or now))
	self.envFadeT = now
	if not first and self.envSettled == self.envClass then return end   -- every weight already there: nothing to send
	local target = self:EnvMix(self.envClass)
	if first then cur = {}; self.envMixNow = cur end
	local moved = false
	local step = self.cfg.env.fade > 0 and dt / self.cfg.env.fade or 1
	for _, g in ipairs(ENV_GLOBALS) do
		local was, want = cur[g], target[g]
		local v = (first or was == nil) and want or (was < want and math.min(want, was + step) or math.max(want, was - step))
		if v ~= was then
			cur[g] = v
			moved = true
			self:Native(string.format("rong %s %.3f", g, v))
		end
	end
	self.envSettled = not moved and self.envClass or nil
end
-- the gun's Ready or Not events load at the handshake, so the first shot has no load delay
function Deagle:RonWarm()
	self.envMixNow = nil   -- the plugin's engine may be new: send every weight again
	for _, g in pairs(self.guns) do
		if g.ron then for _, ev in pairs(g.ron) do self:Native("ronload " .. ev) end end
		local seen = {}
		for _, list in pairs(g.ronCues or {}) do
			for _, c in ipairs(list) do if not seen[c[2]] then seen[c[2]] = true; self:Native("ronload " .. c[2]) end end
		end
	end
	for _, ev in ipairs(self.ronHitEvents) do self:Native("ronload " .. ev) end
end
-- what the plugin can do beyond the handshake ("files": play our own sound files, ron3 file:<path>); its files load now
function Deagle:NativeCaps(caps)
	self.nativeCaps = {}
	for w in tostring(Deagle.Arg(caps) or ""):gmatch("%a+") do self.nativeCaps[w] = true end
	if self.nativeCaps.files then
		local seen = {}
		for _, sf in pairs(SURFACES) do
			if type(sf.ron) == "table" and not seen[sf.ron] then
				seen[sf.ron] = true
				for _, f in ipairs(sf.ron.files) do self:Native("ronload file:" .. f) end
			end
		end
	end
end
function Deagle:RonState(ok)
	self.ronOk = Deagle.Arg(ok) == "1"   -- the console hands %1 over in quotes ("1"): a raw compare always failed
	if not self.ronOk then
		local msg = "Ready or Not's sound engine didn't start: Mods/deagle/ron is missing or broken (see deagle_native.log)"
		log("$4" .. msg); self:Notify(msg)
	end
end
-- the gunshot file for a gun that plays its own recordings
function Deagle:ShotFile()
	local shots = self.gun.shotSounds or {}
	return #shots > 0 and shots[math.random(1, #shots)] or nil
end
function Deagle:EnvPrint()
	if not self.envClass then self:EnvTick() end
	local i = self.envInfo or {}
	local mix, outside = self:EnvMix(self.envClass or "out_street")
	local on = {}
	for _, g in ipairs(ENV_GLOBALS) do if mix[g] > 0 then on[#on + 1] = g end end
	local sh = i.shares or {}
	local msg = string.format("place %s = %s%s (roof rays %s, ring hits %s, near %s, median %.1f m; stone %.0f%% wood %.0f%% metal %.0f%% fabric %.0f%%)",
		tostring(self.envClass), table.concat(on, " + "), outside and " outside" or " inside", tostring(i.roofHits), tostring(i.ringHits),
		tostring(i.ringNear), i.median or -1, 100 * (sh.stone or 0), 100 * (sh.wood or 0), 100 * (sh.metal or 0), 100 * (sh.fabric or 0))
	log(msg); self:Notify(msg)
end
System.AddCCommand("deagle_env", "Deagle.envT, Deagle.envEye = nil, nil Deagle:EnvTick() Deagle:EnvPrint()", "deagle_env: the place the gunshot sounds like (Ready or Not's room and material weights) and the ray numbers")
-- %line: the plugin sends several words ("files fxr"); with %1 KCD2 refused the command ("Too many arguments for:
-- deagle_native_caps", 2026-10-15), so no capability was set at all
System.AddCCommand("deagle_native_caps", "Deagle:NativeCaps([[%line]])", "internal: what the native plugin can do (files, fxr)")

-- for the files loaded after this one
U.RON_SURFACE = RON_SURFACE

-- deagle_stress spawn <name> <per second> (debug.lua): one sound kind N times a second at a fixed spot, for the cost of a
-- Ready or Not sound start next to the plugin's per-start microseconds (ron: update ... sounds started, us each)
Deagle.StressSpawners = Deagle.StressSpawners or {}
Deagle.StressSpawners.hitsound = function(pos) Deagle:HitSoundPlay(8, pos, 1) end              -- stone, no budget
Deagle.StressSpawners.hitsound_budget = function(pos) Deagle:HitSound(8, pos, 1) end           -- stone, through the budget
Deagle.StressSpawners.plate = function(pos) Deagle:HitSoundPlay(PLATE, pos, 1) end              -- our file sound, no budget
Deagle.StressSpawners.gunshot3p = function(pos)                                                  -- the gun's 3D shot, there
	local ron = Deagle:RonSounds()
	if not (ron and ron.fire3p and Deagle.native) then return end
	Deagle:Native(string.format("ron3 1.000 %.2f %.2f %.2f %s|IsOutside=%d Occlusion=0 PropagateDistance=@ Falloff=3", pos.x, pos.y,
		pos.z, ron.fire3p, Deagle:EnvOutside() and 1 or 0))
end
