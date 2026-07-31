#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "schedule.h"

#include "shl_skill.h"
#include "shl_player_state.h"
#include "shl_concussion.h"
#include "shl_scene.h"
#include "shl_npc_recovery.h"

#define SHL_NORMAL_AE_PUNCH_HIT 1

class CMonsterSHLNormal : public CBaseMonster
{
public:
	void Spawn() override;
	void Precache() override;
	int Classify() override;
	void SetYawSpeed() override;

	void PrescheduleThink() override;
	void HandleAnimEvent(MonsterEvent_t* pEvent) override;
	bool CheckMeleeAttack1(float flDot, float flDist) override;
	Schedule_t* GetSchedule() override;

private:
	CBaseEntity* GetEnemyEntity();
	bool IsEnemyGroundedPlayer();
	float DistanceToEnemy2D();
	void FaceEnemyFlat();
	void TryStartGroundedGrab();
	bool IsEnemyInNoMeleeState();

private:
	float m_flNextPunchTime;
	float m_flNextGroundedGrabTryTime;
};

LINK_ENTITY_TO_CLASS(monster_shl_normal, CMonsterSHLNormal);

void CMonsterSHLNormal::Precache()
{
	PRECACHE_MODEL("models/shl/monster_shl_normal.mdl");
	PRECACHE_MODEL("models/shl/player_grounded_scene.mdl");

	PRECACHE_SOUND("zombie/claw_strike1.wav");
	PRECACHE_SOUND("zombie/claw_miss1.wav");
}

void CMonsterSHLNormal::Spawn()
{
	Precache();

	SET_MODEL(ENT(pev), "models/shl/monster_shl_normal.mdl");
	UTIL_SetSize(pev, VEC_HUMAN_HULL_MIN, VEC_HUMAN_HULL_MAX);

	pev->solid = SOLID_SLIDEBOX;
	pev->movetype = MOVETYPE_STEP;

	m_bloodColor = BLOOD_COLOR_RED;
	pev->health = SHL_NormalHealth();

	m_flFieldOfView = 0.5f;
	m_MonsterState = MONSTERSTATE_NONE;

	m_afCapability = bits_CAP_DOORS_GROUP;

	m_flNextPunchTime = 0.0f;
	m_flNextGroundedGrabTryTime = 0.0f;

	MonsterInit();

	ALERT(at_console, "SHL: monster_shl_normal spawned, health %.1f\n", pev->health);
}

int CMonsterSHLNormal::Classify()
{
	return CLASS_ALIEN_MONSTER;
}

void CMonsterSHLNormal::SetYawSpeed()
{
	pev->yaw_speed = 240;
}

CBaseEntity* CMonsterSHLNormal::GetEnemyEntity()
{
	return (CBaseEntity*)m_hEnemy;
}

bool CMonsterSHLNormal::IsEnemyGroundedPlayer()
{
	CBaseEntity* pEnemy = GetEnemyEntity();

	if (pEnemy == nullptr)
		return false;

	if (!pEnemy->IsPlayer())
		return false;

	return SHL_GetPlayerStateId(pEnemy->edict()) == SHL_PLAYERSTATE_GROUNDED;
}

bool CMonsterSHLNormal::IsEnemyInNoMeleeState()
{
	CBaseEntity* pEnemy = GetEnemyEntity();

	if (pEnemy == nullptr)
		return false;

	if (!pEnemy->IsPlayer())
		return false;

	const int state = SHL_GetPlayerStateId(pEnemy->edict());

	switch (state)
	{
	case SHL_PLAYERSTATE_GROUNDED:
	case SHL_PLAYERSTATE_GRABBED:
	case SHL_PLAYERSTATE_ACTIVE_SCENE:
	case SHL_PLAYERSTATE_CLIMAX_LOCKED:
	case SHL_PLAYERSTATE_DEFEAT_MENU:
	case SHL_PLAYERSTATE_DEFEAT_SCENE:
	case SHL_PLAYERSTATE_RECOVERY:
		return true;

	default:
		break;
	}

	return false;
}

float CMonsterSHLNormal::DistanceToEnemy2D()
{
	CBaseEntity* pEnemy = GetEnemyEntity();

	if (pEnemy == nullptr)
		return 999999.0f;

	Vector delta = pEnemy->pev->origin - pev->origin;
	delta.z = 0.0f;

	return delta.Length();
}

