#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"

#include "shl_skill.h"
#include "shl_player_state.h"
#include "shl_scene.h"

#define SHL_MAX_PLAYER_STATE_VIEWS 33

static shl_player_state_t g_SHLPlayerStateViews[SHL_MAX_PLAYER_STATE_VIEWS];

static CBasePlayer* SHL_GetBasePlayer(edict_t* pEntity)
{
	if (pEntity == nullptr)
		return nullptr;

	CBaseEntity* pEntityBase = CBaseEntity::Instance(pEntity);

	if (pEntityBase == nullptr)
		return nullptr;

	if (!pEntityBase->IsPlayer())
		return nullptr;

	return static_cast<CBasePlayer*>(pEntityBase);
}

static int SHL_GetPlayerIndex(edict_t* pEntity)
{
	if (pEntity == nullptr)
		return 0;

	const int index = ENTINDEX(pEntity);

	if (index <= 0 || index >= SHL_MAX_PLAYER_STATE_VIEWS)
		return 0;

	return index;
}

/*
	This returns a snapshot view for debug/backwards compatibility.

	Do not write through this pointer. The real saved values now live on CBasePlayer.
*/
shl_player_state_t* SHL_GetPlayerState(edict_t* pEntity)
{
	CBasePlayer* pPlayer = SHL_GetBasePlayer(pEntity);

	if (pPlayer == nullptr)
		return nullptr;

	const int index = SHL_GetPlayerIndex(pEntity);

	if (index <= 0)
		return nullptr;

	shl_player_state_t* state = &g_SHLPlayerStateViews[index];

	state->initialized = pPlayer->m_iSHLStateInitialized != 0;
	state->shlHP = pPlayer->m_flSHLHP;
	state->stimulation = pPlayer->m_flSHLStimulation;
	state->climaxCount = pPlayer->m_iSHLClimaxCount;
	state->defeatPending = pPlayer->m_iSHLDefeatPending != 0;
	state->playerState = pPlayer->m_iSHLPlayerState;

	return state;
}

void SHL_InitPlayerState(edict_t* pEntity)
{
	CBasePlayer* pPlayer = SHL_GetBasePlayer(pEntity);

	if (pPlayer == nullptr)
		return;

	pPlayer->m_iSHLStateInitialized = 1;
	pPlayer->m_flSHLHP = SHL_PlayerSHLHPMax();
	pPlayer->m_flSHLStimulation = 0.0f;
	pPlayer->m_iSHLClimaxCount = 0;
	pPlayer->m_iSHLDefeatPending = 0;
	pPlayer->m_iSHLPlayerState = SHL_PLAYERSTATE_NORMAL;
	pPlayer->m_flSHLClimaxEndTime = 0.0f;

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: initialized player state: SHLHP %.1f stim %.1f climax %d state %s\n",
			pPlayer->m_flSHLHP,
			pPlayer->m_flSHLStimulation,
			pPlayer->m_iSHLClimaxCount,
			SHL_PlayerStateName(pPlayer->m_iSHLPlayerState));
	}
}

void SHL_ResetPlayerState(edict_t* pEntity)
{
	SHL_InitPlayerState(pEntity);
}

static void SHL_CheckPlayerClimax(edict_t* pEntity)
{
	CBasePlayer* pPlayer = SHL_GetBasePlayer(pEntity);

	if (pPlayer == nullptr || pPlayer->m_iSHLStateInitialized == 0)
		return;

	const float stimMax = SHL_PlayerStimMax();

	if (pPlayer->m_flSHLStimulation < stimMax)
		return;

	SHL_StartPlayerClimax(pEntity);
}

