#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"

#include "shl_defs.h"
#include "shl_skill.h"
#include "shl_player_state.h"
#include "shl_scene.h"
#include "shl_scene_profile.h"
#include "shl_scene_actor.h"
#include "../../dlls/UserMessages.h"
#include "shl_monster_scene_profile.h"
#include "shl_scene_editor.h"

#include <string.h>

#define SHL_MAX_SCENE_PLAYERS 33
#define SHL_MAX_MONSTER_SCENE_RECOVERY 2048

#define SHL_GROUNDED_ESCAPE_REQUIRED 80.0f
#define SHL_GROUNDED_ESCAPE_GAIN_PER_MASH 10.0f
#define SHL_GROUNDED_ESCAPE_MASH_COOLDOWN 0.08f
#define SHL_SCENE_MONSTER_PIVOT_FORWARD 12.0f
#define SHL_SCENE_MONSTER_PIVOT_RIGHT 0.0f
#define SHL_SCENE_MONSTER_PIVOT_Z 32.0f

static bool SHL_GetSceneSlotAnimVariation(
	int index,
	int slot,
	shl_monster_scene_slot_anim_variation_t& outAnim);

static bool SHL_PlaySceneSlotSequence(
	int index,
	int slot,
	const char* pszSequence,
	const char* pszDebugOwner);

static void SHL_HoldSequence(
	CBaseAnimating* pAnimating,
	const char* pszSequenceName,
	const char* pszDebugOwner,
	bool holdLastFrame);

static void SHL_HoldSequenceNoRestart(
	CBaseAnimating* pAnimating,
	const char* pszSequenceName,
	bool holdLastFrame);

static bool SHL_IsSequenceNearEnd(
	CBaseAnimating* pAnimating,
	const char* pszSequenceName,
	float nearEndFrame);

static void SHL_PlaySceneMonsterSlotLoops(int index);
static void SHL_ThinkSceneSlotLoopTransitions(int index);

static void SHL_PlaySceneMonsterSlotPlayerClimax(int index);
static void SHL_PlaySceneMonsterSlotNpcClimax(int index);
static void SHL_HoldSceneMonsterSlotClimax(int index, bool npcClimax);
static bool SHL_IsAnySceneMonsterSlotClimaxNearEnd(int index, bool npcClimax);
static void SHL_ResetSceneSlotLoopsForReturn(int index, float returnTime);

enum SHLMonsterSceneRecoveryPhase
{
	SHL_MONSTER_RECOVERY_PHASE_NONE = 0,
	SHL_MONSTER_RECOVERY_PHASE_KNOCKDOWN,
	SHL_MONSTER_RECOVERY_PHASE_FREEZE,
	SHL_MONSTER_RECOVERY_PHASE_GETUP
};

struct shl_scene_slot_t
{
	bool active;
	int sceneType;
	float startTime;
	float endTime;
	float duration;
	float lastTickTime;

	bool paused;
	float pauseStartTime;

	bool startAnimationStarted;
	bool loopAnimationStarted;

	bool climaxAnimationStarted;
	float climaxStartTime;

	bool pendingLoopAfterPlayerClimax;
	float returnToLoopTime;

	float npcEndurance;
	bool npcClimaxStarted;
	float npcClimaxStartTime;

	float escapeProgress;
	float lastEscapeMashTime;

	Vector sceneOrigin;
	float sceneYaw;

	EHANDLE hOwnerMonster;
	EHANDLE hPlayerSceneActor;

	// Future multi-NPC scene slots.
	// slot0 = owner monster
	// slot1/slot2 = future joiners
	EHANDLE hSlotMonsters[SHL_MONSTER_SCENE_MAX_SLOTS];
	int slotAnimVariation[SHL_MONSTER_SCENE_MAX_SLOTS];

	float slotJoinStartTime[SHL_MONSTER_SCENE_MAX_SLOTS];
	bool slotLoopStarted[SHL_MONSTER_SCENE_MAX_SLOTS];
};

struct shl_monster_scene_recovery_t
{
	bool active;
	EHANDLE hMonster;
	EHANDLE hVisual;

	int phase;
	float phaseEndTime;
	float endTime;

	char knockdownSequenceName[64];
	char holdSequenceName[64];
	char getupSequenceName[64];

	float knockdownDuration;
	float freezeDuration;
	float getupDuration;

	Vector holdOrigin;
	Vector holdAngles;

	int holdSequenceId;
	float holdFrame;
	unsigned char holdController[4];
	unsigned char holdBlending[2];

	int oldEffects;
	int oldSolid;
};

static shl_scene_slot_t g_SHLSceneSlots[SHL_MAX_SCENE_PLAYERS];
static shl_monster_scene_recovery_t g_SHLMonsterSceneRecovery[SHL_MAX_MONSTER_SCENE_RECOVERY];

static float g_SHLGroundedEscapeProgress[SHL_MAX_SCENE_PLAYERS];
static float g_SHLGroundedEscapeLastMashTime[SHL_MAX_SCENE_PLAYERS];
static bool g_SHLGroundedEscapeBarActive[SHL_MAX_SCENE_PLAYERS];

static float g_SHLMonsterRegrabBlockedUntil[SHL_MAX_MONSTER_SCENE_RECOVERY];

static int SHL_GetMonsterRecoveryIndex(CBaseEntity* pMonster);
static void SHL_CancelMonsterSceneRecoveryForDeath(int index);

static void SHL_TryPlaySequence(
	CBaseAnimating* pAnimating,
	const char* pszSequenceName,
	const char* pszDebugOwner);

static bool SHL_PositionSceneMonsterSlot(
	int index,
	edict_t* pPlayer,
	CBaseMonster* pMonster,
	int slot,
	bool recomputeSceneAnchor);

static float SHL_NormalizeYaw360(float yaw);

static Vector SHL_RotateLocalPivot(float pitch, float yaw, const Vector& localPivot)
{
	Vector angles = g_vecZero;
	angles.x = pitch;
	angles.y = yaw;
	angles.z = 0.0f;

	MAKE_VECTORS(angles);

	Vector result = g_vecZero;

	result = result + gpGlobals->v_forward * localPivot.x;
	result = result + gpGlobals->v_right * localPivot.y;
	result = result + gpGlobals->v_up * localPivot.z;

	return result;
}

static Vector SHL_ForwardRightOffset(
	const Vector& origin,
	float yaw,
	float forwardOffset,
	float rightOffset,
	float zOffset);

class CSHLMonsterRecoveryVisual : public CBaseAnimating
{
public:
	void Spawn() override;
	bool TakeDamage(entvars_t* pevInflictor, entvars_t* pevAttacker, float flDamage, int bitsDamageType) override;
	void TraceAttack(entvars_t* pevAttacker, float flDamage, Vector vecDir, TraceResult* ptr, int bitsDamageType) override;

public:
	EHANDLE m_hOwnerMonster;
};

LINK_ENTITY_TO_CLASS(shl_monster_recovery_visual, CSHLMonsterRecoveryVisual);

void CSHLMonsterRecoveryVisual::Spawn()
{
	pev->solid = SOLID_SLIDEBOX;
	pev->movetype = MOVETYPE_NONE;
	pev->takedamage = DAMAGE_YES;
	pev->health = 100000.0f;

	pev->velocity = g_vecZero;
	pev->avelocity = g_vecZero;
	pev->framerate = 0.0f;
	pev->effects &= ~EF_NODRAW;
}

bool CSHLMonsterRecoveryVisual::TakeDamage(
	entvars_t* pevInflictor,
	entvars_t* pevAttacker,
	float flDamage,
	int bitsDamageType)
{
	CBaseEntity* pOwner = (CBaseEntity*)m_hOwnerMonster;

	if (pOwner != nullptr)
	{
		const bool result = pOwner->TakeDamage(
			pevInflictor,
			pevAttacker,
			flDamage,
			bitsDamageType);

		if (!pOwner->IsAlive() || pOwner->pev->deadflag != DEAD_NO)
		{
			const int index = SHL_GetMonsterRecoveryIndex(pOwner);

			if (index > 0)
			{
				SHL_CancelMonsterSceneRecoveryForDeath(index);
			}
		}

		return result;
	}

	return false;
}

void CSHLMonsterRecoveryVisual::TraceAttack(
	entvars_t* pevAttacker,
	float flDamage,
	Vector vecDir,
	TraceResult* ptr,
	int bitsDamageType)
{
	CBaseEntity* pOwner = (CBaseEntity*)m_hOwnerMonster;

	if (pOwner != nullptr)
	{
		pOwner->TraceAttack(
			pevAttacker,
			flDamage,
			vecDir,
			ptr,
			bitsDamageType);
	}
}

static int SHL_GetSceneIndex(edict_t* pPlayer)
{
	if (pPlayer == nullptr)
		return 0;

	const int index = ENTINDEX(pPlayer);

	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return 0;

	return index;
}

static void SHL_SendEscapeBar(edict_t* pPlayer, bool active, float progress)
{
	if (pPlayer == nullptr)
		return;

	if (gmsgSHLEscapeBar <= 0)
		return;

	if (progress < 0.0f)
		progress = 0.0f;

	if (progress > 1.0f)
		progress = 1.0f;

	const int percent = active ? (int)(progress * 100.0f) : 0;

	MESSAGE_BEGIN(MSG_ONE, gmsgSHLEscapeBar, NULL, pPlayer);
	WRITE_BYTE(active ? 1 : 0);
	WRITE_BYTE(percent);
	MESSAGE_END();
}

static void SHL_ResetGroundedEscape(int index)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	g_SHLGroundedEscapeProgress[index] = 0.0f;
	g_SHLGroundedEscapeLastMashTime[index] = 0.0f;
	g_SHLGroundedEscapeBarActive[index] = false;
}

static void SHL_StopPlayerSceneMotion(edict_t* pPlayer)
{
	if (pPlayer == nullptr)
		return;

	pPlayer->v.velocity = g_vecZero;
	pPlayer->v.basevelocity = g_vecZero;
	pPlayer->v.avelocity = g_vecZero;
	pPlayer->v.punchangle = g_vecZero;

	pPlayer->v.flFallVelocity = 0.0f;
}

static void SHL_StopPlayerSceneMotionAndFixAngle(edict_t* pPlayer)
{
	SHL_StopPlayerSceneMotion(pPlayer);

	if (pPlayer == nullptr)
		return;

	pPlayer->v.fixangle = 1;
}

static float SHL_GetGroundedEscapeProgress01(int index)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return 0.0f;

	float progress = g_SHLGroundedEscapeProgress[index] / SHL_GROUNDED_ESCAPE_REQUIRED;

	if (progress < 0.0f)
		progress = 0.0f;

	if (progress > 1.0f)
		progress = 1.0f;

	return progress;
}

static bool SHL_AddGroundedEscapeMash(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return false;

	if (SHL_GetPlayerStateId(pPlayer) != SHL_PLAYERSTATE_GROUNDED)
		return false;

	if (gpGlobals->time < g_SHLGroundedEscapeLastMashTime[index] + SHL_GROUNDED_ESCAPE_MASH_COOLDOWN)
		return false;

	g_SHLGroundedEscapeLastMashTime[index] = gpGlobals->time;
	g_SHLGroundedEscapeProgress[index] += SHL_GROUNDED_ESCAPE_GAIN_PER_MASH;

	if (g_SHLGroundedEscapeProgress[index] > SHL_GROUNDED_ESCAPE_REQUIRED)
		g_SHLGroundedEscapeProgress[index] = SHL_GROUNDED_ESCAPE_REQUIRED;

	SHL_SendEscapeBar(pPlayer, true, SHL_GetGroundedEscapeProgress01(index));
	g_SHLGroundedEscapeBarActive[index] = true;

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: grounded escape mash progress %.1f / %.1f\n",
			g_SHLGroundedEscapeProgress[index],
			SHL_GROUNDED_ESCAPE_REQUIRED);
	}

	if (g_SHLGroundedEscapeProgress[index] >= SHL_GROUNDED_ESCAPE_REQUIRED)
	{
		if (SHL_DebugEnabled())
		{
			ALERT(at_console, "SHL: grounded escape success\n");
		}

		SHL_SetPlayerState(pPlayer, SHL_PLAYERSTATE_NORMAL);
		SHL_SendEscapeBar(pPlayer, false, 0.0f);
		SHL_ResetGroundedEscape(index);

		return true;
	}

	return true;
}

static void SHL_ThinkGroundedEscapeBars()
{
	for (int i = 1; i < SHL_MAX_SCENE_PLAYERS; ++i)
	{
		CBaseEntity* pEntity = UTIL_PlayerByIndex(i);

		if (pEntity == nullptr)
			continue;

		edict_t* pPlayer = pEntity->edict();

		if (pPlayer == nullptr)
			continue;

		if (g_SHLSceneSlots[i].active)
		{
			if (g_SHLGroundedEscapeBarActive[i])
			{
				SHL_SendEscapeBar(pPlayer, false, 0.0f);
				SHL_ResetGroundedEscape(i);
			}

			continue;
		}

		if (SHL_GetPlayerStateId(pPlayer) == SHL_PLAYERSTATE_GROUNDED)
		{
			if (!g_SHLGroundedEscapeBarActive[i])
			{
				g_SHLGroundedEscapeBarActive[i] = true;
				SHL_SendEscapeBar(pPlayer, true, SHL_GetGroundedEscapeProgress01(i));
			}
		}
		else
		{
			if (g_SHLGroundedEscapeBarActive[i])
			{
				SHL_SendEscapeBar(pPlayer, false, 0.0f);
				SHL_ResetGroundedEscape(i);
			}
		}
	}
}

static int SHL_GetMonsterRecoveryIndex(CBaseEntity* pMonster)
{
	if (pMonster == nullptr)
		return 0;

	if (pMonster->edict() == nullptr)
		return 0;

	const int index = ENTINDEX(pMonster->edict());

	if (index <= 0 || index >= SHL_MAX_MONSTER_SCENE_RECOVERY)
		return 0;

	return index;
}

static void SHL_SetMonsterRegrabCooldown(CBaseEntity* pMonster, float duration)
{
	if (pMonster == nullptr)
		return;

	if (duration <= 0.0f)
		return;

	const int index = SHL_GetMonsterRecoveryIndex(pMonster);

	if (index <= 0)
		return;

	g_SHLMonsterRegrabBlockedUntil[index] = gpGlobals->time + duration;

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: monster regrab cooldown started entity=%d duration=%.2f\n",
			index,
			duration);
	}
}