void CMonsterSHLNormal::FaceEnemyFlat()
{
	CBaseEntity* pEnemy = GetEnemyEntity();

	if (pEnemy == nullptr)
		return;

	Vector delta = pEnemy->pev->origin - pev->origin;
	delta.z = 0.0f;

	if (delta.Length() <= 0.1f)
		return;

	pev->ideal_yaw = UTIL_VecToYaw(delta);
	ChangeYaw(pev->yaw_speed);
}

void CMonsterSHLNormal::TryStartGroundedGrab()
{
	CBaseEntity* pEnemy = GetEnemyEntity();

	if (pEnemy == nullptr || !pEnemy->IsPlayer())
		return;

	if (SHL_GetPlayerStateId(pEnemy->edict()) != SHL_PLAYERSTATE_GROUNDED)
		return;

	if (DistanceToEnemy2D() > SHL_NormalGroundedStopRange())
		return;

	if (gpGlobals->time < m_flNextGroundedGrabTryTime)
		return;

	m_flNextGroundedGrabTryTime = gpGlobals->time + SHL_NormalGroundedGrabDelay();

	FaceEnemyFlat();

	if (SHL_DebugEnabled())
	{
		ALERT(at_console, "SHL Normal: trying grounded grab scene\n");
	}

	SHL_TryStartGroundedGrabScene(
		this,
		pEnemy->edict(),
		SHL_NormalGroundedGrabDuration());
}

void CMonsterSHLNormal::PrescheduleThink()
{
	if (SHL_ApplyMonsterSceneRecovery(this))
	{
		m_flNextPunchTime = gpGlobals->time + 0.5f;
		ClearConditions(bits_COND_CAN_MELEE_ATTACK1);
		return;
	}
	
	
	if (SHL_IsMonsterInSceneNpcRecovery(this))
	{
		pev->velocity = g_vecZero;
		pev->avelocity = g_vecZero;
		pev->framerate = 0.0f;
		m_flNextPunchTime = gpGlobals->time + 0.5f;
		return;
	}
	
	if (SHL_IsMonsterSceneOwner(this))
	{
		pev->velocity = g_vecZero;
		pev->avelocity = g_vecZero;

		// Do not request IDLE_STAND while scene owns animation.
		// Returning base schedule can briefly force ACT_IDLE/stand.
	}

	CBaseMonster::PrescheduleThink();

	CBaseEntity* pEnemy = GetEnemyEntity();

	if (pEnemy == nullptr)
	{
		CBaseEntity* pPlayer = UTIL_PlayerByIndex(1);

		if (pPlayer != nullptr && pPlayer->IsAlive())
		{
			m_hEnemy = pPlayer;
			SetConditions(bits_COND_NEW_ENEMY);

			if (SHL_DebugEnabled())
			{
				ALERT(at_console, "SHL: monster_shl_normal acquired player enemy\n");
			}
		}
	}

	if (IsEnemyGroundedPlayer())
	{
		const float dist = DistanceToEnemy2D();

		if (dist <= SHL_NormalGroundedStopRange())
		{
			pev->velocity = g_vecZero;
			pev->avelocity = g_vecZero;

			FaceEnemyFlat();

			m_movementActivity = ACT_IDLE;

			TryStartGroundedGrab();
		}
	}
}

bool CMonsterSHLNormal::CheckMeleeAttack1(float flDot, float flDist)
{
	if (SHL_IsMonsterInSceneNpcRecovery(this))
		return false;
	
	if (SHL_IsNpcRecovering(this))
		return false;
	
	if (SHL_IsMonsterSceneOwner(this))
		return false;

	if (IsEnemyInNoMeleeState())
		return false;

	if (gpGlobals->time < m_flNextPunchTime)
		return false;

	if (flDist <= SHL_NormalMeleeCheckRange() && flDot >= SHL_NormalMeleeDot())
	{
		m_flNextPunchTime = gpGlobals->time + SHL_NormalAttackDelay();
		return true;
	}

	return false;
}

