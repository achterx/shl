#ifndef SHL_SCENE_EDITOR_H
#define SHL_SCENE_EDITOR_H

#include "shl_monster_scene_profile.h"

enum SHLSceneEditorTarget
{
	SHL_SCENE_EDITOR_TARGET_PLAYER = -1,
	SHL_SCENE_EDITOR_TARGET_SLOT0 = 0,
	SHL_SCENE_EDITOR_TARGET_SLOT1,
	SHL_SCENE_EDITOR_TARGET_SLOT2,
	SHL_SCENE_EDITOR_TARGET_SLOT3,
	SHL_SCENE_EDITOR_TARGET_SLOT4,
	SHL_SCENE_EDITOR_TARGET_SLOT5,
	SHL_SCENE_EDITOR_TARGET_SLOT6,
	SHL_SCENE_EDITOR_TARGET_SLOT7
};

void SHL_SceneEditorThink();

bool SHL_SceneEditorClientCommand(edict_t* pPlayer, const char* pszCommand);
bool SHL_SceneEditorIsEnabled(edict_t* pPlayer);

bool SHL_SceneEditorResolveAnchor(
	edict_t* pPlayer,
	int target,
	const shl_scene_actor_anchor_t& baseAnchor,
	shl_scene_actor_anchor_t& outAnchor);

#endif