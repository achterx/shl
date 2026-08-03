#ifndef SHL_MONSTER_SCENE_PROFILE_H
#define SHL_MONSTER_SCENE_PROFILE_H

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

	// slot0 legacy owner anchor. Current gameplay still uses this.
	shl_scene_actor_anchor_t monsterAnchor;

	// Future joiner anchors:
	// slot0 = owner monster
	// slot1 = first joining monster
	// slot2 = second joining monster
	shl_scene_actor_anchor_t monsterSlotAnchors[SHL_MONSTER_SCENE_MAX_SLOTS];
};

bool SHL_GetMonsterSceneSlotAnchor(
	const shl_monster_scene_profile_t* pProfile,
	int slot,
	shl_scene_actor_anchor_t& outAnchor);

const shl_monster_scene_profile_t* SHL_GetMonsterSceneProfile(
	CBaseEntity* pMonster,
	int sceneType);

#endif