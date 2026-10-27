#pragma once
#include <string>
#include <vector>

// The player's kept settings (Lua has no file access in KCD2): <file> next to the plugin, one "<key> <value>" per line
// in the order first set. The mod folder is replaced on every install; the Bin folder isn't. The replay command is the
// plugin's own: deagle_native keeps "deagle_set" / deagle_settings.txt, inventory.asi uses "inv_set" /
// inventory_settings.txt.
void SettingsInit(void* module, const char* file = "deagle_settings.txt", const char* setCmd = "deagle_set");   // read the file
void SettingsSave(const char* key, const char* value);      // remember one; the file is rewritten when it changed
void SettingsReset();                                       // forget them all (the file goes)
std::vector<std::string> SettingsReplay();                  // "<setCmd> <key> <value>" for each, in order
