-- In game (tools/live.sh tools/ik_probe.lua, gun drawn): how far each hand sits from its Ready or Not IK target over 5 s
-- of play, every frame. With the chrparams' animation-driven IK working, both stay under ~1 mm while idling, walking,
-- jumping or crouching; without it the layers slide the gun 1-24 mm in the hands (tools/ron/ron_import.py ADIK).
local vm = Deagle.vmId and System.GetEntity(Deagle.vmId)
if not vm then System.LogAlways("[Deagle] IKPROBE: no viewmodel (draw the gun)") return end
local pairs_ = { { "hand_RI", "ik_hand_weapon_static_RI" }, { "hand_LE", "ik_hand_weapon_static_LE" } }
local worst, sum, n, t0 = { 0, 0 }, { 0, 0 }, 0, System.GetCurrTime()
local function d(a, b) return math.sqrt((a.x - b.x) ^ 2 + (a.y - b.y) ^ 2 + (a.z - b.z) ^ 2) * 1000 end
local function tick()
	for i, p in ipairs(pairs_) do
		local ok1, h = pcall(vm.GetBonePos, vm, p[1]); local ok2, t = pcall(vm.GetBonePos, vm, p[2])
		if not (ok1 and ok2 and h and t) then System.LogAlways("[Deagle] IKPROBE: no bone " .. p[1] .. " / " .. p[2]) return end
		local mm = d(h, t); worst[i] = math.max(worst[i], mm); sum[i] = sum[i] + mm
	end
	n = n + 1
	if System.GetCurrTime() - t0 < 5 then Script.SetTimer(1, tick) return end
	System.LogAlways(string.format("[Deagle] IKPROBE %d frames: right hand %.2f mm max (%.2f mean), left %.2f mm max (%.2f mean)",
		n, worst[1], sum[1] / n, worst[2], sum[2] / n))
end
tick()
