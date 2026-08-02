#ifndef SHL_SCENE_H
#define SHL_SCENE_H

#include "extdll.h"
#include "monsters.h"
#include "shl_npc_recovery.h"

enum SHLSceneType
{
	SHL_SCENE_NONE = 0,
	SHL_SCENE_DEBUG,
	SHL_SCENE_GROUNDED_GRAB
};

enum SHLSceneEndReason
{
	SHL_SCENE_END_NONE = 0,
	SHL_SCENE_END_TIMER,
	SHL_SCENE_END_FORCED,
	SHL_SCENE_END_PLAYER_DIED,
	SHL_SCENE_END_MONSTER_DIED,
	SHL_SCENE_END_STALE_SLOT
};

void SHL_InitSceneSystem();
void SHL_InitNpcRecoverySystem();

void SHL_StartDebugScene(edict_t* pPlayer);
void SHL_EndDebugScene(edict_t* pPlayer);

bool SHL_TryStartGroundedGrabScene(CBaseMonster* pMonster, edict_t* pPlayer, float duration);
bool SHL_IsMonsterSceneOwner(CBaseMonster* pMonster);

void SHL_EndPlayerScene(edict_t* pPlayer, int reason);
void SHL_SceneThink();

bool SHL_IsPlayerInScene(edict_t* pPlayer);
bool SHL_IsPlayerScenePaused(edict_t* pPlayer);
void SHL_SetPlayerScenePaused(edict_t* pPlayer, bool paused);
bool SHL_TogglePlayerScenePaused(edict_t* pPlayer);
int SHL_GetPlayerSceneType(edict_t* pPlayer);

float SHL_GetPlayerSceneElapsed(edict_t* pPlayer);
float SHL_GetPlayerSceneRemaining(edict_t* pPlayer);
float SHL_GetPlayerSceneProgress(edict_t* pPlayer);
float SHL_GetPlayerSceneDuration(edict_t* pPlayer);

float SHL_NormalNpcRecoveryDuration();

const char* SHL_SceneTypeName(int sceneType);
const char* SHL_SceneEndReasonName(int reason);

bool SHL_IsMonsterInSceneNpcRecovery(CBaseEntity* pMonster);
bool SHL_ApplyMonsterSceneRecovery(CBaseAnimating* pMonster);
bool SHL_IsMonsterRegrabBlocked(CBaseEntity* pMonster);

bool SHL_IsMonsterActionLocked(CBaseEntity* pMonster);
bool SHL_IsMonsterGrabBlocked(CBaseEntity* pMonster);

CBaseEntity* SHL_GetPlayerSceneOwner(edict_t* pPlayer);

bool SHL_AddSceneEscapeMash(edict_t* pPlayer);
float SHL_GetPlayerSceneEscapeProgress(edict_t* pPlayer);

bool SHL_GetPlayerSceneAnchor(edict_t* pPlayer, Vector& origin, float& yaw);

#endif // SHL_SCENE_H