Schedule_t* CMonsterSHLNormal::GetSchedule()
{
	if (SHL_IsMonsterSceneOwner(this))
	{
		pev->velocity = g_vecZero;
		pev->avelocity = g_vecZero;

		return CBaseMonster::GetScheduleOfType(SCHED_IDLE_STAND);
	}

	CBaseEntity* pEnemy = GetEnemyEntity();

	if (pEnemy != nullptr)
	{
		if (pEnemy->IsPlayer())
		{
			const int state = SHL_GetPlayerStateId(pEnemy->edict());

			if (state == SHL_PLAYERSTATE_GROUNDED ||
				state == SHL_PLAYERSTATE_GRABBED ||
				state == SHL_PLAYERSTATE_ACTIVE_SCENE ||
				state == SHL_PLAYERSTATE_CLIMAX_LOCKED)
			{
				const float dist = DistanceToEnemy2D();

				if (dist <= SHL_NormalGroundedStopRange())
				{
					pev->velocity = g_vecZero;
					pev->avelocity = g_vecZero;

					FaceEnemyFlat();

					if (state == SHL_PLAYERSTATE_GROUNDED)
					{
						TryStartGroundedGrab();
					}

					if (SHL_DebugEnabled())
					{
						ALERT(at_console, "SHL Normal: player locked/scene state, waiting near player\n");
					}

					return CBaseMonster::GetScheduleOfType(SCHED_IDLE_STAND);
				}

				if (SHL_DebugEnabled())
				{
					ALERT(at_console, "SHL Normal: player locked/grounded, moving closer for grab\n");
				}

				return CBaseMonster::GetScheduleOfType(SCHED_CHASE_ENEMY);
			}
		}

		if (HasConditions(bits_COND_CAN_MELEE_ATTACK1))
			return CBaseMonster::GetScheduleOfType(SCHED_MELEE_ATTACK1);

		return CBaseMonster::GetScheduleOfType(SCHED_CHASE_ENEMY);
	}

	return CBaseMonster::GetSchedule();
}

void CMonsterSHLNormal::HandleAnimEvent(MonsterEvent_t* pEvent)
{
	if (SHL_IsMonsterInSceneNpcRecovery(this))
		return;
	
	switch (pEvent->event)
	{
	case SHL_NORMAL_AE_PUNCH_HIT:
	{
		if (SHL_IsMonsterSceneOwner(this))
			break;

		const float flPunchRange = SHL_NormalPunchRange();
		const float flPunchDamage = SHL_NormalPunchDamage();

		CBaseEntity* pHurt = CheckTraceHullAttack(flPunchRange, flPunchDamage, DMG_CLUB);

		if (pHurt)
		{
			EMIT_SOUND_DYN(
				ENT(pev),
				CHAN_WEAPON,
				"zombie/claw_strike1.wav",
				1.0f,
				ATTN_NORM,
				0,
				100);

			if (pHurt->IsPlayer())
			{
				const int hurtState = SHL_GetPlayerStateId(pHurt->edict());

				if (hurtState == SHL_PLAYERSTATE_GROUNDED ||
					hurtState == SHL_PLAYERSTATE_GRABBED ||
					hurtState == SHL_PLAYERSTATE_ACTIVE_SCENE ||
					hurtState == SHL_PLAYERSTATE_CLIMAX_LOCKED ||
					hurtState == SHL_PLAYERSTATE_DEFEAT_MENU ||
					hurtState == SHL_PLAYERSTATE_DEFEAT_SCENE ||
					hurtState == SHL_PLAYERSTATE_RECOVERY)
				{
					if (SHL_DebugEnabled())
					{
						ALERT(at_console, "SHL Normal: punch SHL effects ignored, player is locked/no-melee\n");
					}

					break;
				}

				SHL_AddPlayerStimulation(pHurt->edict(), SHL_NormalPunchStim());
				SHL_AddPlayerConcussion(pHurt->edict(), SHL_NormalPunchConcussion());

				if (SHL_DebugEnabled())
				{
					ALERT(
						at_console,
						"SHL: monster_shl_normal added %.1f stimulation and %.1f concussion to player\n",
						SHL_NormalPunchStim(),
						SHL_NormalPunchConcussion());
				}
			}

			if (SHL_DebugEnabled())
			{
				ALERT(
					at_console,
					"SHL: monster_shl_normal punch hit %s for %.1f damage\n",
					STRING(pHurt->pev->classname),
					flPunchDamage);
			}
		}
		else
		{
			EMIT_SOUND_DYN(
				ENT(pev),
				CHAN_WEAPON,
				"zombie/claw_miss1.wav",
				1.0f,
				ATTN_NORM,
				0,
				100);

			if (SHL_DebugEnabled())
			{
				ALERT(at_console, "SHL: monster_shl_normal punch missed\n");
			}
		}

		break;
	}

	default:
		CBaseMonster::HandleAnimEvent(pEvent);
		break;
	}
}