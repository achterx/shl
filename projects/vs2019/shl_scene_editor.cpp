#include "extdll.h"
#include "util.h"
#include "cbase.h"

#include "shl_skill.h"
#include "shl_scene.h"
#include "shl_scene_editor.h"
#include "../../dlls/UserMessages.h"

#define SHL_SCENE_EDITOR_MAX_PLAYERS 33
#define SHL_SCENE_EDITOR_AXIS_LENGTH 32.0f
#define SHL_SCENE_EDITOR_DRAW_INTERVAL 0.05f

enum SHLSceneEditorTarget
{
	SHL_SCENE_EDITOR_TARGET_PLAYER = -1,
	SHL_SCENE_EDITOR_TARGET_SLOT0 = 0,
	SHL_SCENE_EDITOR_TARGET_SLOT1,
	SHL_SCENE_EDITOR_TARGET_SLOT2,
};

struct shl_scene_editor_state_t
{
	bool enabled;
	int target;
	float nextDrawTime;
	bool autoPausedScene;
};

static shl_scene_editor_state_t g_SHLSceneEditorStates[SHL_SCENE_EDITOR_MAX_PLAYERS];

static int SHL_SceneEditorPlayerIndex(edict_t* pPlayer)
{
	if (pPlayer == nullptr)
		return 0;

	const int index = ENTINDEX(pPlayer);

	if (index <= 0 || index >= SHL_SCENE_EDITOR_MAX_PLAYERS)
		return 0;

	return index;
}

static const char* SHL_SceneEditorTargetName(int target)
{
	switch (target)
	{
	case SHL_SCENE_EDITOR_TARGET_PLAYER:
		return "player";

	case SHL_SCENE_EDITOR_TARGET_SLOT0:
		return "slot0";

	case SHL_SCENE_EDITOR_TARGET_SLOT1:
		return "slot1";

	case SHL_SCENE_EDITOR_TARGET_SLOT2:
		return "slot2";

	default:
		break;
	}

	return "unknown";
}

static bool SHL_SceneEditorParseTarget(const char* pszTarget, int& outTarget)
{
	if (pszTarget == nullptr || pszTarget[0] == '\0')
		return false;

	if (FStrEq(pszTarget, "player"))
	{
		outTarget = SHL_SCENE_EDITOR_TARGET_PLAYER;
		return true;
	}

	if (FStrEq(pszTarget, "monster") || FStrEq(pszTarget, "npc"))
	{
		// Backward-compatible alias.
		outTarget = SHL_SCENE_EDITOR_TARGET_SLOT0;
		return true;
	}

	if (FStrEq(pszTarget, "slot0"))
	{
		outTarget = SHL_SCENE_EDITOR_TARGET_SLOT0;
		return true;
	}

	if (FStrEq(pszTarget, "slot1"))
	{
		outTarget = SHL_SCENE_EDITOR_TARGET_SLOT1;
		return true;
	}

	if (FStrEq(pszTarget, "slot2"))
	{
		outTarget = SHL_SCENE_EDITOR_TARGET_SLOT2;
		return true;
	}

	return false;
}

bool SHL_SceneEditorIsEnabled(edict_t* pPlayer)
{
	const int index = SHL_SceneEditorPlayerIndex(pPlayer);

	if (index <= 0)
		return false;

	return g_SHLSceneEditorStates[index].enabled;
}

static void SHL_SceneEditorPrintState(edict_t* pPlayer)
{
	const int index = SHL_SceneEditorPlayerIndex(pPlayer);

	if (index <= 0)
		return;

	ALERT(
		at_console,
		"SHL Scene Editor: enabled=%d target=%s inScene=%d\n",
		g_SHLSceneEditorStates[index].enabled ? 1 : 0,
		SHL_SceneEditorTargetName(g_SHLSceneEditorStates[index].target),
		SHL_IsPlayerInScene(pPlayer) ? 1 : 0);
}

