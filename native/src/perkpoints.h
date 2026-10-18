#pragma once
#include <string>
// deagle_points: read or change Henry's unspent perk points per skill / stat (perkpoints.cpp, Claude 1).
// rest: "" (list) | "<skill or stat> <n>" (set) | "<skill or stat> +n / -n" (add). Returns the line to show the user.
std::string Points_Command(const char* rest);
