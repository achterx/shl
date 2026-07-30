#ifndef SHL_SCENE_ACTOR_H
#define SHL_SCENE_ACTOR_H

#include "extdll.h"

CBaseEntity* SHL_CreatePlayerSceneActor(edict_t* pPlayer, const char* pszModelName);
void SHL_RemoveSceneActor(CBaseEntity* pActor);

#endif // SHL_SCENE_ACTOR_H