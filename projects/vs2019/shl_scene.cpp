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

#include <string.h>

#define SHL_MAX_SCENE_PLAYERS 33
#define SHL_MAX_MONSTER_SCENE_RECOVERY 2048

#define SHL_GROUNDED_ESCAPE_REQUIRED 80.0f
#define SHL_GROUNDED_ESCAPE_GAIN_PER_MASH 10.0f
#define SHL_GROUNDED_ESCAPE_MASH_COOLDOWN 0.08f

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

	EHANDLE hOwnerMonster;
	EHANDLE hPlayerSceneActor;
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

	CBaseEntity* pVisual =
		(CBaseEntity*)g_SHLMonsterSceneRecovery[index].hVisual;

	if (pVisual != nullptr)
	{
		UTIL_Remove(pVisual);
	}

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

static void SHL_ClearSceneSlot(int index)
{
	if (index <= 0 || index >= SHL_MAX_SCENE_PLAYERS)
		return;

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

bool SHL_IsPlayerInScene(edict_t* pPlayer)
{
	const int index = SHL_GetSceneIndex(pPlayer);

	if (index <= 0)
		return false;

	if (!g_SHLSceneSlots[index].active)
		return false;

	return true;
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

bool SHL_IsMonsterSceneOwner(CBaseMonster* pMonster)
{
	if (pMonster == nullptr)
		return false;

	for (int i = 1; i < SHL_MAX_SCENE_PLAYERS; ++i)
	{
		if (!g_SHLSceneSlots[i].active)
			continue;

		CBaseMonster* pOwnerMonster =
			(CBaseMonster*)((CBaseEntity*)g_SHLSceneSlots[i].hOwnerMonster);

		if (pOwnerMonster == pMonster)
			return true;
	}

	return false;
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
			const bool wantsDedicatedEscapeRecovery =
				pProfile->monsterEscapeKnockdownSequence != nullptr &&
				pProfile->monsterEscapeKnockdownSequence[0] != '\0' &&
				pProfile->monsterEscapeKnockdownDuration > 0.0f &&
				pProfile->monsterRecoveryGetupSequence != nullptr &&
				pProfile->monsterRecoveryGetupSequence[0] != '\0' &&
				pProfile->monsterRecoveryGetupDuration > 0.0f;

			SHL_StartMonsterSceneRecoveryEx(
				pOwnerMonster,
				pProfile->monsterNpcClimaxSequence,
				wantsDedicatedEscapeRecovery ? 0.0f : SHL_NormalEscapeRecoveryDuration(),
				pProfile->monsterEscapeKnockdownSequence,
				pProfile->monsterEscapeKnockdownDuration,
				pProfile->monsterRecoveryGetupSequence,
				pProfile->monsterRecoveryGetupDuration);

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

	if (sceneType == SHL_SCENE_GROUNDED_GRAB)
	{
		CBaseEntity* pActor = SHL_CreatePlayerSceneActor(
			pPlayer,
			"models/shl/player_grounded_scene.mdl");

		g_SHLSceneSlots[index].hPlayerSceneActor = pActor;
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

	SHL_StartSceneSlot(pPlayer, SHL_SCENE_GROUNDED_GRAB, pMonster, duration);
	SHL_SetPlayerState(pPlayer, SHL_PLAYERSTATE_GRABBED);

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

	if (g_SHLSceneSlots[index].npcClimaxStarted)
		return;

	const shl_scene_profile_t* pProfile =
		SHL_GetSceneProfile(g_SHLSceneSlots[index].sceneType);

	if (pProfile == nullptr)
		return;

	if (!pProfile->supportsSceneAnimation)
		return;

	g_SHLSceneSlots[index].climaxAnimationStarted = true;
	g_SHLSceneSlots[index].climaxStartTime = gpGlobals->time;
	g_SHLSceneSlots[index].pendingLoopAfterPlayerClimax = true;

	g_SHLSceneSlots[index].escapeProgress = 0.0f;
	g_SHLSceneSlots[index].lastEscapeMashTime = 0.0f;
	SHL_SendEscapeBar(pPlayer, false, 0.0f);

	g_SHLSceneSlots[index].npcEndurance -= SHL_NormalNpcEnduranceDrainPerPlayerClimax();

	if (g_SHLSceneSlots[index].npcEndurance < 0.0f)
		g_SHLSceneSlots[index].npcEndurance = 0.0f;

	const float climaxDuration = SHL_PlayerClimaxDuration();
	const float returnDelay = pProfile->postClimaxLoopDelay;

	g_SHLSceneSlots[index].returnToLoopTime =
		gpGlobals->time + climaxDuration + returnDelay;

	if (g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
	{
		CBaseAnimating* pPlayerSceneActor =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

		SHL_TryPlaySequence(
			pPlayerSceneActor,
			pProfile->playerClimaxSequence,
			"player scene actor");
	}

	if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
	{
		CBaseAnimating* pOwnerMonster =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

		SHL_TryPlaySequence(
			pOwnerMonster,
			pProfile->monsterClimaxSequence,
			"monster player climax");
	}

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: player scene climax triggered type=%s returnToLoopIn=%.2f npcEndurance=%.1f\n",
			pProfile->debugName,
			climaxDuration + returnDelay,
			g_SHLSceneSlots[index].npcEndurance);
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

	g_SHLSceneSlots[index].npcClimaxStarted = true;
	g_SHLSceneSlots[index].npcClimaxStartTime = gpGlobals->time;

	g_SHLSceneSlots[index].escapeProgress = 0.0f;
	g_SHLSceneSlots[index].lastEscapeMashTime = 0.0f;
	SHL_SendEscapeBar(pPlayer, false, 0.0f);

	g_SHLSceneSlots[index].climaxAnimationStarted = true;
	g_SHLSceneSlots[index].pendingLoopAfterPlayerClimax = false;
	g_SHLSceneSlots[index].returnToLoopTime = 0.0f;

	if (g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
	{
		CBaseAnimating* pPlayerSceneActor =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

		SHL_TryPlaySequence(
			pPlayerSceneActor,
			pProfile->playerClimaxSequence,
			"player scene actor");
	}

	if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
	{
		CBaseAnimating* pOwnerMonster =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

		SHL_TryPlaySequence(
			pOwnerMonster,
			pProfile->monsterNpcClimaxSequence,
			"monster npc climax");
	}

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

	const char* pszMonsterClimaxSequence =
		g_SHLSceneSlots[index].npcClimaxStarted
			? pProfile->monsterNpcClimaxSequence
			: pProfile->monsterClimaxSequence;

	bool climaxNearEnd = false;

	if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
	{
		CBaseAnimating* pOwnerMonster =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

		if (SHL_IsSequenceNearEnd(pOwnerMonster, pszMonsterClimaxSequence, 248.0f))
			climaxNearEnd = true;
	}

	if (!climaxNearEnd && g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
	{
		CBaseAnimating* pPlayerSceneActor =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

		if (SHL_IsSequenceNearEnd(pPlayerSceneActor, pProfile->playerClimaxSequence, 248.0f))
			climaxNearEnd = true;
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

		if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
		{
			CBaseAnimating* pOwnerMonster =
				(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

			SHL_HoldSequenceNoRestart(
				pOwnerMonster,
				pszMonsterClimaxSequence,
				true);
		}

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

		if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
		{
			CBaseAnimating* pOwnerMonster =
				(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

			SHL_TryPlaySequence(
				pOwnerMonster,
				pProfile->monsterLoopSequence,
				"monster");
		}

		g_SHLSceneSlots[index].loopAnimationStarted = true;

		if (SHL_DebugEnabled())
		{
			ALERT(
				at_console,
				"SHL: player climax animation near end, visually returning to grab_loop\n");
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

	if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
	{
		CBaseAnimating* pOwnerMonster =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

		SHL_HoldSequence(
			pOwnerMonster,
			pszMonsterClimaxSequence,
			"monster player climax",
			false);
	}
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

	if (SHL_GetPlayerStateId(pPlayer) == SHL_PLAYERSTATE_CLIMAX_LOCKED)
	{
		SHL_SetPlayerState(pPlayer, SHL_PLAYERSTATE_GRABBED);
	}

	const shl_scene_profile_t* pEscapeProfile =
		SHL_GetSceneProfile(g_SHLSceneSlots[index].sceneType);

	if (pEscapeProfile != nullptr && pEscapeProfile->supportsEscape)
	{
		SHL_SendEscapeBar(pPlayer, true, 0.0f);
	}

	if (pProfile->supportsSceneAnimation)
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

		if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
		{
			CBaseAnimating* pOwnerMonster =
				(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

			SHL_TryPlaySequence(
				pOwnerMonster,
				pProfile->monsterLoopSequence,
				"monster");
		}
	}

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: player climax finished, returning directly to grab_loop\n");
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
					pProfile->playerStartSequence,
					"player scene actor",
					false);
			}

			if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
			{
				CBaseAnimating* pOwnerMonster =
					(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

				SHL_HoldSequence(
					pOwnerMonster,
					pProfile->monsterStartSequence,
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
			pProfile->playerStartSequence,
			"player scene actor");
	}

	if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
	{
		CBaseAnimating* pOwnerMonster =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

		SHL_TryPlaySequence(
			pOwnerMonster,
			pProfile->monsterStartSequence,
			"monster");
	}

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: scene start animation triggered type=%s\n",
			pProfile->debugName);
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

	bool startIsNearEnd = false;

	if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
	{
		CBaseAnimating* pOwnerMonster =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

		if (SHL_IsSequenceNearEnd(pOwnerMonster, pProfile->monsterStartSequence, 220.0f))
			startIsNearEnd = true;
	}

	if (!startIsNearEnd && g_SHLSceneSlots[index].hPlayerSceneActor != nullptr)
	{
		CBaseAnimating* pPlayerSceneActor =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hPlayerSceneActor);

		if (SHL_IsSequenceNearEnd(pPlayerSceneActor, pProfile->playerStartSequence, 220.0f))
			startIsNearEnd = true;
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
				pProfile->playerLoopSequence,
				"player scene actor");
		}

		if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
		{
			CBaseAnimating* pOwnerMonster =
				(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

			SHL_TryPlaySequence(
				pOwnerMonster,
				pProfile->monsterLoopSequence,
				"monster");
		}

		if (SHL_DebugEnabled())
		{
			ALERT(
				at_console,
				"SHL: scene loop animation triggered type=%s elapsed=%.2f nearEnd=%d\n",
				pProfile->debugName,
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
			pProfile->playerLoopSequence,
			"player scene actor",
			true);
	}

	if (g_SHLSceneSlots[index].hOwnerMonster != nullptr)
	{
		CBaseAnimating* pOwnerMonster =
			(CBaseAnimating*)((CBaseEntity*)g_SHLSceneSlots[index].hOwnerMonster);

		SHL_HoldSequence(
			pOwnerMonster,
			pProfile->monsterLoopSequence,
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