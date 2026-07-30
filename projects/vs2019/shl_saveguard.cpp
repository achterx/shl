#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "shl_skill.h"
#include "shl_saveguard.h"
#include "shl_player_state.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <io.h>
#endif

#define SHL_MAX_SAVEGUARD_STATES 33

static int g_SHLUnsafeSaveFlags[SHL_MAX_SAVEGUARD_STATES];

static float g_flNextSavePurgeTime = 0.0f;
static bool g_fSavePurgeActive = false;
static time_t g_SHLUnsafeStartTime = 0;

static int SHL_GetPlayerIndexForSaveGuard(edict_t* pEntity)
{
	if (pEntity == nullptr)
		return 0;

	const int index = ENTINDEX(pEntity);

	if (index <= 0 || index >= SHL_MAX_SAVEGUARD_STATES)
		return 0;

	return index;
}

static bool SHL_AnyPlayerUnsafe()
{
	for (int i = 1; i <= gpGlobals->maxClients && i < SHL_MAX_SAVEGUARD_STATES; ++i)
	{
		if (g_SHLUnsafeSaveFlags[i] != SHL_UNSAFE_NONE)
			return true;

		CBaseEntity* pPlayer = UTIL_PlayerByIndex(i);

		if (pPlayer != nullptr && SHL_IsPlayerInUnsafeState(pPlayer->edict()))
			return true;
	}

	return false;
}

void SHL_InitSaveGuardForPlayer(edict_t* pEntity)
{
	const int index = SHL_GetPlayerIndexForSaveGuard(pEntity);

	if (index <= 0)
		return;

	g_SHLUnsafeSaveFlags[index] = SHL_UNSAFE_NONE;

	if (!SHL_AnyPlayerUnsafe())
	{
		g_fSavePurgeActive = false;
		g_SHLUnsafeStartTime = 0;
	}

	if (SHL_DebugEnabled())
	{
		ALERT(at_console, "SHL: save guard initialized for player %d\n", index);
	}
}

void SHL_ClearUnsafeSaveFlags(edict_t* pEntity)
{
	const int index = SHL_GetPlayerIndexForSaveGuard(pEntity);

	if (index <= 0)
		return;

	g_SHLUnsafeSaveFlags[index] = SHL_UNSAFE_NONE;

	if (!SHL_AnyPlayerUnsafe())
	{
		g_fSavePurgeActive = false;
		g_SHLUnsafeStartTime = 0;
	}

	if (SHL_DebugEnabled())
	{
		ALERT(at_console, "SHL: unsafe save flags cleared\n");
	}
}

void SHL_SetUnsafeSaveFlag(edict_t* pEntity, int flag)
{
	const int index = SHL_GetPlayerIndexForSaveGuard(pEntity);

	if (index <= 0)
		return;

	g_SHLUnsafeSaveFlags[index] |= flag;

	g_fSavePurgeActive = true;
	g_flNextSavePurgeTime = 0.0f;

	if (g_SHLUnsafeStartTime == 0)
	{
		// Give ourselves a small grace window so a save created immediately after
		// setting unsafe is still considered unsafe.
		g_SHLUnsafeStartTime = time(nullptr) - 3;
	}

	if (SHL_DebugEnabled())
	{
		ALERT(
			at_console,
			"SHL: set unsafe save flag %d, now %d, purge start time %lld\n",
			flag,
			g_SHLUnsafeSaveFlags[index],
			(long long)g_SHLUnsafeStartTime);
	}
}

void SHL_ClearUnsafeSaveFlag(edict_t* pEntity, int flag)
{
	const int index = SHL_GetPlayerIndexForSaveGuard(pEntity);

	if (index <= 0)
		return;

	g_SHLUnsafeSaveFlags[index] &= ~flag;

	if (!SHL_AnyPlayerUnsafe())
	{
		g_fSavePurgeActive = false;
		g_SHLUnsafeStartTime = 0;
	}

	if (SHL_DebugEnabled())
	{
		ALERT(at_console, "SHL: cleared unsafe save flag %d, now %d\n", flag, g_SHLUnsafeSaveFlags[index]);
	}
}

bool SHL_HasUnsafeSaveFlag(edict_t* pEntity, int flag)
{
	const int index = SHL_GetPlayerIndexForSaveGuard(pEntity);

	if (index <= 0)
		return false;

	return (g_SHLUnsafeSaveFlags[index] & flag) != 0;
}

bool SHL_CanSaveNow(edict_t* pEntity)
{
	const int index = SHL_GetPlayerIndexForSaveGuard(pEntity);

	if (index <= 0)
		return true;

	if (g_SHLUnsafeSaveFlags[index] != SHL_UNSAFE_NONE)
		return false;

	if (SHL_IsPlayerInUnsafeState(pEntity))
		return false;

	return true;
}

