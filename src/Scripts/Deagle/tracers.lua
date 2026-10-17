-- Tracers (Claude 4): the trail pieces along a round's path, the muzzle position.
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U
local log, vadd, vlen, vnorm, vscale, vsub = U.log, U.vadd, U.vlen, U.vnorm, U.vscale, U.vsub

-- ---------------------------------------------------------------------------
-- shooting, casings
-- ---------------------------------------------------------------------------
-- ---------------------------------------------------------------------------
-- tracers and debug paths (the rounds themselves fly in ballistics.lua)
-- ---------------------------------------------------------------------------
local TRACER_SPEEDS = { 250, 1000, 25 }   -- deagle_tracers.v<speed> (tools/gen_tracers.py): first, last, step

-- Tracers. cfg.tracers / deagle_tracers turns them off; Ready or Not hides your own tracers, ours show (no NPC carries
-- a gun yet).
-- "trail" (cfg.tracer.mode, the default): the path the round flies in each frame is laid with still glowing pieces
-- (cfg.tracer.fx, cfg.tracer.dash m each, living 0.12 s and fading: tools/gen_tracers.py), so the streak is exactly
-- where the round went, ricochets and wallbangs included, and ends on what it hit. The round leaves the eye; the
-- streak leaves the muzzle and joins the round's path by the point the shot is headed for (the first thing along it,
-- or 100 m out): trailW in Deagle:TracerBullet.
-- "flying": one particle that flies by itself with the round's speed, gravity and drag (deagle_tracers.v<speed>,
-- nearest 25 m/s), aimed from the muzzle at that same point; dies on the first thing it touches.
function Deagle:TracerFx(speed)
	local first, last, step = TRACER_SPEEDS[1], TRACER_SPEEDS[2], TRACER_SPEEDS[3]
	local v = math.max(first, math.min(last, step * math.floor(speed / step + 0.5)))
	return string.format("deagle_tracers.v%d", v)
end

-- at the shot: the flying tracer, or the trail's start (muzzle offset, where it joins the path) for the round to carry
function Deagle:Tracer(from, dir, phys)
	if not self.cfg.tracers then return nil end
	local muzzle = self:VmBone(self.gun.bones.muzzle)
	if not muzzle then return nil end
	local reach = 100
	local hits, p = {}, self:Player()
	local ok, n = pcall(Physics.RayWorldIntersection, from, vscale(dir, reach), 1, ent_all, p and p.id, nil, hits)
	if ok and n and n > 0 and hits[1] then reach = math.max(1, hits[1].dist) end
	if self.cfg.tracer.mode == "flying" then
		local aim = vnorm(vsub(vadd(from, vscale(dir, reach)), muzzle))
		self:SpawnFx(self:TracerFx(phys.velocity), muzzle, aim, 1.0)
		return nil
	end
	return { off = vsub(muzzle, from), reach = reach }
end

-- the "auto" tracer effect: one that glows about a frame at this frame time
local function autoFx(dt)
	dt = dt or 1 / 60
	return dt <= 1 / 50 and "deagle_tracers.trail_025" or dt <= 1 / 30 and "deagle_tracers.trail_040" or "deagle_tracers.trail_060"
end

-- where the streak is drawn for a point d m along the round's path: the muzzle's offset from the eye, shrinking to
-- nothing at trail.reach (the weight on trail.off)
local function trailW(trail, d)
	local w = 1 - d / trail.reach
	return w > 0 and w or 0
end

