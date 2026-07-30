#ifndef SHL_NPC_RECOVERY_H
#define SHL_NPC_RECOVERY_H

#include "extdll.h"
#include "cbase.h"

void SHL_InitNpcRecoverySystem();

void SHL_StartNpcRecovery(CBaseEntity* pEntity, float duration);
void SHL_StartNpcRecovery(CBaseEntity* pEntity, float duration, const char* pszHoldSequence);

bool SHL_IsNpcRecovering(CBaseEntity* pEntity);
float SHL_GetNpcRecoveryRemaining(CBaseEntity* pEntity);

// Call this at the very top of monster PrescheduleThink().
// Returns true while the NPC is locked in recovery.
bool SHL_ApplyNpcRecovery(CBaseAnimating* pAnimating);

#endif