int SHL_GetUnsafeSaveFlags(edict_t* pEntity)
{
	const int index = SHL_GetPlayerIndexForSaveGuard(pEntity);

	if (index <= 0)
		return SHL_UNSAFE_NONE;

	return g_SHLUnsafeSaveFlags[index];
}

void SHL_DebugPrintSaveGuard(edict_t* pEntity)
{
	const int flags = SHL_GetUnsafeSaveFlags(pEntity);
	const int state = SHL_GetPlayerStateId(pEntity);
	const bool stateUnsafe = SHL_IsPlayerInUnsafeState(pEntity);

	ALERT(
		at_console,
		"SHL SAVEGUARD: flags=%d state=%s stateUnsafe=%d canSave=%d purgeActive=%d unsafeStart=%lld\n",
		flags,
		SHL_PlayerStateName(state),
		stateUnsafe ? 1 : 0,
		SHL_CanSaveNow(pEntity) ? 1 : 0,
		g_fSavePurgeActive ? 1 : 0,
		(long long)g_SHLUnsafeStartTime);
}

static bool SHL_IsSaveExtension(const char* fileName)
{
	if (fileName == nullptr)
		return false;

	const char* dot = strrchr(fileName, '.');

	if (dot == nullptr)
		return false;

	if (!stricmp(dot, ".sav"))
		return true;

	if (!stricmp(dot, ".hl1"))
		return true;

	return false;
}

void SHL_OnPlayerLoadCleanup(edict_t* pEntity)
{
	if (pEntity == nullptr)
		return;

	/*
		Temporary load safety cleanup.

		Later this will also call:
		SHL_AbortActiveScene(pEntity);
		SHL_ClearSceneSlots();
		SHL_RestorePlayerControl(pEntity);
		SHL_RestorePlayerCamera(pEntity);
		SHL_ClearSceneHUD(pEntity);
	*/

	SHL_ClearUnsafeSaveFlags(pEntity);

	if (SHL_DebugEnabled())
	{
		ALERT(at_console, "SHL: load cleanup ran for player\n");
	}
}

static void SHL_PurgeSaveFolder(const char* saveFolder)
{
#if defined(_WIN32)
	if (saveFolder == nullptr || saveFolder[0] == '\0')
		return;

	char pattern[512];
	snprintf(pattern, sizeof(pattern), "%s\\*.*", saveFolder);

	_finddata_t data;
	const intptr_t handle = _findfirst(pattern, &data);

	if (handle == -1)
	{
		if (SHL_DebugEnabled())
		{
			ALERT(at_console, "SHL: save purge could not open folder: %s\n", saveFolder);
		}

		return;
	}

	do
	{
		if ((data.attrib & _A_SUBDIR) != 0)
			continue;

		if (!SHL_IsSaveExtension(data.name))
			continue;

		if (g_SHLUnsafeStartTime != 0 && data.time_write < g_SHLUnsafeStartTime)
			continue;

		char fullPath[512];
		snprintf(fullPath, sizeof(fullPath), "%s\\%s", saveFolder, data.name);

		const int removed = remove(fullPath);

		if (SHL_DebugEnabled())
		{
			ALERT(
				at_console,
				"SHL: purge candidate %s time=%lld removed=%d\n",
				fullPath,
				(long long)data.time_write,
				removed == 0 ? 1 : 0);
		}
	} while (_findnext(handle, &data) == 0);

	_findclose(handle);
#endif
}

void SHL_SaveGuardThink()
{
	const bool anyUnsafe = SHL_AnyPlayerUnsafe();

	if (!anyUnsafe)
	{
		g_fSavePurgeActive = false;
		g_SHLUnsafeStartTime = 0;
		return;
	}

	if (!g_fSavePurgeActive)
	{
		g_fSavePurgeActive = true;
		g_flNextSavePurgeTime = 0.0f;
		g_SHLUnsafeStartTime = time(nullptr) - 3;

		if (SHL_DebugEnabled())
		{
			ALERT(at_console, "SHL: save purge activated by unsafe player state\n");
		}
	}

	if (gpGlobals->time < g_flNextSavePurgeTime)
		return;

	g_flNextSavePurgeTime = gpGlobals->time + 0.05f;

	char gameDir[256];
	gameDir[0] = '\0';

	g_engfuncs.pfnGetGameDir(gameDir);

	if (gameDir[0] == '\0')
	{
		if (SHL_DebugEnabled())
		{
			ALERT(at_console, "SHL: pfnGetGameDir returned empty path\n");
		}

		return;
	}

	char saveFolder[512];
	snprintf(saveFolder, sizeof(saveFolder), "%s\\SAVE", gameDir);

	///(SHL_DebugEnabled())
	//
	//LERT(at_console, "SHL: scanning save folder: %s\n", saveFolder);
	//

	SHL_PurgeSaveFolder(saveFolder);
}