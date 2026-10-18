#pragma once
#include <string>
// deagle_points: read or change Henry's unspent perk points per skill / stat (perkpoints.cpp, Claude 1).
// rest: "" (list) | "<skill or stat> <n>" (set) | "<skill or stat> +n / -n" (add). Returns the line to show the user.
std::string Points_Command(const char* rest);
// deagle_respec <skill>: "REMOVE <label> <guid>..." for Lua to remove (deagle_respec_remove), or a message.
std::string Respec_Command(const char* rest);
// after Lua removed them: counts the perks really gone and adds that many points; the message for the user.
std::string Respec_Refund();
