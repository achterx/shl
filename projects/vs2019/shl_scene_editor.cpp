#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include <stdlib.h>
#include <math.h>

#include "shl_skill.h"
#include "shl_scene.h"
#include "shl_scene_editor.h"
#include "../../dlls/UserMessages.h"

#define SHL_SCENE_EDITOR_MAX_PLAYERS 33
#define SHL_SCENE_EDITOR_AXIS_LENGTH 32.0f
#define SHL_SCENE_EDITOR_DRAW_INTERVAL 0.05f

#define SHL_SCENE_EDITOR_MAX_TARGETS 3

enum SHLSceneEditorAxis
{
	SHL_SCENE_EDITOR_AXIS_FORWARD = 0,
	SHL_SCENE_EDITOR_AXIS_RIGHT,
	SHL_SCENE_EDITOR_AXIS_Z,
	SHL_SCENE_EDITOR_AXIS_YAW,
	SHL_SCENE_EDITOR_AXIS_PITCH
};

struct shl_scene_editor_state_t
{
	bool enabled;
	int target;
	float nextDrawTime;

	int axis;
	float step;

	bool anchorOverrideActive[SHL_SCENE_EDITOR_MAX_TARGETS];
	shl_scene_actor_anchor_t anchorOverride[SHL_SCENE_EDITOR_MAX_TARGETS];
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

static const char* SHL_SceneEditorAxisName(int axis)
{
	switch (axis)
	{
	case SHL_SCENE_EDITOR_AXIS_FORWARD:
		return "forward";

	case SHL_SCENE_EDITOR_AXIS_RIGHT:
		return "right";

	case SHL_SCENE_EDITOR_AXIS_Z:
		return "z";

	case SHL_SCENE_EDITOR_AXIS_YAW:
		return "yaw";

	case SHL_SCENE_EDITOR_AXIS_PITCH:
		return "pitch";

	default:
		break;
	}

	return "unknown";
}

static int SHL_SceneEditorParseAxis(const char* pszAxis)
{
	if (pszAxis == nullptr)
		return SHL_SCENE_EDITOR_AXIS_FORWARD;

	if (FStrEq(pszAxis, "forward") || FStrEq(pszAxis, "f"))
		return SHL_SCENE_EDITOR_AXIS_FORWARD;

	if (FStrEq(pszAxis, "right") || FStrEq(pszAxis, "r"))
		return SHL_SCENE_EDITOR_AXIS_RIGHT;

	if (FStrEq(pszAxis, "z") || FStrEq(pszAxis, "up"))
		return SHL_SCENE_EDITOR_AXIS_Z;

	if (FStrEq(pszAxis, "yaw") || FStrEq(pszAxis, "y"))
		return SHL_SCENE_EDITOR_AXIS_YAW;

	if (FStrEq(pszAxis, "pitch") || FStrEq(pszAxis, "p"))
		return SHL_SCENE_EDITOR_AXIS_PITCH;

	return -1;
}

static int SHL_SceneEditorTargetToIndex(int target)
{
	if (target == SHL_SCENE_EDITOR_TARGET_PLAYER)
		return 0;

	if (target >= SHL_SCENE_EDITOR_TARGET_SLOT0 &&
		target <= SHL_SCENE_EDITOR_TARGET_SLOT2)
	{
		return target + 1;
	}

	return -1;
}

static bool SHL_SceneEditorGetBaseAnchorForTarget(
	edict_t* pPlayer,
	int target,
	shl_scene_actor_anchor_t& outAnchor)
{
	outAnchor.forward = 0.0f;
	outAnchor.right = 0.0f;
	outAnchor.z = 0.0f;
	outAnchor.pitchOffset = 0.0f;
	outAnchor.yawOffset = 0.0f;
	outAnchor.dropToFloor = false;

	if (pPlayer == nullptr)
		return false;

	if (!SHL_IsPlayerInScene(pPlayer))
		return false;

	CBaseEntity* pOwner = SHL_GetPlayerSceneOwner(pPlayer);

	if (pOwner == nullptr)
		return false;

	const int sceneType = SHL_GetPlayerSceneType(pPlayer);

	const shl_monster_scene_profile_t* pMonsterProfile =
		SHL_GetMonsterSceneProfile(pOwner, sceneType);

	if (pMonsterProfile == nullptr)
		return false;

	if (target == SHL_SCENE_EDITOR_TARGET_PLAYER)
	{
		outAnchor = pMonsterProfile->playerAnchor;
		return true;
	}

	if (target == SHL_SCENE_EDITOR_TARGET_SLOT0)
	{
		outAnchor = pMonsterProfile->monsterAnchor;
		return true;
	}

	return false;
}

bool SHL_SceneEditorResolveAnchor(
	edict_t* pPlayer,
	int target,
	const shl_scene_actor_anchor_t& baseAnchor,
	shl_scene_actor_anchor_t& outAnchor)
{
	outAnchor = baseAnchor;

	const int playerIndex = SHL_SceneEditorPlayerIndex(pPlayer);

	if (playerIndex <= 0)
		return false;

	const int targetIndex = SHL_SceneEditorTargetToIndex(target);

	if (targetIndex < 0 || targetIndex >= SHL_SCENE_EDITOR_MAX_TARGETS)
		return false;

	if (!g_SHLSceneEditorStates[playerIndex].anchorOverrideActive[targetIndex])
		return false;

	outAnchor = g_SHLSceneEditorStates[playerIndex].anchorOverride[targetIndex];
	return true;
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

	g_SHLSceneEditorStates[index].enabled = enabled;

	if (g_SHLSceneEditorStates[index].step <= 0.0f)
		g_SHLSceneEditorStates[index].step = 1.0f;

	if (g_SHLSceneEditorStates[index].axis < SHL_SCENE_EDITOR_AXIS_FORWARD ||
		g_SHLSceneEditorStates[index].axis > SHL_SCENE_EDITOR_AXIS_PITCH)
	{
		g_SHLSceneEditorStates[index].axis = SHL_SCENE_EDITOR_AXIS_FORWARD;
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

static void SHL_SceneEditorNudge(
	edict_t* pPlayer,
	const char* pszAxis,
	float amount)
{
	if (pPlayer == nullptr)
		return;

	const int playerIndex = SHL_SceneEditorPlayerIndex(pPlayer);

	if (playerIndex <= 0)
		return;

	if (pszAxis == nullptr || pszAxis[0] == '\0')
	{
		ALERT(at_console, "Usage: shl_sceneedit_nudge forward/right/z/yaw amount\n");
		return;
	}

	const int target = g_SHLSceneEditorStates[playerIndex].target;
	const int targetIndex = SHL_SceneEditorTargetToIndex(target);

	if (targetIndex < 0 || targetIndex >= SHL_SCENE_EDITOR_MAX_TARGETS)
	{
		ALERT(at_console, "SHL Scene Editor: bad target.\n");
		return;
	}

	if (target != SHL_SCENE_EDITOR_TARGET_SLOT0)
	{
		ALERT(
			at_console,
			"SHL Scene Editor: only slot0 is editable right now. Current target=%s\n",
			SHL_SceneEditorTargetName(target));
		return;
	}

	if (!g_SHLSceneEditorStates[playerIndex].anchorOverrideActive[targetIndex])
	{
		shl_scene_actor_anchor_t baseAnchor;

		if (!SHL_SceneEditorGetBaseAnchorForTarget(pPlayer, target, baseAnchor))
		{
			ALERT(at_console, "SHL Scene Editor: no active editable scene/profile.\n");
			return;
		}

		g_SHLSceneEditorStates[playerIndex].anchorOverride[targetIndex] = baseAnchor;
		g_SHLSceneEditorStates[playerIndex].anchorOverrideActive[targetIndex] = true;
	}

	shl_scene_actor_anchor_t& anchor =
		g_SHLSceneEditorStates[playerIndex].anchorOverride[targetIndex];

	if (FStrEq(pszAxis, "forward") || FStrEq(pszAxis, "f"))
	{
		anchor.forward += amount;
	}
	else if (FStrEq(pszAxis, "right") || FStrEq(pszAxis, "r"))
	{
		anchor.right += amount;
	}
	else if (FStrEq(pszAxis, "z") || FStrEq(pszAxis, "up"))
	{
		anchor.z += amount;
	}
	else if (FStrEq(pszAxis, "yaw") || FStrEq(pszAxis, "y"))
	{
		anchor.yawOffset += amount;
	}
	else if (FStrEq(pszAxis, "pitch") || FStrEq(pszAxis, "p"))
	{
		anchor.pitchOffset += amount;

		if (anchor.pitchOffset > 89.0f)
			anchor.pitchOffset = 89.0f;

		if (anchor.pitchOffset < -89.0f)
			anchor.pitchOffset = -89.0f;
	}
	else
	{
		ALERT(at_console, "Usage: shl_sceneedit_nudge forward/right/z/yaw/pitch amount\n");
		return;
	}

	if (!SHL_ReapplyCurrentSceneAnchors(pPlayer))
	{
		ALERT(at_console, "SHL Scene Editor: failed to reapply scene anchors.\n");
		return;
	}

	ALERT(
		at_console,
		"SHL Scene Editor: %s %s %.2f -> f=%.2f r=%.2f z=%.2f pitch=%.2f yaw=%.2f\n",
		SHL_SceneEditorTargetName(target),
		pszAxis,
		amount,
		anchor.forward,
		anchor.right,
		anchor.z,
		anchor.pitchOffset,
		anchor.yawOffset);
}

static void SHL_SceneEditorMouseDrag(edict_t* pPlayer, float mouseDx, float mouseDy)
{
	if (pPlayer == nullptr)
		return;

	const int playerIndex = SHL_SceneEditorPlayerIndex(pPlayer);

	if (playerIndex <= 0)
		return;

	if (!g_SHLSceneEditorStates[playerIndex].enabled)
		return;

	if (!SHL_IsPlayerInScene(pPlayer))
		return;

	const int axis = g_SHLSceneEditorStates[playerIndex].axis;

	float step = g_SHLSceneEditorStates[playerIndex].step;

	if (step <= 0.0f)
		step = 1.0f;

	float amount = 0.0f;

	switch (axis)
	{
	case SHL_SCENE_EDITOR_AXIS_FORWARD:
		amount = -mouseDy * step * 0.05f;
		SHL_SceneEditorNudge(pPlayer, "forward", amount);
		break;

	case SHL_SCENE_EDITOR_AXIS_RIGHT:
		amount = mouseDx * step * 0.05f;
		SHL_SceneEditorNudge(pPlayer, "right", amount);
		break;

	case SHL_SCENE_EDITOR_AXIS_Z:
		amount = -mouseDy * step * 0.05f;
		SHL_SceneEditorNudge(pPlayer, "z", amount);
		break;

	case SHL_SCENE_EDITOR_AXIS_YAW:
		amount = mouseDx * step * 0.10f;
		SHL_SceneEditorNudge(pPlayer, "yaw", amount);
		break;

	case SHL_SCENE_EDITOR_AXIS_PITCH:
		amount = -mouseDy * step * 0.10f;
		SHL_SceneEditorNudge(pPlayer, "pitch", amount);
		break;

	default:
		break;
	}
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

		if (FStrEq(pszCommand, "shl_sceneedit_nudge"))
	{
		const char* pszAxis = CMD_ARGV(1);
		const char* pszAmount = CMD_ARGV(2);

		if (pszAxis == nullptr || pszAxis[0] == '\0' ||
			pszAmount == nullptr || pszAmount[0] == '\0')
		{
			ALERT(at_console, "Usage: shl_sceneedit_nudge forward/right/z/yaw amount\n");
			return true;
		}

		SHL_SceneEditorNudge(
			pPlayer,
			pszAxis,
			(float)atof(pszAmount));

		return true;
	}

		if (FStrEq(pszCommand, "shl_sceneedit_axis"))
	{
		const int playerIndex = SHL_SceneEditorPlayerIndex(pPlayer);

		if (playerIndex <= 0)
			return true;

		const int axis = SHL_SceneEditorParseAxis(CMD_ARGV(1));

		if (axis < 0)
		{
			ALERT(at_console, "Usage: shl_sceneedit_axis forward/right/z/yaw/pitch\n");
			return true;
		}

		g_SHLSceneEditorStates[playerIndex].axis = axis;

		ALERT(
			at_console,
			"SHL Scene Editor: axis=%s\n",
			SHL_SceneEditorAxisName(axis));

		return true;
	}

	if (FStrEq(pszCommand, "shl_sceneedit_step"))
	{
		const int playerIndex = SHL_SceneEditorPlayerIndex(pPlayer);

		if (playerIndex <= 0)
			return true;

		const char* pszStep = CMD_ARGV(1);

		if (pszStep == nullptr || pszStep[0] == '\0')
		{
			ALERT(
				at_console,
				"SHL Scene Editor: step=%.2f\n",
				g_SHLSceneEditorStates[playerIndex].step);
			return true;
		}

		g_SHLSceneEditorStates[playerIndex].step = (float)atof(pszStep);

		if (g_SHLSceneEditorStates[playerIndex].step <= 0.0f)
			g_SHLSceneEditorStates[playerIndex].step = 1.0f;

		ALERT(
			at_console,
			"SHL Scene Editor: step=%.2f\n",
			g_SHLSceneEditorStates[playerIndex].step);

		return true;
	}

	if (FStrEq(pszCommand, "shl_sceneedit_mousedrag"))
	{
		const char* pszDx = CMD_ARGV(1);
		const char* pszDy = CMD_ARGV(2);

		if (pszDx == nullptr || pszDx[0] == '\0' ||
			pszDy == nullptr || pszDy[0] == '\0')
		{
			return true;
		}

		SHL_SceneEditorMouseDrag(
			pPlayer,
			(float)atof(pszDx),
			(float)atof(pszDy));

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

static void SHL_SceneEditorSendRing(
	edict_t* pPlayer,
	const Vector& origin,
	float yaw,
	bool vertical,
	int r,
	int g,
	int b);

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

	// Rotation rings.
	SHL_SceneEditorSendRing(pPlayer, origin, yaw, false, 255, 220, 40); // yaw
	SHL_SceneEditorSendRing(pPlayer, origin, yaw, true, 190, 80, 255);	// pitch

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

static void SHL_SceneEditorSendRing(
	edict_t* pPlayer,
	const Vector& origin,
	float yaw,
	bool vertical,
	int r,
	int g,
	int b)
{
	const int segments = 16;
	const float radius = SHL_SCENE_EDITOR_AXIS_LENGTH * 0.75f;

	for (int i = 0; i < segments; ++i)
	{
		const float a0 = ((float)i / (float)segments) * 6.2831853f;
		const float a1 = ((float)(i + 1) / (float)segments) * 6.2831853f;

		Vector p0 = origin;
		Vector p1 = origin;

		if (!vertical)
		{
			p0.x += cos(a0) * radius;
			p0.y += sin(a0) * radius;

			p1.x += cos(a1) * radius;
			p1.y += sin(a1) * radius;
		}
		else
		{
			const float yawRad = yaw * 0.017453292f;
			const float forwardX = cos(yawRad);
			const float forwardY = sin(yawRad);

			p0.x += forwardX * cos(a0) * radius;
			p0.y += forwardY * cos(a0) * radius;
			p0.z += sin(a0) * radius;

			p1.x += forwardX * cos(a1) * radius;
			p1.y += forwardY * cos(a1) * radius;
			p1.z += sin(a1) * radius;
		}

		SHL_SceneEditorSendBeam(pPlayer, p0, p1, r, g, b);
	}
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

	

		if (gpGlobals->time < g_SHLSceneEditorStates[i].nextDrawTime)
			continue;

		g_SHLSceneEditorStates[i].nextDrawTime =
			gpGlobals->time + SHL_SCENE_EDITOR_DRAW_INTERVAL;

		SHL_SceneEditorDrawGizmo(
			pPlayer,
			g_SHLSceneEditorStates[i].target);
	}
}