#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "usercmd.h"
#include "ref_params.h"
#include <stdio.h>
#include <math.h>

#include "../projects/vs2019/shl_client_scene_editor.h"

#ifndef PITCH
#define PITCH 0
#endif

#ifndef YAW
#define YAW 1
#endif

#ifndef ROLL
#define ROLL 2
#endif

static bool g_bSHLClientSceneEditorEnabled = false;
static bool g_bSHLClientSceneEditorCursorEnabled = false;

static float g_vecSHLClientSceneEditorOffset[3] = {0.0f, 0.0f, 0.0f};

static int g_iSHLClientSceneEditorLastMouseX = 0;
static int g_iSHLClientSceneEditorLastMouseY = 0;
static bool g_bSHLClientSceneEditorHasMouse = false;
static bool g_bSHLClientSceneEditorDragging = false;
static bool g_bSHLClientSceneEditorRightWasDown = false;

static float SHL_DegToRad(float degrees)
{
	return degrees * 3.1415926535f / 180.0f;
}

static void SHL_ClientSceneEditorSetCursorEnabled(bool enabled)
{
	if (g_bSHLClientSceneEditorCursorEnabled == enabled)
		return;

	g_bSHLClientSceneEditorCursorEnabled = enabled;

	g_iSHLClientSceneEditorLastMouseX = 0;
	g_iSHLClientSceneEditorLastMouseY = 0;
	g_bSHLClientSceneEditorHasMouse = false;
	g_bSHLClientSceneEditorDragging = false;
	g_bSHLClientSceneEditorRightWasDown = false;

	gEngfuncs.Con_Printf(
		enabled
			? "SHL Scene Editor: cursor drag mode enabled\n"
			: "SHL Scene Editor: cursor drag mode disabled\n");
}

bool SHL_ClientSceneEditorCursorEnabled()
{
	return g_bSHLClientSceneEditorCursorEnabled;
}

void SHL_ClientSceneEditorCursorCommand()
{
	if (!g_bSHLClientSceneEditorEnabled)
	{
		gEngfuncs.Con_Printf("SHL Scene Editor: enable sceneedit first\n");
		return;
	}

	SHL_ClientSceneEditorSetCursorEnabled(
		!g_bSHLClientSceneEditorCursorEnabled);
}

void SHL_ClientSceneEditorInit()
{
	g_bSHLClientSceneEditorEnabled = false;
	g_vecSHLClientSceneEditorOffset[0] = 0.0f;
	g_vecSHLClientSceneEditorOffset[1] = 0.0f;
	g_vecSHLClientSceneEditorOffset[2] = 0.0f;
	g_bSHLClientSceneEditorCursorEnabled = false;
	g_bSHLClientSceneEditorHasMouse = false;
	g_bSHLClientSceneEditorDragging = false;
	g_bSHLClientSceneEditorRightWasDown = false;
}

void SHL_ClientSceneEditorVidInit()
{
	g_vecSHLClientSceneEditorOffset[0] = 0.0f;
	g_vecSHLClientSceneEditorOffset[1] = 0.0f;
	g_vecSHLClientSceneEditorOffset[2] = 0.0f;
}

