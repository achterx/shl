#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "effects.h"

#include "shl_skill.h"
#include "shl_scene.h"
#include "shl_scene_actor.h"

class CSHLSceneActor : public CBaseAnimating
{
public:
	void Spawn() override;
	void Precache() override;
	void Think() override;

	void Setup(edict_t* pPlayer, const char* pszModelName);

private:
	EHANDLE m_hPlayer;
	string_t m_iszModelName;
};

LINK_ENTITY_TO_CLASS(shl_scene_actor, CSHLSceneActor);

static float SHL_NormalizeActorYaw360(float yaw)
{
	while (yaw >= 360.0f)
		yaw -= 360.0f;

	while (yaw < 0.0f)
		yaw += 360.0f;

	return yaw;
}

static void SHL_ResetSceneActorControllers(CBaseAnimating* pActor)
{
	if (pActor == nullptr)
		return;

	pActor->pev->controller[0] = 127;
	pActor->pev->controller[1] = 127;
	pActor->pev->controller[2] = 127;
	pActor->pev->controller[3] = 127;

	pActor->pev->blending[0] = 0;
	pActor->pev->blending[1] = 0;
}

void CSHLSceneActor::Precache()
{
	const char* pszModelName = STRING(m_iszModelName);

	if (pszModelName != nullptr && pszModelName[0] != '\0')
	{
		PRECACHE_MODEL((char*)pszModelName);
	}
}

void CSHLSceneActor::Spawn()
{
	Precache();

	pev->movetype = MOVETYPE_NOCLIP;
	pev->solid = SOLID_NOT;
	pev->takedamage = DAMAGE_NO;
	pev->health = 1;
	pev->effects = 0;

	const char* pszModelName = STRING(m_iszModelName);

	if (pszModelName != nullptr && pszModelName[0] != '\0')
	{
		SET_MODEL(edict(), pszModelName);
	}

	pev->sequence = 0;
	pev->frame = 0.0f;
	pev->framerate = 1.0f;
	pev->velocity = g_vecZero;
	pev->avelocity = g_vecZero;

	ResetSequenceInfo();
	SHL_ResetSceneActorControllers(this);

	pev->nextthink = gpGlobals->time + 0.01f;
}

void CSHLSceneActor::Setup(edict_t* pPlayer, const char* pszModelName)
{
	m_hPlayer = CBaseEntity::Instance(pPlayer);

	if (pszModelName != nullptr)
	{
		m_iszModelName = MAKE_STRING(pszModelName);
	}
	else
	{
		m_iszModelName = MAKE_STRING("");
	}
}

void CSHLSceneActor::Think()
{
	CBaseEntity* pPlayerEntity = (CBaseEntity*)m_hPlayer;

	if (pPlayerEntity == nullptr || pPlayerEntity->edict() == nullptr)
	{
		UTIL_Remove(this);
		return;
	}

	entvars_t* pPlayerVars = pPlayerEntity->pev;

	Vector sceneOrigin = g_vecZero;
	float sceneYaw = 0.0f;

	if (SHL_GetPlayerSceneAnchor(pPlayerEntity->edict(), sceneOrigin, sceneYaw))
	{
		pev->origin = sceneOrigin;

		pev->angles.x = 0.0f;
		pev->angles.y = SHL_NormalizeActorYaw360(sceneYaw);
		pev->angles.z = 0.0f;
	}
	else
	{
		pev->origin = pPlayerVars->origin;

		pev->angles.x = 0.0f;
		pev->angles.y = pPlayerVars->angles.y;
		pev->angles.z = 0.0f;
	}

	pev->velocity = g_vecZero;
	pev->avelocity = g_vecZero;

	SHL_ResetSceneActorControllers(this);

	StudioFrameAdvance();

	pev->nextthink = gpGlobals->time + 0.01f;
}

CBaseEntity* SHL_CreatePlayerSceneActor(edict_t* pPlayer, const char* pszModelName)
{
	if (pPlayer == nullptr)
		return nullptr;

	if (pszModelName == nullptr || pszModelName[0] == '\0')
		return nullptr;

	edict_t* pEdict = CREATE_NAMED_ENTITY(MAKE_STRING("shl_scene_actor"));

	if (FNullEnt(pEdict))
		return nullptr;

	CSHLSceneActor* pActor = GetClassPtr((CSHLSceneActor*)VARS(pEdict));

	if (pActor == nullptr)
		return nullptr;

	pActor->Setup(pPlayer, pszModelName);
	pActor->Spawn();

	if (SHL_DebugEnabled())
	{
		ALERT(at_console, "SHL: player scene actor spawned model=%s\n", pszModelName);
	}

	return pActor;
}

void SHL_RemoveSceneActor(CBaseEntity* pActor)
{
	if (pActor == nullptr)
		return;

	if (SHL_DebugEnabled())
	{
		ALERT(at_console, "SHL: player scene actor removed\n");
	}

	UTIL_Remove(pActor);
}