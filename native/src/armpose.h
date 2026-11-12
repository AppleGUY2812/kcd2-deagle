#pragma once
#include <cstdint>
// Henry's own arms in the gun viewmodel's arm pose (armpose.cpp; docs/ARMSWAP.md "Posing Henry's real arms").
// Command (from Command() in main.cpp): armpose 0|1|2|status
//   0 off; 1 his arm joints are written after the camera (skin and sleeves exact); 2 as 1, and the pose is queued in
//   his pose job so cloth collision, cloth simulation and hand attachments follow (one frame old against the camera).
void ArmPose_Install();                                   // once, after MH_Initialize: the pose job hook (WHGame 1.5.6 only)
// Main thread, from the camera hook after the viewmodel is placed (env 0: it is not shown). newFrame false: the
// same frame's camera again (the write is repeated against the viewmodel's new place; nothing else moves on).
void ArmPose_AfterCamera(uintptr_t env, uint32_t vmId, bool newFrame);
bool ArmPose_Command(const char* cmd, const char* rest);  // true when cmd was "armpose"
bool ArmPose_On();
