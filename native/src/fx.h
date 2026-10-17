#pragma once
// Tracer emitters for native callers (Claude 5's round sim): the same slots the Lua "fxr" command fills (main.cpp).
// Main thread only, like the command handler and the camera step that let done slots go.
// - id: Lua's rounds are 1..1,000,000; native ones FX_TRACE_BASE + their round id, so the two never share a slot.
// - effect: a preloaded tracer piece (deagle_tracers.trail_025 / _040 / _060); scale = the piece count (length / dash),
//   as fxr's scale field. pos / dir: where the piece sits and the way it points (+y along dir).
// - One emitter per round, moved and restarted every frame; a slot no one restarts for 0.25 s of game time is let go.
// - FX_TRACE_MAX slots in all, Lua's and native's together: emitters cost GPU descriptors, and ~1024 alive ran KCD2's
//   DX12 descriptor heap out (crash 2026-10-15). Past it a round gets no tracer (returns false, logged): more streaks
//   than that need one emitter per effect firing a particle per round (EmitParticle), not more emitters.
#define FX_TRACE_BASE 2000000
#define FX_TRACE_MAX 128
bool Fx_TracerRestart(int id, const char* effect, float scale, const float pos[3], const float dir[3]);
int Fx_TracersInUse();
