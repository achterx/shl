#ifndef SHL_CONCUSSION_H
#define SHL_CONCUSSION_H

#include "extdll.h"

void SHL_InitPlayerConcussion(edict_t* pPlayer);
void SHL_AddPlayerConcussion(edict_t* pPlayer, float amount);
void SHL_SetPlayerConcussion(edict_t* pPlayer, float amount);
float SHL_GetPlayerConcussion(edict_t* pPlayer);

void SHL_PlayerConcussionThink(edict_t* pPlayer);

#endif // SHL_CONCUSSION_H