void SHL_AddPlayerStimulation(edict_t* pEntity, float amount)
{
	CBasePlayer* pPlayer = SHL_GetBasePlayer(pEntity);

	if (pPlayer == nullptr || pPlayer->m_iSHLStateInitialized == 0)
		return;

	if (pPlayer->m_iSHLPlayerState == SHL_PLAYERSTATE_CLIMAX_LOCKED)
		return;
	
	pPlayer->m_flSHLStimulation += amount;

	if (pPlayer->m_flSHLStimulation < 0.0f)
		pPlayer->m_flSHLStimulation = 0.0f;

	if (pPlayer->m_iSHLPlayerState == SHL_PLAYERSTATE_NORMAL && pPlayer->m_flSHLStimulation > 0.0f)
	{
		pPlayer->m_iSHLPlayerState = SHL_PLAYERSTATE_STIMULATED;
	}

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: add stim %.1f -> %.1f / %.1f state %s\n",
			amount,
			pPlayer->m_flSHLStimulation,
			SHL_PlayerStimMax(),
			SHL_PlayerStateName(pPlayer->m_iSHLPlayerState));
	}

	SHL_CheckPlayerClimax(pEntity);
}

void SHL_SetPlayerStimulation(edict_t* pEntity, float amount)
{
	CBasePlayer* pPlayer = SHL_GetBasePlayer(pEntity);

	if (pPlayer == nullptr || pPlayer->m_iSHLStateInitialized == 0)
		return;

	pPlayer->m_flSHLStimulation = amount;

	if (pPlayer->m_flSHLStimulation < 0.0f)
		pPlayer->m_flSHLStimulation = 0.0f;

	if (pPlayer->m_iSHLPlayerState == SHL_PLAYERSTATE_NORMAL && pPlayer->m_flSHLStimulation > 0.0f)
	{
		pPlayer->m_iSHLPlayerState = SHL_PLAYERSTATE_STIMULATED;
	}

	if (pPlayer->m_flSHLStimulation <= 0.0f && pPlayer->m_iSHLPlayerState == SHL_PLAYERSTATE_STIMULATED)
	{
		pPlayer->m_iSHLPlayerState = SHL_PLAYERSTATE_NORMAL;
	}

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: set stim %.1f / %.1f state %s\n",
			pPlayer->m_flSHLStimulation,
			SHL_PlayerStimMax(),
			SHL_PlayerStateName(pPlayer->m_iSHLPlayerState));
	}

	SHL_CheckPlayerClimax(pEntity);
}

void SHL_SetPlayerSHLHP(edict_t* pEntity, float amount)
{
	CBasePlayer* pPlayer = SHL_GetBasePlayer(pEntity);

	if (pPlayer == nullptr || pPlayer->m_iSHLStateInitialized == 0)
		return;

	pPlayer->m_flSHLHP = amount;

	if (pPlayer->m_flSHLHP < 0.0f)
		pPlayer->m_flSHLHP = 0.0f;

	if (pPlayer->m_flSHLHP <= 0.0f)
	{
		pPlayer->m_iSHLDefeatPending = 1;
		pPlayer->m_iSHLPlayerState = SHL_PLAYERSTATE_DEFEAT_MENU;
	}

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: set SHL HP %.1f / %.1f defeatPending=%d state %s\n",
			pPlayer->m_flSHLHP,
			SHL_PlayerSHLHPMax(),
			pPlayer->m_iSHLDefeatPending,
			SHL_PlayerStateName(pPlayer->m_iSHLPlayerState));
	}
}

void SHL_StartPlayerClimax(edict_t* pEntity)
{
	CBasePlayer* pPlayer = SHL_GetBasePlayer(pEntity);

	if (pPlayer == nullptr || pPlayer->m_iSHLStateInitialized == 0)
		return;

	if (pPlayer->m_iSHLPlayerState == SHL_PLAYERSTATE_CLIMAX_LOCKED)
		return;

	pPlayer->m_iSHLPlayerState = SHL_PLAYERSTATE_CLIMAX_LOCKED;
	pPlayer->m_flSHLClimaxEndTime = gpGlobals->time + SHL_PlayerClimaxDuration();

	ALERT(
		at_console,
		"SHL: climax phase started, duration %.2f seconds. Animation/profile placeholder only.\n",
		SHL_PlayerClimaxDuration());
}

