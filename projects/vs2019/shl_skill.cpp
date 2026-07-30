#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "shl_skill.h"


cvar_t shl_debug =
	{
		"shl_debug",
		"0",
		FCVAR_SERVER};
//----------------------------------------------
cvar_t sk_shl_normal_health =
	{
		"sk_shl_normal_health",
		"50",
		FCVAR_SERVER};

cvar_t sk_shl_normal_punch_damage =
	{
		"sk_shl_normal_punch_damage",
		"15",
		FCVAR_SERVER};

cvar_t sk_shl_normal_punch_range =
	{
		"sk_shl_normal_punch_range",
		"90",
		FCVAR_SERVER};

cvar_t sk_shl_normal_melee_check_range =
	{
		"sk_shl_normal_melee_check_range",
		"85",
		FCVAR_SERVER};

cvar_t sk_shl_normal_melee_dot =
	{
		"sk_shl_normal_melee_dot",
		"0.4",
		FCVAR_SERVER};

cvar_t sk_shl_normal_attack_delay =
	{
		"sk_shl_normal_attack_delay",
		"0.4",
		FCVAR_SERVER};

cvar_t sk_shl_normal_punch_stim =
	{
		"sk_shl_normal_punch_stim",
		"8",
		FCVAR_SERVER};

cvar_t sk_shl_normal_npc_endurance_max =
	{
		"sk_shl_normal_npc_endurance_max",
		"200",
		FCVAR_SERVER};

cvar_t sk_shl_normal_npc_endurance_drain_per_second =
	{
		"sk_shl_normal_npc_endurance_drain_per_second",
		"2",
		FCVAR_SERVER};

cvar_t sk_shl_normal_npc_endurance_drain_per_player_climax =
	{
		"sk_shl_normal_npc_endurance_drain_per_player_climax",
		"50",
		FCVAR_SERVER};

cvar_t sk_shl_normal_npc_climax_duration =
	{
		"sk_shl_normal_npc_climax_duration",
		"3.0",
		FCVAR_SERVER};

cvar_t sk_shl_normal_npc_recovery_duration =
	{
		"sk_shl_normal_npc_recovery_duration",
		"3.0",
		FCVAR_SERVER};
// ----------------------------------------------------
cvar_t sk_shl_player_shl_hp_max =
	{
		"sk_shl_player_shl_hp_max",
		"100",
		FCVAR_SERVER};

cvar_t sk_shl_player_stim_max =
	{
		"sk_shl_player_stim_max",
		"100",
		FCVAR_SERVER};

cvar_t sk_shl_player_climax_hp_damage =
	{
		"sk_shl_player_climax_hp_damage",
		"25",
		FCVAR_SERVER};

cvar_t sk_shl_player_first_climax_reset =
	{
		"sk_shl_player_first_climax_reset",
		"0",
		FCVAR_SERVER};

cvar_t sk_shl_player_later_climax_reset =
	{
		"sk_shl_player_later_climax_reset",
		"25",
		FCVAR_SERVER};

cvar_t sk_shl_player_climax_duration =
	{
		"sk_shl_player_climax_duration",
		"2.0",
		FCVAR_SERVER};

float SHL_NormalNpcEnduranceMax()
{
	return sk_shl_normal_npc_endurance_max.value;
}

float SHL_NormalNpcEnduranceDrainPerSecond()
{
	return sk_shl_normal_npc_endurance_drain_per_second.value;
}

float SHL_NormalNpcEnduranceDrainPerPlayerClimax()
{
	return sk_shl_normal_npc_endurance_drain_per_player_climax.value;
}

float SHL_NormalNpcClimaxDuration()
{
	return sk_shl_normal_npc_climax_duration.value;
}

float SHL_NormalNpcRecoveryDuration()
{
	return sk_shl_normal_npc_recovery_duration.value;
}

cvar_t sk_shl_player_concussion_max = {"sk_shl_player_concussion_max", "100"};
cvar_t sk_shl_player_concussion_recover_rate = {"sk_shl_player_concussion_recover_rate", "8"};
cvar_t sk_shl_player_concussion_grounded_duration = {"sk_shl_player_concussion_grounded_duration", "4"};

cvar_t sk_shl_normal_punch_concussion = {"sk_shl_normal_punch_concussion", "25"};
cvar_t sk_shl_normal_kick_concussion = {"sk_shl_normal_kick_concussion", "35"};
cvar_t sk_shl_normal_grounded_stop_range = {"sk_shl_normal_grounded_stop_range", "96"};

cvar_t sk_shl_normal_grounded_grab_delay = {"sk_shl_normal_grounded_grab_delay", "1.0"};
cvar_t sk_shl_normal_grounded_grab_duration = {"sk_shl_normal_grounded_grab_duration", "5.0"};

void SHL_RegisterSkillCVars()
{
	CVAR_REGISTER(&sk_shl_normal_health);
	CVAR_REGISTER(&sk_shl_normal_punch_damage);
	CVAR_REGISTER(&sk_shl_normal_punch_range);
	CVAR_REGISTER(&sk_shl_normal_melee_check_range);
	CVAR_REGISTER(&sk_shl_normal_melee_dot);
	CVAR_REGISTER(&shl_debug);
	CVAR_REGISTER(&sk_shl_normal_attack_delay);
	CVAR_REGISTER(&sk_shl_normal_punch_stim);
	CVAR_REGISTER(&sk_shl_normal_npc_endurance_max);
	CVAR_REGISTER(&sk_shl_normal_npc_endurance_drain_per_second);
	CVAR_REGISTER(&sk_shl_normal_npc_endurance_drain_per_player_climax);
	CVAR_REGISTER(&sk_shl_normal_npc_climax_duration);
	CVAR_REGISTER(&sk_shl_normal_npc_recovery_duration);
	CVAR_REGISTER(&sk_shl_normal_punch_concussion);
	CVAR_REGISTER(&sk_shl_normal_kick_concussion);
	CVAR_REGISTER(&sk_shl_normal_grounded_stop_range);
	CVAR_REGISTER(&sk_shl_normal_grounded_grab_delay);
	CVAR_REGISTER(&sk_shl_normal_grounded_grab_duration);

	CVAR_REGISTER(&sk_shl_player_shl_hp_max);
	CVAR_REGISTER(&sk_shl_player_stim_max);
	CVAR_REGISTER(&sk_shl_player_climax_hp_damage);
	CVAR_REGISTER(&sk_shl_player_first_climax_reset);
	CVAR_REGISTER(&sk_shl_player_later_climax_reset);
	CVAR_REGISTER(&sk_shl_player_climax_duration);

	CVAR_REGISTER(&sk_shl_player_concussion_max);
	CVAR_REGISTER(&sk_shl_player_concussion_recover_rate);
	CVAR_REGISTER(&sk_shl_player_concussion_grounded_duration);

	ALERT(at_console, "SHL: skill CVars registered\n");
}