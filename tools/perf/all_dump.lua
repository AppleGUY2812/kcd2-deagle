-- Ends tools/perf/all_on.lua: restores every function and logs per-frame calls and ms, idle vs shooting
-- ("[Deagle] CHECK4" lines in kcd.log).
local D = Deagle
local A = D._all
D.VmTick = A.vt
for i = #A.orig, 1, -1 do local o = A.orig[i]; o[1][o[2]] = o[3] end
D._all = nil
local fi, fs = math.max(1, A.frames.idle), math.max(1, A.frames.shoot)
local rows = {}
local seen = {}
for _, ph in ipairs({ "shoot", "idle" }) do
	for k in pairs(A.st[ph]) do
		if not seen[k] then
			seen[k] = true
			local i, s = A.st.idle[k] or { 0, 0 }, A.st.shoot[k] or { 0, 0 }
			rows[#rows + 1] = { k, i[1] / fi, s[1] / fs, i[2] * 1000 / fi, s[2] * 1000 / fs }
		end
	end
end
System.LogAlways(string.format("[Deagle] CHECK4 all frames: idle %d at %.1f ms, shooting %d at %.1f ms", A.frames.idle,
	A.frameMs.idle / fi, A.frames.shoot, A.frameMs.shoot / fs))
table.sort(rows, function(a, b) return (a[5] - a[4]) > (b[5] - b[4]) end)
local out = {}
for i = 1, math.min(30, #rows) do local r = rows[i]; out[#out + 1] = string.format("%s %.2f->%.2f ms (%.1f->%.1f calls)", r[1], r[4], r[5], r[2], r[3]) end
System.LogAlways("[Deagle] CHECK4 all per frame idle->shooting, most added time first: " .. table.concat(out, " | "))
table.sort(rows, function(a, b) return (a[3] - a[2]) > (b[3] - b[2]) end)
out = {}
for i = 1, math.min(15, #rows) do local r = rows[i]; out[#out + 1] = string.format("%s %.1f->%.1f calls", r[1], r[2], r[3]) end
System.LogAlways("[Deagle] CHECK4 all most added calls: " .. table.concat(out, " | "))