bool SHL_IsMonsterRegrabBlocked(CBaseEntity* pMonster)
{
	if (pMonster == nullptr)
		return false;

	const int index = SHL_GetMonsterRecoveryIndex(pMonster);

	if (index <= 0)
		return false;

	return gpGlobals->time < g_SHLMonsterRegrabBlockedUntil[index];
}

bool SHL_IsMonsterActionLocked(CBaseEntity* pMonster)
{
	if (pMonster == nullptr)
		return false;

	if (SHL_IsMonsterInSceneNpcRecovery(pMonster))
		return true;

	CBaseMonster* pBaseMonster = pMonster->MyMonsterPointer();

	if (pBaseMonster != nullptr)
	{
		if (SHL_IsMonsterSceneOwner(pBaseMonster))
			return true;
	}

	return false;
}

bool SHL_IsMonsterGrabBlocked(CBaseEntity* pMonster)
{
	if (pMonster == nullptr)
		return false;

	if (SHL_IsMonsterActionLocked(pMonster))
		return true;

	if (SHL_IsMonsterRegrabBlocked(pMonster))
		return true;

	return false;
}

static void SHL_CopyRecoverySequenceName(char* pszDest, int destSize, const char* pszSource)
{
	if (pszDest == nullptr || destSize <= 0)
		return;

	pszDest[0] = '\0';

	if (pszSource == nullptr || pszSource[0] == '\0')
		return;

	strncpy(pszDest, pszSource, destSize - 1);
	pszDest[destSize - 1] = '\0';
}

static void SHL_ClearMonsterSceneRecoverySlot(int index)
{
	if (index <= 0 || index >= SHL_MAX_MONSTER_SCENE_RECOVERY)
		return;

	CBaseEntity* pMonster =
		(CBaseEntity*)g_SHLMonsterSceneRecovery[index].hMonster;

	if (g_SHLMonsterSceneRecovery[index].active &&
		pMonster != nullptr &&
		pMonster->IsAlive() &&
		pMonster->pev->deadflag == DEAD_NO)
	{
		pMonster->pev->effects = g_SHLMonsterSceneRecovery[index].oldEffects;
		pMonster->pev->solid = g_SHLMonsterSceneRecovery[index].oldSolid;
		pMonster->pev->origin = g_SHLMonsterSceneRecovery[index].holdOrigin;
		pMonster->pev->angles = g_SHLMonsterSceneRecovery[index].holdAngles;
		pMonster->pev->velocity = g_vecZero;
		pMonster->pev->avelocity = g_vecZero;
		pMonster->pev->framerate = 1.0f;
		pMonster->pev->nextthink = gpGlobals->time + 0.01f;
	}

	CBaseEntity* pVisual =
		(CBaseEntity*)g_SHLMonsterSceneRecovery[index].hVisual;

	if (pVisual != nullptr)
	{
		UTIL_Remove(pVisual);
	}

	// rest of your existing clear code continues here...

	g_SHLMonsterSceneRecovery[index].active = false;
	g_SHLMonsterSceneRecovery[index].hMonster = nullptr;
	g_SHLMonsterSceneRecovery[index].hVisual = nullptr;
	g_SHLMonsterSceneRecovery[index].endTime = 0.0f;

	g_SHLMonsterSceneRecovery[index].phase = SHL_MONSTER_RECOVERY_PHASE_NONE;
	g_SHLMonsterSceneRecovery[index].phaseEndTime = 0.0f;
	g_SHLMonsterSceneRecovery[index].endTime = 0.0f;

	g_SHLMonsterSceneRecovery[index].knockdownSequenceName[0] = '\0';
	g_SHLMonsterSceneRecovery[index].holdSequenceName[0] = '\0';
	g_SHLMonsterSceneRecovery[index].getupSequenceName[0] = '\0';

	g_SHLMonsterSceneRecovery[index].knockdownDuration = 0.0f;
	g_SHLMonsterSceneRecovery[index].freezeDuration = 0.0f;
	g_SHLMonsterSceneRecovery[index].getupDuration = 0.0f;

	g_SHLMonsterSceneRecovery[index].holdSequenceName[0] = '\0';
	g_SHLMonsterSceneRecovery[index].holdOrigin = g_vecZero;
	g_SHLMonsterSceneRecovery[index].holdAngles = g_vecZero;
	g_SHLMonsterSceneRecovery[index].holdSequenceId = 0;
	g_SHLMonsterSceneRecovery[index].holdFrame = 0.0f;

	for (int controllerIndex = 0; controllerIndex < 4; ++controllerIndex)
	{
		g_SHLMonsterSceneRecovery[index].holdController[controllerIndex] = 0;
	}

	for (int blendingIndex = 0; blendingIndex < 2; ++blendingIndex)
	{
		g_SHLMonsterSceneRecovery[index].holdBlending[blendingIndex] = 0;
	}

	g_SHLMonsterSceneRecovery[index].oldEffects = 0;
	g_SHLMonsterSceneRecovery[index].oldSolid = SOLID_NOT;
}

static CBaseEntity* SHL_CreateMonsterRecoveryVisual(
	CBaseEntity* pMonster,
	int holdSequenceId,
	float holdFrame,
	const unsigned char* pHoldController,
	const unsigned char* pHoldBlending)
{
	if (pMonster == nullptr)
		return nullptr;

	CSHLMonsterRecoveryVisual* pVisual =
		GetClassPtr((CSHLMonsterRecoveryVisual*)nullptr);

	if (pVisual == nullptr)
		return nullptr;

	pVisual->pev->classname = MAKE_STRING("shl_monster_recovery_visual");
	pVisual->m_hOwnerMonster = pMonster;

	pVisual->pev->origin = pMonster->pev->origin;
	pVisual->pev->angles = pMonster->pev->angles;
	pVisual->pev->body = pMonster->pev->body;
	pVisual->pev->skin = pMonster->pev->skin;
	pVisual->pev->scale = pMonster->pev->scale;

	pVisual->pev->rendermode = pMonster->pev->rendermode;
	pVisual->pev->renderamt = pMonster->pev->renderamt;
	pVisual->pev->rendercolor = pMonster->pev->rendercolor;
	pVisual->pev->renderfx = pMonster->pev->renderfx;
	pVisual->pev->effects = pMonster->pev->effects & ~EF_NODRAW;

	pVisual->Spawn();

	SET_MODEL(ENT(pVisual->pev), STRING(pMonster->pev->model));
	UTIL_SetOrigin(pVisual->pev, pMonster->pev->origin);
	UTIL_SetSize(pVisual->pev, pMonster->pev->mins, pMonster->pev->maxs);

	pVisual->pev->origin = pMonster->pev->origin;
	pVisual->pev->angles = pMonster->pev->angles;
	pVisual->pev->solid = SOLID_SLIDEBOX;
	pVisual->pev->movetype = MOVETYPE_NONE;
	pVisual->pev->takedamage = DAMAGE_YES;
	pVisual->pev->health = 100000.0f;
	pVisual->pev->velocity = g_vecZero;
	pVisual->pev->avelocity = g_vecZero;

	pVisual->pev->sequence = holdSequenceId;
	pVisual->pev->frame = holdFrame;
	pVisual->pev->framerate = 0.0f;

	for (int controllerIndex = 0; controllerIndex < 4; ++controllerIndex)
	{
		pVisual->pev->controller[controllerIndex] =
			pHoldController[controllerIndex];
	}

	for (int blendingIndex = 0; blendingIndex < 2; ++blendingIndex)
	{
		pVisual->pev->blending[blendingIndex] =
			pHoldBlending[blendingIndex];
	}

	pVisual->ResetSequenceInfo();

	pVisual->pev->sequence = holdSequenceId;
	pVisual->pev->frame = holdFrame;
	pVisual->pev->framerate = 0.0f;

	for (int controllerIndex = 0; controllerIndex < 4; ++controllerIndex)
	{
		pVisual->pev->controller[controllerIndex] =
			pHoldController[controllerIndex];
	}

	for (int blendingIndex = 0; blendingIndex < 2; ++blendingIndex)
	{
		pVisual->pev->blending[blendingIndex] =
			pHoldBlending[blendingIndex];
	}

	return pVisual;
}

static void SHL_SetGroundedGrabSceneAnchorsEx(
	int index,
	CBaseMonster* pMonster,
	edict_t* pPlayer,
	bool recomputeSceneAnchor)
{
	SHL_PositionSceneMonsterSlot(
		index,
		pPlayer,
		pMonster,
		0,
		recomputeSceneAnchor);
}

static bool SHL_PlaySceneSlotSequence(
	int index,
	int slot,
	const char* pszSequence,
	const char* pszDebugOwner)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return false;

	if (slot < 0 || slot >= SHL_MONSTER_SCENE_MAX_SLOTS)
		return false;

	if (pszSequence == nullptr || pszSequence[0] == '\0')
		return false;

	CBaseEntity* pSlotEntity =
		(CBaseEntity*)g_SHLSceneSlots[index].hSlotMonsters[slot];

	if (pSlotEntity == nullptr)
		return false;

	CBaseAnimating* pAnimating = (CBaseAnimating*)pSlotEntity;

	SHL_TryPlaySequence(
		pAnimating,
		pszSequence,
		pszDebugOwner);

	pSlotEntity->pev->velocity = g_vecZero;
	pSlotEntity->pev->avelocity = g_vecZero;

	return true;
}

static bool SHL_GetSceneSlotAnimVariation(
	int index,
	int slot,
	shl_monster_scene_slot_anim_variation_t& outAnim)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return false;

	if (slot < 0 || slot >= SHL_MONSTER_SCENE_MAX_SLOTS)
		return false;

	CBaseEntity* pSlotEntity =
		(CBaseEntity*)g_SHLSceneSlots[index].hSlotMonsters[slot];

	if (pSlotEntity == nullptr)
		return false;

	const shl_monster_scene_profile_t* pProfile =
		SHL_GetMonsterSceneProfile(
			pSlotEntity,
			g_SHLSceneSlots[index].sceneType);

	if (pProfile == nullptr)
		return false;

	return SHL_GetMonsterSceneSlotAnimVariation(
		pProfile,
		slot,
		g_SHLSceneSlots[index].slotAnimVariation[slot],
		outAnim);
}

static void SHL_PlaySceneMonsterSlotLoops(int index)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	for (int slot = 0; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
	{
		if (g_SHLSceneSlots[index].hSlotMonsters[slot] == nullptr)
			continue;

		shl_monster_scene_slot_anim_variation_t slotAnim;

		if (!SHL_GetSceneSlotAnimVariation(index, slot, slotAnim))
			continue;

		if (SHL_PlaySceneSlotSequence(
				index,
				slot,
				slotAnim.loopSequence,
				"scene slot loop"))
		{
			g_SHLSceneSlots[index].slotLoopStarted[slot] = true;
		}
	}
}

static void SHL_PlaySceneMonsterSlotPlayerClimax(int index)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	for (int slot = 0; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
	{
		if (g_SHLSceneSlots[index].hSlotMonsters[slot] == nullptr)
			continue;

		shl_monster_scene_slot_anim_variation_t slotAnim;

		if (!SHL_GetSceneSlotAnimVariation(index, slot, slotAnim))
			continue;

		SHL_PlaySceneSlotSequence(
			index,
			slot,
			slotAnim.playerClimaxSequence,
			"scene slot player climax");
	}
}

static void SHL_PlaySceneMonsterSlotNpcClimax(int index)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	for (int slot = 0; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
	{
		if (g_SHLSceneSlots[index].hSlotMonsters[slot] == nullptr)
			continue;

		shl_monster_scene_slot_anim_variation_t slotAnim;

		if (!SHL_GetSceneSlotAnimVariation(index, slot, slotAnim))
			continue;

		SHL_PlaySceneSlotSequence(
			index,
			slot,
			slotAnim.npcClimaxSequence,
			"scene slot npc climax");
	}
}

static void SHL_HoldSceneMonsterSlotClimax(int index, bool npcClimax)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	for (int slot = 0; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
	{
		CBaseEntity* pSlotEntity =
			(CBaseEntity*)g_SHLSceneSlots[index].hSlotMonsters[slot];

		if (pSlotEntity == nullptr)
			continue;

		shl_monster_scene_slot_anim_variation_t slotAnim;

		if (!SHL_GetSceneSlotAnimVariation(index, slot, slotAnim))
			continue;

		const char* pszSequence =
			npcClimax ? slotAnim.npcClimaxSequence : slotAnim.playerClimaxSequence;

		if (pszSequence == nullptr || pszSequence[0] == '\0')
			continue;

		CBaseAnimating* pAnimating = (CBaseAnimating*)pSlotEntity;

		SHL_HoldSequence(
			pAnimating,
			pszSequence,
			npcClimax ? "scene slot npc climax" : "scene slot player climax",
			false);

		pSlotEntity->pev->velocity = g_vecZero;
		pSlotEntity->pev->avelocity = g_vecZero;
	}
}

static bool SHL_IsAnySceneMonsterSlotClimaxNearEnd(int index, bool npcClimax)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return false;

	for (int slot = 0; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
	{
		CBaseEntity* pSlotEntity =
			(CBaseEntity*)g_SHLSceneSlots[index].hSlotMonsters[slot];

		if (pSlotEntity == nullptr)
			continue;

		shl_monster_scene_slot_anim_variation_t slotAnim;

		if (!SHL_GetSceneSlotAnimVariation(index, slot, slotAnim))
			continue;

		const char* pszSequence =
			npcClimax ? slotAnim.npcClimaxSequence : slotAnim.playerClimaxSequence;

		if (pszSequence == nullptr || pszSequence[0] == '\0')
			continue;

		CBaseAnimating* pAnimating = (CBaseAnimating*)pSlotEntity;

		if (SHL_IsSequenceNearEnd(pAnimating, pszSequence, 248.0f))
			return true;
	}

	return false;
}

static void SHL_ResetSceneSlotLoopsForReturn(int index, float returnTime)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	for (int slot = 0; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
	{
		if (g_SHLSceneSlots[index].hSlotMonsters[slot] == nullptr)
			continue;

		g_SHLSceneSlots[index].slotLoopStarted[slot] = false;
		g_SHLSceneSlots[index].slotJoinStartTime[slot] = returnTime;
	}
}

