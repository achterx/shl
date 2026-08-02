#include "extdll.h"
#include "util.h"
#include "cbase.h"

#include "shl_skill.h"
#include "shl_scene.h"
#include "shl_scene_editor.h"

#define SHL_SCENE_EDITOR_MAX_PLAYERS 33

enum SHLSceneEditorTarget
{
	SHL_SCENE_EDITOR_TARGET_MONSTER = 0,
	SHL_SCENE_EDITOR_TARGET_PLAYER
};

struct shl_scene_editor_state_t
{
	bool enabled;
	int target;
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

	case SHL_SCENE_EDITOR_TARGET_MONSTER:
	default:
		break;
	}

	return "monster";
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

static void SHL_SceneEditorSetEnabled(edict_t* pPlayer, bool enabled)
{
	const int index = SHL_SceneEditorPlayerIndex(pPlayer);

	if (index <= 0)
		return;

	g_SHLSceneEditorStates[index].enabled = enabled;

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

	if (pszTarget == nullptr)
		return;

	if (FStrEq(pszTarget, "player"))
	{
		g_SHLSceneEditorStates[index].target =
			SHL_SCENE_EDITOR_TARGET_PLAYER;
	}
	else if (FStrEq(pszTarget, "monster") || FStrEq(pszTarget, "npc"))
	{
		g_SHLSceneEditorStates[index].target =
			SHL_SCENE_EDITOR_TARGET_MONSTER;
	}
	else
	{
		ALERT(
			at_console,
			"SHL Scene Editor: unknown target '%s'. Use player or monster.\n",
			pszTarget);
		return;
	}

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

	return false;
}

void SHL_SceneEditorThink()
{
	// Safe editor shell only.
	// No scene movement, no gizmo drawing, no anchor edits yet.
}