static void SHL_SceneEditorSendState(edict_t* pPlayer)
{
	if (pPlayer == nullptr)
		return;

	const int index = SHL_SceneEditorPlayerIndex(pPlayer);

	if (index <= 0)
		return;

	if (gmsgSHLSceneEditor <= 0)
		return;

	MESSAGE_BEGIN(MSG_ONE, gmsgSHLSceneEditor, NULL, pPlayer);
	WRITE_BYTE(g_SHLSceneEditorStates[index].enabled ? 1 : 0);
	MESSAGE_END();
}

static void SHL_SceneEditorSetEnabled(edict_t* pPlayer, bool enabled)
{
	const int index = SHL_SceneEditorPlayerIndex(pPlayer);

	if (index <= 0)
		return;

	const bool wasEnabled = g_SHLSceneEditorStates[index].enabled;

	g_SHLSceneEditorStates[index].enabled = enabled;

	if (enabled && !wasEnabled)
	{
		g_SHLSceneEditorStates[index].autoPausedScene = false;

		if (SHL_IsPlayerInScene(pPlayer) &&
			!SHL_IsPlayerScenePaused(pPlayer))
		{
			SHL_SetPlayerScenePaused(pPlayer, true);
			g_SHLSceneEditorStates[index].autoPausedScene = true;

			ALERT(at_console, "SHL Scene Editor: auto-paused scene\n");
		}
	}
	else if (!enabled && wasEnabled)
	{
		if (g_SHLSceneEditorStates[index].autoPausedScene &&
			SHL_IsPlayerInScene(pPlayer) &&
			SHL_IsPlayerScenePaused(pPlayer))
		{
			SHL_SetPlayerScenePaused(pPlayer, false);

			ALERT(at_console, "SHL Scene Editor: auto-resumed scene\n");
		}

		g_SHLSceneEditorStates[index].autoPausedScene = false;
	}

	SHL_SceneEditorSendState(pPlayer);

	ALERT(
		at_console,
		"SHL Scene Editor: %s\n",
		enabled ? "enabled" : "disabled");
}

static void SHL_SceneEditorSetTarget(edict_t* pPlayer, const char* pszTarget)
{
	const int index = SHL_SceneEditorPlayerIndex(pPlayer);

	if (index <= 0)
		return;

	int target = SHL_SCENE_EDITOR_TARGET_SLOT0;

	if (!SHL_SceneEditorParseTarget(pszTarget, target))
	{
		ALERT(
			at_console,
			"SHL Scene Editor: unknown target '%s'. Use player, slot0-slot7.\n",
			pszTarget != nullptr ? pszTarget : "");
		return;
	}

	g_SHLSceneEditorStates[index].target = target;

	ALERT(
		at_console,
		"SHL Scene Editor: target=%s\n",
		SHL_SceneEditorTargetName(g_SHLSceneEditorStates[index].target));
}

bool SHL_SceneEditorClientCommand(edict_t* pPlayer, const char* pszCommand)
{
	if (pPlayer == nullptr || pszCommand == nullptr)
		return false;

	if (FStrEq(pszCommand, "shl_sceneedit"))
	{
		const char* pszArg = CMD_ARGV(1);

		if (pszArg == nullptr || pszArg[0] == '\0')
		{
			const bool currentlyEnabled = SHL_SceneEditorIsEnabled(pPlayer);
			SHL_SceneEditorSetEnabled(pPlayer, !currentlyEnabled);
			SHL_SceneEditorPrintState(pPlayer);
			return true;
		}

		if (FStrEq(pszArg, "1") || FStrEq(pszArg, "on"))
		{
			SHL_SceneEditorSetEnabled(pPlayer, true);
			SHL_SceneEditorPrintState(pPlayer);
			return true;
		}

		if (FStrEq(pszArg, "0") || FStrEq(pszArg, "off"))
		{
			SHL_SceneEditorSetEnabled(pPlayer, false);
			SHL_SceneEditorPrintState(pPlayer);
			return true;
		}

		ALERT(at_console, "Usage: shl_sceneedit 0/1\n");
		return true;
	}

	if (FStrEq(pszCommand, "shl_sceneedit_target"))
	{
		SHL_SceneEditorSetTarget(pPlayer, CMD_ARGV(1));
		return true;
	}

	if (FStrEq(pszCommand, "shl_sceneedit_print"))
	{
		SHL_SceneEditorPrintState(pPlayer);
		return true;
	}

	if (FStrEq(pszCommand, "shl_scenepause"))
	{
		if (!SHL_IsPlayerInScene(pPlayer))
		{
			ALERT(at_console, "SHL Scene Editor: no active scene to pause.\n");
			return true;
		}

		const bool paused = SHL_TogglePlayerScenePaused(pPlayer);

		ALERT(
			at_console,
			"SHL Scene Editor: scene %s\n",
			paused ? "paused" : "resumed");

		return true;
	}

	return false;
}

