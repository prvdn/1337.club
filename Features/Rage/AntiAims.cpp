
#include "../../Hooks/hooks.h"
#include "../Features.h"

bool CanExploit() {
	return (
		((g_Binds[bind_double_tap].active || g_Binds[bind_hide_shots].active) && csgo->skip_ticks >= 16)
		/*|| (g_Binds[bind_hide_shots].active && csgo->skip_ticks >= 8)*/)
		&& !csgo->need_to_recharge;
}

void CAntiAim::is_lby_update()
{
	CUserCmd* cmd = csgo->cmd;
	float cur_time = csgo->curtime;
	CCSGOPlayerAnimState* animstate = csgo->local->GetPlayerAnimState();
	IBasePlayer* local = csgo->local;
	if (!(local->GetFlags() & FL_ONGROUND))
		return;

	if (local->GetVelocity().Length2D() > 0.1f) {
		next_break = cur_time + .22f;
		return; // well we dont break lby when moving :kappa:
	}

	if (next_break == cur_time)
	{
		Currently_Breaking = true;
		next_break = cur_time + .22f;
		csgo->should_sidemove = false;
		return;
	}
	else
	{
		Pre_Breaking = true;
		next_break = cur_time - .22f;
		csgo->should_sidemove = true;
		return;
	}

}

IBasePlayer* GetNearestTarget(bool check = false)
{
	int y, x;
	y = csgo->h / 2;
	x = csgo->w / 2;

	IBasePlayer* best_ent = nullptr;
	float best_dist = FLT_MAX;

	for (int i = 1; i < 65; i++)
	{
		auto ent = interfaces.ent_list->GetClientEntity(i);
		if (!ent)
			continue;
		if (
			!ent->isAlive()
			|| !ent->IsPlayer()
			|| ent == csgo->local
			|| ent->GetTeam() == csgo->local->GetTeam()
			|| ent->IsDormant()
			|| ent->DormantWrapped())
			continue;

		Vector origin_2d;
		if (!Math::WorldToScreen(ent->GetOrigin(), origin_2d) && check)
			continue;

		float dist = Vector(x, y, 0).DistTo(origin_2d);
		if (dist < best_dist)
		{
			best_ent = ent;
			best_dist = dist;
		}
	}

	if (best_ent)
		return best_ent;

	return nullptr;
}

