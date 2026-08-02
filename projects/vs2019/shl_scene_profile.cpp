#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"

#include "shl_scene.h"
#include "shl_scene_profile.h"

static const shl_scene_profile_t g_SHLSceneProfileNone =
	{
		SHL_SCENE_NONE,
		"NONE",
		0.0f,

		false,
		false,
		false,
		false,

		0.0f,

		false,

		nullptr,
		nullptr,
		nullptr,

		nullptr,
		nullptr,
		nullptr,
		nullptr,

		nullptr,
		nullptr,
		0.0f,
		0.0f,

		// Player anchor offsets.
		0.0f,
		0.0f,
		0.0f,
		0.0f,

		// Monster anchor offsets.
		0.0f,
		0.0f,
		0.0f,
		0.0f,

		0.0f,

		true,

		false,
		0.0f,
		0.0f,

		0.0f};

static const shl_scene_profile_t g_SHLSceneProfileDebug =
	{
		SHL_SCENE_DEBUG,
		"DEBUG",
		5.0f,

		true,
		true,
		false,
		true,

		0.0f,

		false,

		nullptr,
		nullptr,
		nullptr,

		nullptr,
		nullptr,
		nullptr,
		nullptr,

		nullptr,
		nullptr,
		0.0f,
		0.0f,

		// Player anchor offsets.
		0.0f,
		0.0f,
		0.0f,
		0.0f,

		// Monster anchor offsets.
		0.0f,
		0.0f,
		0.0f,
		0.0f,

		0.0f,

		true,

		false,
		0.0f,
		0.0f,

		0.0f};

static const shl_scene_profile_t g_SHLSceneProfileGroundedGrab =
	{
		SHL_SCENE_GROUNDED_GRAB,
		"GROUNDED_GRAB",
		3.0f,

		true,
		true,
		true,
		true,

		6.0f,

		true,

		"shl_grab_start",
		"shl_grab_loop",
		"shl_climax",

		"shl_grab_start",
		"shl_grab_loop",
		"shl_grab_climax",
		"shl_npc_climax",

		"shl_escape_knockdown",
		"shl_recovery_getup",
		4.31f,
		1.60f,

		// Player anchor offsets.
		0.0f,
		0.0f,
		0.0f,
		0.0f,

		// Monster anchor offsets.
		8.0f,
		0.0f,
		0.0f,
		180.0f,

		0.8f,

		// GROUNDED_GRAB should not end just because the old timer expires.
		false,

		// Escape settings.
		true,
		160.0f,
		6.0f,

		// After player climax animation, wait this long before returning to grab loop.
		1.0f};

const shl_scene_profile_t* SHL_GetSceneProfile(int sceneType)
{
	switch (sceneType)
	{
	case SHL_SCENE_DEBUG:
		return &g_SHLSceneProfileDebug;

	case SHL_SCENE_GROUNDED_GRAB:
		return &g_SHLSceneProfileGroundedGrab;

	case SHL_SCENE_NONE:
	default:
		break;
	}

	return &g_SHLSceneProfileNone;
}