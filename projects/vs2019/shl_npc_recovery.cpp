#include "extdll.h"
#include "util.h"
#include "cbase.h"

#include "shl_defs.h"
#include "shl_skill.h"
#include "shl_npc_recovery.h"

#include <string.h>

#define SHL_MAX_RECOVERY_ENTITIES 2048
#define SHL_RECOVERY_SEQUENCE_NAME_LENGTH 64

struct shl_npc_recovery_slot_t
{
	bool active;
	EHANDLE hEntity;
	float endTime;
	char holdSequence[SHL_RECOVERY_SEQUENCE_NAME_LENGTH];
};

static shl_npc_recovery_slot_t g_SHLNpcRecoverySlots[SHL_MAX_RECOVERY_ENTITIES];
static float g_flSHLLastRecoveryTime = 0.0f;

static void SHL_ClearNpcRecoverySlot(int index)
{
	if (index < 0 || index >= SHL_MAX_RECOVERY_ENTITIES)
		return;

	g_SHLNpcRecoverySlots[index].active = false;
	g_SHLNpcRecoverySlots[index].hEntity = nullptr;
	g_SHLNpcRecoverySlots[index].endTime = 0.0f;
	g_SHLNpcRecoverySlots[index].holdSequence[0] = '\0';
}

void SHL_InitNpcRecoverySystem()
{
	for (int i = 0; i < SHL_MAX_RECOVERY_ENTITIES; ++i)
	{
		SHL_ClearNpcRecoverySlot(i);
	}

	if (gpGlobals != nullptr)
		g_flSHLLastRecoveryTime = gpGlobals->time;
	else
		g_flSHLLastRecoveryTime = 0.0f;
}

static void SHL_CheckNpcRecoveryMapTimeReset()
{
	if (gpGlobals == nullptr)
		return;

	if (gpGlobals->time < g_flSHLLastRecoveryTime)
	{
		SHL_InitNpcRecoverySystem();

		if (SHL_DebugEnabled())
		{
			ALERT(at_console, "SHL: npc recovery cleared after map time reset\n");
		}
	}

	g_flSHLLastRecoveryTime = gpGlobals->time;
}

static int SHL_GetRecoveryIndex(CBaseEntity* pEntity)
{
	if (pEntity == nullptr)
		return 0;

	if (pEntity->edict() == nullptr)
		return 0;

	const int index = ENTINDEX(pEntity->edict());

	if (index <= 0 || index >= SHL_MAX_RECOVERY_ENTITIES)
		return 0;

	return index;
}

void SHL_StartNpcRecovery(CBaseEntity* pEntity, float duration)
{
	SHL_StartNpcRecovery(pEntity, duration, nullptr);
}

void SHL_StartNpcRecovery(CBaseEntity* pEntity, float duration, const char* pszHoldSequence)
{
	SHL_CheckNpcRecoveryMapTimeReset();

	const int index = SHL_GetRecoveryIndex(pEntity);

	if (index <= 0)
		return;

	if (duration <= 0.0f)
	{
		SHL_ClearNpcRecoverySlot(index);
		return;
	}

	g_SHLNpcRecoverySlots[index].active = true;
	g_SHLNpcRecoverySlots[index].hEntity = pEntity;
	g_SHLNpcRecoverySlots[index].endTime = gpGlobals->time + duration;

	if (pszHoldSequence != nullptr && pszHoldSequence[0] != '\0')
	{
		strncpy(
			g_SHLNpcRecoverySlots[index].holdSequence,
			pszHoldSequence,
			SHL_RECOVERY_SEQUENCE_NAME_LENGTH - 1);

		g_SHLNpcRecoverySlots[index].holdSequence[SHL_RECOVERY_SEQUENCE_NAME_LENGTH - 1] = '\0';
	}
	else
	{
		g_SHLNpcRecoverySlots[index].holdSequence[0] = '\0';
	}

	pEntity->pev->velocity = g_vecZero;
	pEntity->pev->avelocity = g_vecZero;
	pEntity->pev->framerate = 0.0f;

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: npc recovery started entity=%d duration=%.2f holdSequence=%s\n",
			index,
			duration,
			g_SHLNpcRecoverySlots[index].holdSequence[0] != '\0'
				? g_SHLNpcRecoverySlots[index].holdSequence
				: "CURRENT_POSE");
	}
}

bool SHL_IsNpcRecovering(CBaseEntity* pEntity)
{
	SHL_CheckNpcRecoveryMapTimeReset();

	const int index = SHL_GetRecoveryIndex(pEntity);

	if (index <= 0)
		return false;

	if (!g_SHLNpcRecoverySlots[index].active)
		return false;

	CBaseEntity* pStoredEntity =
		(CBaseEntity*)g_SHLNpcRecoverySlots[index].hEntity;

	if (pStoredEntity != pEntity)
	{
		SHL_ClearNpcRecoverySlot(index);
		return false;
	}

	if (g_SHLNpcRecoverySlots[index].endTime <= gpGlobals->time)
	{
		SHL_ClearNpcRecoverySlot(index);
		return false;
	}

	return true;
}

float SHL_GetNpcRecoveryRemaining(CBaseEntity* pEntity)
{
	if (!SHL_IsNpcRecovering(pEntity))
		return 0.0f;

	const int index = SHL_GetRecoveryIndex(pEntity);

	if (index <= 0)
		return 0.0f;

	const float remaining = g_SHLNpcRecoverySlots[index].endTime - gpGlobals->time;

	if (remaining < 0.0f)
		return 0.0f;

	return remaining;
}

bool SHL_ApplyNpcRecovery(CBaseAnimating* pAnimating)
{
	if (pAnimating == nullptr)
		return false;

	if (!SHL_IsNpcRecovering(pAnimating))
		return false;

	const int index = SHL_GetRecoveryIndex(pAnimating);

	if (index <= 0)
		return false;

	pAnimating->pev->velocity = g_vecZero;
	pAnimating->pev->avelocity = g_vecZero;

	const char* pszSequenceName = g_SHLNpcRecoverySlots[index].holdSequence;

	// Normal path for NPC climax recovery:
	// no sequence name means freeze exactly where the scene released it.
	// This avoids replaying shl_npc_climax and avoids release-frame flicker.
	if (pszSequenceName == nullptr || pszSequenceName[0] == '\0')
	{
		if (pAnimating->pev->frame >= 248.0f)
			pAnimating->pev->frame = 255.0f;

		pAnimating->pev->framerate = 0.0f;
		pAnimating->pev->nextthink = gpGlobals->time + 0.05f;
		return true;
	}

	const int sequence = pAnimating->LookupSequence(pszSequenceName);

	if (sequence >= 0)
	{
		if (pAnimating->pev->sequence != sequence)
		{
			pAnimating->pev->sequence = sequence;
			pAnimating->pev->frame = 255.0f;
			pAnimating->pev->framerate = 0.0f;
			pAnimating->ResetSequenceInfo();
			pAnimating->pev->frame = 255.0f;
			pAnimating->pev->framerate = 0.0f;
		}
		else
		{
			if (pAnimating->pev->frame >= 248.0f)
				pAnimating->pev->frame = 255.0f;

			pAnimating->pev->framerate = 0.0f;
		}
	}

	pAnimating->pev->nextthink = gpGlobals->time + 0.05f;
	return true;
}