void SHL_ClientSceneEditorMouse(usercmd_s* cmd)
{
	if (!g_bSHLClientSceneEditorEnabled)
		return;

	if (cmd == nullptr)
		return;

#ifndef IN_ATTACK
#define IN_ATTACK (1 << 0)
#endif

	// Old drag mode: mouse-look delta mode.
	// Only used when OS cursor mode is OFF.
	if (!g_bSHLClientSceneEditorCursorEnabled)
	{
		static bool s_wasDragging = false;
		static float s_lockedAngles[3] = {0.0f, 0.0f, 0.0f};

		float currentAngles[3];
		currentAngles[0] = cmd->viewangles[0];
		currentAngles[1] = cmd->viewangles[1];
		currentAngles[2] = cmd->viewangles[2];

		if (!(cmd->buttons & IN_ATTACK))
		{
			s_wasDragging = false;

			s_lockedAngles[0] = currentAngles[0];
			s_lockedAngles[1] = currentAngles[1];
			s_lockedAngles[2] = currentAngles[2];

			return;
		}

		if (!s_wasDragging)
		{
			s_wasDragging = true;

			s_lockedAngles[0] = currentAngles[0];
			s_lockedAngles[1] = currentAngles[1];
			s_lockedAngles[2] = currentAngles[2];

			return;
		}

		float dx = currentAngles[YAW] - s_lockedAngles[YAW];
		float dy = currentAngles[PITCH] - s_lockedAngles[PITCH];

		while (dx > 180.0f)
			dx -= 360.0f;

		while (dx < -180.0f)
			dx += 360.0f;

		if (!(dx > -0.001f && dx < 0.001f &&
				dy > -0.001f && dy < 0.001f))
		{
			char command[128];

			sprintf(
				command,
				"shl_sceneedit_mousedrag %.3f %.3f\n",
				dx,
				dy);

			gEngfuncs.pfnServerCmd(command);
		}

		cmd->viewangles[0] = s_lockedAngles[0];
		cmd->viewangles[1] = s_lockedAngles[1];
		cmd->viewangles[2] = s_lockedAngles[2];

		gEngfuncs.SetViewAngles(s_lockedAngles);
		return;
	}

		// OS cursor position is not available in this client SDK.
	// Fall back to locked view-angle drag mode even when cursor mode is on.
	static bool s_cursorWasDragging = false;
	static float s_cursorLockedAngles[3] = {0.0f, 0.0f, 0.0f};

	float currentAngles[3];
	currentAngles[0] = cmd->viewangles[0];
	currentAngles[1] = cmd->viewangles[1];
	currentAngles[2] = cmd->viewangles[2];

	if (!(cmd->buttons & IN_ATTACK))
	{
		s_cursorWasDragging = false;

		s_cursorLockedAngles[0] = currentAngles[0];
		s_cursorLockedAngles[1] = currentAngles[1];
		s_cursorLockedAngles[2] = currentAngles[2];

		return;
	}

	if (!s_cursorWasDragging)
	{
		s_cursorWasDragging = true;

		s_cursorLockedAngles[0] = currentAngles[0];
		s_cursorLockedAngles[1] = currentAngles[1];
		s_cursorLockedAngles[2] = currentAngles[2];

		return;
	}

	float dx = currentAngles[YAW] - s_cursorLockedAngles[YAW];
	float dy = currentAngles[PITCH] - s_cursorLockedAngles[PITCH];

	while (dx > 180.0f)
		dx -= 360.0f;

	while (dx < -180.0f)
		dx += 360.0f;

	if (!(dx > -0.001f && dx < 0.001f &&
			dy > -0.001f && dy < 0.001f))
	{
		char command[128];

		sprintf(
			command,
			"shl_sceneedit_mousedrag %.3f %.3f\n",
			dx,
			dy);

		gEngfuncs.pfnServerCmd(command);
	}

	cmd->viewangles[0] = s_cursorLockedAngles[0];
	cmd->viewangles[1] = s_cursorLockedAngles[1];
	cmd->viewangles[2] = s_cursorLockedAngles[2];

	gEngfuncs.SetViewAngles(s_cursorLockedAngles);

}

void SHL_ClientSceneEditorButtons(usercmd_s* cmd)
{
	if (!g_bSHLClientSceneEditorEnabled)
		return;

	if (cmd == nullptr)
		return;

#ifndef IN_ATTACK2
#define IN_ATTACK2 (1 << 11)
#endif

#ifndef IN_SPEED
#define IN_SPEED (1 << 17)
#endif

	const bool rightDown = (cmd->buttons & IN_ATTACK2) ? true : false;

	if (rightDown && !g_bSHLClientSceneEditorRightWasDown)
	{
		if (cmd->buttons & IN_SPEED)
		{
			gEngfuncs.pfnServerCmd("shl_sceneedit_axiscycle -1\n");
		}
		else
		{
			gEngfuncs.pfnServerCmd("shl_sceneedit_axiscycle 1\n");
		}
	}

	g_bSHLClientSceneEditorRightWasDown = rightDown;
}

