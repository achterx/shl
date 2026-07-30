#ifndef SHL_SAVEGUARD_H
#define SHL_SAVEGUARD_H

#include "extdll.h"

enum SHLUnsafeSaveFlags
{
	SHL_UNSAFE_NONE = 0,
	SHL_UNSAFE_ACTIVE_SCENE = 1 << 0,
	SHL_UNSAFE_GRABBED = 1 << 1,
	SHL_UNSAFE_GROUNDED = 1 << 2,
	SHL_UNSAFE_CLIMAX = 1 << 3,
	SHL_UNSAFE_DEFEAT_MENU = 1 << 4,
	SHL_UNSAFE_DEFEAT_SCENE = 1 << 5,
	SHL_UNSAFE_DEBUG_TEST = 1 << 6
};

void SHL_InitSaveGuardForPlayer(edict_t* pEntity);
void SHL_ClearUnsafeSaveFlags(edict_t* pEntity);
void SHL_SetUnsafeSaveFlag(edict_t* pEntity, int flag);
void SHL_ClearUnsafeSaveFlag(edict_t* pEntity, int flag);
void SHL_OnPlayerLoadCleanup(edict_t* pEntity);

bool SHL_HasUnsafeSaveFlag(edict_t* pEntity, int flag);
bool SHL_CanSaveNow(edict_t* pEntity);

int SHL_GetUnsafeSaveFlags(edict_t* pEntity);
void SHL_DebugPrintSaveGuard(edict_t* pEntity);

void SHL_SaveGuardThink();

#endif // SHL_SAVEGUARD_H