#pragma once
#include <string>
#include <vector>

// The player's deagle_set settings kept across game starts (Lua has no file access in KCD2): deagle_settings.txt next
// to the plugin, one "<key> <value>" per line in the order first set. The mod folder is replaced on every install;
// the Bin folder isn't.
void SettingsInit(void* module);                            // read the file
void SettingsSave(const char* key, const char* value);      // remember one; the file is rewritten when it changed
void SettingsReset();                                       // forget them all (the file goes)
std::vector<std::string> SettingsReplay();                  // "deagle_set <key> <value>" for each, in order
