#ifndef SHL_CONTROL_H
#define SHL_CONTROL_H

#include "extdll.h"

void SHL_UpdatePlayerControl(edict_t* pEntity);
void SHL_ForceRestorePlayerControl(edict_t* pEntity);
bool SHL_GetPlayerSceneCameraCenterAngles(edict_t* pPlayer, float& pitch, float& yaw);

#endif // SHL_CONTROL_H