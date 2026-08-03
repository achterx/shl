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

		// Current slot0 / owner monster anchor.
		// Replace these numbers with your final editor-tuned values.
		{8.0f, 0.0f, 0.0f, 0.0f, 180.0f, false}, // monsterAnchor legacy slot0

		// Future multi-NPC slots. Not used by gameplay yet.
		{
			{8.0f, 0.0f, 0.0f, 0.0f, 180.0f, false},  // slot0 owner
			{8.0f, 28.0f, 0.0f, 0.0f, 180.0f, false}, // slot1 reserved
			{8.0f, -28.0f, 0.0f, 0.0f, 180.0f, false} // slot2 reserved
		}
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

bool SHL_GetMonsterSceneSlotAnchor(
	const shl_monster_scene_profile_t* pProfile,
	int slot,
	shl_scene_actor_anchor_t& outAnchor)
{
	if (pProfile == nullptr)
		return false;

	if (slot < 0 || slot >= SHL_MONSTER_SCENE_MAX_SLOTS)
		return false;

	outAnchor = pProfile->monsterSlotAnchors[slot];
	return true;
}