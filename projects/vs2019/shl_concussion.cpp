#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"

#include "shl_defs.h"
#include "shl_skill.h"
#include "shl_player_state.h"
#include "shl_concussion.h"

static float SHL_ClampFloat(float value, float minValue, float maxValue)
{
	if (value < minValue)
		return minValue;

	if (value > maxValue)
		return maxValue;

	return value;
}

static CBasePlayer* SHL_GetBasePlayer(edict_t* pPlayer)
{
	if (pPlayer == nullptr)
		return nullptr;

	return (CBasePlayer*)GET_PRIVATE(pPlayer);
}

void SHL_InitPlayerConcussion(edict_t* pPlayer)
{
	CBasePlayer* pBasePlayer = SHL_GetBasePlayer(pPlayer);

	if (pBasePlayer == nullptr)
		return;

	pBasePlayer->m_flSHLConcussion = 0.0f;
	pBasePlayer->m_flSHLConcussionRecoverTime = 0.0f;
	pBasePlayer->m_flSHLGroundedEndTime = 0.0f;
	pBasePlayer->m_iSHLGroundedCameraActive = 0;
	pBasePlayer->m_vecSHLGroundedSavedViewOfs = g_vecZero;
	pBasePlayer->m_vecSHLGroundedSavedPunchAngle = g_vecZero;
}

float SHL_GetPlayerConcussion(edict_t* pPlayer)
{
	CBasePlayer* pBasePlayer = SHL_GetBasePlayer(pPlayer);

	if (pBasePlayer == nullptr)
		return 0.0f;

	return pBasePlayer->m_flSHLConcussion;
}

void SHL_SetPlayerConcussion(edict_t* pPlayer, float amount)
{
	CBasePlayer* pBasePlayer = SHL_GetBasePlayer(pPlayer);

	if (pBasePlayer == nullptr)
		return;

	const float maxConcussion = SHL_PlayerConcussionMax();

	pBasePlayer->m_flSHLConcussion = SHL_ClampFloat(amount, 0.0f, maxConcussion);
	pBasePlayer->m_flSHLConcussionRecoverTime = gpGlobals->time + 1.0f;

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: concussion set %.1f / %.1f\n",
			pBasePlayer->m_flSHLConcussion,
			maxConcussion);
	}
}

void SHL_AddPlayerConcussion(edict_t* pPlayer, float amount)
{
	CBasePlayer* pBasePlayer = SHL_GetBasePlayer(pPlayer);

	if (pBasePlayer == nullptr)
		return;

	if (!pBasePlayer->IsAlive())
		return;

	if (amount <= 0.0f)
		return;

	const int state = SHL_GetPlayerStateId(pPlayer);

	if (
		state == SHL_PLAYERSTATE_ACTIVE_SCENE ||
		state == SHL_PLAYERSTATE_CLIMAX_LOCKED ||
		state == SHL_PLAYERSTATE_DEFEAT_MENU ||
		state == SHL_PLAYERSTATE_DEFEAT_SCENE)
	{
		return;
	}

	const float maxConcussion = SHL_PlayerConcussionMax();

	pBasePlayer->m_flSHLConcussion = SHL_ClampFloat(
		pBasePlayer->m_flSHLConcussion + amount,
		0.0f,
		maxConcussion);

	pBasePlayer->m_flSHLConcussionRecoverTime = gpGlobals->time + 2.0f;

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: concussion +%.1f = %.1f / %.1f\n",
			amount,
			pBasePlayer->m_flSHLConcussion,
			maxConcussion);
	}

	if (pBasePlayer->m_flSHLConcussion >= maxConcussion)
	{
		pBasePlayer->m_flSHLConcussion = maxConcussion;
		SHL_SetPlayerState(pPlayer, SHL_PLAYERSTATE_GROUNDED);

		if (SHL_DebugEnabled())
		{
			ALERT(at_console, "SHL: player grounded by concussion\n");
		}
	}
}