static void SHL_ThinkSceneSlotLoopTransitions(int index)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	const shl_scene_profile_t* pProfile =
		SHL_GetSceneProfile(g_SHLSceneSlots[index].sceneType);

	float startDuration = 0.5f;

	if (pProfile != nullptr && pProfile->startSequenceDuration > 0.0f)
		startDuration = pProfile->startSequenceDuration;

	for (int slot = 0; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
	{
		if (g_SHLSceneSlots[index].hSlotMonsters[slot] == nullptr)
			continue;

		if (g_SHLSceneSlots[index].slotLoopStarted[slot])
			continue;

		if (g_SHLSceneSlots[index].slotJoinStartTime[slot] <= 0.0f)
			continue;

		if (gpGlobals->time <
			g_SHLSceneSlots[index].slotJoinStartTime[slot] + startDuration)
		{
			continue;
		}

		shl_monster_scene_slot_anim_variation_t slotAnim;

		if (!SHL_GetSceneSlotAnimVariation(index, slot, slotAnim))
			continue;

		if (SHL_PlaySceneSlotSequence(
				index,
				slot,
				slotAnim.loopSequence,
				"scene slot loop transition"))
		{
			g_SHLSceneSlots[index].slotLoopStarted[slot] = true;
		}
	}
}

static bool SHL_PositionSceneMonsterSlot(
	int index,
	edict_t* pPlayer,
	CBaseMonster* pMonster,
	int slot,
	bool recomputeSceneAnchor)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return false;

	if (pPlayer == nullptr || pMonster == nullptr)
		return false;

	if (slot < 0 || slot >= SHL_MONSTER_SCENE_MAX_SLOTS)
		return false;

	const int sceneType = g_SHLSceneSlots[index].sceneType;

	const shl_monster_scene_profile_t* pMonsterProfile =
		SHL_GetMonsterSceneProfile(pMonster, sceneType);

	if (pMonsterProfile == nullptr)
		return false;

	Vector sceneOrigin = g_SHLSceneSlots[index].sceneOrigin;
	float sceneYaw = g_SHLSceneSlots[index].sceneYaw;

	if (recomputeSceneAnchor || sceneOrigin.Length() <= 0.1f)
	{
		sceneOrigin = pPlayer->v.origin;
		sceneYaw = SHL_NormalizeYaw360(pPlayer->v.angles.y);

		g_SHLSceneSlots[index].sceneOrigin = sceneOrigin;
		g_SHLSceneSlots[index].sceneYaw = sceneYaw;
	}
	else
	{
		sceneYaw = SHL_NormalizeYaw360(sceneYaw);
	}

	shl_scene_actor_anchor_t anchor = pMonsterProfile->monsterAnchor;

	SHL_GetMonsterSceneSlotAnchor(
		pMonsterProfile,
		slot,
		anchor);

	if (slot == 0)
	{
		SHL_SceneEditorResolveAnchor(
			pPlayer,
			SHL_SCENE_EDITOR_TARGET_SLOT0,
			anchor,
			anchor);
	}
	else if (slot == 1)
	{
		SHL_SceneEditorResolveAnchor(
			pPlayer,
			SHL_SCENE_EDITOR_TARGET_SLOT1,
			anchor,
			anchor);
	}
	else if (slot == 2)
	{
		SHL_SceneEditorResolveAnchor(
			pPlayer,
			SHL_SCENE_EDITOR_TARGET_SLOT2,
			anchor,
			anchor);
	}

	Vector monsterPivot =
		SHL_ForwardRightOffset(
			sceneOrigin,
			sceneYaw,
			anchor.forward,
			anchor.right,
			0.0f);



	const bool allowDropToFloor =
		recomputeSceneAnchor && anchor.dropToFloor;

	if (allowDropToFloor)
	{
		monsterPivot.z =
			pMonster->pev->origin.z + anchor.z + SHL_SCENE_MONSTER_PIVOT_Z;
	}
	else
	{
		monsterPivot.z =
			sceneOrigin.z + anchor.z + SHL_SCENE_MONSTER_PIVOT_Z;
	}

	const float monsterPitch = anchor.pitchOffset;
	const float monsterYaw = SHL_NormalizeYaw360(sceneYaw + anchor.yawOffset);

	Vector localPivot = g_vecZero;
	localPivot.x = SHL_SCENE_MONSTER_PIVOT_FORWARD;
	localPivot.y = SHL_SCENE_MONSTER_PIVOT_RIGHT;
	localPivot.z = SHL_SCENE_MONSTER_PIVOT_Z;

	Vector rotatedPivot =
		SHL_RotateLocalPivot(
			-monsterPitch,
			monsterYaw,
			localPivot);

	Vector monsterOrigin = monsterPivot - rotatedPivot;

	UTIL_SetOrigin(pMonster->pev, monsterOrigin);

	if (allowDropToFloor)
	{
		DROP_TO_FLOOR(ENT(pMonster->pev));
	}

	pMonster->pev->angles.x = monsterPitch;
	pMonster->pev->angles.y = monsterYaw;
	pMonster->pev->angles.z = 0.0f;
	pMonster->pev->ideal_yaw = pMonster->pev->angles.y;

	pMonster->pev->velocity = g_vecZero;
	pMonster->pev->avelocity = g_vecZero;

	return true;
}


static void SHL_SetGroundedGrabSceneAnchors(
	int index,
	CBaseMonster* pMonster,
	edict_t* pPlayer)
{
	SHL_SetGroundedGrabSceneAnchorsEx(
		index,
		pMonster,
		pPlayer,
		true);
}

static void SHL_SetMonsterExactSceneRange(CBaseEntity* pMonster, edict_t* pPlayer, float exactRange)
{
	if (pMonster == nullptr || pPlayer == nullptr)
		return;

	if (exactRange <= 1.0f)
		return;

	Vector fromPlayerToMonster = pMonster->pev->origin - pPlayer->v.origin;
	fromPlayerToMonster.z = 0.0f;

	if (fromPlayerToMonster.Length() <= 0.1f)
	{
		MAKE_VECTORS(pPlayer->v.angles);

		fromPlayerToMonster = gpGlobals->v_forward * -1.0f;
		fromPlayerToMonster.z = 0.0f;
	}

	fromPlayerToMonster = fromPlayerToMonster.Normalize();

	Vector newOrigin = pPlayer->v.origin + fromPlayerToMonster * exactRange;
	newOrigin.z = pMonster->pev->origin.z;

	UTIL_SetOrigin(pMonster->pev, newOrigin);

	pMonster->pev->velocity = g_vecZero;
	pMonster->pev->avelocity = g_vecZero;

	Vector faceDelta = pPlayer->v.origin - pMonster->pev->origin;
	faceDelta.z = 0.0f;

	if (faceDelta.Length() > 0.1f)
	{
		pMonster->pev->angles.y = UTIL_VecToYaw(faceDelta);
		pMonster->pev->ideal_yaw = pMonster->pev->angles.y;
	}

	pMonster->pev->angles.x = 0.0f;
	pMonster->pev->angles.z = 0.0f;

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: scene exact range set %.1f monster origin %.1f %.1f %.1f yaw %.1f\n",
			exactRange,
			pMonster->pev->origin.x,
			pMonster->pev->origin.y,
			pMonster->pev->origin.z,
			pMonster->pev->angles.y);
	}
}

static bool SHL_RecoverySequenceExists(CBaseAnimating* pAnimating, const char* pszSequenceName)
{
	if (pAnimating == nullptr)
	{
		if (SHL_DebugEnabled())
		{
			ALERT(at_console, "SHL: recovery sequence check failed: animating null\n");
		}

		return false;
	}

	if (pszSequenceName == nullptr || pszSequenceName[0] == '\0')
	{
		if (SHL_DebugEnabled())
		{
			ALERT(at_console, "SHL: recovery sequence check failed: sequence null/empty\n");
		}

		return false;
	}

	const int sequence = pAnimating->LookupSequence(pszSequenceName);

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: recovery sequence lookup name=%s result=%d\n",
			pszSequenceName,
			sequence);
	}

	return sequence >= 0;
}

static bool SHL_StartRecoverySequence(CBaseAnimating* pAnimating, const char* pszSequenceName)
{
	if (pAnimating == nullptr)
		return false;

	if (pszSequenceName == nullptr || pszSequenceName[0] == '\0')
		return false;

	const int sequence = pAnimating->LookupSequence(pszSequenceName);

	if (sequence < 0)
		return false;

	pAnimating->pev->sequence = sequence;
	pAnimating->pev->frame = 0.0f;
	pAnimating->pev->framerate = 1.0f;
	pAnimating->pev->velocity = g_vecZero;
	pAnimating->pev->avelocity = g_vecZero;
	pAnimating->ResetSequenceInfo();

	return true;
}

static void SHL_AdvanceRecoveryVisual(CBaseEntity* pVisual)
{
	if (pVisual == nullptr)
		return;

	CBaseAnimating* pAnimating = (CBaseAnimating*)pVisual;

	pAnimating->pev->velocity = g_vecZero;
	pAnimating->pev->avelocity = g_vecZero;
	pAnimating->pev->framerate = 1.0f;
	pAnimating->StudioFrameAdvance();
}

static void SHL_StartMonsterSceneRecoveryEx(
	CBaseEntity* pMonster,
	const char* pszHoldSequence,
	float freezeDuration,
	const char* pszKnockdownSequence,
	float knockdownDuration,
	const char* pszGetupSequence,
	float getupDuration)
{
	if (pMonster == nullptr)
		return;

	const int index = SHL_GetMonsterRecoveryIndex(pMonster);

	if (index <= 0)
		return;

	SHL_ClearMonsterSceneRecoverySlot(index);

	CBaseAnimating* pMonsterAnimating = (CBaseAnimating*)pMonster;

	const bool hasKnockdown =
		knockdownDuration > 0.0f &&
		SHL_RecoverySequenceExists(pMonsterAnimating, pszKnockdownSequence);

	const bool hasGetup =
		getupDuration > 0.0f &&
		SHL_RecoverySequenceExists(pMonsterAnimating, pszGetupSequence);

	if (freezeDuration < 0.0f)
		freezeDuration = 0.0f;

	if (!hasKnockdown && freezeDuration <= 0.0f && !hasGetup)
		return;

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: recovery Ex requested hold=%s freeze=%.2f knockdown=%s %.2f hasKnockdown=%d getup=%s %.2f hasGetup=%d\n",
			pszHoldSequence != nullptr ? pszHoldSequence : "NULL",
			freezeDuration,
			pszKnockdownSequence != nullptr ? pszKnockdownSequence : "NULL",
			knockdownDuration,
			hasKnockdown ? 1 : 0,
			pszGetupSequence != nullptr ? pszGetupSequence : "NULL",
			getupDuration,
			hasGetup ? 1 : 0);
	}

	g_SHLMonsterSceneRecovery[index].active = true;
	g_SHLMonsterSceneRecovery[index].hMonster = pMonster;

	g_SHLMonsterSceneRecovery[index].knockdownDuration = hasKnockdown ? knockdownDuration : 0.0f;
	g_SHLMonsterSceneRecovery[index].freezeDuration = freezeDuration;
	g_SHLMonsterSceneRecovery[index].getupDuration = hasGetup ? getupDuration : 0.0f;

	g_SHLMonsterSceneRecovery[index].oldEffects = pMonster->pev->effects;
	g_SHLMonsterSceneRecovery[index].oldSolid = pMonster->pev->solid;

	g_SHLMonsterSceneRecovery[index].holdOrigin = pMonster->pev->origin;
	g_SHLMonsterSceneRecovery[index].holdAngles = pMonster->pev->angles;

	SHL_CopyRecoverySequenceName(
		g_SHLMonsterSceneRecovery[index].knockdownSequenceName,
		sizeof(g_SHLMonsterSceneRecovery[index].knockdownSequenceName),
		hasKnockdown ? pszKnockdownSequence : nullptr);

	SHL_CopyRecoverySequenceName(
		g_SHLMonsterSceneRecovery[index].holdSequenceName,
		sizeof(g_SHLMonsterSceneRecovery[index].holdSequenceName),
		pszHoldSequence);

	SHL_CopyRecoverySequenceName(
		g_SHLMonsterSceneRecovery[index].getupSequenceName,
		sizeof(g_SHLMonsterSceneRecovery[index].getupSequenceName),
		hasGetup ? pszGetupSequence : nullptr);

	if (hasKnockdown)
	{
		SHL_StartRecoverySequence(pMonsterAnimating, pszKnockdownSequence);
	}
	else if (pszHoldSequence != nullptr && pszHoldSequence[0] != '\0')
	{
		const int holdSequence = pMonsterAnimating->LookupSequence(pszHoldSequence);

		if (holdSequence >= 0)
		{
			pMonsterAnimating->pev->sequence = holdSequence;
			pMonsterAnimating->pev->frame = 31.0f;
			pMonsterAnimating->pev->framerate = 0.0f;
			pMonsterAnimating->pev->velocity = g_vecZero;
			pMonsterAnimating->pev->avelocity = g_vecZero;

			pMonsterAnimating->ResetSequenceInfo();

			pMonsterAnimating->pev->sequence = holdSequence;
			pMonsterAnimating->pev->frame = 31.0f;
			pMonsterAnimating->pev->framerate = 0.0f;
			pMonsterAnimating->pev->velocity = g_vecZero;
			pMonsterAnimating->pev->avelocity = g_vecZero;
		}
		else if (SHL_DebugEnabled())
		{
			ALERT(
				at_console,
				"SHL: monster recovery hold sequence missing: %s\n",
				pszHoldSequence);
		}
	}

	g_SHLMonsterSceneRecovery[index].holdSequenceId = pMonster->pev->sequence;
	g_SHLMonsterSceneRecovery[index].holdFrame = pMonster->pev->frame;

	for (int controllerIndex = 0; controllerIndex < 4; ++controllerIndex)
	{
		g_SHLMonsterSceneRecovery[index].holdController[controllerIndex] =
			pMonster->pev->controller[controllerIndex];
	}

	for (int blendingIndex = 0; blendingIndex < 2; ++blendingIndex)
	{
		g_SHLMonsterSceneRecovery[index].holdBlending[blendingIndex] =
			pMonster->pev->blending[blendingIndex];
	}

	CBaseEntity* pVisual = SHL_CreateMonsterRecoveryVisual(
		pMonster,
		g_SHLMonsterSceneRecovery[index].holdSequenceId,
		g_SHLMonsterSceneRecovery[index].holdFrame,
		g_SHLMonsterSceneRecovery[index].holdController,
		g_SHLMonsterSceneRecovery[index].holdBlending);

	g_SHLMonsterSceneRecovery[index].hVisual = pVisual;

	if (hasKnockdown && pVisual != nullptr)
	{
		SHL_StartRecoverySequence((CBaseAnimating*)pVisual, pszKnockdownSequence);
	}

	const float now = gpGlobals->time;

	if (hasKnockdown)
	{
		g_SHLMonsterSceneRecovery[index].phase = SHL_MONSTER_RECOVERY_PHASE_KNOCKDOWN;
		g_SHLMonsterSceneRecovery[index].phaseEndTime = now + knockdownDuration;
	}
	else if (freezeDuration > 0.0f)
	{
		g_SHLMonsterSceneRecovery[index].phase = SHL_MONSTER_RECOVERY_PHASE_FREEZE;
		g_SHLMonsterSceneRecovery[index].phaseEndTime = now + freezeDuration;
	}
	else if (hasGetup && pVisual != nullptr)
	{
		SHL_StartRecoverySequence((CBaseAnimating*)pVisual, pszGetupSequence);

		g_SHLMonsterSceneRecovery[index].phase = SHL_MONSTER_RECOVERY_PHASE_GETUP;
		g_SHLMonsterSceneRecovery[index].phaseEndTime = now + getupDuration;
	}

	g_SHLMonsterSceneRecovery[index].endTime =
		now +
		g_SHLMonsterSceneRecovery[index].knockdownDuration +
		g_SHLMonsterSceneRecovery[index].freezeDuration +
		g_SHLMonsterSceneRecovery[index].getupDuration;

	pMonster->pev->origin = g_SHLMonsterSceneRecovery[index].holdOrigin;
	pMonster->pev->angles = g_SHLMonsterSceneRecovery[index].holdAngles;
	pMonster->pev->velocity = g_vecZero;
	pMonster->pev->avelocity = g_vecZero;
	pMonster->pev->framerate = 0.0f;
	pMonster->pev->effects |= EF_NODRAW;
	pMonster->pev->solid = SOLID_NOT;

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: monster recovery started entity=%d freeze=%.2f knockdown=%s %.2f getup=%s %.2f visual=%d\n",
			index,
			freezeDuration,
			g_SHLMonsterSceneRecovery[index].knockdownSequenceName,
			g_SHLMonsterSceneRecovery[index].knockdownDuration,
			g_SHLMonsterSceneRecovery[index].getupSequenceName,
			g_SHLMonsterSceneRecovery[index].getupDuration,
			pVisual != nullptr ? 1 : 0);
	}
}

