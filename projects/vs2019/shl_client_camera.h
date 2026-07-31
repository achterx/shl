#ifndef SHL_CLIENT_CAMERA_H
#define SHL_CLIENT_CAMERA_H

void SHL_ClientCameraInit();
void SHL_ClientCameraVidInit();
void SHL_ClientCameraMessage(const char* pszName, int iSize, void* pbuf);

int SHL_ClientCameraMode();
bool SHL_ClientCameraActive();

void SHL_ClientCameraApplyRefdef(struct ref_params_s* pparams);

#endif