void SHL_ClientSceneEditorMessage(const char* pszName, int iSize, void* pbuf)
{
	BEGIN_READ(pbuf, iSize);

	const bool oldEnabled = g_bSHLClientSceneEditorEnabled;
	g_bSHLClientSceneEditorEnabled = READ_BYTE() ? true : false;

	if (!g_bSHLClientSceneEditorEnabled &&
		g_bSHLClientSceneEditorCursorEnabled)
	{
		SHL_ClientSceneEditorSetCursorEnabled(false);
	}

	if (!oldEnabled && g_bSHLClientSceneEditorEnabled)
	{
		g_vecSHLClientSceneEditorOffset[0] = 0.0f;
		g_vecSHLClientSceneEditorOffset[1] = 0.0f;
		g_vecSHLClientSceneEditorOffset[2] = 0.0f;

		float viewangles[3];
		gEngfuncs.GetViewAngles(viewangles);

		if (viewangles[PITCH] > 75.0f)
			viewangles[PITCH] = 75.0f;

		if (viewangles[PITCH] < -75.0f)
			viewangles[PITCH] = -75.0f;

		viewangles[ROLL] = 0.0f;
		gEngfuncs.SetViewAngles(viewangles);

		if (viewangles[PITCH] > 45.0f || viewangles[PITCH] < -45.0f)
		{
			viewangles[PITCH] = 15.0f;
			viewangles[ROLL] = 0.0f;
			gEngfuncs.SetViewAngles(viewangles);
		}
	}
}

bool SHL_ClientSceneEditorEnabled()
{
	return g_bSHLClientSceneEditorEnabled;
}

void SHL_ClientSceneEditorMove(usercmd_s* cmd)
{
	if (!g_bSHLClientSceneEditorEnabled)
		return;

	if (cmd == nullptr)
		return;

	float viewangles[3];
	gEngfuncs.GetViewAngles(viewangles);

	const float yawRad = SHL_DegToRad(viewangles[YAW]);

	const float forwardX = cos(yawRad);
	const float forwardY = sin(yawRad);

	const float rightX = sin(yawRad);
	const float rightY = -cos(yawRad);

	float dt = (float)cmd->msec / 1000.0f;

	if (dt <= 0.0f)
		dt = 0.015f;

	float speed = 140.0f;

	const int SHL_IN_SPEED = (1 << 17);

	if (cmd->buttons & SHL_IN_SPEED)
		speed = 260.0f;

	const float move = speed * dt;

	if (cmd->buttons & IN_FORWARD)
	{
		g_vecSHLClientSceneEditorOffset[0] += forwardX * move;
		g_vecSHLClientSceneEditorOffset[1] += forwardY * move;
	}

	if (cmd->buttons & IN_BACK)
	{
		g_vecSHLClientSceneEditorOffset[0] -= forwardX * move;
		g_vecSHLClientSceneEditorOffset[1] -= forwardY * move;
	}

	if (cmd->buttons & IN_MOVELEFT)
	{
		g_vecSHLClientSceneEditorOffset[0] -= rightX * move;
		g_vecSHLClientSceneEditorOffset[1] -= rightY * move;
	}

	if (cmd->buttons & IN_MOVERIGHT)
	{
		g_vecSHLClientSceneEditorOffset[0] += rightX * move;
		g_vecSHLClientSceneEditorOffset[1] += rightY * move;
	}

	if (cmd->buttons & IN_JUMP)
	{
		g_vecSHLClientSceneEditorOffset[2] += move;
	}

	if (cmd->buttons & IN_DUCK)
	{
		g_vecSHLClientSceneEditorOffset[2] -= move;
	}
}

void SHL_ClientSceneEditorApplyRefdef(ref_params_s* pparams)
{
	if (!g_bSHLClientSceneEditorEnabled)
		return;

	if (pparams == nullptr)
		return;

	pparams->vieworg[0] += g_vecSHLClientSceneEditorOffset[0];
	pparams->vieworg[1] += g_vecSHLClientSceneEditorOffset[1];
	pparams->vieworg[2] += g_vecSHLClientSceneEditorOffset[2];
}