bool CanTriggerFakeLag() {

	const bool disable_fakelag_on_exploit = []() {
		if (vars.antiaim.fakelag_when_exploits)
			return CanExploit();
		else
			return g_Binds[bind_double_tap].active || g_Binds[bind_hide_shots].active;
	}();

	if (vars.antiaim.fakelag_on_peek && !disable_fakelag_on_exploit) {
		auto predicted_eye_pos = csgo->eyepos + csgo->vecUnpredictedVel * (TICKS_TO_TIME(15));

		IBasePlayer* best_ent = GetNearestTarget();
		
		if (best_ent) {
			auto head_pos = best_ent->GetBonePos(best_ent->GetBoneCache().Base(), 8);

			Ray_t ray;
			ray.Init(predicted_eye_pos, head_pos);

			trace_t trace;
			CTraceFilterWorldAndPropsOnly filter;
			interfaces.trace->TraceRay(ray, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
			
			return trace.fraction > 0.97f;
		}
		return false;
	}

	if (disable_fakelag_on_exploit
		|| (csgo->local->GetVelocity().Length2D() < 10.f && !vars.antiaim.fakelag_when_standing)
		|| vars.antiaim.fakelag == 0)
		return false;

	return true;
}

void CAntiAim::Fakelag()
{
	if (!vars.antiaim.enable)
		return;

	if (vars.antiaim.fakelagfactor == 0)
		return;

	if (csgo->game_rules->IsFreezeTime()
		|| csgo->local->HasGunGameImmunity()
		|| csgo->local->GetFlags() & FL_FROZEN)
		return;

	if (csgo->fake_duck && csgo->local->GetFlags() & FL_ONGROUND && !(csgo->cmd->buttons & IN_JUMP))
	{
		if (csgo->local->GetFlags() & FL_ONGROUND)
			return;
	}

	static Vector origin = Vector();

	if (!CanTriggerFakeLag()) {
		csgo->send_packet = csgo->client_state->iChokedCommands >= 1;
		csgo->max_fakelag_choke = 1;
	}
	else {

		csgo->max_fakelag_choke = clamp(vars.antiaim.fakelagfactor, 1,
			((vars.misc.restrict_type == 0 || csgo->game_rules->IsValveDS()) ? 6 : 14));

		switch (vars.antiaim.fakelag)
		{
		case 1:
			csgo->send_packet = false;
			break;
		case 2:
		{
			if (csgo->cmd->command_number % 30 < csgo->max_fakelag_choke)
				csgo->send_packet = false;
			else
				csgo->send_packet = csgo->client_state->iChokedCommands >= 1;
		}
		break;
		case 3:
		{
			float diff = (csgo->local->GetAbsOrigin() - origin).LengthSqr();
			if (diff <= 4096.f)
				csgo->send_packet = false;
		}
		break;
		}

		if (vars.antiaim.fakelag != 2 && 
			csgo->client_state->iChokedCommands >= csgo->max_fakelag_choke)
			csgo->send_packet = true;
	}

	if (csgo->send_packet)
		origin = csgo->local->GetAbsOrigin();
}

void CAntiAim::Pitch(bool legit_aa)
{
	if (legit_aa)
		return;

	auto state = csgo->local->GetPlayerAnimState();
	if (!state)
		return;

	switch (vars.antiaim.pitch)
	{
	case 1:
		csgo->cmd->viewangles.x = vars.misc.antiuntrusted ? 89.f : 179.98f;
		break;
	case 2:
		csgo->cmd->viewangles.x = state->m_aim_pitch_max;
		break;
	}
}

void CAntiAim::Sidemove() {
	if (csgo->weapon->GetItemDefinitionIndex() != weapon_revolver) {
		if (g_Binds[bind_double_tap].active && csgo->skip_ticks > 0 && csgo->cmd->buttons & IN_ATTACK || csgo->game_rules->IsFreezeTime())
			return;
	}
	if (csgo->local->GetMoveType() == MoveType_t::MOVETYPE_NOCLIP || csgo->local->GetMoveType() == MoveType_t::MOVETYPE_LADDER)
		return;
	if (!csgo->should_sidemove)
		return;

	const float& sideAmount = csgo->cmd->buttons & IN_DUCK || csgo->fake_duck ? 3.25f : 1.01f;
	if (csgo->local->GetVelocity().Length2D() <= 0.f || std::fabs(csgo->local->GetVelocity().z) <= 100.f)
		csgo->cmd->sidemove += csgo->cmd->command_number % 2 ? sideAmount : -sideAmount;
}

void CAntiAim::Yaw(bool legit_aa)
{
	bool check = vars.antiaim.ignore_attarget &&
		(g_Binds[bind_manual_back].active
			|| g_Binds[bind_manual_right].active
			|| g_Binds[bind_manual_left].active
			|| g_Binds[bind_manual_forward].active);

	if (vars.antiaim.attarget && !check && !legit_aa)
	{
		auto best_ent = GetNearestTarget(vars.antiaim.attarget_off_when_offsreen);
		if (best_ent)
			csgo->cmd->viewangles.y = Math::CalculateAngle(csgo->local->GetOrigin(), best_ent->GetOrigin()).y;
	}

	is_lby_update();

	static bool sw = false;
	static bool avoid_overlap_side = false;
	static float last_angle = 0.f;
	static bool once = false;
	static bool twice = false;

	bool inverted = g_Binds[bind_aa_inverter].active;
	if (!twice)
	{
		if (inverted)
			Msg("Inverting!", color_t(255, 255, 0, 255));
	}

	int side = csgo->SwitchAA ? 1 : -1;


	if (legit_aa)
		side *= -1;

	const float desync_amount = legit_aa ? 60.f : 60.f * (vars.antiaim.desync_amount / 100.f);


	float delay = csgo->curtime + (4.2 * 4);

	if (vars.antiaim.enable) {
		if (vars.antiaim.desync) {
			if (!csgo->send_packet)
			{
				if (Currently_Breaking) {
					csgo->cmd->viewangles.y += (vars.antiaim.desync_amount * 2) * side;
				}
				else
					csgo->cmd->viewangles.y += delay * (vars.antiaim.desync_amount * 2) * side / 100; // this is supposed to sway, but the breaker is retarded and made in like 2 seconds so its not good at all.
			}
		}
		csgo->cmd->viewangles.y -= vars.antiaim.yaw_offset;
	}

	if (!legit_aa) {
		csgo->cmd->viewangles.y += vars.antiaim.jitter_angle * (sw ? 1 : -1);
		if (vars.antiaim.manual_antiaim) {
			if (g_Binds[bind_manual_forward].active)
				csgo->cmd->viewangles.y -= 180.f;
			if (g_Binds[bind_manual_left].active)
				csgo->cmd->viewangles.y -= 90.f;
			if (g_Binds[bind_manual_right].active)
				csgo->cmd->viewangles.y += 90.f;
		}
	}
	else
	{
		if (vars.antiaim.yaw == 1)
			csgo->cmd->viewangles.y -= 180.f;
	}

	if (csgo->send_packet)
		sw = !sw;
	else
		last_angle = csgo->cmd->viewangles.y;
}
bool CAntiAim::ShouldAA()
{
	bool use_aa_on_e = !csgo->local->IsDefusing() && vars.antiaim.aa_on_use && csgo->cmd->buttons & IN_USE;

	if (csgo->game_rules->IsFreezeTime()
		|| csgo->local->HasGunGameImmunity()
		|| csgo->local->GetFlags() & FL_FROZEN)
		return false;

	if (!vars.antiaim.enable)
		return false;

	if (csgo->local->GetMoveType() == MOVETYPE_NOCLIP
		|| csgo->local->GetMoveType() == MOVETYPE_LADDER)
		return false;

	if (csgo->cmd->buttons & IN_USE && !use_aa_on_e)
		return false;

	bool in_attack = [&]() {
		bool atk = csgo->cmd->buttons & IN_ATTACK;
		if (csgo->weapon->GetItemDefinitionIndex() == WEAPON_REVOLVER)
			return g_Ragebot->m_revolver_fire && atk;
		else
			return atk;
	}();

	if (F::Shooting()/* || (CanExploit() && in_attack)*/)
		return false;

	return true;
}

void CAntiAim::Initialize()
{
	override_off_yaw = false;
	override_off_pitch = false;
	body_lean = 120.f;
}

void CAntiAim::Run()
{
	if (g_Binds[bind_slow_walk].active || csgo->should_stop_slide)
	{
		const auto weapon = csgo->weapon;
		const auto info = csgo->weapon->GetCSWpnData();
		if (!weapon)
			return;

		if (!info)
			return;

		float speed = csgo->local->IsScoped() ? info->m_flMaxSpeedAlt : info->m_flMaxSpeed;

		speed /= 3.4f;
		float min_speed = (float)(sqrt(pow(csgo->cmd->forwardmove, 2) + pow(csgo->cmd->sidemove, 2) + pow(csgo->cmd->upmove, 2)));

		if (min_speed > 0.f)
		{
			float ratio = speed / min_speed;
			csgo->cmd->forwardmove *= ratio;
			csgo->cmd->sidemove *= ratio;
			csgo->cmd->upmove *= ratio;
		}


		csgo->should_stop_slide = false;
	}
	bool use_aa_on_e = !csgo->local->IsDefusing() && vars.antiaim.aa_on_use && csgo->cmd->buttons & IN_USE;

	if (!vars.ragebot.enable && vars.legitbot.enable)
		use_aa_on_e = true;

	if (ShouldAA())
	{
		if (!override_off_pitch)
			Pitch(use_aa_on_e);
		if (!override_off_yaw)
			Yaw(use_aa_on_e);
	}
}
