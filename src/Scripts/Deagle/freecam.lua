-- Free camera (Claude 4): flying the view away from Henry while time runs.
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U
local vadd, vscale, vsub = U.vadd, U.vscale, U.vsub

-- ---------------------------------------------------------------------------
-- free camera look (the plugin flies the camera: freecam.cpp). While it's on, KCD2's own "no_camera" filter keeps the
-- mouse from turning Henry, and the mouse's raw movement (our deagle_look_x / _y on maxis_x / _y) turns the camera;
-- holding right mouse lifts the filter so the mouse aims Henry (the camera stays). A line from Henry's eye shows where a
-- shot goes. The plugin tells us when it starts and stops (deagle_freecam_state). (Snapping Henry's aim back from Lua
-- every frame lagged a frame and the camera spun, in game 2026-10-12.)
-- The mouse-to-angle scale is learned from Henry's own turning while the free camera is off (radians per unit, with
-- its sign), so the camera turns like the game does.
-- ---------------------------------------------------------------------------
local function viewAngles()
	local d = System.GetViewCameraDir()   -- Henry's real eye, not the free camera
	return math.asin(math.max(-1, math.min(1, d.z))), math.atan2(-d.x, d.y)
end
local function wrap(a) return (a + math.pi) % (2 * math.pi) - math.pi end
Deagle.lookScale = Deagle.lookScale or { x = { sv = 0, sa = 0 }, y = { sv = 0, sa = 0 } }
function Deagle:FreecamLook(action, v)
	local axis = action == "deagle_look_x" and "x" or "y"
	local L = self.lookScale[axis]
	if self.fc and not self.fcAim then
		if not L.k then return false end                     -- not learned yet (move the mouse once without the freecam)
		if axis == "x" then self.fc.camY = wrap(self.fc.camY + v * L.k)
		else self.fc.camP = math.max(-1.55, math.min(1.55, self.fc.camP + v * L.k)) end
		return false
	end
	if not self.fc and v ~= 0 then                          -- learn how far Henry turns per unit of mouse
		local p, y = viewAngles()
		local last = self.lookLast and self.lookLast[axis]
		if last then
			local da = axis == "x" and wrap(y - last.y) or (p - last.p)
			L.sv, L.sa = L.sv + v * v, L.sa + v * da         -- least squares through zero: k = sum(v da) / sum(v^2)
			if L.sv > 50 then L.k = L.sa / L.sv end
		end
		self.lookLast = self.lookLast or {}
		self.lookLast[axis] = { p = p, y = y }
	end
	return false
end
function Deagle:FreecamState(on)
	on = Deagle.Arg(on) ~= "0"
	if on then
		local p, y = viewAngles()
		self.fc = { camP = p, camY = y, token = {} }
		self:Native("nocamera " .. (self.fcAim and "0" or "1"))
		local tok = self.fc.token
		Script.SetTimer(0, function() Deagle:FreecamTick(tok) end)
		if not (self.lookScale.x.k and self.lookScale.y.k) then
			self:Notify("free camera: move the mouse once with it off, so it learns your mouse speed")
		end
	else
		self.fc = nil
		self:Native("nocamera 0")
		self:Native("freecam rotoff")
	end
end
function Deagle:FreecamTick(token)
	local fc = self.fc
	if not fc or fc.token ~= token then return end
	self:Native(string.format("freecam rot %.5f %.5f", fc.camP, fc.camY))
	-- where a shot goes: Henry's eye along his aim to the first thing it meets (or 150 m)
	local eye, d = System.GetViewCameraPos(), System.GetViewCameraDir()
	local hits, pl, reach = {}, self:Player(), 150
	local ok, n = pcall(Physics.RayWorldIntersection, eye, vscale(d, reach), 1, ent_all, pl and pl.id, nil, hits)
	if ok and n and n > 0 and hits[1] then reach = hits[1].dist end
	local tip = vadd(eye, vscale(d, reach))
	pcall(System.DrawLine, vadd(eye, vscale(d, 0.3)), tip, 1, 0.25, 0.1, 1)
	for _, a in ipairs({ { x = 0.15, y = 0, z = 0 }, { x = 0, y = 0.15, z = 0 }, { x = 0, y = 0, z = 0.15 } }) do
		pcall(System.DrawLine, vsub(tip, a), vadd(tip, a), 1, 0.25, 0.1, 1)
	end
	Script.SetTimer(0, function() Deagle:FreecamTick(token) end)
end
