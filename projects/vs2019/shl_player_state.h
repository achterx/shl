#ifndef SHL_PLAYER_STATE_H
#define SHL_PLAYER_STATE_H

#include "extdll.h"
#include "shl_defs.h"

struct shl_player_state_t
{
	bool initialized;

	float shlHP;
	float stimulation;

	int climaxCount;
	bool defeatPending;

	int playerState;
};

void SHL_InitPlayerState(edict_t* pEntity);
void SHL_ResetPlayerState(edict_t* pEntity);

void SHL_AddPlayerStimulation(edict_t* pEntity, float amount);
void SHL_SetPlayerStimulation(edict_t* pEntity, float amount);
void SHL_SetPlayerSHLHP(edict_t* pEntity, float amount);
void SHL_ForcePlayerClimax(edict_t* pEntity);
void SHL_StartPlayerClimax(edict_t* pEntity);
void SHL_FinishPlayerClimax(edict_t* pEntity);
void SHL_PlayerThink(edict_t* pEntity);

shl_player_state_t* SHL_GetPlayerState(edict_t* pEntity);
void SHL_DebugPrintPlayerState(edict_t* pEntity);

void SHL_SetPlayerState(edict_t* pEntity, int playerState);
int SHL_GetPlayerStateId(edict_t* pEntity);
bool SHL_IsPlayerInUnsafeState(edict_t* pEntity);
bool SHL_ShouldBlockPlayerInput(edict_t* pEntity);

#endif // SHL_PLAYER_STATE_H