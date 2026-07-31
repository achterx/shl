#include "extdll.h"
#include "util.h"
#include "cbase.h"

#include "shl_camera.h"
#include "../../dlls/UserMessages.h"

static int SHL_AngleToShort(float angle)
{
	while (angle < 0.0f)
		angle += 360.0f;

	while (angle >= 360.0f)
		angle -= 360.0f;

	return (int)(angle * 65536.0f / 360.0f) & 65535;
}

void SHL_SendCameraModeAngles(edict_t* pPlayer, int mode, float pitch, float yaw)
{
	if (pPlayer == nullptr)
		return;

	if (gmsgSHLCamera <= 0)
		return;

	if (mode < SHL_CAMERA_NONE)
		mode = SHL_CAMERA_NONE;

	if (mode > SHL_CAMERA_DEFEAT)
		mode = SHL_CAMERA_DEFEAT;

	MESSAGE_BEGIN(MSG_ONE, gmsgSHLCamera, NULL, pPlayer);
	WRITE_BYTE(mode);
	WRITE_SHORT(SHL_AngleToShort(pitch));
	WRITE_SHORT(SHL_AngleToShort(yaw));
	MESSAGE_END();
}

void SHL_SendCameraMode(edict_t* pPlayer, int mode)
{
	SHL_SendCameraModeAngles(pPlayer, mode, 0.0f, 0.0f);
}