void SHL_FinishPlayerClimax(edict_t* pEntity)
{
	CBasePlayer* pPlayer = SHL_GetBasePlayer(pEntity);

	if (pPlayer == nullptr || pPlayer->m_iSHLStateInitialized == 0)
		return;

	if (pPlayer->m_iSHLPlayerState != SHL_PLAYERSTATE_CLIMAX_LOCKED)
		return;

	pPlayer->m_flSHLClimaxEndTime = 0.0f;

	pPlayer->m_iSHLClimaxCount++;

	pPlayer->m_flSHLHP -= SHL_PlayerClimaxHPDamage();

	if (pPlayer->m_flSHLHP < 0.0f)
		pPlayer->m_flSHLHP = 0.0f;

	if (pPlayer->m_iSHLClimaxCount <= 1)
	{
		pPlayer->m_flSHLStimulation = SHL_PlayerFirstClimaxReset();
	}
	else
	{
		pPlayer->m_flSHLStimulation = SHL_PlayerLaterClimaxReset();
	}

	if (pPlayer->m_flSHLStimulation < 0.0f)
		pPlayer->m_flSHLStimulation = 0.0f;

	if (pPlayer->m_flSHLStimulation >= SHL_PlayerStimMax())
	{
		pPlayer->m_flSHLStimulation = SHL_PlayerStimMax() * 0.25f;
	}

	// Active scene owns the post-climax state.
	// Do not switch to STIMULATED/NORMAL here or SHL_SceneThink will clear the scene as stale.
	if (SHL_IsPlayerInScene(pEntity))
	{
		if (pPlayer->m_flSHLHP <= 0.0f)
		{
			pPlayer->m_iSHLDefeatPending = 1;
			pPlayer->m_iSHLPlayerState = SHL_PLAYERSTATE_DEFEAT_MENU;

			ALERT(
				at_console,
				"SHL: climax phase finished inside scene #%d | SHL HP %.1f / %.1f | stim reset to %.1f | state %s\n",
				pPlayer->m_iSHLClimaxCount,
				pPlayer->m_flSHLHP,
				SHL_PlayerSHLHPMax(),
				pPlayer->m_flSHLStimulation,
				SHL_PlayerStateName(pPlayer->m_iSHLPlayerState));

			ALERT(at_console, "SHL: player SHL HP reached 0, defeat flow pending\n");
			return;
		}

		pPlayer->m_iSHLPlayerState = SHL_PLAYERSTATE_CLIMAX_LOCKED;

		if (SHL_DebugEnabled())
		{
			ALERT(
				at_console,
				"SHL: climax phase finished inside scene #%d | SHL HP %.1f / %.1f | stim reset to %.1f | state kept CLIMAX_LOCKED\n",
				pPlayer->m_iSHLClimaxCount,
				pPlayer->m_flSHLHP,
				SHL_PlayerSHLHPMax(),
				pPlayer->m_flSHLStimulation);
		}

		return;
	}

	if (pPlayer->m_flSHLHP <= 0.0f)
	{
		pPlayer->m_iSHLDefeatPending = 1;
		pPlayer->m_iSHLPlayerState = SHL_PLAYERSTATE_DEFEAT_MENU;
	}
	else if (pPlayer->m_flSHLStimulation > 0.0f)
	{
		pPlayer->m_iSHLPlayerState = SHL_PLAYERSTATE_STIMULATED;
	}
	else
	{
		pPlayer->m_iSHLPlayerState = SHL_PLAYERSTATE_NORMAL;
	}

	ALERT(
		at_console,
		"SHL: climax phase finished #%d | SHL HP %.1f / %.1f | stim reset to %.1f | state %s\n",
		pPlayer->m_iSHLClimaxCount,
		pPlayer->m_flSHLHP,
		SHL_PlayerSHLHPMax(),
		pPlayer->m_flSHLStimulation,
		SHL_PlayerStateName(pPlayer->m_iSHLPlayerState));

	if (pPlayer->m_iSHLDefeatPending != 0)
	{
		ALERT(at_console, "SHL: player SHL HP reached 0, defeat flow pending\n");
	}
}

