#ifndef SHL_SCENE_PROFILE_H
#define SHL_SCENE_PROFILE_H

#include "shl_scene.h"

struct shl_scene_profile_t
{
	int sceneType;

	const char* debugName;

	float defaultDuration;

	bool blocksInput;
	bool hidesWeapon;
	bool usesGroundedCamera;

	bool restorePlayerToNormalOnEnd;

	float stimulationPerSecond;

	bool supportsSceneAnimation;

	const char* playerStartSequence;
	const char* playerLoopSequence;
	const char* playerClimaxSequence;

	const char* monsterStartSequence;
	const char* monsterLoopSequence;
	const char* monsterClimaxSequence;
	const char* monsterNpcClimaxSequence;

	float startSequenceDuration;

	bool endsByTimer;

	bool supportsEscape;
	float escapeRequired;
	float escapeGainPerMash;

	float postClimaxLoopDelay;
};

const shl_scene_profile_t* SHL_GetSceneProfile(int sceneType);

#endif // SHL_SCENE_PROFILE_H