static void SHL_StartMonsterSceneRecovery(
	CBaseEntity* pMonster,
	const char* pszHoldSequence,
	float duration)
{
	SHL_StartMonsterSceneRecoveryEx(
		pMonster,
		pszHoldSequence,
		duration,
		nullptr,
		0.0f,
		nullptr,
		0.0f);
}

static void SHL_ReleaseMonsterFromSceneRecovery(int index)
{
	if (index <= 0 || index >= SHL_MAX_MONSTER_SCENE_RECOVERY)
		return;

	CBaseEntity* pMonster =
		(CBaseEntity*)g_SHLMonsterSceneRecovery[index].hMonster;

	CBaseEntity* pVisual =
		(CBaseEntity*)g_SHLMonsterSceneRecovery[index].hVisual;

	if (pVisual != nullptr)
	{
		UTIL_Remove(pVisual);
		g_SHLMonsterSceneRecovery[index].hVisual = nullptr;
	}

	if (pMonster != nullptr)
	{
		pMonster->pev->effects = g_SHLMonsterSceneRecovery[index].oldEffects;
		pMonster->pev->solid = g_SHLMonsterSceneRecovery[index].oldSolid;
		pMonster->pev->origin = g_SHLMonsterSceneRecovery[index].holdOrigin;
		pMonster->pev->angles = g_SHLMonsterSceneRecovery[index].holdAngles;
		pMonster->pev->velocity = g_vecZero;
		pMonster->pev->avelocity = g_vecZero;
		pMonster->pev->framerate = 1.0f;

		if (pMonster->IsAlive() && pMonster->pev->deadflag == DEAD_NO)
		{
			CBaseAnimating* pAnimating = (CBaseAnimating*)pMonster;

			const int idleSequence = pAnimating->LookupActivity(ACT_IDLE);

			if (idleSequence >= 0)
			{
				pAnimating->pev->sequence = idleSequence;
				pAnimating->pev->frame = 0.0f;
				pAnimating->pev->framerate = 1.0f;
				pAnimating->ResetSequenceInfo();
			}
		}
		else
		{
			pMonster->pev->frame = 0.0f;
			pMonster->pev->framerate = 1.0f;
		}

		pMonster->pev->nextthink = gpGlobals->time + 0.01f;
	}
}

static void SHL_CancelMonsterSceneRecoveryForDeath(int index)
{
	if (index <= 0 || index >= SHL_MAX_MONSTER_SCENE_RECOVERY)
		return;

	CBaseEntity* pMonster =
		(CBaseEntity*)g_SHLMonsterSceneRecovery[index].hMonster;

	CBaseEntity* pVisual =
		(CBaseEntity*)g_SHLMonsterSceneRecovery[index].hVisual;

	if (pVisual != nullptr)
	{
		UTIL_Remove(pVisual);
		g_SHLMonsterSceneRecovery[index].hVisual = nullptr;
	}

	if (pMonster != nullptr)
	{
		pMonster->pev->effects = g_SHLMonsterSceneRecovery[index].oldEffects;
		pMonster->pev->solid = g_SHLMonsterSceneRecovery[index].oldSolid;
		pMonster->pev->origin = g_SHLMonsterSceneRecovery[index].holdOrigin;
		pMonster->pev->angles = g_SHLMonsterSceneRecovery[index].holdAngles;
		pMonster->pev->velocity = g_vecZero;
		pMonster->pev->avelocity = g_vecZero;
		pMonster->pev->framerate = 1.0f;
		pMonster->pev->frame = 0.0f;
		pMonster->pev->nextthink = gpGlobals->time + 0.01f;
	}

	SHL_ClearMonsterSceneRecoverySlot(index);
}

static void SHL_SetRecoveryFreezePose(int index)
{
	if (index <= 0 || index >= SHL_MAX_MONSTER_SCENE_RECOVERY)
		return;

	CBaseEntity* pMonster =
		(CBaseEntity*)g_SHLMonsterSceneRecovery[index].hMonster;

	if (pMonster == nullptr)
		return;

	CBaseAnimating* pMonsterAnimating = (CBaseAnimating*)pMonster;

	const char* pszHoldSequence =
		g_SHLMonsterSceneRecovery[index].holdSequenceName;

	if (pszHoldSequence != nullptr && pszHoldSequence[0] != '\0')
	{
		const int holdSequence = pMonsterAnimating->LookupSequence(pszHoldSequence);

		if (holdSequence >= 0)
		{
			pMonsterAnimating->pev->sequence = holdSequence;
			pMonsterAnimating->pev->frame = 31.0f;
			pMonsterAnimating->pev->framerate = 0.0f;
			pMonsterAnimating->ResetSequenceInfo();

			pMonsterAnimating->pev->sequence = holdSequence;
			pMonsterAnimating->pev->frame = 31.0f;
			pMonsterAnimating->pev->framerate = 0.0f;
		}
	}

	g_SHLMonsterSceneRecovery[index].holdSequenceId = pMonster->pev->sequence;
	g_SHLMonsterSceneRecovery[index].holdFrame = pMonster->pev->frame;

	for (int controllerIndex = 0; controllerIndex < 4; ++controllerIndex)
	{
		g_SHLMonsterSceneRecovery[index].holdController[controllerIndex] =
			pMonster->pev->controller[controllerIndex];
	}

	for (int blendingIndex = 0; blendingIndex < 2; ++blendingIndex)
	{
		g_SHLMonsterSceneRecovery[index].holdBlending[blendingIndex] =
			pMonster->pev->blending[blendingIndex];
	}

	CBaseEntity* pVisual =
		(CBaseEntity*)g_SHLMonsterSceneRecovery[index].hVisual;

	if (pVisual != nullptr)
	{
		CBaseAnimating* pVisualAnimating = (CBaseAnimating*)pVisual;

		pVisualAnimating->pev->sequence =
			g_SHLMonsterSceneRecovery[index].holdSequenceId;

		pVisualAnimating->pev->frame =
			g_SHLMonsterSceneRecovery[index].holdFrame;

		pVisualAnimating->pev->framerate = 0.0f;
		pVisualAnimating->ResetSequenceInfo();

		pVisualAnimating->pev->sequence =
			g_SHLMonsterSceneRecovery[index].holdSequenceId;

		pVisualAnimating->pev->frame =
			g_SHLMonsterSceneRecovery[index].holdFrame;

		pVisualAnimating->pev->framerate = 0.0f;
	}
}

static void SHL_EnterRecoveryFreezePhase(int index)
{
	if (index <= 0 || index >= SHL_MAX_MONSTER_SCENE_RECOVERY)
		return;

	g_SHLMonsterSceneRecovery[index].phase = SHL_MONSTER_RECOVERY_PHASE_FREEZE;
	g_SHLMonsterSceneRecovery[index].phaseEndTime =
		gpGlobals->time + g_SHLMonsterSceneRecovery[index].freezeDuration;

	SHL_SetRecoveryFreezePose(index);
	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: recovery entered FREEZE phase entity=%d duration=%.2f\n",
			index,
			g_SHLMonsterSceneRecovery[index].freezeDuration);
	}
}

static bool SHL_EnterRecoveryGetupPhase(int index)
{
	if (index <= 0 || index >= SHL_MAX_MONSTER_SCENE_RECOVERY)
		return false;

	if (g_SHLMonsterSceneRecovery[index].getupDuration <= 0.0f)
		return false;

	if (g_SHLMonsterSceneRecovery[index].getupSequenceName[0] == '\0')
		return false;

	CBaseEntity* pVisual =
		(CBaseEntity*)g_SHLMonsterSceneRecovery[index].hVisual;

	if (pVisual == nullptr)
		return false;

	if (!SHL_StartRecoverySequence(
			(CBaseAnimating*)pVisual,
			g_SHLMonsterSceneRecovery[index].getupSequenceName))
	{
		return false;
	}

	g_SHLMonsterSceneRecovery[index].phase = SHL_MONSTER_RECOVERY_PHASE_GETUP;
	g_SHLMonsterSceneRecovery[index].phaseEndTime =
		gpGlobals->time + g_SHLMonsterSceneRecovery[index].getupDuration;

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: recovery entered GETUP phase entity=%d sequence=%s duration=%.2f\n",
			index,
			g_SHLMonsterSceneRecovery[index].getupSequenceName,
			g_SHLMonsterSceneRecovery[index].getupDuration);
	}

	return true;
}

static void SHL_MonsterSceneRecoveryThink()
{
	for (int i = 1; i < SHL_MAX_MONSTER_SCENE_RECOVERY; ++i)
	{
		if (!g_SHLMonsterSceneRecovery[i].active)
			continue;

		CBaseEntity* pMonster =
			(CBaseEntity*)g_SHLMonsterSceneRecovery[i].hMonster;

		if (pMonster == nullptr)
		{
			SHL_ClearMonsterSceneRecoverySlot(i);
			continue;
		}

		if (!pMonster->IsAlive() || pMonster->pev->deadflag != DEAD_NO)
		{
			SHL_CancelMonsterSceneRecoveryForDeath(i);
			continue;
		}

		if (gpGlobals->time >= g_SHLMonsterSceneRecovery[i].phaseEndTime)
		{
			if (g_SHLMonsterSceneRecovery[i].phase == SHL_MONSTER_RECOVERY_PHASE_KNOCKDOWN)
			{
				if (g_SHLMonsterSceneRecovery[i].freezeDuration > 0.0f)
				{
					SHL_EnterRecoveryFreezePhase(i);
				}
				else if (!SHL_EnterRecoveryGetupPhase(i))
				{
					SHL_ReleaseMonsterFromSceneRecovery(i);

					if (SHL_DebugEnabled())
					{
						ALERT(
							at_console,
							"SHL: monster recovery finished after knockdown entity=%d\n",
							i);
					}

					SHL_ClearMonsterSceneRecoverySlot(i);
					continue;
				}
			}
			else if (g_SHLMonsterSceneRecovery[i].phase == SHL_MONSTER_RECOVERY_PHASE_FREEZE)
			{
				if (!SHL_EnterRecoveryGetupPhase(i))
				{
					SHL_ReleaseMonsterFromSceneRecovery(i);

					if (SHL_DebugEnabled())
					{
						ALERT(
							at_console,
							"SHL: monster recovery finished entity=%d\n",
							i);
					}

					SHL_ClearMonsterSceneRecoverySlot(i);
					continue;
				}
			}
			else if (g_SHLMonsterSceneRecovery[i].phase == SHL_MONSTER_RECOVERY_PHASE_GETUP)
			{
				SHL_ReleaseMonsterFromSceneRecovery(i);

				if (SHL_DebugEnabled())
				{
					ALERT(
						at_console,
						"SHL: monster recovery getup finished entity=%d\n",
						i);
				}

				SHL_ClearMonsterSceneRecoverySlot(i);
				continue;
			}
		}

		pMonster->pev->origin = g_SHLMonsterSceneRecovery[i].holdOrigin;
		pMonster->pev->angles = g_SHLMonsterSceneRecovery[i].holdAngles;
		pMonster->pev->velocity = g_vecZero;
		pMonster->pev->avelocity = g_vecZero;
		pMonster->pev->framerate = 0.0f;
		pMonster->pev->effects |= EF_NODRAW;
		pMonster->pev->solid = SOLID_NOT;

		CBaseEntity* pVisual =
			(CBaseEntity*)g_SHLMonsterSceneRecovery[i].hVisual;

		if (pVisual != nullptr)
		{
			pVisual->pev->origin = g_SHLMonsterSceneRecovery[i].holdOrigin;
			pVisual->pev->angles = g_SHLMonsterSceneRecovery[i].holdAngles;
			UTIL_SetOrigin(pVisual->pev, g_SHLMonsterSceneRecovery[i].holdOrigin);

			pVisual->pev->velocity = g_vecZero;
			pVisual->pev->avelocity = g_vecZero;
			pVisual->pev->solid = SOLID_SLIDEBOX;
			pVisual->pev->movetype = MOVETYPE_NONE;
			pVisual->pev->takedamage = DAMAGE_YES;
			pVisual->pev->health = 100000.0f;
			pVisual->pev->effects &= ~EF_NODRAW;

			UTIL_SetSize(pVisual->pev, pMonster->pev->mins, pMonster->pev->maxs);

			CSHLMonsterRecoveryVisual* pVisualAnimating =
				(CSHLMonsterRecoveryVisual*)pVisual;

			if (g_SHLMonsterSceneRecovery[i].phase == SHL_MONSTER_RECOVERY_PHASE_KNOCKDOWN ||
				g_SHLMonsterSceneRecovery[i].phase == SHL_MONSTER_RECOVERY_PHASE_GETUP)
			{
				SHL_AdvanceRecoveryVisual(pVisual);
			}
			else
			{
				pVisualAnimating->pev->sequence =
					g_SHLMonsterSceneRecovery[i].holdSequenceId;

				pVisualAnimating->pev->frame =
					g_SHLMonsterSceneRecovery[i].holdFrame;

				pVisualAnimating->pev->framerate = 0.0f;

				for (int controllerIndex = 0; controllerIndex < 4; ++controllerIndex)
				{
					pVisualAnimating->pev->controller[controllerIndex] =
						g_SHLMonsterSceneRecovery[i].holdController[controllerIndex];
				}

				for (int blendingIndex = 0; blendingIndex < 2; ++blendingIndex)
				{
					pVisualAnimating->pev->blending[blendingIndex] =
						g_SHLMonsterSceneRecovery[i].holdBlending[blendingIndex];
				}
			}
		}
	}
}