void SHL_ForcePlayerClimax(edict_t* pEntity)
{
	SHL_StartPlayerClimax(pEntity);
}

void SHL_PlayerThink(edict_t* pEntity)
{
	CBasePlayer* pPlayer = SHL_GetBasePlayer(pEntity);

	if (pPlayer == nullptr || pPlayer->m_iSHLStateInitialized == 0)
		return;

	if (pPlayer->m_iSHLPlayerState != SHL_PLAYERSTATE_CLIMAX_LOCKED)
		return;

	if (pPlayer->m_flSHLClimaxEndTime <= 0.0f)
		return;

	if (gpGlobals->time >= pPlayer->m_flSHLClimaxEndTime)
	{
		SHL_FinishPlayerClimax(pEntity);
	}
}

void SHL_SetPlayerState(edict_t* pEntity, int playerState)
{
	CBasePlayer* pPlayer = SHL_GetBasePlayer(pEntity);

	if (pPlayer == nullptr || pPlayer->m_iSHLStateInitialized == 0)
		return;

	pPlayer->m_iSHLPlayerState = playerState;

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: player state set to %s\n",
			SHL_PlayerStateName(playerState));
	}
}

int SHL_GetPlayerStateId(edict_t* pEntity)
{
	CBasePlayer* pPlayer = SHL_GetBasePlayer(pEntity);

	if (pPlayer == nullptr || pPlayer->m_iSHLStateInitialized == 0)
		return SHL_PLAYERSTATE_NORMAL;

	return pPlayer->m_iSHLPlayerState;
}

bool SHL_IsPlayerInUnsafeState(edict_t* pEntity)
{
	const int state = SHL_GetPlayerStateId(pEntity);

	switch (state)
	{
	case SHL_PLAYERSTATE_GROUNDED:
	case SHL_PLAYERSTATE_TRAMPLED:
	case SHL_PLAYERSTATE_GRABBED:
	case SHL_PLAYERSTATE_ACTIVE_SCENE:
	case SHL_PLAYERSTATE_CLIMAX_LOCKED:
	case SHL_PLAYERSTATE_DEFEAT_MENU:
	case SHL_PLAYERSTATE_DEFEAT_SCENE:
	case SHL_PLAYERSTATE_RECOVERY:
		return true;

	default:
		break;
	}

	return false;
}

bool SHL_ShouldBlockPlayerInput(edict_t* pEntity)
{
	const int state = SHL_GetPlayerStateId(pEntity);

	switch (state)
	{
	case SHL_PLAYERSTATE_GROUNDED:
	case SHL_PLAYERSTATE_TRAMPLED:
	case SHL_PLAYERSTATE_GRABBED:
	case SHL_PLAYERSTATE_ACTIVE_SCENE:
	case SHL_PLAYERSTATE_CLIMAX_LOCKED:
	case SHL_PLAYERSTATE_DEFEAT_MENU:
	case SHL_PLAYERSTATE_DEFEAT_SCENE:
	case SHL_PLAYERSTATE_RECOVERY:
		return true;

	default:
		break;
	}

	return false;
}
void SHL_DebugPrintPlayerState(edict_t* pEntity)
{
	CBasePlayer* pPlayer = SHL_GetBasePlayer(pEntity);

	if (pPlayer == nullptr || pPlayer->m_iSHLStateInitialized == 0)
	{
		ALERT(at_console, "SHL: player state not initialized\n");
		return;
	}

	ALERT(
		at_console,
		"SHL STATE: SHLHP %.1f / %.1f | stim %.1f / %.1f | climax %d | defeatPending %d | state %s | saved-on-player 1\n",
		pPlayer->m_flSHLHP,
		SHL_PlayerSHLHPMax(),
		pPlayer->m_flSHLStimulation,
		SHL_PlayerStimMax(),
		pPlayer->m_iSHLClimaxCount,
		pPlayer->m_iSHLDefeatPending,
		SHL_PlayerStateName(pPlayer->m_iSHLPlayerState));
}