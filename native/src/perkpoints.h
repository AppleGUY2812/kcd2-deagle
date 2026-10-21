#pragma once
#include <string>
#include "scan.h"
// deagle_points: read or change Henry's unspent perk points per skill / stat (perkpoints.cpp).
// rest: "" (list) | "<skill or stat> <n>" (set) | "<skill or stat> +n / -n" (add). Returns the line to show the user.
std::string Points_Command(const char* rest);
// deagle_respec <skill>: "REMOVE <label> <guid>..." for Lua to remove (deagle_respec_remove), or a message.
std::string Respec_Command(const char* rest);
// after Lua removed them: counts the perks really gone and adds that many points; the message for the user.
std::string Respec_Refund();
// once at start: the game's WUID -> soul lookup (an AOB in WHGame.dll)
void Points_Install(const Module& game);
// "points_soul <tostring(player.soul.__ThisWUID)>": Lua sends Henry's soul handle before every points / respec call
void Points_Soul(const char* rest);
