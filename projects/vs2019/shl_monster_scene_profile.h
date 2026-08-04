<<<<<<< HEAD
#ifndef SHL_MONSTER_SCENE_PROFILE_H
#define SHL_MONSTER_SCENE_PROFILE_H

class CBaseEntity;

struct shl_scene_actor_anchor_t
{
	float forward;
	float right;
	float z;
	float pitchOffset;
	float yawOffset;
	bool dropToFloor;
};

#define SHL_MONSTER_SCENE_MAX_SLOTS 3
#define SHL_MONSTER_SCENE_MAX_SLOT_ANIM_VARIANTS 4

// Legacy/simple anim struct.
// Kept so older scene code can still ask for "slot anim" without variation.
struct shl_monster_scene_slot_anim_t
{
	const char* startSequence;
	const char* loopSequence;
	const char* playerClimaxSequence;
	const char* npcClimaxSequence;
};

struct shl_monster_scene_slot_anim_variation_t
{
	const char* debugName;

	const char* startSequence;
	const char* loopSequence;
	const char* playerClimaxSequence;
	const char* npcClimaxSequence;
};

struct shl_monster_scene_slot_anim_set_t
{
	int variationCount;

	shl_monster_scene_slot_anim_variation_t variations
		[SHL_MONSTER_SCENE_MAX_SLOT_ANIM_VARIANTS];
};

struct shl_monster_scene_profile_t
{
	const char* monsterClassname;
	int sceneType;
	const char* debugName;

	const char* playerSceneActorModel;

	const char* playerStartSequence;
	const char* playerLoopSequence;
	const char* playerClimaxSequence;

	const char* monsterStartSequence;
	const char* monsterLoopSequence;
	const char* monsterClimaxSequence;
	const char* monsterNpcClimaxSequence;

	const char* monsterEscapeKnockdownSequence;
	const char* monsterRecoveryGetupSequence;
	float monsterEscapeKnockdownDuration;
	float monsterRecoveryGetupDuration;

	shl_scene_actor_anchor_t playerAnchor;

	// Legacy slot0 owner anchor.
	// Kept for compatibility with older placement code.
	shl_scene_actor_anchor_t monsterAnchor;

	// slot0 = owner monster
	// slot1 = first joining monster
	// slot2 = second joining monster
	shl_scene_actor_anchor_t monsterSlotAnchors[SHL_MONSTER_SCENE_MAX_SLOTS];

	// Per-slot animation variation sets.
	// Each slot can have multiple authored animation variants.
	shl_monster_scene_slot_anim_set_t monsterSlotAnimSets[SHL_MONSTER_SCENE_MAX_SLOTS];
};

const shl_monster_scene_profile_t* SHL_GetMonsterSceneProfile(
	CBaseEntity* pMonster,
	int sceneType);

bool SHL_GetMonsterSceneSlotAnchor(
	const shl_monster_scene_profile_t* pProfile,
	int slot,
	shl_scene_actor_anchor_t& outAnchor);

bool SHL_GetMonsterSceneSlotAnim(
	const shl_monster_scene_profile_t* pProfile,
	int slot,
	shl_monster_scene_slot_anim_t& outAnim);

bool SHL_GetMonsterSceneSlotAnimVariation(
	const shl_monster_scene_profile_t* pProfile,
	int slot,
	int variation,
	shl_monster_scene_slot_anim_variation_t& outAnim);

=======
#ifndef SHL_MONSTER_SCENE_PROFILE_H
#define SHL_MONSTER_SCENE_PROFILE_H

class CBaseEntity;

struct shl_scene_actor_anchor_t
{
	float forward;
	float right;
	float z;
	float pitchOffset;
	float yawOffset;
	bool dropToFloor;
};

#define SHL_MONSTER_SCENE_MAX_SLOTS 3
#define SHL_MONSTER_SCENE_MAX_SLOT_ANIM_VARIANTS 4

// Legacy/simple anim struct.
// Kept so older scene code can still ask for "slot anim" without variation.
struct shl_monster_scene_slot_anim_t
{
	const char* startSequence;
	const char* loopSequence;
	const char* playerClimaxSequence;
	const char* npcClimaxSequence;
};

struct shl_monster_scene_slot_anim_variation_t
{
	const char* debugName;

	const char* startSequence;
	const char* loopSequence;
	const char* playerClimaxSequence;
	const char* npcClimaxSequence;
};

struct shl_monster_scene_slot_anim_set_t
{
	int variationCount;

	shl_monster_scene_slot_anim_variation_t variations
		[SHL_MONSTER_SCENE_MAX_SLOT_ANIM_VARIANTS];
};

struct shl_monster_scene_profile_t
{
	const char* monsterClassname;
	int sceneType;
	const char* debugName;

	const char* playerSceneActorModel;

	const char* playerStartSequence;
	const char* playerLoopSequence;
	const char* playerClimaxSequence;

	const char* monsterStartSequence;
	const char* monsterLoopSequence;
	const char* monsterClimaxSequence;
	const char* monsterNpcClimaxSequence;

	const char* monsterEscapeKnockdownSequence;
	const char* monsterRecoveryGetupSequence;
	float monsterEscapeKnockdownDuration;
	float monsterRecoveryGetupDuration;

	shl_scene_actor_anchor_t playerAnchor;

	// Legacy slot0 owner anchor.
	// Kept for compatibility with older placement code.
	shl_scene_actor_anchor_t monsterAnchor;

	// slot0 = owner monster
	// slot1 = first joining monster
	// slot2 = second joining monster
	shl_scene_actor_anchor_t monsterSlotAnchors[SHL_MONSTER_SCENE_MAX_SLOTS];

	// Per-slot animation variation sets.
	// Each slot can have multiple authored animation variants.
	shl_monster_scene_slot_anim_set_t monsterSlotAnimSets[SHL_MONSTER_SCENE_MAX_SLOTS];
};

const shl_monster_scene_profile_t* SHL_GetMonsterSceneProfile(
	CBaseEntity* pMonster,
	int sceneType);

bool SHL_GetMonsterSceneSlotAnchor(
	const shl_monster_scene_profile_t* pProfile,
	int slot,
	shl_scene_actor_anchor_t& outAnchor);

bool SHL_GetMonsterSceneSlotAnim(
	const shl_monster_scene_profile_t* pProfile,
	int slot,
	shl_monster_scene_slot_anim_t& outAnim);

bool SHL_GetMonsterSceneSlotAnimVariation(
	const shl_monster_scene_profile_t* pProfile,
	int slot,
	int variation,
	shl_monster_scene_slot_anim_variation_t& outAnim);

>>>>>>> c804012e1e66dedb655c9d5f112ec56678a2f206
#endif