static void SHL_SceneEditorSendDLight(
	edict_t* pPlayer,
	const Vector& origin,
	int radius,
	int r,
	int g,
	int b,
	int life,
	int decay)
{
	if (pPlayer == nullptr)
		return;

	MESSAGE_BEGIN(MSG_ONE, SVC_TEMPENTITY, NULL, pPlayer);
	WRITE_BYTE(TE_DLIGHT);
	WRITE_COORD(origin.x);
	WRITE_COORD(origin.y);
	WRITE_COORD(origin.z);
	WRITE_BYTE(radius);
	WRITE_BYTE(r);
	WRITE_BYTE(g);
	WRITE_BYTE(b);
	WRITE_BYTE(life);
	WRITE_BYTE(decay);
	MESSAGE_END();
}

static Vector SHL_SceneEditorForwardRightOffset(
	const Vector& origin,
	float yaw,
	float forward,
	float right,
	float z)
{
	Vector angles = g_vecZero;
	angles.y = yaw;

	MAKE_VECTORS(angles);

	Vector result = origin;
	result = result + gpGlobals->v_forward * forward;
	result = result + gpGlobals->v_right * right;
	result.z += z;

	return result;
}

static bool SHL_SceneEditorGetTargetOriginYaw(
	edict_t* pPlayer,
	int target,
	Vector& origin,
	float& yaw)
{
	origin = g_vecZero;
	yaw = 0.0f;

	if (pPlayer == nullptr)
		return false;

	Vector sceneOrigin = g_vecZero;
	float sceneYaw = 0.0f;

	if (!SHL_GetPlayerSceneAnchor(pPlayer, sceneOrigin, sceneYaw))
		return false;

	if (target == SHL_SCENE_EDITOR_TARGET_PLAYER)
	{
		origin = sceneOrigin;
		origin.z += 48.0f;
		yaw = sceneYaw;
		return true;
	}

	if (target == SHL_SCENE_EDITOR_TARGET_SLOT0)
	{
		CBaseEntity* pOwner = SHL_GetPlayerSceneOwner(pPlayer);

		if (pOwner == nullptr)
			return false;

		origin = pOwner->pev->origin;
		origin.z += 64.0f;
		yaw = pOwner->pev->angles.y;
		return true;
	}

	// Future multi-actor slots.
	// For now, draw reserved slots near the scene anchor so the selector works.
	if (target >= SHL_SCENE_EDITOR_TARGET_SLOT1 &&
		target <= SHL_SCENE_EDITOR_TARGET_SLOT2)
	{
		const float slotOffset = 16.0f + ((float)target * 8.0f);

		origin = SHL_SceneEditorForwardRightOffset(
			sceneOrigin,
			sceneYaw,
			0.0f,
			slotOffset,
			16.0f);

		yaw = sceneYaw;
		return true;
	}

	return false;
}

static void SHL_SceneEditorSendBeam(
	edict_t* pPlayer,
	const Vector& start,
	const Vector& end,
	int r,
	int g,
	int b)
{
	if (pPlayer == nullptr)
		return;

	// GoldSrc normally precaches this for standard beam effects.
	extern short g_sModelIndexLaser;

	if (g_sModelIndexLaser <= 0)
		return;

	MESSAGE_BEGIN(MSG_ONE, SVC_TEMPENTITY, NULL, pPlayer);
	WRITE_BYTE(TE_BEAMPOINTS);
	WRITE_COORD(start.x);
	WRITE_COORD(start.y);
	WRITE_COORD(start.z);
	WRITE_COORD(end.x);
	WRITE_COORD(end.y);
	WRITE_COORD(end.z);
	WRITE_SHORT(g_sModelIndexLaser);
	WRITE_BYTE(0); // start frame
	WRITE_BYTE(0); // frame rate
	WRITE_BYTE(2); // life, 0.2 sec
	WRITE_BYTE(8); // width
	WRITE_BYTE(0); // noise
	WRITE_BYTE(r);
	WRITE_BYTE(g);
	WRITE_BYTE(b);
	WRITE_BYTE(255); // brightness
	WRITE_BYTE(0);	 // scroll speed
	MESSAGE_END();
}