bool SHL_IsMonsterInSceneNpcRecovery(CBaseEntity* pMonster)
{
	if (pMonster == nullptr)
		return false;

	const int index = SHL_GetMonsterRecoveryIndex(pMonster);

	if (index <= 0)
		return false;

	if (!g_SHLMonsterSceneRecovery[index].active)
		return false;

	CBaseEntity* pStoredMonster =
		(CBaseEntity*)g_SHLMonsterSceneRecovery[index].hMonster;

	if (pStoredMonster != pMonster)
		return false;

	if (gpGlobals->time >= g_SHLMonsterSceneRecovery[index].endTime)
		return false;

	return true;
}

bool SHL_ApplyMonsterSceneRecovery(CBaseAnimating* pMonster)
{
	if (pMonster == nullptr)
		return false;

	if (!SHL_IsMonsterInSceneNpcRecovery(pMonster))
		return false;

	pMonster->pev->velocity = g_vecZero;
	pMonster->pev->avelocity = g_vecZero;
	pMonster->pev->framerate = 0.0f;
	pMonster->pev->effects |= EF_NODRAW;
	pMonster->pev->solid = SOLID_NOT;

	return true;
}

static bool SHL_IsSceneState(int state)
{
	switch (state)
	{
	case SHL_PLAYERSTATE_GRABBED:
	case SHL_PLAYERSTATE_ACTIVE_SCENE:
	case SHL_PLAYERSTATE_CLIMAX_LOCKED:
	case SHL_PLAYERSTATE_DEFEAT_SCENE:
		return true;

	default:
		break;
	}

	return false;
}

static void SHL_ReleaseSceneSlotMonsters(int index)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	for (int slot = 1; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
	{
		CBaseEntity* pMonster =
			(CBaseEntity*)g_SHLSceneSlots[index].hSlotMonsters[slot];

		if (pMonster == nullptr)
			continue;

		pMonster->pev->velocity = g_vecZero;
		pMonster->pev->avelocity = g_vecZero;
		pMonster->pev->framerate = 1.0f;
		pMonster->pev->nextthink = gpGlobals->time + 0.01f;

		g_SHLSceneSlots[index].hSlotMonsters[slot] = nullptr;

		if (SHL_DebugEnabled())
		{
			ALERT(
				at_console,
				"SHL: released scene joiner slot=%d entity=%d\n",
				slot,
				ENTINDEX(pMonster->edict()));
		}
	}
}

static void SHL_FreezeSceneSlotMonsters(int index)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	for (int slot = 0; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
	{
		CBaseEntity* pMonster =
			(CBaseEntity*)g_SHLSceneSlots[index].hSlotMonsters[slot];

		if (pMonster == nullptr)
			continue;

		pMonster->pev->velocity = g_vecZero;
		pMonster->pev->avelocity = g_vecZero;
		pMonster->pev->framerate = 1.0f;
		pMonster->pev->nextthink = gpGlobals->time + 0.01f;
	}
}

static void SHL_ClearSceneSlot(int index)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;
	SHL_ReleaseSceneSlotMonsters(index);

	CBaseEntity* pActor = (CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor;

	if (pActor != nullptr)
	{
		SHL_RemoveSceneActor(pActor);
	}

	g_SHLSceneSlots[index].active = false;
	g_SHLSceneSlots[index].sceneType = SHL_SCENE_NONE;
	g_SHLSceneSlots[index].startTime = 0.0f;
	g_SHLSceneSlots[index].endTime = 0.0f;
	g_SHLSceneSlots[index].duration = 0.0f;
	g_SHLSceneSlots[index].lastTickTime = 0.0f;

	g_SHLSceneSlots[index].paused = false;
	g_SHLSceneSlots[index].pauseStartTime = 0.0f;

	g_SHLSceneSlots[index].startAnimationStarted = false;
	g_SHLSceneSlots[index].loopAnimationStarted = false;

	g_SHLSceneSlots[index].climaxAnimationStarted = false;
	g_SHLSceneSlots[index].climaxStartTime = 0.0f;

	g_SHLSceneSlots[index].pendingLoopAfterPlayerClimax = false;
	g_SHLSceneSlots[index].returnToLoopTime = 0.0f;

	g_SHLSceneSlots[index].npcEndurance = 0.0f;
	g_SHLSceneSlots[index].npcClimaxStarted = false;
	g_SHLSceneSlots[index].npcClimaxStartTime = 0.0f;

	g_SHLSceneSlots[index].escapeProgress = 0.0f;
	g_SHLSceneSlots[index].lastEscapeMashTime = 0.0f;

	g_SHLSceneSlots[index].hOwnerMonster = nullptr;
	g_SHLSceneSlots[index].hPlayerSceneActor = nullptr;

	for (int slot = 0; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
	{
		g_SHLSceneSlots[index].hSlotMonsters[slot] = nullptr;
		g_SHLSceneSlots[index].slotAnimVariation[slot] = -1;
		g_SHLSceneSlots[index].slotJoinStartTime[slot] = 0.0f;
		g_SHLSceneSlots[index].slotLoopStarted[slot] = false;
	}

	g_SHLSceneSlots[index].sceneOrigin = g_vecZero;
	g_SHLSceneSlots[index].sceneYaw = 0.0f;
}

const char* SHL_SceneTypeName(int sceneType)
{
	switch (sceneType)
	{
	case SHL_SCENE_NONE:
		return "NONE";

	case SHL_SCENE_DEBUG:
		return "DEBUG";

	case SHL_SCENE_GROUNDED_GRAB:
		return "GROUNDED_GRAB";

	default:
		break;
	}

	return "UNKNOWN";
}

const char* SHL_SceneEndReasonName(int reason)
{
	switch (reason)
	{
	case SHL_SCENE_END_NONE:
		return "NONE";

	case SHL_SCENE_END_TIMER:
		return "TIMER";

	case SHL_SCENE_END_FORCED:
		return "FORCED";

	case SHL_SCENE_END_PLAYER_DIED:
		return "PLAYER_DIED";

	case SHL_SCENE_END_MONSTER_DIED:
		return "MONSTER_DIED";

	case SHL_SCENE_END_STALE_SLOT:
		return "STALE_SLOT";

	default:
		break;
	}

	return "UNKNOWN";
}

void SHL_InitSceneSystem()
{
	for (int i = 0; i < SHL_MAX_SCENE_PLAYERS; ++i)
	{
		SHL_ClearSceneSlot(i);
		SHL_ResetGroundedEscape(i);
	}

	for (int i = 0; i < SHL_MAX_MONSTER_SCENE_RECOVERY; ++i)
	{
		SHL_ClearMonsterSceneRecoverySlot(i);
		g_SHLMonsterRegrabBlockedUntil[i] = 0.0f;
	}
}

bool SHL_GetPlayerSceneAnchor(edict_t* pPlayer, Vector& origin, float& yaw)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	origin = g_vecZero;
	yaw = 0.0f;

	if (index <= 0)
		return false;

	if (!g_SHLSceneSlots[index].active)
		return false;

	origin = g_SHLSceneSlots[index].sceneOrigin;
	yaw = g_SHLSceneSlots[index].sceneYaw;

	return true;
}

bool SHL_ReapplyCurrentSceneAnchors(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return false;

	if (!g_SHLSceneSlots[index].active)
		return false;

	SHL_StopPlayerSceneMotion(pPlayer);

	CBaseEntity* pOwner =
		(CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster;

	if (pOwner == nullptr)
		return false;

	CBaseMonster* pMonster = pOwner->MyMonsterPointer();

	if (pMonster == nullptr)
		return false;

	switch (g_SHLSceneSlots[index].sceneType)
	{
	case SHL_SCENE_GROUNDED_GRAB:
		for (int slot = 0; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
		{
			CBaseMonster* pSlotMonster =
				(CBaseMonster*)((CBaseEntity*)g_SHLSceneSlots[index].hSlotMonsters[slot]);

			if (pSlotMonster == nullptr)
				continue;

			SHL_PositionSceneMonsterSlot(
				index,
				pPlayer,
				pSlotMonster,
				slot,
				false);
		}

		return true;

	default:
		break;
	}

	return false;
}

bool SHL_IsPlayerInScene(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return false;

	if (!g_SHLSceneSlots[index].active)
		return false;

	return true;
}

static void SHL_FreezeSceneAnimating(CBaseEntity* pEntity)
{
	if (pEntity == nullptr)
		return;

	CBaseAnimating* pAnimating = (CBaseAnimating*)pEntity;

	pAnimating->pev->velocity = g_vecZero;
	pAnimating->pev->avelocity = g_vecZero;
	pAnimating->pev->framerate = 0.0f;
}

static void SHL_FreezeSceneSlotVisuals(int index)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	if (g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
	{
		SHL_FreezeSceneAnimating(
			(CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);
	}

	if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
	{
		SHL_FreezeSceneAnimating(
			(CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);
	}
}

bool SHL_IsPlayerScenePaused(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return false;

	if (!g_SHLSceneSlots[index].active)
		return false;

	return g_SHLSceneSlots[index].paused;
}

void SHL_SetPlayerScenePaused(edict_t* pPlayer, bool paused)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return;

	if (!g_SHLSceneSlots[index].active)
		return;

	if (g_SHLSceneSlots[index].paused == paused)
		return;

	if (paused)
	{
		g_SHLSceneSlots[index].paused = true;
		g_SHLSceneSlots[index].pauseStartTime = gpGlobals->time;
		g_SHLSceneSlots[index].lastTickTime = gpGlobals->time;

		SHL_FreezeSceneSlotVisuals(index);

		if (SHL_DebugEnabled())
			ALERT(at_console, "SHL: scene paused\n");

		return;
	}

	const float pausedDuration =
		gpGlobals->time - g_SHLSceneSlots[index].pauseStartTime;

	g_SHLSceneSlots[index].paused = false;
	g_SHLSceneSlots[index].pauseStartTime = 0.0f;

	if (pausedDuration > 0.0f)
	{
		g_SHLSceneSlots[index].startTime += pausedDuration;
		g_SHLSceneSlots[index].endTime += pausedDuration;
		g_SHLSceneSlots[index].lastTickTime = gpGlobals->time;

		if (g_SHLSceneSlots[index].climaxStartTime > 0.0f)
			g_SHLSceneSlots[index].climaxStartTime += pausedDuration;

		if (g_SHLSceneSlots[index].returnToLoopTime > 0.0f)
			g_SHLSceneSlots[index].returnToLoopTime += pausedDuration;

		if (g_SHLSceneSlots[index].npcClimaxStartTime > 0.0f)
			g_SHLSceneSlots[index].npcClimaxStartTime += pausedDuration;
	}

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: scene resumed pausedDuration=%.2f\n",
			pausedDuration);
	}
}

bool SHL_TogglePlayerScenePaused(edict_t* pPlayer)
{
	const bool paused = SHL_IsPlayerScenePaused(pPlayer);

	SHL_SetPlayerScenePaused(pPlayer, !paused);

	return !paused;
}

CBaseEntity* SHL_GetPlayerSceneOwner(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return nullptr;

	if (!g_SHLSceneSlots[index].active)
		return nullptr;

	return (CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster;
}

CBaseEntity* SHL_GetPlayerSceneSlotMonster(edict_t* pPlayer, int slot)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return nullptr;

	if (!g_SHLSceneSlots[index].active)
		return nullptr;

	if (slot < 0 || slot >= SHL_MONSTER_SCENE_MAX_SLOTS)
		return nullptr;

	return (CBaseEntity*)g_SHLSceneSlots[index].hSlotMonsters[slot];
}

int SHL_GetPlayerSceneUsedMonsterSlots(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return 0;

	if (!g_SHLSceneSlots[index].active)
		return 0;

	int count = 0;

	for (int slot = 0; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
	{
		if (g_SHLSceneSlots[index].hSlotMonsters[slot] != nullptr)
			++count;
	}

	return count;
}

bool SHL_TryAddMonsterToPlayerSceneSlot(
	edict_t* pPlayer,
	CBaseMonster* pMonster,
	int slot)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return false;

	if (!g_SHLSceneSlots[index].active)
		return false;

	if (pMonster == nullptr)
		return false;

	if (slot <= 0 || slot >= SHL_MONSTER_SCENE_MAX_SLOTS)
		return false;

	if (g_SHLSceneSlots[index].hSlotMonsters[slot] != nullptr)
		return false;

	if (!pMonster->IsAlive() || pMonster->pev->deadflag != DEAD_NO)
		return false;

	CBaseEntity* pOwner =
		(CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster;

	if (pOwner == pMonster)
		return false;

	const shl_monster_scene_profile_t* pMonsterProfile =
		SHL_GetMonsterSceneProfile(
			pMonster,
			g_SHLSceneSlots[index].sceneType);

	if (pMonsterProfile == nullptr)
		return false;

	if (!SHL_PositionSceneMonsterSlot(
			index,
			pPlayer,
			pMonster,
			slot,
			false))
	{
		return false;
	}

	int variation = 0;

	const shl_monster_scene_slot_anim_set_t& animSet =
		pMonsterProfile->monsterSlotAnimSets[slot];

	if (animSet.variationCount > 1)
	{
		int maxVariation = animSet.variationCount - 1;

		if (maxVariation >= SHL_MONSTER_SCENE_MAX_SLOT_ANIM_VARIANTS)
			maxVariation = SHL_MONSTER_SCENE_MAX_SLOT_ANIM_VARIANTS - 1;

		variation = RANDOM_LONG(0, maxVariation);
	}

	g_SHLSceneSlots[index].hSlotMonsters[slot] = pMonster;
	g_SHLSceneSlots[index].slotAnimVariation[slot] = variation;

	g_SHLSceneSlots[index].slotJoinStartTime[slot] = gpGlobals->time;
	g_SHLSceneSlots[index].slotLoopStarted[slot] = false;

	CBaseAnimating* pAnimating = (CBaseAnimating*)pMonster;

	shl_monster_scene_slot_anim_variation_t slotAnim;

	if (SHL_GetMonsterSceneSlotAnimVariation(
			pMonsterProfile,
			slot,
			variation,
			slotAnim))
	{
		SHL_TryPlaySequence(
			pAnimating,
			slotAnim.startSequence,
			"scene joiner start");
	}

	pMonster->pev->velocity = g_vecZero;
	pMonster->pev->avelocity = g_vecZero;

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: monster joined scene slot=%d classname=%s\n",
			slot,
			STRING(pMonster->pev->classname));
	}

	return true;
}

