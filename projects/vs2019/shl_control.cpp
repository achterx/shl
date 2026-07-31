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
#include "shl_camera.h"

#define SHL_MAX_CONTROL_STATES 33

static bool g_SHLWeaponHidden[SHL_MAX_CONTROL_STATES];
static string_t g_SHLSavedViewModel[SHL_MAX_CONTROL_STATES];
static string_t g_SHLSavedWeaponModel[SHL_MAX_CONTROL_STATES];

static bool g_SHLLastSentInputLock[SHL_MAX_CONTROL_STATES];
static int g_SHLLastSentCameraMode[SHL_MAX_CONTROL_STATES];
static int g_SHLLastSentCameraPitch[SHL_MAX_CONTROL_STATES];
static int g_SHLLastSentCameraYaw[SHL_MAX_CONTROL_STATES];
static bool g_SHLCameraInitialized[SHL_MAX_CONTROL_STATES];

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

static int SHL_GetCameraModeForPlayer(edict_t* pEntity)
{
	if (pEntity == nullptr)
		return SHL_CAMERA_NONE;

	const int state = SHL_GetPlayerStateId(pEntity);

	switch (state)
	{
	case SHL_PLAYERSTATE_GROUNDED:
		return SHL_CAMERA_GROUNDED;

	case SHL_PLAYERSTATE_GRABBED:
		return SHL_CAMERA_GRABBED;

	case SHL_PLAYERSTATE_ACTIVE_SCENE:
		return SHL_CAMERA_SCENE;

	case SHL_PLAYERSTATE_CLIMAX_LOCKED:
		return SHL_CAMERA_CLIMAX;

	case SHL_PLAYERSTATE_DEFEAT_SCENE:
	case SHL_PLAYERSTATE_DEFEAT_MENU:
		return SHL_CAMERA_DEFEAT;

	default:
		break;
	}

	return SHL_CAMERA_NONE;
}

static int SHL_CameraAngleToShort(float angle)
{
	while (angle < 0.0f)
		angle += 360.0f;

	while (angle >= 360.0f)
		angle -= 360.0f;

	return (int)(angle * 65536.0f / 360.0f) & 65535;
}

static void SHL_GetCameraLookAnglesToSceneOwner(
	edict_t* pEntity,
	float& pitch,
	float& yaw,
	bool& hasTarget)
{
	pitch = 0.0f;
	yaw = 0.0f;
	hasTarget = false;

	if (pEntity == nullptr)
		return;

	CBaseEntity* pOwner = SHL_GetPlayerSceneOwner(pEntity);

	if (pOwner == nullptr)
		return;

	Vector from = pEntity->v.origin + pEntity->v.view_ofs;
	Vector to = pOwner->pev->origin + Vector(0.0f, 0.0f, 48.0f);

	Vector delta = to - from;

	if (delta.Length() <= 1.0f)
		return;

	Vector angles = UTIL_VecToAngles(delta);

	pitch = -angles.x;
	yaw = angles.y;
	hasTarget = true;
}

static void SHL_UpdateCameraModeMessage(edict_t* pEntity)
{
	const int index = SHL_GetControlIndex(pEntity);

	if (index <= 0)
		return;

	const int cameraMode = SHL_GetCameraModeForPlayer(pEntity);

	float pitch = 0.0f;
	float yaw = 0.0f;
	bool hasTarget = false;

	if (cameraMode == SHL_CAMERA_GRABBED ||
		cameraMode == SHL_CAMERA_SCENE ||
		cameraMode == SHL_CAMERA_CLIMAX)
	{
		SHL_GetCameraLookAnglesToSceneOwner(
			pEntity,
			pitch,
			yaw,
			hasTarget);
	}

	const int pitchShort = hasTarget ? SHL_CameraAngleToShort(pitch) : 0;
	const int yawShort = hasTarget ? SHL_CameraAngleToShort(yaw) : 0;

	if (g_SHLCameraInitialized[index] &&
		g_SHLLastSentCameraMode[index] == cameraMode &&
		g_SHLLastSentCameraPitch[index] == pitchShort &&
		g_SHLLastSentCameraYaw[index] == yawShort)
	{
		return;
	}

	g_SHLCameraInitialized[index] = true;
	g_SHLLastSentCameraMode[index] = cameraMode;
	g_SHLLastSentCameraPitch[index] = pitchShort;
	g_SHLLastSentCameraYaw[index] = yawShort;

	if (hasTarget)
	{
		SHL_SendCameraModeAngles(pEntity, cameraMode, pitch, yaw);
	}
	else
	{
		SHL_SendCameraMode(pEntity, cameraMode);
	}

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: sent camera mode %d pitch %.1f yaw %.1f target=%d\n",
			cameraMode,
			pitch,
			yaw,
			hasTarget ? 1 : 0);
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
	SHL_UpdateCameraModeMessage(pEntity);
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

	g_SHLLastSentCameraMode[index] = -1;
	g_SHLCameraInitialized[index] = false;
	g_SHLLastSentCameraMode[index] = SHL_CAMERA_NONE;
	g_SHLLastSentCameraPitch[index] = 0;
	g_SHLLastSentCameraYaw[index] = 0;

	SHL_SendCameraMode(pEntity, SHL_CAMERA_NONE);
	g_SHLLastSentCameraMode[index] = SHL_CAMERA_NONE;

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