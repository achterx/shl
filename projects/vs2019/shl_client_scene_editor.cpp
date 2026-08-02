#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "usercmd.h"
#include "ref_params.h"

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
static float g_vecSHLClientSceneEditorOffset[3] = {0.0f, 0.0f, 0.0f};

static float SHL_DegToRad(float degrees)
{
	return degrees * 3.1415926535f / 180.0f;
}

void SHL_ClientSceneEditorInit()
{
	g_bSHLClientSceneEditorEnabled = false;
	g_vecSHLClientSceneEditorOffset[0] = 0.0f;
	g_vecSHLClientSceneEditorOffset[1] = 0.0f;
	g_vecSHLClientSceneEditorOffset[2] = 0.0f;
}

void SHL_ClientSceneEditorVidInit()
{
	g_vecSHLClientSceneEditorOffset[0] = 0.0f;
	g_vecSHLClientSceneEditorOffset[1] = 0.0f;
	g_vecSHLClientSceneEditorOffset[2] = 0.0f;
}

void SHL_ClientSceneEditorMessage(const char* pszName, int iSize, void* pbuf)
{
	BEGIN_READ(pbuf, iSize);

	const bool oldEnabled = g_bSHLClientSceneEditorEnabled;
	g_bSHLClientSceneEditorEnabled = READ_BYTE() ? true : false;

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