-- The tracer is the round itself glowing: once a frame, a short streak (cfg.tracer.length m) ending where the round is,
-- along the way it's going. Far away it's a dot, close by a short dash; it doesn't draw the whole stretch flown in the
-- frame (that drew a laser line: a filmed tracer is a line only in the frames the camera's motion blur smears it, the
-- user's reference clip). Pieces glow about one frame (fx "auto"), the plugin spawning them so they show at once.
-- Plain numbers, no vector tables: per round per frame those were ~1.5 KB of garbage (44 KB a frame at 30 rounds,
-- Claude 3's audit 2026-10-15); the command string is the one allocation.
function Deagle:TracerBullet(r, from)
	local trail, c = r.trail, self.cfg.tracer
	if not trail or not self.cfg.tracers or r.dist < c.start then return end
	local restart = c.spawn == "restart" and self.native and self.nativeCaps and self.nativeCaps.fxr
	if c.spawn == "restart" and self.native and not restart then
		-- a plugin from before fxr: no tracers rather than a new emitter per round per frame (the path that took 140 ->
		-- 70 fps and crashed KCD2's DX12 descriptor heap). Without any plugin: Particle.SpawnEffect below.
		if not self.fxrSaid then
			self.fxrSaid = true
			log("$4tracers: the running plugin has no fxr (one emitter per round): no tracers until the game is restarted")
		end
		return
	end
	-- turned off a surface (Deflect, a corner): this frame's streak starts at the turn, not back on the old line, never
	-- reaches back past it, and the muzzle offset is over
	local turned = r.turnDist
	if r.turnPos then from, r.turnPos = r.turnPos, nil end
	local px, py, pz = r.pos.x, r.pos.y, r.pos.z
	local ux, uy, uz = px - from.x, py - from.y, pz - from.z
	local ml = math.sqrt(ux * ux + uy * uy + uz * uz)
	if ml <= 1e-4 then ux, uy, uz = r.vel.x, r.vel.y, r.vel.z; ml = math.sqrt(ux * ux + uy * uy + uz * uz) end
	if ml <= 0 then return end
	ux, uy, uz = ux / ml, uy / ml, uz / ml
	local l = math.min(c.length, r.dist - c.start, turned and r.dist - turned or math.huge)
	if l < 0.05 then return end
	local o = trail.off
	local w1 = turned and 0 or trailW(trail, r.dist)
	local w0 = turned and 0 or trailW(trail, r.dist - l)
	local x1, y1, z1 = px + o.x * w1, py + o.y * w1, pz + o.z * w1
	local x0, y0, z0 = px - ux * l + o.x * w0, py - uy * l + o.y * w0, pz - uz * l + o.z * w0
	local dx, dy, dz = x1 - x0, y1 - y0, z1 - z0
	local dl = math.sqrt(dx * dx + dy * dy + dz * dz)
	if dl <= 0 then return end
	dx, dy, dz = dx / dl, dy / dl, dz / dl
	local fx = c.fx
	if fx == "auto" then   -- glows about a frame: never shorter than the frame, or it's never drawn. Picked once per round:
		-- a new effect name makes the plugin replace the round's emitter (fxr), and a frame rate near 50 / 30 fps flipped
		-- it every frame (Claude 3's audit 2026-10-15)
		r.traceFx = r.traceFx or autoFx(self.frameDt)
		fx = r.traceFx
	end
	local back = c.anchor == "start" and l or c.anchor == "end" and 0 or l / 2   -- where the effect sits; the end is p1
	local ax, ay, az = x1 - dx * back, y1 - dy * back, z1 - dz * back
	if restart then   -- the round's own emitter, restarted here (plugin fxr)
		if not r.traceId then self.traceIds = (self.traceIds or 0) % 1000000 + 1; r.traceId = self.traceIds end
		self:Native(string.format("fxr %d %s %.3f %.3f %.3f %.3f %.3f %.4f %.4f %.4f", r.traceId, fx, l / c.dash, l, ax, ay, az, dx, dy, dz))
	elseif c.spawn == "plugin" and self.native then   -- a new emitter per piece (the old way; seen from its first frame)
		self:Native(string.format("fxs %s %.3f %.3f %.3f %.3f %.3f %.4f %.4f %.4f", fx, l / c.dash, l, ax, ay, az, dx, dy, dz))
	else self:SpawnFx(fx, { x = ax, y = ay, z = az }, { x = dx, y = dy, z = dz }, l / c.dash) end
end

-- The native round path (cfg.roundSim "native", ballistics.lua): the plugin draws the streak every frame from the
-- round's own path, so nothing per frame stays in Lua. This hands it, once per round at launch, what TracerBullet uses:
-- the effect (picked once, as "auto" does), the streak length, the muzzle offset blending out by trail.reach (trailW),
-- where it starts (cfg.tracer.start m flown), the piece length (dash) and where the effect sits on the streak (anchor:
-- 1 = its start, 0 = its end, 0.5 = the middle). After a turn (Deflect) the plugin starts the streak at the turn point.
function Deagle:TracerNative(N, id, r)
	local trail, c = r.trail, self.cfg.tracer
	if not trail or not self.cfg.tracers or not (N and N.tracer) then return end
	local fx = c.fx == "auto" and autoFx(self.frameDt) or c.fx
	r.traceFx = fx
	local o = trail.off
	N.tracer(id, fx, c.length, trail.reach, o.x, o.y, o.z, c.start, c.dash, c.anchor == "start" and 1 or c.anchor == "end" and 0 or 0.5)
end

function Deagle:MuzzlePos()
	return self:VmBone(self.gun.bones.muzzle) or vadd(System.GetViewCameraPos(), vscale(System.GetViewCameraDir(), 0.5))
end