static void SHL_ApplyGroundedCamera(edict_t* pPlayer)
{
	CBasePlayer* pBasePlayer = SHL_GetBasePlayer(pPlayer);

	if (pBasePlayer == nullptr)
		return;

	entvars_t* pev = &pPlayer->v;

	if (pBasePlayer->m_iSHLGroundedCameraActive == 0)
	{
		pBasePlayer->m_iSHLGroundedCameraActive = 1;
		pBasePlayer->m_vecSHLGroundedSavedViewOfs = pev->view_ofs;
		pBasePlayer->m_vecSHLGroundedSavedPunchAngle = pev->punchangle;

		if (SHL_DebugEnabled())
		{
			ALERT(at_console, "SHL: grounded camera applied\n");
		}
	}

	// Same basic idea as death cam: eyes at body origin.
	// Do not set observer/dead flags.
	pev->view_ofs = g_vecZero;

	// Do not use punchangle roll here. Client DLL handles real roll.
	pev->punchangle = g_vecZero;
}

static void SHL_RestoreGroundedCamera(edict_t* pPlayer)
{
	CBasePlayer* pBasePlayer = SHL_GetBasePlayer(pPlayer);

	if (pBasePlayer == nullptr)
		return;

	if (pBasePlayer->m_iSHLGroundedCameraActive == 0)
		return;

	entvars_t* pev = &pPlayer->v;

	pev->view_ofs = pBasePlayer->m_vecSHLGroundedSavedViewOfs;
	pev->punchangle = pBasePlayer->m_vecSHLGroundedSavedPunchAngle;

	pBasePlayer->m_iSHLGroundedCameraActive = 0;
	pBasePlayer->m_vecSHLGroundedSavedViewOfs = g_vecZero;
	pBasePlayer->m_vecSHLGroundedSavedPunchAngle = g_vecZero;

	if (SHL_DebugEnabled())
	{
		ALERT(at_console, "SHL: grounded camera restored\n");
	}
}

void SHL_PlayerConcussionThink(edict_t* pPlayer)
{
	CBasePlayer* pBasePlayer = SHL_GetBasePlayer(pPlayer);

	if (pBasePlayer == nullptr)
		return;

	if (!pBasePlayer->IsAlive())
	{
		pBasePlayer->m_flSHLConcussion = 0.0f;
		return;
	}

	if (pBasePlayer->m_flSHLConcussion <= 0.0f)
		return;

	const int state = SHL_GetPlayerStateId(pPlayer);

	if (
		state == SHL_PLAYERSTATE_ACTIVE_SCENE ||
		state == SHL_PLAYERSTATE_CLIMAX_LOCKED ||
		state == SHL_PLAYERSTATE_DEFEAT_MENU ||
		state == SHL_PLAYERSTATE_DEFEAT_SCENE)
	{
		return;
	}

	if (gpGlobals->time < pBasePlayer->m_flSHLConcussionRecoverTime)
		return;

	const float recoverAmount = SHL_PlayerConcussionRecoverRate() * gpGlobals->frametime;

	pBasePlayer->m_flSHLConcussion = SHL_ClampFloat(
		pBasePlayer->m_flSHLConcussion - recoverAmount,
		0.0f,
		SHL_PlayerConcussionMax());

if (state == SHL_PLAYERSTATE_GROUNDED)
	{
	if (SHL_DebugEnabled())
	{
		ALERT(at_console, "SHL: grounded think running\n");
	}	

	entvars_t* pev = &pPlayer->v;

		pev->button = 0;
		pev->oldbuttons = 0;
		pev->impulse = 0;

		pev->velocity.x = 0.0f;
		pev->velocity.y = 0.0f;

		SHL_ApplyGroundedCamera(pPlayer);

		if (gpGlobals->time >= pBasePlayer->m_flSHLGroundedEndTime)
		{
			pBasePlayer->m_flSHLConcussion = 50.0f;
			pBasePlayer->m_flSHLGroundedEndTime = 0.0f;
			pBasePlayer->m_flSHLConcussionRecoverTime = gpGlobals->time + 1.0f;

			SHL_RestoreGroundedCamera(pPlayer);

			SHL_SetPlayerState(pPlayer, SHL_PLAYERSTATE_NORMAL);

			if (SHL_DebugEnabled())
			{
				ALERT(at_console, "SHL: player got up from grounded state\n");
			}
		}

		return;
	}
	if (state != SHL_PLAYERSTATE_GROUNDED)
	{
		SHL_RestoreGroundedCamera(pPlayer);
	}
}