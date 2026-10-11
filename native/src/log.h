#pragma once
// Plain text log next to the .asi (Bin/Win64MasterMasterSteamPGO/deagle_native.log), flushed per line.
void LogInit(void* module);
void Log(const char* fmt, ...);
