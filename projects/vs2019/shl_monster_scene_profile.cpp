#include "extdll.h"
#include "util.h"
#include "cbase.h"

#include "shl_scene.h"
#include "shl_monster_scene_profile.h"

static const shl_monster_scene_profile_t g_SHLNormalGroundedGrab =
	{
		"monster_shl_normal",
		SHL_SCENE_GROUNDED_GRAB,
		"normal_grounded_grab",

		"models/shl/player_grounded_scene.mdl",

		"shl_grab_start",
		"shl_grab_loop",
		"shl_climax",

		"shl_grab_start",
		"shl_grab_loop",
		"shl_grab_climax",
		"shl_npc_climax",

		"shl_escape_knockdown",
		"shl_recovery_getup",
		4.31f,
		1.60f,

		// Player scene actor anchor.
		{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false}, // player

		// Monster anchor.
		// Smaller forward = more centered over player.
		{8.0f, 0.0f, 0.0f, 0.0f, 180.0f, true}
};

const shl_monster_scene_profile_t* SHL_GetMonsterSceneProfile(
	CBaseEntity* pMonster,
	int sceneType)
{
	if (pMonster == nullptr)
		return nullptr;

	const char* pszClassname = STRING(pMonster->pev->classname);

	if (pszClassname == nullptr)
		return nullptr;

	if (sceneType == SHL_SCENE_GROUNDED_GRAB &&
		FStrEq(pszClassname, "monster_shl_normal"))
	{
		return &g_SHLNormalGroundedGrab;
	}

	return nullptr;
}