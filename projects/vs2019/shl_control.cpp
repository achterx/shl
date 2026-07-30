#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "UserMessages.h"

#include "shl_skill.h"
#include "shl_player_state.h"
#include "shl_control.h"
#include "shl_scene.h"
#include "shl_scene_profile.h"

#define SHL_MAX_CONTROL_STATES 33

static bool g_SHLWeaponHidden[SHL_MAX_CONTROL_STATES];
static bool g_SHLLastSentGroundedCam[SHL_MAX_CONTROL_STATES];
static string_t g_SHLSavedViewModel[SHL_MAX_CONTROL_STATES];
static string_t g_SHLSavedWeaponModel[SHL_MAX_CONTROL_STATES];

static bool g_SHLLastSentInputLock[SHL_MAX_CONTROL_STATES];

static int SHL_GetControlIndex(edict_t* pEntity)
{
	if (pEntity == nullptr)
		return 0;

	const int index = ENTINDEX(pEntity);

	if (index <= 0 || index >= SHL_MAX_CONTROL_STATES)
		return 0;

	return index;
}

static const shl_scene_profile_t* SHL_GetActiveSceneProfileForPlayer(edict_t* pEntity)
{
	if (pEntity == nullptr)
		return nullptr;

	if (!SHL_IsPlayerInScene(pEntity))
		return nullptr;

	const int sceneType = SHL_GetPlayerSceneType(pEntity);

	if (sceneType == SHL_SCENE_NONE)
		return nullptr;

	return SHL_GetSceneProfile(sceneType);
}

static bool SHL_ProfileBlocksInput(edict_t* pEntity)
{
	const shl_scene_profile_t* pProfile = SHL_GetActiveSceneProfileForPlayer(pEntity);

	if (pProfile == nullptr)
		return false;

	return pProfile->blocksInput;
}

static bool SHL_ProfileHidesWeapon(edict_t* pEntity)
{
	const shl_scene_profile_t* pProfile = SHL_GetActiveSceneProfileForPlayer(pEntity);

	if (pProfile == nullptr)
		return false;

	return pProfile->hidesWeapon;
}

static bool SHL_ProfileUsesGroundedCamera(edict_t* pEntity)
{
	const shl_scene_profile_t* pProfile = SHL_GetActiveSceneProfileForPlayer(pEntity);

	if (pProfile == nullptr)
		return false;

	return pProfile->usesGroundedCamera;
}

static void SHL_UpdateGroundedCameraMessage(edict_t* pEntity)
{
	const int index = SHL_GetControlIndex(pEntity);

	if (index <= 0)
		return;

	const int state = SHL_GetPlayerStateId(pEntity);

	const bool shouldUseGroundedCam =
		state == SHL_PLAYERSTATE_GROUNDED ||
		SHL_ProfileUsesGroundedCamera(pEntity);

	if (g_SHLLastSentGroundedCam[index] == shouldUseGroundedCam)
		return;

	if (gmsgSHLGroundedCam <= 0)
	{
		if (SHL_DebugEnabled())
		{
			ALERT(at_console, "SHL ERROR: SHLGCam message id is 0, not sending\n");
		}

		return;
	}

	g_SHLLastSentGroundedCam[index] = shouldUseGroundedCam;

	MESSAGE_BEGIN(MSG_ONE, gmsgSHLGroundedCam, nullptr, pEntity);
	WRITE_BYTE(shouldUseGroundedCam ? 1 : 0);
	MESSAGE_END();

	if (SHL_DebugEnabled())
	{
		ALERT(at_console, "SHL: sent grounded camera %d\n", shouldUseGroundedCam ? 1 : 0);
	}
}

static void SHL_UpdateClientInputLockMessage(edict_t* pEntity)
{
	const int index = SHL_GetControlIndex(pEntity);

	if (index <= 0)
		return;

	const bool shouldLock =
		SHL_ShouldBlockPlayerInput(pEntity) ||
		SHL_ProfileBlocksInput(pEntity);

	if (g_SHLLastSentInputLock[index] == shouldLock)
		return;

	if (gmsgSHLInputLock <= 0)
	{
		if (SHL_DebugEnabled())
			ALERT(at_console, "SHL ERROR: SHLInL message id is 0, not sending\n");

		return;
	}

	g_SHLLastSentInputLock[index] = shouldLock;

	MESSAGE_BEGIN(MSG_ONE, gmsgSHLInputLock, nullptr, pEntity);
	WRITE_BYTE(shouldLock ? 1 : 0);
	MESSAGE_END();

	if (SHL_DebugEnabled())
		ALERT(at_console, "SHL: sent client input lock %d\n", shouldLock ? 1 : 0);
}

static void SHL_UpdatePlayerWeaponHide(edict_t* pEntity)
{
	const int index = SHL_GetControlIndex(pEntity);

	if (index <= 0)
		return;

	entvars_t* pev = &pEntity->v;

	const bool shouldHide =
		SHL_ShouldBlockPlayerInput(pEntity) ||
		SHL_ProfileHidesWeapon(pEntity);

	if (shouldHide)
	{
		if (!g_SHLWeaponHidden[index])
		{
			g_SHLWeaponHidden[index] = true;
			g_SHLSavedViewModel[index] = pev->viewmodel;
			g_SHLSavedWeaponModel[index] = pev->weaponmodel;

			if (SHL_DebugEnabled())
				ALERT(at_console, "SHL: weapon hidden\n");
		}

		pev->viewmodel = 0;
		pev->weaponmodel = 0;
		return;
	}

	if (g_SHLWeaponHidden[index])
	{
		g_SHLWeaponHidden[index] = false;

		pev->viewmodel = g_SHLSavedViewModel[index];
		pev->weaponmodel = g_SHLSavedWeaponModel[index];

		g_SHLSavedViewModel[index] = 0;
		g_SHLSavedWeaponModel[index] = 0;

		if (SHL_DebugEnabled())
			ALERT(at_console, "SHL: weapon restored\n");
	}
}

void SHL_UpdatePlayerControl(edict_t* pEntity)
{
	SHL_UpdateClientInputLockMessage(pEntity);
	SHL_UpdateGroundedCameraMessage(pEntity);
	SHL_UpdatePlayerWeaponHide(pEntity);
}

void SHL_ForceRestorePlayerControl(edict_t* pEntity)
{
	const int index = SHL_GetControlIndex(pEntity);

	if (index <= 0)
		return;

	g_SHLLastSentInputLock[index] = true;

	if (gmsgSHLInputLock > 0)
	{
		MESSAGE_BEGIN(MSG_ONE, gmsgSHLInputLock, nullptr, pEntity);
		WRITE_BYTE(0);
		MESSAGE_END();
	}

	g_SHLLastSentInputLock[index] = false;

	if (gmsgSHLGroundedCam > 0)
	{
		MESSAGE_BEGIN(MSG_ONE, gmsgSHLGroundedCam, nullptr, pEntity);
		WRITE_BYTE(0);
		MESSAGE_END();
	}

	g_SHLLastSentGroundedCam[index] = false;

	if (g_SHLWeaponHidden[index])
	{
		entvars_t* pev = &pEntity->v;

		pev->viewmodel = g_SHLSavedViewModel[index];
		pev->weaponmodel = g_SHLSavedWeaponModel[index];

		g_SHLWeaponHidden[index] = false;
		g_SHLSavedViewModel[index] = 0;
		g_SHLSavedWeaponModel[index] = 0;
	}

	if (SHL_DebugEnabled())
		ALERT(at_console, "SHL: force restored player control\n");
}