bool SHL_IsMonsterSceneOwner(CBaseMonster* pMonster)
{
	if (pMonster == nullptr)
		return false;

	for (int i = 1; i < SHL_MAX_SCENE_PLAYERS; ++i)
	{
		if (!g_SHLSceneSlots[i].active)
			continue;

		for (int slot = 0; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
		{
			CBaseMonster* pSlotMonster =
				(CBaseMonster*)((CBaseEntity*)g_SHLSceneSlots[i].hSlotMonsters[slot]);

			if (pSlotMonster == pMonster)
				return true;
		}
	}

	return false;
}

static Vector SHL_ForwardRightOffset(const Vector& origin, float yaw, float forwardOffset, float rightOffset, float zOffset)
{
	Vector angles = g_vecZero;
	angles.y = yaw;

	MAKE_VECTORS(angles);

	Vector result = origin;
	result = result + gpGlobals->v_forward * forwardOffset;
	result = result + gpGlobals->v_right * rightOffset;
	result.z += zOffset;

	return result;
}

static float SHL_NormalizeYaw360(float yaw)
{
	while (yaw >= 360.0f)
		yaw -= 360.0f;

	while (yaw < 0.0f)
		yaw += 360.0f;

	return yaw;
}

int SHL_GetPlayerSceneType(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return SHL_SCENE_NONE;

	if (!g_SHLSceneSlots[index].active)
		return SHL_SCENE_NONE;

	return g_SHLSceneSlots[index].sceneType;
}

float SHL_GetPlayerSceneDuration(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return 0.0f;

	if (!g_SHLSceneSlots[index].active)
		return 0.0f;

	return g_SHLSceneSlots[index].duration;
}

float SHL_GetPlayerSceneElapsed(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return 0.0f;

	if (!g_SHLSceneSlots[index].active)
		return 0.0f;

	const float elapsed = gpGlobals->time - g_SHLSceneSlots[index].startTime;

	if (elapsed < 0.0f)
		return 0.0f;

	return elapsed;
}

float SHL_GetPlayerSceneRemaining(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return 0.0f;

	if (!g_SHLSceneSlots[index].active)
		return 0.0f;

	const float remaining = g_SHLSceneSlots[index].endTime - gpGlobals->time;

	if (remaining < 0.0f)
		return 0.0f;

	return remaining;
}

float SHL_GetPlayerSceneProgress(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return 0.0f;

	if (!g_SHLSceneSlots[index].active)
		return 0.0f;

	const float duration = g_SHLSceneSlots[index].duration;

	if (duration <= 0.0f)
		return 1.0f;

	float progress = SHL_GetPlayerSceneElapsed(pPlayer) / duration;

	if (progress < 0.0f)
		progress = 0.0f;

	if (progress > 1.0f)
		progress = 1.0f;

	return progress;
}

float SHL_GetPlayerSceneEscapeProgress(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return 0.0f;

	if (!g_SHLSceneSlots[index].active)
		return 0.0f;

	const shl_scene_profile_t* pProfile =
		SHL_GetSceneProfile(g_SHLSceneSlots[index].sceneType);

	if (pProfile == nullptr)
		return 0.0f;

	if (!pProfile->supportsEscape)
		return 0.0f;

	if (pProfile->escapeRequired <= 0.0f)
		return 1.0f;

	float progress =
		g_SHLSceneSlots[index].escapeProgress / pProfile->escapeRequired;

	if (progress < 0.0f)
		progress = 0.0f;

	if (progress > 1.0f)
		progress = 1.0f;

	return progress;
}

bool SHL_AddSceneEscapeMash(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return false;

	if (!g_SHLSceneSlots[index].active)
	{
		return SHL_AddGroundedEscapeMash(pPlayer);
	}

	if (g_SHLSceneSlots[index].paused)
		return false;

	const shl_scene_profile_t* pProfile =
		SHL_GetSceneProfile(g_SHLSceneSlots[index].sceneType);

	if (pProfile == nullptr)
		return false;

	if (!pProfile->supportsEscape)
		return false;

	if (pProfile->escapeRequired <= 0.0f)
		return false;

	if (pProfile->escapeGainPerMash <= 0.0f)
		return false;

	if (g_SHLSceneSlots[index].npcClimaxStarted)
		return false;

	if (SHL_GetPlayerStateId(pPlayer) != SHL_PLAYERSTATE_GRABBED &&
		SHL_GetPlayerStateId(pPlayer) != SHL_PLAYERSTATE_ACTIVE_SCENE)
	{
		return false;
	}

	const float mashCooldown = 0.08f;

	if (gpGlobals->time < g_SHLSceneSlots[index].lastEscapeMashTime + mashCooldown)
		return false;

	g_SHLSceneSlots[index].lastEscapeMashTime = gpGlobals->time;
	g_SHLSceneSlots[index].escapeProgress += pProfile->escapeGainPerMash;

	if (g_SHLSceneSlots[index].escapeProgress > pProfile->escapeRequired)
		g_SHLSceneSlots[index].escapeProgress = pProfile->escapeRequired;

	SHL_SendEscapeBar(
		pPlayer,
		true,
		g_SHLSceneSlots[index].escapeProgress / pProfile->escapeRequired);

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: scene escape mash progress %.1f / %.1f\n",
			g_SHLSceneSlots[index].escapeProgress,
			pProfile->escapeRequired);
	}

	if (g_SHLSceneSlots[index].escapeProgress >= pProfile->escapeRequired)
	{
		if (SHL_DebugEnabled())
		{
			ALERT(
				at_console,
				"SHL: scene escape success type=%s, starting monster escape recovery\n",
				SHL_SceneTypeName(g_SHLSceneSlots[index].sceneType));
		}

		CBaseEntity* pOwnerMonster =
			(CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster;

		if (pOwnerMonster != nullptr)
		{
			const shl_monster_scene_profile_t* pMonsterProfile =
				SHL_GetMonsterSceneProfile(
					pOwnerMonster,
					g_SHLSceneSlots[index].sceneType);

			if (pMonsterProfile != nullptr)
			{
				const bool wantsDedicatedEscapeRecovery =
					pMonsterProfile->monsterEscapeKnockdownSequence != nullptr &&
					pMonsterProfile->monsterEscapeKnockdownSequence[0] != '\0' &&
					pMonsterProfile->monsterEscapeKnockdownDuration > 0.0f &&
					pMonsterProfile->monsterRecoveryGetupSequence != nullptr &&
					pMonsterProfile->monsterRecoveryGetupSequence[0] != '\0' &&
					pMonsterProfile->monsterRecoveryGetupDuration > 0.0f;

				SHL_StartMonsterSceneRecoveryEx(
					pOwnerMonster,
					pMonsterProfile->monsterNpcClimaxSequence,
					wantsDedicatedEscapeRecovery ? 0.0f : SHL_NormalEscapeRecoveryDuration(),
					pMonsterProfile->monsterEscapeKnockdownSequence,
					pMonsterProfile->monsterEscapeKnockdownDuration,
					pMonsterProfile->monsterRecoveryGetupSequence,
					pMonsterProfile->monsterRecoveryGetupDuration);
			}

			SHL_SetMonsterRegrabCooldown(
				pOwnerMonster,
				SHL_NormalEscapeRecoveryDuration() + SHL_NormalRegrabCooldown());
		}

		SHL_SendEscapeBar(pPlayer, false, 0.0f);
		SHL_EndPlayerScene(pPlayer, SHL_SCENE_END_FORCED);
		return true;
	}
}

static void SHL_StartSceneSlot(edict_t* pPlayer, int sceneType, CBaseMonster* pOwnerMonster, float duration)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return;

	g_SHLSceneSlots[index].active = true;
	g_SHLSceneSlots[index].sceneType = sceneType;
	g_SHLSceneSlots[index].startTime = gpGlobals->time;
	g_SHLSceneSlots[index].duration = duration;
	g_SHLSceneSlots[index].endTime = gpGlobals->time + duration;
	g_SHLSceneSlots[index].lastTickTime = gpGlobals->time;

	g_SHLSceneSlots[index].paused = false;
	g_SHLSceneSlots[index].pauseStartTime = 0.0f;

	g_SHLSceneSlots[index].startAnimationStarted = false;
	g_SHLSceneSlots[index].loopAnimationStarted = false;

	g_SHLSceneSlots[index].climaxAnimationStarted = false;
	g_SHLSceneSlots[index].climaxStartTime = 0.0f;

	g_SHLSceneSlots[index].pendingLoopAfterPlayerClimax = false;
	g_SHLSceneSlots[index].returnToLoopTime = 0.0f;

	g_SHLSceneSlots[index].npcEndurance = SHL_NormalNpcEnduranceMax();
	g_SHLSceneSlots[index].npcClimaxStarted = false;
	g_SHLSceneSlots[index].npcClimaxStartTime = 0.0f;

	g_SHLSceneSlots[index].escapeProgress = 0.0f;
	g_SHLSceneSlots[index].lastEscapeMashTime = 0.0f;
	SHL_ResetGroundedEscape(index);

	const shl_scene_profile_t* pProfile = SHL_GetSceneProfile(sceneType);

	if (pProfile != nullptr && pProfile->supportsEscape)
	{
		SHL_SendEscapeBar(pPlayer, true, 0.0f);
	}
	else
	{
		SHL_SendEscapeBar(pPlayer, false, 0.0f);
	}

	g_SHLSceneSlots[index].hOwnerMonster = pOwnerMonster;
	g_SHLSceneSlots[index].hPlayerSceneActor = nullptr;

	for (int slot = 0; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
	{
		g_SHLSceneSlots[index].hSlotMonsters[slot] = nullptr;
		g_SHLSceneSlots[index].slotAnimVariation[slot] = -1;
		g_SHLSceneSlots[index].slotJoinStartTime[slot] = 0.0f;
		g_SHLSceneSlots[index].slotLoopStarted[slot] = false;
	}

	g_SHLSceneSlots[index].hSlotMonsters[0] = pOwnerMonster;
	g_SHLSceneSlots[index].slotAnimVariation[0] = 0;
	g_SHLSceneSlots[index].slotJoinStartTime[0] = gpGlobals->time;
	g_SHLSceneSlots[index].slotLoopStarted[0] = false;

	if (pOwnerMonster != nullptr)
	{
		const shl_monster_scene_profile_t* pMonsterProfile =
			SHL_GetMonsterSceneProfile(pOwnerMonster, sceneType);

		if (pMonsterProfile != nullptr &&
			pMonsterProfile->playerSceneActorModel != nullptr &&
			pMonsterProfile->playerSceneActorModel[0] != '\0')
		{
			CBaseEntity* pActor = SHL_CreatePlayerSceneActor(
				pPlayer,
				pMonsterProfile->playerSceneActorModel);

			g_SHLSceneSlots[index].hPlayerSceneActor = pActor;
		}
	}
}

void SHL_EndPlayerScene(edict_t* pPlayer, int reason)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return;

	if (!g_SHLSceneSlots[index].active)
		return;

	const int sceneType = g_SHLSceneSlots[index].sceneType;
	const shl_scene_profile_t* pProfile = SHL_GetSceneProfile(sceneType);

	SHL_SendEscapeBar(pPlayer, false, 0.0f);
	
	SHL_ClearSceneSlot(index);

	const int state = SHL_GetPlayerStateId(pPlayer);

	if (pProfile != nullptr && pProfile->restorePlayerToNormalOnEnd && SHL_IsSceneState(state))
	{
		SHL_SetPlayerState(pPlayer, SHL_PLAYERSTATE_NORMAL);
	}

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: scene ended type=%s reason=%s restoreNormal=%d\n",
			SHL_SceneTypeName(sceneType),
			SHL_SceneEndReasonName(reason),
			(pProfile != nullptr && pProfile->restorePlayerToNormalOnEnd) ? 1 : 0);
	}
}

