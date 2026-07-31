#ifndef SHL_SKILL_H
#define SHL_SKILL_H

#include "extdll.h"

void SHL_RegisterSkillCVars();

// normal skills
extern cvar_t sk_shl_normal_health;
extern cvar_t sk_shl_normal_punch_damage;
extern cvar_t sk_shl_normal_punch_range;
extern cvar_t sk_shl_normal_melee_check_range;
extern cvar_t sk_shl_normal_melee_dot;
extern cvar_t shl_debug;
extern cvar_t sk_shl_normal_attack_delay;
extern cvar_t sk_shl_normal_punch_stim;
extern cvar_t sk_shl_normal_punch_concussion;
extern cvar_t sk_shl_normal_kick_concussion;

extern cvar_t sk_shl_normal_grounded_grab_delay;
extern cvar_t sk_shl_normal_grounded_grab_duration;
inline float SHL_NormalGroundedGrabDelay() { return sk_shl_normal_grounded_grab_delay.value; }
inline float SHL_NormalGroundedGrabDuration() { return sk_shl_normal_grounded_grab_duration.value; }

float SHL_NormalNpcEnduranceMax();
float SHL_NormalNpcEnduranceDrainPerSecond();
float SHL_NormalNpcEnduranceDrainPerPlayerClimax();
float SHL_NormalNpcClimaxDuration();
float SHL_NormalEscapeRecoveryDuration();
float SHL_NormalRegrabCooldown();

// player skills
extern cvar_t sk_shl_player_shl_hp_max;
extern cvar_t sk_shl_player_stim_max;
extern cvar_t sk_shl_player_climax_hp_damage;
extern cvar_t sk_shl_player_first_climax_reset;
extern cvar_t sk_shl_player_later_climax_reset;
extern cvar_t sk_shl_player_climax_duration;

extern cvar_t sk_shl_player_concussion_max;
extern cvar_t sk_shl_player_concussion_recover_rate;
extern cvar_t sk_shl_player_concussion_grounded_duration;

inline float SHL_PlayerConcussionMax() { return sk_shl_player_concussion_max.value; }
inline float SHL_PlayerConcussionRecoverRate() { return sk_shl_player_concussion_recover_rate.value; }
inline float SHL_PlayerConcussionGroundedDuration() { return sk_shl_player_concussion_grounded_duration.value; }

inline float SHL_NormalPunchConcussion() { return sk_shl_normal_punch_concussion.value; }
inline float SHL_NormalKickConcussion() { return sk_shl_normal_kick_concussion.value; }
extern cvar_t sk_shl_normal_grounded_stop_range;
inline float SHL_NormalGroundedStopRange() { return sk_shl_normal_grounded_stop_range.value; }




inline float SHL_NormalHealth()
{
	return sk_shl_normal_health.value;
}

inline float SHL_NormalPunchDamage()
{
	return sk_shl_normal_punch_damage.value;
}

inline float SHL_NormalPunchRange()
{
	return sk_shl_normal_punch_range.value;
}

inline float SHL_NormalMeleeCheckRange()
{
	return sk_shl_normal_melee_check_range.value;
}

inline float SHL_NormalMeleeDot()
{
	return sk_shl_normal_melee_dot.value;
}

inline bool SHL_DebugEnabled()
{
	return shl_debug.value > 0.0f;
}

inline float SHL_NormalAttackDelay()
{
	return sk_shl_normal_attack_delay.value;
}

inline float SHL_NormalPunchStim()
{
	return sk_shl_normal_punch_stim.value;
}
// ---------------------------------------------------

inline float SHL_PlayerSHLHPMax()
{
	return sk_shl_player_shl_hp_max.value;
}

inline float SHL_PlayerStimMax()
{
	return sk_shl_player_stim_max.value;
}

inline float SHL_PlayerClimaxHPDamage()
{
	return sk_shl_player_climax_hp_damage.value;
}

inline float SHL_PlayerFirstClimaxReset()
{
	return sk_shl_player_first_climax_reset.value;
}

inline float SHL_PlayerLaterClimaxReset()
{
	return sk_shl_player_later_climax_reset.value;
}

inline float SHL_PlayerClimaxDuration()
{
	return sk_shl_player_climax_duration.value;
}
#endif // SHL_SKILL_H