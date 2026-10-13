#pragma once
// Ready or Not's own FMOD Studio 2.03.10, run live inside KCD2 (Mods/deagle/ron: fmor.dll, fmorstudio.dll, banks;
// made by tools/sound/make_ron_engine.py). Every gun sound is RoN's real event, with RoN's buses, limiters and room
// snapshots, mixed by RoN's engine.
#include <string>

// dir ends in a backslash. wavOut: render offline into that WAV instead of the speakers (the coexistence test).
bool RonInit(const std::string& dir, const char* wavOut = nullptr);
bool RonReady();
void RonRelease();
// every camera step: the listener (camera 3x4, CryEngine axes) and the engine's update
void RonUpdate(const float* cam);
// play an event once. params "name=value name=value" (event parameters; a name the event doesn't have is set as a
// global). at = world position for 3D events, nullptr = at the listener. Returns false if the event is unknown.
bool RonPlay(const char* event, const char* params, const float* at, float vol = 1.0f);
// load an event's audio now (it loads in the background), so its first play has no delay
bool RonLoad(const char* event);
// a 3D event at a world point, heard after the sound's travel time from there to the listener (343 m/s, stretched by
// the game speed); "PropagateDistance=@" in params becomes that distance in m (RoN's impact events fade by it)
bool RonPlayAt(const char* event, const char* params, const float* at, float vol);
void RonGlobal(const char* name, float value);
// start (on) or stop (fade out as authored) a snapshot or any looping event, e.g. snapshot:/Reverb_IN_Small01
bool RonHold(const char* event, bool on);
void RonSpeed(float speed);    // game speed (slow motion): the whole engine plays at it
void RonVolume(float vol);     // the final fader
// test helper: run n engine updates (offline rendering advances 1024 samples per update)
void RonStep(int n);
