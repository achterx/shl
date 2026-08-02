#ifndef SHL_CLIENT_SCENE_EDITOR_H
#define SHL_CLIENT_SCENE_EDITOR_H

struct ref_params_s;
struct usercmd_s;

void SHL_ClientSceneEditorInit();
void SHL_ClientSceneEditorVidInit();

void SHL_ClientSceneEditorMessage(const char* pszName, int iSize, void* pbuf);

bool SHL_ClientSceneEditorEnabled();

void SHL_ClientSceneEditorMove(struct usercmd_s* cmd);
void SHL_ClientSceneEditorApplyRefdef(struct ref_params_s* pparams);
void SHL_ClientSceneEditorMouse(struct usercmd_s* cmd);
void SHL_ClientSceneEditorCursorCommand();
bool SHL_ClientSceneEditorCursorEnabled();

#endif