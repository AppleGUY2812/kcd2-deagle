#pragma once
// Frame rate in play (framelog.cpp): fps a minute and each slow second, plus our own tick's cost.
void Framelog_Install(const char* dir);               // dir ends with a backslash
long long Framelog_TickStart();                       // before the plugin's frame tick
void Framelog_TickEnd(long long t0, int levelState);  // after it
