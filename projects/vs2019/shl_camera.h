#ifndef SHL_CAMERA_H
#define SHL_CAMERA_H

enum SHLCameraMode
{
	SHL_CAMERA_NONE = 0,
	SHL_CAMERA_GROUNDED,
	SHL_CAMERA_GRABBED,
	SHL_CAMERA_SCENE,
	SHL_CAMERA_CLIMAX,
	SHL_CAMERA_DEFEAT
};

void SHL_SendCameraMode(edict_t* pPlayer, int mode);
void SHL_SendCameraModeAngles(edict_t* pPlayer, int mode, float pitch, float yaw);

#endif