bool SHL_TryStartGroundedGrabScene(CBaseMonster* pMonster, edict_t* pPlayer, float duration)
{
	if (pMonster == nullptr)
	{
		if (SHL_DebugEnabled())
			ALERT(at_console, "SHL: grounded grab rejected: monster null\n");

		return false;
	}

	if (pPlayer == nullptr)
	{
		if (SHL_DebugEnabled())
			ALERT(at_console, "SHL: grounded grab rejected: player edict null\n");

		return false;
	}

	CBaseEntity* pPlayerEntity = CBaseEntity::Instance(pPlayer);

	if (pPlayerEntity == nullptr)
	{
		if (SHL_DebugEnabled())
			ALERT(at_console, "SHL: grounded grab rejected: player entity null\n");

		return false;
	}

	if (!pPlayerEntity->IsPlayer())
	{
		if (SHL_DebugEnabled())
			ALERT(at_console, "SHL: grounded grab rejected: target is not player\n");

		return false;
	}

	if (!pPlayerEntity->IsAlive())
	{
		if (SHL_DebugEnabled())
			ALERT(at_console, "SHL: grounded grab rejected: player not alive\n");

		return false;
	}

	if (!pMonster->IsAlive())
	{
		if (SHL_DebugEnabled())
			ALERT(at_console, "SHL: grounded grab rejected: monster not alive\n");

		return false;
	}

	const int playerState = SHL_GetPlayerStateId(pPlayer);

	if (playerState != SHL_PLAYERSTATE_GROUNDED)
	{
		if (SHL_DebugEnabled())
		{
			ALERT(
				at_console,
				"SHL: grounded grab rejected: player state is %d, expected GROUNDED\n",
				playerState);
		}

		return false;
	}

	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
	{
		if (SHL_DebugEnabled())
			ALERT(at_console, "SHL: grounded grab rejected: bad scene index\n");

		return false;
	}

	if (g_SHLSceneSlots[index].active)
	{
		const int currentState = SHL_GetPlayerStateId(pPlayer);

		if (!SHL_IsSceneState(currentState))
		{
			if (SHL_DebugEnabled())
			{
				ALERT(
					at_console,
					"SHL: grounded grab found stale scene slot, clearing it. state=%d\n",
					currentState);
			}

			SHL_ClearSceneSlot(index);
		}
		else
		{
			if (SHL_DebugEnabled())
			{
				ALERT(
					at_console,
					"SHL: grounded grab rejected: scene slot already active type=%s\n",
					SHL_SceneTypeName(g_SHLSceneSlots[index].sceneType));
			}

			return false;
		}
	}

	const shl_scene_profile_t* pProfile = SHL_GetSceneProfile(SHL_SCENE_GROUNDED_GRAB);

	if (duration <= 0.0f)
		duration = pProfile->defaultDuration;

	if (duration <= 0.0f)
	{
		if (SHL_DebugEnabled())
			ALERT(at_console, "SHL: grounded grab rejected: bad duration %.2f\n", duration);

		return false;
	}

	SHL_StopPlayerSceneMotionAndFixAngle(pPlayer);

	SHL_StartSceneSlot(pPlayer, SHL_SCENE_GROUNDED_GRAB, pMonster, duration);

	SHL_StopPlayerSceneMotion(pPlayer);

	SHL_SetGroundedGrabSceneAnchors(index, pMonster, pPlayer);

	SHL_StopPlayerSceneMotion(pPlayer);

	SHL_SetPlayerState(pPlayer, SHL_PLAYERSTATE_GRABBED);

	SHL_StopPlayerSceneMotionAndFixAngle(pPlayer);

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: scene started type=%s owner=%s duration=%.1f\n",
			pProfile->debugName,
			STRING(pMonster->pev->classname),
			duration);
	}

	return true;
}

void SHL_StartDebugScene(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return;

	if (g_SHLSceneSlots[index].active)
	{
		if (SHL_DebugEnabled())
		{
			ALERT(
				at_console,
				"SHL: debug scene rejected: scene already active type=%s\n",
				SHL_SceneTypeName(g_SHLSceneSlots[index].sceneType));
		}

		return;
	}

	const shl_scene_profile_t* pProfile = SHL_GetSceneProfile(SHL_SCENE_DEBUG);

	SHL_StartSceneSlot(
		pPlayer,
		SHL_SCENE_DEBUG,
		nullptr,
		pProfile->defaultDuration);

	SHL_SetPlayerState(pPlayer, SHL_PLAYERSTATE_ACTIVE_SCENE);

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: scene started type=%s duration=%.1f\n",
			pProfile->debugName,
			pProfile->defaultDuration);
	}
}

void SHL_EndDebugScene(edict_t* pPlayer)
{
	SHL_EndPlayerScene(pPlayer, SHL_SCENE_END_FORCED);
}

static void SHL_TryPlaySequence(CBaseAnimating* pAnimating, const char* pszSequenceName, const char* pszDebugOwner)
{
	if (pAnimating == nullptr)
		return;

	if (pszSequenceName == nullptr || pszSequenceName[0] == '\0')
		return;

	const int sequence = pAnimating->LookupSequence(pszSequenceName);

	if (sequence < 0)
	{
		if (SHL_DebugEnabled())
		{
			ALERT(
				at_console,
				"SHL: %s sequence missing: %s\n",
				pszDebugOwner,
				pszSequenceName);
		}

		return;
	}

	pAnimating->pev->sequence = sequence;
	pAnimating->pev->frame = 0.0f;
	pAnimating->pev->framerate = 1.0f;
	pAnimating->ResetSequenceInfo();

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: %s sequence started: %s\n",
			pszDebugOwner,
			pszSequenceName);
	}
}

static void SHL_HoldSequence(CBaseAnimating* pAnimating, const char* pszSequenceName, const char* pszDebugOwner, bool wrapFrame)
{
	if (pAnimating == nullptr)
		return;

	if (pszSequenceName == nullptr || pszSequenceName[0] == '\0')
		return;

	const int sequence = pAnimating->LookupSequence(pszSequenceName);

	if (sequence < 0)
		return;

	if (pAnimating->pev->sequence != sequence)
	{
		SHL_TryPlaySequence(pAnimating, pszSequenceName, pszDebugOwner);
		return;
	}

	if (wrapFrame && pAnimating->pev->frame >= 248.0f)
	{
		pAnimating->pev->frame = 0.0f;
		pAnimating->pev->framerate = 1.0f;
		pAnimating->ResetSequenceInfo();
		return;
	}

	pAnimating->pev->framerate = 1.0f;
	pAnimating->StudioFrameAdvance();
}

static void SHL_HoldSequenceNoRestart(CBaseAnimating* pAnimating, const char* pszSequenceName, bool freezeAtEnd)
{
	if (pAnimating == nullptr)
		return;

	if (pszSequenceName == nullptr || pszSequenceName[0] == '\0')
		return;

	const int sequence = pAnimating->LookupSequence(pszSequenceName);

	if (sequence < 0)
		return;

	if (pAnimating->pev->sequence != sequence)
		return;

	if (freezeAtEnd && pAnimating->pev->frame >= 248.0f)
	{
		pAnimating->pev->framerate = 0.0f;
		return;
	}

	pAnimating->pev->framerate = 1.0f;
	pAnimating->StudioFrameAdvance();
}

static bool SHL_IsSequenceNearEnd(CBaseAnimating* pAnimating, const char* pszSequenceName, float endWindow)
{
	if (pAnimating == nullptr)
		return false;

	if (pszSequenceName == nullptr || pszSequenceName[0] == '\0')
		return false;

	const int sequence = pAnimating->LookupSequence(pszSequenceName);

	if (sequence < 0)
		return false;

	if (pAnimating->pev->sequence != sequence)
		return false;

	return pAnimating->pev->frame >= endWindow;
}

static void SHL_StartSceneClimaxAnimation(edict_t* pPlayer, int index)
{
	if (pPlayer == nullptr)
		return;

	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	if (!g_SHLSceneSlots[index].active)
		return;

	if (g_SHLSceneSlots[index].climaxAnimationStarted)
		return;

	const shl_scene_profile_t* pProfile =
		SHL_GetSceneProfile(g_SHLSceneSlots[index].sceneType);

	if (pProfile == nullptr)
		return;

	if (!pProfile->supportsSceneAnimation)
		return;

	CBaseEntity* pOwnerMonster =
		(CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster;

	const shl_monster_scene_profile_t* pMonsterProfile =
		SHL_GetMonsterSceneProfile(
			pOwnerMonster,
			g_SHLSceneSlots[index].sceneType);

	g_SHLSceneSlots[index].climaxAnimationStarted = true;
	g_SHLSceneSlots[index].climaxStartTime = gpGlobals->time;
	g_SHLSceneSlots[index].pendingLoopAfterPlayerClimax = true;
	g_SHLSceneSlots[index].returnToLoopTime =
		gpGlobals->time + pProfile->postClimaxLoopDelay;

	SHL_ResetSceneSlotLoopsForReturn(
		index,
		g_SHLSceneSlots[index].returnToLoopTime);

	if (pMonsterProfile != nullptr &&
		g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
	{
		CBaseAnimating* pPlayerSceneActor =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

		SHL_TryPlaySequence(
			pPlayerSceneActor,
			pMonsterProfile->playerClimaxSequence,
			"player scene actor");
	}

	SHL_PlaySceneMonsterSlotPlayerClimax(index);

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: player climax animation started type=%s\n",
			pProfile->debugName);
	}
}

static void SHL_StartNpcClimaxAnimation(edict_t* pPlayer, int index)
{
	if (pPlayer == nullptr)
		return;

	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	if (!g_SHLSceneSlots[index].active)
		return;

	if (g_SHLSceneSlots[index].npcClimaxStarted)
		return;

	const shl_scene_profile_t* pProfile =
		SHL_GetSceneProfile(g_SHLSceneSlots[index].sceneType);

	if (pProfile == nullptr)
		return;

	if (!pProfile->supportsSceneAnimation)
		return;

	CBaseEntity* pOwnerMonster =
		(CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster;

	const shl_monster_scene_profile_t* pMonsterProfile =
		SHL_GetMonsterSceneProfile(
			pOwnerMonster,
			g_SHLSceneSlots[index].sceneType);

	g_SHLSceneSlots[index].npcClimaxStarted = true;
	g_SHLSceneSlots[index].npcClimaxStartTime = gpGlobals->time;
	g_SHLSceneSlots[index].climaxAnimationStarted = true;
	g_SHLSceneSlots[index].climaxStartTime = gpGlobals->time;
	g_SHLSceneSlots[index].pendingLoopAfterPlayerClimax = false;
	g_SHLSceneSlots[index].returnToLoopTime = 0.0f;

	if (pMonsterProfile != nullptr &&
		g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
	{
		CBaseAnimating* pPlayerSceneActor =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

		SHL_TryPlaySequence(
			pPlayerSceneActor,
			pMonsterProfile->playerClimaxSequence,
			"player scene actor");
	}

	SHL_PlaySceneMonsterSlotNpcClimax(index);

	if (SHL_GetPlayerStateId(pPlayer) != SHL_PLAYERSTATE_CLIMAX_LOCKED)
	{
		SHL_SetPlayerState(pPlayer, SHL_PLAYERSTATE_CLIMAX_LOCKED);
	}

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: npc climax started type=%s\n",
			pProfile->debugName);
	}
}

