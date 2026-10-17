-- tools/live.sh tools/probe/ends.lua (Claude 5, 2026-10-17): where do stress rounds end? Claude 1's run B: at 75 sky
-- rounds live ~1.5 s, at 250+ ~0.1 s with ~16 Impact calls a frame. Start it, then within a few seconds run
-- deagle_lua Deagle:Stress('250 sky'). For SECONDS of game time it tallies every round that ends (StepRound true):
-- HIT (an Impact said stop) or END, by surface, entity, distance flown and view pitch at the end; then it restores
-- the original functions and logs "[Deagle] probe ends: ..." lines. Read-only: wraps Deagle.StepRound / Deagle.Impact
-- in memory only (ballistics.lua is Claude 4's; nothing on disk changes). Run it again to restart the tally.
local SECONDS = 4
local D = Deagle
if D.probeEnds then D.StepRound, D.Impact = D.probeEnds.step, D.probeEnds.impact end   -- a rerun: unwrap first
local step, impact = D.StepRound, D.Impact
local P = { step = step, impact = impact, t0 = System.GetCurrAsyncTime(), ends = 0, hits = {}, ended = {}, last = nil }
D.probeEnds = P
local function bucket(m) return m < 2 and "<2m" or m < 10 and "2-10m" or m < 50 and "10-50m" or m < 200 and "50-200m" or ">200m" end
local function bump(t, k) t[k] = (t[k] or 0) + 1 end

D.Impact = function(self, hit, dir, phys, r)
	local on = impact(self, hit, dir, phys, r)
	local e = hit.entity
	P.last = string.format("%s|%s|hitdist %s", tostring(D.U.surfaceName(hit)),
		e and (tostring(e.class) .. ":" .. tostring(e.GetName and e:GetName())) or "static", bucket(hit.dist or 0))
	if not on then bump(P.hits, P.last) end
	return on
end
D.StepRound = function(self, r, dt)
	P.last = nil
	local done = step(self, r, dt)
	if done then
		P.ends = P.ends + 1
		local z = System.GetViewCameraDir().z
		bump(P.ended, string.format("%s flown %s pitch %s", r.landed and "HIT" or "END", bucket(r.dist or 0),
			z > 0.9 and "up>0.9" or z > 0.5 and "up0.5-0.9" or "below0.5"))
	end
	if System.GetCurrAsyncTime() - P.t0 > SECONDS and D.probeEnds == P then
		D.StepRound, D.Impact, D.probeEnds = step, impact, nil
		local function dump(name, t)
			local rows = {}
			for k, n in pairs(t) do rows[#rows + 1] = { k, n } end
			table.sort(rows, function(a, b) return a[2] > b[2] end)
			for i = 1, math.min(12, #rows) do
				System.LogAlways(string.format("[Deagle] probe ends: %s %5d  %s", name, rows[i][2], rows[i][1]))
			end
		end
		System.LogAlways(string.format("[Deagle] probe ends: %d rounds ended in %d s", P.ends, SECONDS))
		dump("end", P.ended)
		dump("hit", P.hits)
	end
	return done
end
System.LogAlways(string.format("[Deagle] probe ends: tallying for %d s; start the stress now", SECONDS))
