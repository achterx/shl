#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "shl_defs.h"

const char* SHL_PlayerStateName(int state)
{
	switch (state)
	{
	case SHL_PLAYERSTATE_NORMAL:
		return "NORMAL";

	case SHL_PLAYERSTATE_STIMULATED:
		return "STIMULATED";

	case SHL_PLAYERSTATE_GROUNDED:
		return "GROUNDED";

	case SHL_PLAYERSTATE_TRAMPLED:
		return "TRAMPLED";

	case SHL_PLAYERSTATE_GRABBED:
		return "GRABBED";

	case SHL_PLAYERSTATE_ACTIVE_SCENE:
		return "ACTIVE_SCENE";

	case SHL_PLAYERSTATE_CLIMAX_LOCKED:
		return "CLIMAX_LOCKED";

	case SHL_PLAYERSTATE_DEFEAT_MENU:
		return "DEFEAT_MENU";

	case SHL_PLAYERSTATE_DEFEAT_SCENE:
		return "DEFEAT_SCENE";

	case SHL_PLAYERSTATE_RECOVERY:
		return "RECOVERY";

	default:
		return "UNKNOWN";
	}
}