#ifndef SHL_SCENE_EDITOR_H
#define SHL_SCENE_EDITOR_H

void SHL_SceneEditorThink();

bool SHL_SceneEditorClientCommand(edict_t* pPlayer, const char* pszCommand);
bool SHL_SceneEditorIsEnabled(edict_t* pPlayer);

#endif