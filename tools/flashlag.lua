-- tools/flashlag.lua (Claude 4): which way can KCD2 show a particle flash on the shot's own frame?
-- Run while the game window is recorded at 60 fps (um win record), the gun drawn in first person, Henry standing
-- still, the mouse untouched: tools/live.sh tools/flashlag.lua. No shot is fired (no round, sound or recoil).
-- Each trial shows the gun's flash card (the mesh flash: drawn on the frame it's shown, the bar) and, in the same Lua
-- call, a candidate particle flash 0.3 m left of and 0.1 m above the muzzle. The video gives the frames from the card
-- to the candidate. Candidates, one block each (all on the installed plugin, no new code):
--   new     a new emitter per trial (flashfx), the known-late way: the control (it must lag, or the method is wrong)
--   seed    the same with a box written around it at once (flashseed 0.3)
--   restart one emitter restarted per trial (fxr, the tracers' way; its first trial makes it: not counted)
--   live    one never-stopping emitter shown by its size (flasharm / flashshot, flashMode "live")
-- Top left, every frame: "#" in one of 4 slots, slot = engine frame id % 4, so the video counts game frames even when
-- the capture repeats or drops one. Log: "[Deagle] FLASHLAG <pass> <block> <trial> frame <id>".
-- Pass 1 at the game's frame rate, pass 2 with sys_MaxFPS 30 (put back after). About 35 s in all.
-- Known leftover: the live block's emitter can't be stopped (no Kill found, MODLOG 2026-10-12), so after flashdisarm it
-- stays where it was at a size of nothing (1e-4): invisible, until the game closes.
local D = Deagle
FLASHLAG = FLASHLAG or {}
local T = FLASHLAG
T.token = (T.token or 0) + 1
T.done = nil
local tok = T.token
local function log(s) System.LogAlways("[Deagle] FLASHLAG " .. s) end

local TRIALS, GAP, REST = 12, 0.2, 0.8   -- trials a block, s between trials, s between blocks (game time)
local OFF = { x = -0.3, z = 0.1 }        -- the candidate, in the viewmodel's axes from the muzzle: left, up
local FX, LIVE_FX, RESTART_ID = D.cfg.muzzleFx, D.cfg.muzzleFxLive, 1900001
local BLOCKS = { "mesh", "new", "seed", "restart", "live" }

local function marker()
	if T.token ~= tok or T.done then return end
	local f = System.GetFrameID()
	pcall(System.DrawText, 40 + (f % 4) * 90, 40, "#", "default", 5, 0, 1, 1, 1, 1)
	Script.SetTimer(0, marker)
end

local function scale()
	local c, phys = D.cfg, D:ShotPhysics()
	return (phys and c.physics.flashPerUnit * phys.blast or 1) * c.muzzleFxScale
end

-- the muzzle in the viewmodel's axes, the candidate's place there, and both in the world
local function where()
	local vm = D.vmId and System.GetEntity(D.vmId)
	if not vm then return nil end
	local ok, lp, ld = pcall(D.MuzzleLocal, D, vm)
	if not ok then return nil end
	local cp = { x = lp.x + OFF.x, y = lp.y, z = lp.z + OFF.z }
	local o, X, Y, Z = vm:GetWorldPos(), vm:GetDirectionVector(0), vm:GetDirectionVector(1), vm:GetDirectionVector(2)
	local function w(v, base)
		return { x = (base and base.x or 0) + X.x * v.x + Y.x * v.y + Z.x * v.z,
			y = (base and base.y or 0) + X.y * v.x + Y.y * v.y + Z.y * v.z,
			z = (base and base.z or 0) + X.z * v.x + Y.z * v.y + Z.z * v.z }
	end
	return cp, ld, w(cp, o), w(ld)
end

local function cmd(fmt, ...) D:Native(string.format(fmt, ...)) end

local function trial(pass, block, i)
	local cp, ld, wp, wd = where()
	if not cp then log(pass .. " " .. block .. " " .. i .. " no muzzle: skipped"); return end
	local s = scale()
	D:FlashMeshShot()
	if block == "new" or block == "seed" then
		cmd("flashfx %s %.4f %.4f %.4f %.4f %.4f %.4f %.4f", FX, s, cp.x, cp.y, cp.z, ld.x, ld.y, ld.z)
	elseif block == "restart" then
		cmd("fxr %d %s %.4f 1 %.4f %.4f %.4f %.4f %.4f %.4f", RESTART_ID, FX, s, wp.x, wp.y, wp.z, wd.x, wd.y, wd.z)
	elseif block == "live" then
		D:Native("flashshot " .. table.concat(D.cfg.flashLive, " "))
	end
	log(string.format("%s %s %d frame %d", pass, block, i, System.GetFrameID()))
end

-- a block's set-up before its first trial, and its clean-up after the last
local function blockStart(block)
	if block == "new" then D:Native("flashseed 0")
	elseif block == "seed" then D:Native("flashseed 0.3")
	elseif block == "restart" then D:Native("flashseed 0")
	elseif block == "live" then
		local cp, ld = where()
		if cp then cmd("flasharm %s %.4f %.4f %.4f %.4f %.4f %.4f %.4f", LIVE_FX, scale(), cp.x, cp.y, cp.z, ld.x, ld.y, ld.z) end
	end
end
local function blockEnd(block)
	if block == "live" then D:Native("flashdisarm") end
end

local maxWas = System.GetCVar and System.GetCVar("sys_MaxFPS")
local plan = {}
for _, pass in ipairs({ "p1", "p2" }) do
	for _, b in ipairs(BLOCKS) do plan[#plan + 1] = { pass = pass, block = b } end
end

local function run(k, i)
	if T.token ~= tok then return end
	local step = plan[k]
	if not step then
		T.done = true
		if maxWas ~= nil then System.SetCVar("sys_MaxFPS", tostring(maxWas)) end
		D:Native("flashseed 0")
		log("done (sys_MaxFPS back to " .. tostring(maxWas) .. ")")
		return
	end
	if i == 0 then
		if step.pass == "p2" and step.block == BLOCKS[1] then
			if maxWas == nil then log("no sys_MaxFPS: pass 2 skipped"); return run(#plan + 1, 0) end
			System.SetCVar("sys_MaxFPS", "30")
		end
		log(string.format("%s %s start frame %d sys_MaxFPS %s", step.pass, step.block, System.GetFrameID(), tostring(System.GetCVar and System.GetCVar("sys_MaxFPS"))))
		blockStart(step.block)
		Script.SetTimer(math.floor(REST * 1000), function() run(k, 1) end)
		return
	end
	trial(step.pass, step.block, i)
	if i < TRIALS then
		Script.SetTimer(math.floor(GAP * 1000), function() run(k, i + 1) end)
	else
		Script.SetTimer(math.floor(REST * 1000), function() blockEnd(step.block); run(k + 1, 0) end)
	end
end

if not (D.native and D.vmShown and D.vmInFp and D.flashMesh) then
	log("needs the plugin and the gun drawn in first person: nothing run")
else
	log(string.format("start: fx %s, live %s, card look %s, sys_MaxFPS %s", FX, LIVE_FX, D.flashMesh.look.look, tostring(maxWas)))
	marker()
	run(1, 0)
end
