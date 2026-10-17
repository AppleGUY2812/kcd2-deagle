-- tools/live.sh tools/probe/ticks.lua (Claude 5, 2026-10-17): are the rounds in flight stepped every frame? Claude 1's
-- profiles: at 250 sky with maxRounds 1024 StepRound ran ~27 times a frame, while rounds fly ~2-3 s to the 1000 m range
-- (ends probe: those that end, end there), so ~500+ should be alive. Start it, then within a few seconds run
-- deagle_lua Deagle:Stress('250 sky'). For SECONDS it counts launches, rounds dropped by maxRounds, RoundsTick runs,
-- the rounds each run steps, the gaps between runs, and the chains started (LaunchRound restarts a dead chain after
-- 0.5 s), then restores the functions and logs "[Deagle] probe ticks: ...". In memory only (ballistics.lua is Claude 4's).
local SECONDS = 4
local D = Deagle
if D.probeTicks then D.LaunchRound, D.RoundsTick = D.probeTicks.launch, D.probeTicks.tick end   -- a rerun: unwrap first
local launch, tick = D.LaunchRound, D.RoundsTick
local clock = System.GetCurrAsyncTime
local P = { launch = launch, tick = tick, t0 = clock(), launches = 0, dropped = 0, landedAtLaunch = 0, chains = 0,
	ticks = 0, stale = 0, stepped = 0, maxLive = 0, lastTick = nil, gaps = { 0, 0, 0, 0 } }   -- gaps: <25 ms, <100, <500, >=500
D.probeTicks = P
local function report()
	D.LaunchRound, D.RoundsTick, D.probeTicks = launch, tick, nil
	local s = clock() - P.t0
	System.LogAlways(string.format("[Deagle] probe ticks: %.2f s, cap %s: %d launches (%d landed in their launch frame), %d dropped by the cap, "
		.. "%d chains started", s, tostring(D.cfg.impactCap.maxRounds), P.launches, P.landedAtLaunch, P.dropped, P.chains))
	System.LogAlways(string.format("[Deagle] probe ticks: RoundsTick %d runs (%.1f a second; %d with a stale token), %.1f rounds stepped a run, "
		.. "most alive %d; gaps between runs <25 ms %d, 25-100 %d, 100-500 %d, >=500 %d", P.ticks, P.ticks / s, P.stale,
		P.stepped / math.max(1, P.ticks - P.stale), P.maxLive, P.gaps[1], P.gaps[2], P.gaps[3], P.gaps[4]))
end

D.LaunchRound = function(self, ...)
	local before, token = #(self.rounds or {}), self.roundsToken
	local r = launch(self, ...)
	local after = #(self.rounds or {})
	P.launches = P.launches + 1
	if not self.rounds or self.rounds[after] ~= r then P.landedAtLaunch = P.landedAtLaunch + 1   -- never inserted
	else P.dropped = P.dropped + (before + 1 - after) end                                        -- inserted; the cap took the oldest
	if self.roundsToken ~= token then P.chains = P.chains + 1 end
	P.maxLive = math.max(P.maxLive, after)
	if clock() - P.t0 > SECONDS and D.probeTicks == P then report() end
	return r
end
D.RoundsTick = function(self, token)
	local now = clock()
	if token ~= self.roundsToken then P.stale = P.stale + 1
	else
		P.stepped = P.stepped + #(self.rounds or {})
		if P.lastTick then
			local g = now - P.lastTick
			local i = g < 0.025 and 1 or g < 0.1 and 2 or g < 0.5 and 3 or 4
			P.gaps[i] = P.gaps[i] + 1
		end
		P.lastTick = now
	end
	P.ticks = P.ticks + 1
	return tick(self, token)
end
System.LogAlways(string.format("[Deagle] probe ticks: counting for %d s; start the stress now", SECONDS))
