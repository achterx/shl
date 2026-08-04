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
		{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false},

		// Legacy slot0 anchor.
		{8.0f, 0.0f, 0.0f, 0.0f, 180.0f, false},

		// Per-slot anchors.
		{
			{8.0f, 0.0f, 0.0f, 0.0f, 180.0f, false},  // slot0 owner
			{8.0f, 28.0f, 0.0f, 0.0f, 180.0f, false}, // slot1 joiner
			{8.0f, -28.0f, 0.0f, 0.0f, 180.0f, false} // slot2 joiner
		},

		// Per-slot animation variation sets.
		{
			// slot0 owner variations
			{
				2,
				{{"slot0_default",
					 "shl_grab_start",
					 "shl_grab_loop",
					 "shl_grab_climax",
					 "shl_npc_climax"},
					{"slot0_alt_a",
						"shl_grab_start_alt",
						"shl_grab_loop_alt",
						"shl_grab_climax_alt",
						"shl_npc_climax_alt"}}},

			// slot1 joiner variations
			{
				2,
				{{"slot1_default",
					 "shl_slot1_start",
					 "shl_slot1_loop",
					 "shl_slot1_climax",
					 "shl_slot1_npc_climax"},
					{"slot1_alt_a",
						"shl_slot1_start_alt",
						"shl_slot1_loop_alt",
						"shl_slot1_climax_alt",
						"shl_slot1_npc_climax_alt"}}},

			// slot2 joiner variations
			{
				2,
				{{"slot2_default",
					 "shl_slot2_start",
					 "shl_slot2_loop",
					 "shl_slot2_climax",
					 "shl_slot2_npc_climax"},
					{"slot2_alt_a",
						"shl_slot2_start_alt",
						"shl_slot2_loop_alt",
						"shl_slot2_climax_alt",
						"shl_slot2_npc_climax_alt"}}}}};

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

bool SHL_GetMonsterSceneSlotAnimVariation(
	const shl_monster_scene_profile_t* pProfile,
	int slot,
	int variation,
	shl_monster_scene_slot_anim_variation_t& outAnim)
{
	if (pProfile == nullptr)
		return false;

	if (slot < 0 || slot >= SHL_MONSTER_SCENE_MAX_SLOTS)
		return false;

	const shl_monster_scene_slot_anim_set_t& animSet =
		pProfile->monsterSlotAnimSets[slot];

	if (animSet.variationCount <= 0)
		return false;

	if (variation < 0 || variation >= animSet.variationCount)
		variation = 0;

	if (variation >= SHL_MONSTER_SCENE_MAX_SLOT_ANIM_VARIANTS)
		variation = 0;

	outAnim = animSet.variations[variation];

	if (outAnim.startSequence == nullptr)
		outAnim.startSequence = pProfile->monsterStartSequence;

	if (outAnim.loopSequence == nullptr)
		outAnim.loopSequence = pProfile->monsterLoopSequence;

	if (outAnim.playerClimaxSequence == nullptr)
		outAnim.playerClimaxSequence = pProfile->monsterClimaxSequence;

	if (outAnim.npcClimaxSequence == nullptr)
		outAnim.npcClimaxSequence = pProfile->monsterNpcClimaxSequence;

	return true;
}

bool SHL_GetMonsterSceneSlotAnim(
	const shl_monster_scene_profile_t* pProfile,
	int slot,
	shl_monster_scene_slot_anim_t& outAnim)
{
	outAnim.startSequence = nullptr;
	outAnim.loopSequence = nullptr;
	outAnim.playerClimaxSequence = nullptr;
	outAnim.npcClimaxSequence = nullptr;

	shl_monster_scene_slot_anim_variation_t variation;

	if (!SHL_GetMonsterSceneSlotAnimVariation(
			pProfile,
			slot,
			0,
			variation))
	{
		return false;
	}

	outAnim.startSequence = variation.startSequence;
	outAnim.loopSequence = variation.loopSequence;
	outAnim.playerClimaxSequence = variation.playerClimaxSequence;
	outAnim.npcClimaxSequence = variation.npcClimaxSequence;

	return true;
}