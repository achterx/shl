#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "ref_params.h"
#include "view.h"

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

static float SHL_NormalizeAngle180(float angle)
{
	while (angle > 180.0f)
		angle -= 360.0f;

	while (angle < -180.0f)
		angle += 360.0f;

	return angle;
}

static float SHL_NormalizeAngle360(float angle)
{
	while (angle < 0.0f)
		angle += 360.0f;

	while (angle >= 360.0f)
		angle -= 360.0f;

	return angle;
}

static float SHL_ClampFloat(float value, float minValue, float maxValue)
{
	if (value < minValue)
		return minValue;

	if (value > maxValue)
		return maxValue;

	return value;
}

static float SHL_ClampYawAroundTarget(float current, float target, float limit)
{
	const float delta = SHL_NormalizeAngle180(current - target);
	const float clampedDelta = SHL_ClampFloat(delta, -limit, limit);

	return SHL_NormalizeAngle360(target + clampedDelta);
}

static bool SHL_ClientCameraUsesSceneAngleLimit()
{
	return g_iSHLClientCameraMode == SHL_CAMERA_GRABBED ||
		   g_iSHLClientCameraMode == SHL_CAMERA_SCENE ||
		   g_iSHLClientCameraMode == SHL_CAMERA_CLIMAX;
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

	const int oldMode = g_iSHLClientCameraMode;

	g_iSHLClientCameraMode = READ_BYTE();

	if (iSize >= 5)
	{
		g_flSHLClientCameraPitch =
			SHL_NormalizePitch(SHL_ShortToAngle(READ_SHORT()));

		g_flSHLClientCameraYaw =
			SHL_ShortToAngle(READ_SHORT());

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

	const bool enteredGrabbedFromNonScene =
		(oldMode == SHL_CAMERA_NONE || oldMode == SHL_CAMERA_GROUNDED) &&
		g_iSHLClientCameraMode == SHL_CAMERA_GRABBED;

	if (enteredGrabbedFromNonScene && g_bSHLClientCameraHasAngles)
	{
		float viewangles[3];

		gEngfuncs.GetViewAngles(viewangles);

		viewangles[PITCH] = g_flSHLClientCameraPitch;
		viewangles[YAW] = g_flSHLClientCameraYaw;
		viewangles[ROLL] = 0.0f;

		gEngfuncs.SetViewAngles(viewangles);
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
		// Old grounded/dead-view camera, but only while actually grounded.
		VectorCopy(pparams->simorg, pparams->vieworg);
		VectorAdd(pparams->vieworg, VEC_DEAD_VIEW, pparams->vieworg);
		pparams->viewangles[ROLL] = 80.0f;
		break;

	case SHL_CAMERA_GRABBED:
	case SHL_CAMERA_SCENE:
	case SHL_CAMERA_CLIMAX:
		// Lying scene camera.
		// Low origin like player is on the ground, but no dead sideways roll.
		VectorCopy(pparams->simorg, pparams->vieworg);
		pparams->vieworg[2] += -3.0f;

		pparams->viewangles[ROLL] = 0.0f;
		break;

	case SHL_CAMERA_DEFEAT:
		pparams->viewangles[ROLL] = 0.0f;
		break;

	case SHL_CAMERA_NONE:
	default:
		break;
	}
}

void SHL_ClientCameraClampUserCmdAngles(float* viewangles)
{
	if (viewangles == nullptr)
		return;

	if (!SHL_ClientCameraUsesSceneAngleLimit())
		return;

	if (!g_bSHLClientCameraHasAngles)
		return;

	const float pitchLimit = 35.0f;
	const float yawLimit = 65.0f;

	viewangles[PITCH] =
		SHL_ClampFloat(
			viewangles[PITCH],
			g_flSHLClientCameraPitch - pitchLimit,
			g_flSHLClientCameraPitch + pitchLimit);

	viewangles[YAW] =
		SHL_ClampYawAroundTarget(
			viewangles[YAW],
			g_flSHLClientCameraYaw,
			yawLimit);

	viewangles[ROLL] = 0.0f;

	gEngfuncs.SetViewAngles(viewangles);
}