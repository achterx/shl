#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "ref_params.h"

#include "../projects/vs2019/shl_camera.h"
#include "shl_client_camera.h"

#ifndef PITCH
#define PITCH 0
#endif

#ifndef YAW
#define YAW 1
#endif

#ifndef ROLL
#define ROLL 2
#endif

static int g_iSHLClientCameraMode = SHL_CAMERA_NONE;
static float g_flSHLClientCameraPitch = 0.0f;
static float g_flSHLClientCameraYaw = 0.0f;
static bool g_bSHLClientCameraHasAngles = false;

static float SHL_ShortToAngle(int value)
{
	return (float)(value & 65535) * 360.0f / 65536.0f;
}

static float SHL_NormalizePitch(float pitch)
{
	if (pitch > 180.0f)
		pitch -= 360.0f;

	if (pitch > 89.0f)
		pitch = 89.0f;

	if (pitch < -89.0f)
		pitch = -89.0f;

	return pitch;
}

void SHL_ClientCameraInit()
{
	g_iSHLClientCameraMode = SHL_CAMERA_NONE;
	g_flSHLClientCameraPitch = 0.0f;
	g_flSHLClientCameraYaw = 0.0f;
	g_bSHLClientCameraHasAngles = false;
}

void SHL_ClientCameraVidInit()
{
	SHL_ClientCameraInit();
}

void SHL_ClientCameraMessage(const char* pszName, int iSize, void* pbuf)
{
	BEGIN_READ(pbuf, iSize);

	g_iSHLClientCameraMode = READ_BYTE();

	if (iSize >= 5)
	{
		g_flSHLClientCameraPitch = SHL_NormalizePitch(SHL_ShortToAngle(READ_SHORT()));
		g_flSHLClientCameraYaw = SHL_ShortToAngle(READ_SHORT());
		g_bSHLClientCameraHasAngles = true;
	}
	else
	{
		g_flSHLClientCameraPitch = 0.0f;
		g_flSHLClientCameraYaw = 0.0f;
		g_bSHLClientCameraHasAngles = false;
	}

	if (g_iSHLClientCameraMode < SHL_CAMERA_NONE)
		g_iSHLClientCameraMode = SHL_CAMERA_NONE;

	if (g_iSHLClientCameraMode > SHL_CAMERA_DEFEAT)
		g_iSHLClientCameraMode = SHL_CAMERA_DEFEAT;

	if (g_iSHLClientCameraMode == SHL_CAMERA_NONE)
	{
		g_flSHLClientCameraPitch = 0.0f;
		g_flSHLClientCameraYaw = 0.0f;
		g_bSHLClientCameraHasAngles = false;
	}
}

int SHL_ClientCameraMode()
{
	return g_iSHLClientCameraMode;
}

bool SHL_ClientCameraActive()
{
	return g_iSHLClientCameraMode != SHL_CAMERA_NONE;
}

void SHL_ClientCameraApplyRefdef(ref_params_s* pparams)
{
	if (pparams == nullptr)
		return;

	switch (g_iSHLClientCameraMode)
	{
	case SHL_CAMERA_GROUNDED:
		// Grounded camera only.
		// Low first-person, no dead sideways angle, no NPC look-at.
		pparams->vieworg[2] -= 18.0f;
		pparams->viewangles[ROLL] = 0.0f;
		break;

	case SHL_CAMERA_GRABBED:
	case SHL_CAMERA_SCENE:
	case SHL_CAMERA_CLIMAX:
		// New scene camera.
		// Do not use grounded/dead camera here.
		pparams->viewangles[ROLL] = 0.0f;

		if (g_bSHLClientCameraHasAngles)
		{
			pparams->viewangles[PITCH] = g_flSHLClientCameraPitch;
			pparams->viewangles[YAW] = g_flSHLClientCameraYaw;
		}
		break;

	case SHL_CAMERA_DEFEAT:
		pparams->viewangles[ROLL] = 0.0f;
		break;

	case SHL_CAMERA_NONE:
	default:
		break;
	}
}