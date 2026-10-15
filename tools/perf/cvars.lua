-- tools/live.sh tools/perf/cvars.lua: which CryEngine profiling cvars KCD2 kept (read-only, System.GetCVar).
-- read-only: which CryEngine profiling cvars / commands KCD2 kept (GetCVar returns nil for missing ones)
local names = { "r_DisplayInfo", "r_Stats", "r_ProfileGPU", "r_GPUTimers", "profile", "profile_peak", "profile_graph", "profile_filter",
	"profile_allthreads", "sys_budget", "sys_budget_sysmem", "e_ParticlesDebug", "e_ParticlesMaxScreenFill", "e_StatoscopeEnabled",
	"e_StatoscopeDataGroups", "e_StatoscopeLogDestination", "e_DebugDraw", "p_profile_entities", "p_profile", "ai_DebugDraw",
	"sys_profiler", "sys_profile_sampler", "sys_streaming_debug", "r_FlushToGPU", "sys_MaxFPS", "sys_maxfps", "r_VSync", "e_Decals",
	"e_DecalsMaxTrisInObject", "e_DecalsOverlapping", "e_DecalsLifeTimeScale", "e_ParticlesPoolSize", "e_ParticlesMaxDrawScreen",
	"s_DrawAudioDebug", "s_DebugDraw", "sys_memory_debug", "memstats", "sys_enable_budgetmonitoring", "e_ParticlesThread",
	"e_ParticlesDebugCount", "r_DisplayInfoGraph", "t_FixedStep", "sys_PhysicsCPU", "e_ParticlesLod", "sys_job_system_enable",
	"sys_job_system_profiler", "sys_vtune", "e_ParticlesMinDrawPixels" }
local have, missing = {}, {}
for _, n in ipairs(names) do
	local ok, v = pcall(System.GetCVar, n)
	if ok and v ~= nil then have[#have + 1] = n .. "=" .. tostring(v) else missing[#missing + 1] = n end
end
System.LogAlways("[Deagle] perf cvars HAVE: " .. table.concat(have, " "))
System.LogAlways("[Deagle] perf cvars MISSING: " .. table.concat(missing, " "))