static void SHL_SceneEditorDrawGizmo(edict_t* pPlayer, int target)
{
	if (pPlayer == nullptr)
		return;

	Vector origin = g_vecZero;
	float yaw = 0.0f;

	if (!SHL_SceneEditorGetTargetOriginYaw(pPlayer, target, origin, yaw))
		return;

	const float axisLength = SHL_SCENE_EDITOR_AXIS_LENGTH;

	Vector forwardPoint =
		SHL_SceneEditorForwardRightOffset(origin, yaw, axisLength, 0.0f, 0.0f);

	Vector backPoint =
		SHL_SceneEditorForwardRightOffset(origin, yaw, -axisLength, 0.0f, 0.0f);

	Vector rightPoint =
		SHL_SceneEditorForwardRightOffset(origin, yaw, 0.0f, axisLength, 0.0f);

	Vector leftPoint =
		SHL_SceneEditorForwardRightOffset(origin, yaw, 0.0f, -axisLength, 0.0f);

	Vector upPoint = origin;
	upPoint.z += axisLength;

	Vector downPoint = origin;
	downPoint.z -= axisLength;

	// Axis beams.
	SHL_SceneEditorSendBeam(pPlayer, backPoint, forwardPoint, 255, 40, 40); // red
	SHL_SceneEditorSendBeam(pPlayer, leftPoint, rightPoint, 40, 255, 40);	// green
	SHL_SceneEditorSendBeam(pPlayer, downPoint, upPoint, 40, 120, 255);		// blue

	// White center cross.
	Vector c1 = origin;
	Vector c2 = origin;

	c1.x -= 5.0f;
	c2.x += 5.0f;
	SHL_SceneEditorSendBeam(pPlayer, c1, c2, 255, 255, 255);

	c1 = origin;
	c2 = origin;
	c1.y -= 5.0f;
	c2.y += 5.0f;
	SHL_SceneEditorSendBeam(pPlayer, c1, c2, 255, 255, 255);

	c1 = origin;
	c2 = origin;
	c1.z -= 5.0f;
	c2.z += 5.0f;
	SHL_SceneEditorSendBeam(pPlayer, c1, c2, 255, 255, 255);
}

void SHL_SceneEditorThink()
{
	for (int i = 1; i < SHL_SCENE_EDITOR_MAX_PLAYERS; ++i)
	{
		if (!g_SHLSceneEditorStates[i].enabled)
			continue;

		CBaseEntity* pEntity = UTIL_PlayerByIndex(i);

		if (pEntity == nullptr)
			continue;

		edict_t* pPlayer = pEntity->edict();

		if (pPlayer == nullptr)
			continue;

		if (!SHL_IsPlayerInScene(pPlayer))
		{
			g_SHLSceneEditorStates[i].autoPausedScene = false;
			continue;
		}

		if (!SHL_IsPlayerScenePaused(pPlayer))
		{
			SHL_SetPlayerScenePaused(pPlayer, true);
			g_SHLSceneEditorStates[i].autoPausedScene = true;

			if (SHL_DebugEnabled())
			{
				ALERT(at_console, "SHL Scene Editor: auto-paused newly active scene\n");
			}
		}

		if (gpGlobals->time < g_SHLSceneEditorStates[i].nextDrawTime)
			continue;

		g_SHLSceneEditorStates[i].nextDrawTime =
			gpGlobals->time + SHL_SCENE_EDITOR_DRAW_INTERVAL;

		SHL_SceneEditorDrawGizmo(
			pPlayer,
			g_SHLSceneEditorStates[i].target);
	}
}