static void SHL_HoldSceneClimaxAnimation(edict_t* pPlayer, int index)
{
	if (pPlayer == nullptr)
		return;

	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	if (!g_SHLSceneSlots[index].active)
		return;

	if (!g_SHLSceneSlots[index].climaxAnimationStarted)
		return;

	if (!g_SHLSceneSlots[index].pendingLoopAfterPlayerClimax &&
		!g_SHLSceneSlots[index].npcClimaxStarted)
		return;

	const shl_scene_profile_t* pProfile =
		SHL_GetSceneProfile(g_SHLSceneSlots[index].sceneType);

	if (pProfile == nullptr)
		return;

	if (!pProfile->supportsSceneAnimation)
		return;

	const bool npcClimax =
		g_SHLSceneSlots[index].npcClimaxStarted ? true : false;

	bool climaxNearEnd =
		SHL_IsAnySceneMonsterSlotClimaxNearEnd(index, npcClimax);

	if (!climaxNearEnd && g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
	{
		CBaseAnimating* pPlayerSceneActor =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

		if (SHL_IsSequenceNearEnd(
				pPlayerSceneActor,
				pProfile->playerClimaxSequence,
				248.0f))
		{
			climaxNearEnd = true;
		}
	}

	if (g_SHLSceneSlots[index].npcClimaxStarted)
	{
		if (g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
		{
			CBaseAnimating* pPlayerSceneActor =
				(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

			SHL_HoldSequenceNoRestart(
				pPlayerSceneActor,
				pProfile->playerClimaxSequence,
				true);
		}

		SHL_HoldSceneMonsterSlotClimax(index, true);
		return;
	}

	if (climaxNearEnd)
	{
		if (g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
		{
			CBaseAnimating* pPlayerSceneActor =
				(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

			SHL_TryPlaySequence(
				pPlayerSceneActor,
				pProfile->playerLoopSequence,
				"player scene actor");
		}

		SHL_PlaySceneMonsterSlotLoops(index);

		g_SHLSceneSlots[index].loopAnimationStarted = true;

		if (SHL_DebugEnabled())
		{
			ALERT(
				at_console,
				"SHL: player climax animation near end, visually returning to slot loops\n");
		}

		return;
	}

	if (g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
	{
		CBaseAnimating* pPlayerSceneActor =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

		SHL_HoldSequence(
			pPlayerSceneActor,
			pProfile->playerClimaxSequence,
			"player scene actor",
			false);
	}

	SHL_HoldSceneMonsterSlotClimax(index, false);
}

static void SHL_ThinkNpcClimaxFlow(edict_t* pPlayer, int index)
{
	if (pPlayer == nullptr)
		return;

	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	if (!g_SHLSceneSlots[index].active)
		return;

	const shl_scene_profile_t* pProfile =
		SHL_GetSceneProfile(g_SHLSceneSlots[index].sceneType);

	if (pProfile == nullptr)
		return;

	if (g_SHLSceneSlots[index].npcClimaxStarted)
	{
		bool npcClimaxFinished = false;

		if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
		{
			CBaseAnimating* pOwnerMonster =
				(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

			if (SHL_IsSequenceNearEnd(
					pOwnerMonster,
					pProfile->monsterNpcClimaxSequence,
					248.0f))
			{
				npcClimaxFinished = true;
			}
		}

		if (!npcClimaxFinished && g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
		{
			CBaseAnimating* pPlayerSceneActor =
				(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

			if (SHL_IsSequenceNearEnd(
					pPlayerSceneActor,
					pProfile->playerClimaxSequence,
					248.0f))
			{
				npcClimaxFinished = true;
			}
		}

		if (npcClimaxFinished)
		{
			if (SHL_DebugEnabled())
			{
				ALERT(
					at_console,
					"SHL: npc climax finished, releasing player and starting monster-only recovery\n");
			}

			CBaseEntity* pOwnerMonster =
				(CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster;

			if (SHL_GetPlayerStateId(pPlayer) == SHL_PLAYERSTATE_CLIMAX_LOCKED ||
				SHL_GetPlayerStateId(pPlayer) == SHL_PLAYERSTATE_GRABBED ||
				SHL_GetPlayerStateId(pPlayer) == SHL_PLAYERSTATE_ACTIVE_SCENE)
			{
				SHL_SetPlayerState(pPlayer, SHL_PLAYERSTATE_NORMAL);
			}

			if (pOwnerMonster != nullptr)
			{
				SHL_StartMonsterSceneRecoveryEx(
					pOwnerMonster,
					pProfile->monsterNpcClimaxSequence,
					SHL_NormalNpcRecoveryDuration(),
					nullptr,
					0.0f,
					pProfile->monsterRecoveryGetupSequence,
					pProfile->monsterRecoveryGetupDuration);
			}

			SHL_EndPlayerScene(pPlayer, SHL_SCENE_END_FORCED);
			return;
		}

		return;
	}

	if (g_SHLSceneSlots[index].npcEndurance <= 0.0f)
	{
		SHL_StartNpcClimaxAnimation(pPlayer, index);
		return;
	}
}

static void SHL_ThinkPlayerClimaxReturn(edict_t* pPlayer, int index)
{
	if (pPlayer == nullptr)
		return;

	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	if (!g_SHLSceneSlots[index].active)
		return;

	if (!g_SHLSceneSlots[index].pendingLoopAfterPlayerClimax)
		return;

	if (g_SHLSceneSlots[index].npcClimaxStarted)
		return;

	if (gpGlobals->time < g_SHLSceneSlots[index].returnToLoopTime)
		return;

	const shl_scene_profile_t* pProfile =
		SHL_GetSceneProfile(g_SHLSceneSlots[index].sceneType);

	if (pProfile == nullptr)
		return;

	g_SHLSceneSlots[index].pendingLoopAfterPlayerClimax = false;
	g_SHLSceneSlots[index].climaxAnimationStarted = false;
	g_SHLSceneSlots[index].climaxStartTime = 0.0f;

	g_SHLSceneSlots[index].startAnimationStarted = true;
	g_SHLSceneSlots[index].loopAnimationStarted = true;

	g_SHLSceneSlots[index].escapeProgress = 0.0f;
	g_SHLSceneSlots[index].lastEscapeMashTime = 0.0f;

	for (int slot = 0; slot < SHL_MONSTER_SCENE_MAX_SLOTS; ++slot)
	{
		if (g_SHLSceneSlots[index].hSlotMonsters[slot] == nullptr)
			continue;

		g_SHLSceneSlots[index].slotLoopStarted[slot] = true;
		g_SHLSceneSlots[index].slotJoinStartTime[slot] = gpGlobals->time;
	}

	if (SHL_GetPlayerStateId(pPlayer) == SHL_PLAYERSTATE_CLIMAX_LOCKED)
	{
		SHL_SetPlayerState(pPlayer, SHL_PLAYERSTATE_GRABBED);
	}

	if (pProfile->supportsEscape)
	{
		SHL_SendEscapeBar(pPlayer, true, 0.0f);
	}

	if (pProfile->supportsSceneAnimation)
	{
		const shl_monster_scene_profile_t* pMonsterProfile =
			SHL_GetMonsterSceneProfile(
				(CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster,
				g_SHLSceneSlots[index].sceneType);

		if (pMonsterProfile != nullptr &&
			g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
		{
			CBaseAnimating* pPlayerSceneActor =
				(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

			SHL_TryPlaySequence(
				pPlayerSceneActor,
				pMonsterProfile->playerLoopSequence,
				"player scene actor loop return");
		}

		SHL_PlaySceneMonsterSlotLoops(index);
	}

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: player climax finished, returning to selected slot loops\n");
	}
}

static void SHL_PlaySceneStartAnimation(edict_t* pPlayer, int index)
{
	if (pPlayer == nullptr)
		return;

	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	if (!g_SHLSceneSlots[index].active)
		return;

	const shl_scene_profile_t* pProfile =
		SHL_GetSceneProfile(g_SHLSceneSlots[index].sceneType);

	if (pProfile == nullptr)
		return;

	if (!pProfile->supportsSceneAnimation)
		return;

	const shl_monster_scene_profile_t* pMonsterProfile =
		SHL_GetMonsterSceneProfile(
			(CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster,
			g_SHLSceneSlots[index].sceneType);

	if (pMonsterProfile == nullptr)
		return;

	if (g_SHLSceneSlots[index].startAnimationStarted)
	{
		if (!g_SHLSceneSlots[index].loopAnimationStarted &&
			!g_SHLSceneSlots[index].climaxAnimationStarted)
		{
			if (g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
			{
				CBaseAnimating* pPlayerSceneActor =
					(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

				SHL_HoldSequence(
					pPlayerSceneActor,
					pMonsterProfile->playerStartSequence,
					"player scene actor",
					false);
			}

			if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
			{
				CBaseAnimating* pOwnerMonster =
					(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

				SHL_HoldSequence(
					pOwnerMonster,
					pMonsterProfile->monsterStartSequence,
					"monster",
					false);
			}
		}

		return;
	}

	g_SHLSceneSlots[index].startAnimationStarted = true;

	if (g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
	{
		CBaseAnimating* pPlayerSceneActor =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

		SHL_TryPlaySequence(
			pPlayerSceneActor,
			pMonsterProfile->playerStartSequence,
			"player scene actor");
	}

	if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
	{
		CBaseAnimating* pOwnerMonster =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

		SHL_TryPlaySequence(
			pOwnerMonster,
			pMonsterProfile->monsterStartSequence,
			"monster");
	}

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: scene start animation triggered type=%s monsterProfile=%s\n",
			pProfile->debugName,
			pMonsterProfile->debugName);
	}
}

static void SHL_PlaySceneLoopAnimation(edict_t* pPlayer, int index)
{
	if (pPlayer == nullptr)
		return;

	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	if (!g_SHLSceneSlots[index].active)
		return;

	if (g_SHLSceneSlots[index].climaxAnimationStarted)
		return;

	const shl_scene_profile_t* pProfile =
		SHL_GetSceneProfile(g_SHLSceneSlots[index].sceneType);

	if (pProfile == nullptr)
		return;

	if (!pProfile->supportsSceneAnimation)
		return;

	const shl_monster_scene_profile_t* pMonsterProfile =
		SHL_GetMonsterSceneProfile(
			(CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster,
			g_SHLSceneSlots[index].sceneType);

	if (pMonsterProfile == nullptr)
		return;

	bool startIsNearEnd = false;

	if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
	{
		CBaseAnimating* pOwnerMonster =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

		if (SHL_IsSequenceNearEnd(
				pOwnerMonster,
				pMonsterProfile->monsterStartSequence,
				220.0f))
		{
			startIsNearEnd = true;
		}
	}

	if (!startIsNearEnd && g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
	{
		CBaseAnimating* pPlayerSceneActor =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

		if (SHL_IsSequenceNearEnd(
				pPlayerSceneActor,
				pMonsterProfile->playerStartSequence,
				220.0f))
		{
			startIsNearEnd = true;
		}
	}

	const float elapsed = gpGlobals->time - g_SHLSceneSlots[index].startTime;

	if (!g_SHLSceneSlots[index].loopAnimationStarted)
	{
		if (!startIsNearEnd && elapsed < pProfile->startSequenceDuration)
			return;

		g_SHLSceneSlots[index].loopAnimationStarted = true;

		if (g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
		{
			CBaseAnimating* pPlayerSceneActor =
				(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

			SHL_TryPlaySequence(
				pPlayerSceneActor,
				pMonsterProfile->playerLoopSequence,
				"player scene actor");
		}

		if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
		{
			CBaseAnimating* pOwnerMonster =
				(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

			SHL_PlaySceneMonsterSlotLoops(index);
		}

		if (SHL_DebugEnabled())
		{
			ALERT(
				at_console,
				"SHL: scene loop animation triggered type=%s monsterProfile=%s elapsed=%.2f nearEnd=%d\n",
				pProfile->debugName,
				pMonsterProfile->debugName,
				elapsed,
				startIsNearEnd ? 1 : 0);
		}

		return;
	}

	if (g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
	{
		CBaseAnimating* pPlayerSceneActor =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

		SHL_HoldSequence(
			pPlayerSceneActor,
			pMonsterProfile->playerLoopSequence,
			"player scene actor",
			true);
	}

	if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
	{
		CBaseAnimating* pOwnerMonster =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

		SHL_HoldSequence(
			pOwnerMonster,
			pMonsterProfile->monsterLoopSequence,
			"monster",
			true);
	}
}

static void SHL_ApplySceneProfileTick(edict_t* pPlayer, int index)
{
	if (pPlayer == nullptr)
		return;

	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

	if (!g_SHLSceneSlots[index].active)
		return;

	const shl_scene_profile_t* pProfile =
		SHL_GetSceneProfile(g_SHLSceneSlots[index].sceneType);

	if (pProfile == nullptr)
		return;

	float deltaTime = gpGlobals->time - g_SHLSceneSlots[index].lastTickTime;

	if (deltaTime <= 0.0f)
		return;

	if (deltaTime > 0.25f)
		deltaTime = 0.25f;

	g_SHLSceneSlots[index].lastTickTime = gpGlobals->time;

	const int stateBeforeStim = SHL_GetPlayerStateId(pPlayer);

	if (stateBeforeStim != SHL_PLAYERSTATE_CLIMAX_LOCKED &&
		!g_SHLSceneSlots[index].climaxAnimationStarted &&
		pProfile->stimulationPerSecond > 0.0f)
	{
		const float amount = pProfile->stimulationPerSecond * deltaTime;

		if (amount > 0.0f)
			SHL_AddPlayerStimulation(pPlayer, amount);
	}

	if (!g_SHLSceneSlots[index].npcClimaxStarted &&
		!g_SHLSceneSlots[index].climaxAnimationStarted)
	{
		const float npcDrain =
			SHL_NormalNpcEnduranceDrainPerSecond() * deltaTime;

		if (npcDrain > 0.0f)
		{
			g_SHLSceneSlots[index].npcEndurance -= npcDrain;

			if (g_SHLSceneSlots[index].npcEndurance < 0.0f)
				g_SHLSceneSlots[index].npcEndurance = 0.0f;
		}
	}

	const int stateAfterTick = SHL_GetPlayerStateId(pPlayer);

	if (stateAfterTick == SHL_PLAYERSTATE_CLIMAX_LOCKED)
	{
		SHL_StartSceneClimaxAnimation(pPlayer, index);
	}
}

void SHL_SceneThink()
{
	SHL_MonsterSceneRecoveryThink();
	SHL_ThinkGroundedEscapeBars();

	for (int i = 1; i < SHL_MAX_SCENE_PLAYERS; ++i)
	{
		if (!g_SHLSceneSlots[i].active)
			continue;

		CBaseEntity* pEntity = UTIL_PlayerByIndex(i);

		if (pEntity == nullptr)
		{
			SHL_ClearSceneSlot(i);
			continue;
		}

		edict_t* pPlayer = pEntity->edict();

		if (pPlayer == nullptr)
		{
			SHL_ClearSceneSlot(i);
			continue;
		}

		SHL_StopPlayerSceneMotion(pPlayer);

		SHL_FreezeSceneSlotMonsters(i);

		SHL_ThinkSceneSlotLoopTransitions(i);

		const int state = SHL_GetPlayerStateId(pPlayer);

		if (!SHL_IsSceneState(state))
		{
			if (SHL_DebugEnabled())
			{
				ALERT(
					at_console,
					"SHL: scene slot stale during think, clearing. state=%d type=%s\n",
					state,
					SHL_SceneTypeName(g_SHLSceneSlots[i].sceneType));
			}

			SHL_ClearSceneSlot(i);
			continue;
		}

		if (!pEntity->IsAlive())
		{
			SHL_EndPlayerScene(pPlayer, SHL_SCENE_END_PLAYER_DIED);
			continue;
		}

		if (g_SHLSceneSlots[i].hOwnerMonster != nullptr)
		{
			CBaseMonster* pOwnerMonster =
				(CBaseMonster*)((CBaseEntity*)g_SHLSceneSlots[i].hOwnerMonster);

			if (pOwnerMonster == nullptr || !pOwnerMonster->IsAlive())
			{
				SHL_EndPlayerScene(pPlayer, SHL_SCENE_END_MONSTER_DIED);
				continue;
			}
		}

		if (g_SHLSceneSlots[i].paused)
		{
			g_SHLSceneSlots[i].lastTickTime = gpGlobals->time;
			SHL_FreezeSceneSlotVisuals(i);
			continue;
		}

		SHL_PlaySceneStartAnimation(pPlayer, i);
		SHL_PlaySceneLoopAnimation(pPlayer, i);
		SHL_HoldSceneClimaxAnimation(pPlayer, i);
		SHL_ApplySceneProfileTick(pPlayer, i);
		SHL_ThinkPlayerClimaxReturn(pPlayer, i);
		SHL_ThinkNpcClimaxFlow(pPlayer, i);

		const shl_scene_profile_t* pProfile =
			SHL_GetSceneProfile(g_SHLSceneSlots[i].sceneType);

		if (pProfile != nullptr && pProfile->endsByTimer)
		{
			if (g_SHLSceneSlots[i].endTime > 0.0f &&
				gpGlobals->time >= g_SHLSceneSlots[i].endTime)
			{
				SHL_EndPlayerScene(pPlayer, SHL_SCENE_END_TIMER);
				continue;
			}
		}
	}
}