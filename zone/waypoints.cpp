/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2004 EQEMu Development Team (http://eqemu.org)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY except by those people which sell it, which
	are required to give you total support for your newly bought product;
	without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/
#include "../common/global_define.h"
#ifdef _EQDEBUG
#include <iostream>
#endif

#include "../common/features.h"
#include "../common/rulesys.h"
#include "../common/string_util.h"
#include "../common/misc_functions.h"

#include "map.h"
#include "npc.h"
#include "quest_parser_collection.h"
#include "water_map.h"

#include <math.h>
#include <stdlib.h>

struct wp_distance
{
	float dist;
	int index;
};

void NPC::AI_SetRoambox(float iDist, float iRoamDist, uint32 iDelay, uint32 iMinDelay) {
	AI_SetRoambox(iDist, GetX()+iRoamDist, GetX()-iRoamDist, GetY()+iRoamDist, GetY()-iRoamDist, iDelay, iMinDelay);
}

void NPC::AI_SetRoambox(float iDist, float iMaxX, float iMinX, float iMaxY, float iMinY, uint32 iDelay, uint32 iMinDelay) {
	roambox_distance = iDist;
	roambox_max_x = iMaxX;
	roambox_min_x = iMinX;
	roambox_max_y = iMaxY;
	roambox_min_y = iMinY;
	roambox_movingto_x = roambox_max_x + 1; // this will trigger a recalc
	roambox_delay = iDelay;
	roambox_min_delay = iMinDelay;
}

void NPC::DisplayWaypointInfo(Client *c) {

	SpawnGroup* sg = zone->spawn_group_list.GetSpawnGroup(GetSp2());

	//Mob is on a roambox.
	if(sg && GetGrid() == 0 && sg->roamdist != 0)
	{
		c->Message(CC_Default, "Mob in spawn group %d is on a roambox.", GetSp2());
		c->Message(CC_Default, "MinX: %0.2f MaxX: %0.2f MinY: %0.2f MaxY: %0.2f", sg->roambox[1], sg->roambox[0], sg->roambox[3], sg->roambox[2]);
		c->Message(CC_Default, "Distance: %0.2f MinDelay: %d Delay: %d", sg->roamdist, sg->min_delay, sg->delay);
		return;
	}
	//Mob either doesn't have a grid, or has a quest assigned grid. 
	else if(GetGrid() == 0)
	{
		c->Message(CC_Default, "Mob in spawn group %d is not on a permanent grid.", GetSp2());
	}
	//Mob is on a normal grid.
	else
	{
		int MaxWp = GetMaxWp();
		//If the Mob hasn't moved yet, this will be 0. Set to 1 so it looks correct.
		if(MaxWp == 0)
			MaxWp = 1;

		c->Message(CC_Default, "Mob is on grid %d, in spawn group %d, on waypoint %d/%d",
			GetGrid(),
			GetSp2(),
			GetCurWp()+1, //We start from 0 internally, but in the DB and lua functions we start from 1.
			MaxWp );
	}

	//Waypoints won't load into memory until the Mob moves.
	std::vector<wplist>::iterator cur, end;
	cur = Waypoints.begin();
	end = Waypoints.end();
	for(; cur != end; ++cur) {
		c->Message(CC_Default,"Waypoint %d: (%.2f,%.2f,%.2f,%.2f) pause %d",
				cur->index+1, //We start from 0 internally, but in the DB and lua functions we start from 1.
				cur->x,
				cur->y,
				cur->z,
				cur->heading,
				cur->pause );
	}
}

void NPC::StopWandering()
{	// stops a mob from wandering, takes him off grid and sends him back to spawn point
	roamer=false;
	CastToNPC()->SetGrid(0);
	SendPosition();
	AIwalking_timer->Start(2000);
	Log.Out(Logs::Detail, Logs::Pathing, "Stop Wandering requested.");
	return;
}

void NPC::ResumeWandering()
{	// causes wandering to continue - overrides waypoint pause timer and PauseWandering()
	if(!IsNPC())
		return;
	if (GetGrid() != 0)
	{
		if (GetGrid() < 0)
		{	// we were paused by a quest
			AIwalking_timer->Disable();
			SetGrid( 0 - GetGrid());
			if (cur_wp==-1)
			{	// got here by a MoveTo()
				cur_wp=save_wp;
				UpdateWaypoint(cur_wp);	// have him head to last destination from here
			}
			Log.Out(Logs::Detail, Logs::Pathing, "Resume Wandering requested. Grid %d, wp %d", GetGrid(), cur_wp);
		}
		else if (AIwalking_timer->Enabled())
		{	// we are at a waypoint paused normally
			Log.Out(Logs::Detail, Logs::Pathing, "Resume Wandering on timed pause. Grid %d, wp %d", GetGrid(), cur_wp);
			AIwalking_timer->Trigger();	// disable timer to end pause now
		}
		else
		{
			Log.Out(Logs::General, Logs::Error, "NPC not paused - can't resume wandering: %lu", (unsigned long)GetNPCTypeID());
			return;
		}

		if (m_CurrentWayPoint.x == GetX() && m_CurrentWayPoint.y == GetY())
		{	// are we we at a waypoint? if so, trigger event and start to next
			char temp[100];
			itoa(cur_wp,temp,10);	//do this before updating to next waypoint
			CalculateNewWaypoint();
			SetAppearance(eaStanding, false);
			parse->EventNPC(EVENT_WAYPOINT_DEPART, this, nullptr, temp, 0);
		}	// if not currently at a waypoint, we continue on to the one we were headed to before the stop
	}
	else
	{
		Log.Out(Logs::General, Logs::Error, "NPC not on grid - can't resume wandering: %lu", (unsigned long)GetNPCTypeID());
	}
	return;
}

void NPC::PauseWandering(int pausetime)
{	// causes wandering to stop but is resumable
	// 0 pausetime means pause until resumed
	// otherwise automatically resume when time is up
	if (GetGrid() != 0)
	{
		DistractedFromGrid = true;
		Log.Out(Logs::Detail, Logs::Pathing, "Paused Wandering requested for: %s. Grid %d. Resuming in %d ms (0=not until told)", GetName(), GetGrid(), pausetime*1000);
		if (pausetime<1)
		{	// negative grid number stops him dead in his tracks until ResumeWandering()
			SetGrid( 0 - GetGrid());
		}
		else
		{	// specified waiting time, he'll resume after that
			AIwalking_timer->Start(pausetime*1000); // set the timer
		}
		SetMoving(false);
		SendPosition();
	}
	else if(roambox_distance > 0)
	{
		roambox_movingto_x = roambox_max_x + 1; // force update
		pLastFightingDelayMoving = Timer::GetCurrentTime() + pausetime*1000;
		SetMoving(false);
		SendPosition();	// makes mobs stop clientside
	}
	else 
	{
		Log.Out(Logs::General, Logs::Error, "NPC not on grid - can't pause wandering: %lu", (unsigned long)GetNPCTypeID());
		return;
	}

	// this stops them from auto changing direction, in AI_DoMovement()
	AIhail_timer->Start((RuleI(NPC, SayPauseTimeInSec)-1)*1000);
	
}

void NPC::MoveTo(const glm::vec4& position, bool saveguardspot)
{	// makes mob walk to specified location
	if (IsNPC() && GetGrid() != 0)
	{	// he is on a grid
		if (GetGrid() < 0)
		{	// currently stopped by a quest command
			SetGrid( 0 - GetGrid());	// get him moving again
			Log.Out(Logs::Detail, Logs::AI, "MoveTo during quest wandering. Canceling quest wandering and going back to grid %d when MoveTo is done.", GetGrid());
		}
		AIwalking_timer->Disable();	// disable timer in case he is paused at a wp
		if (cur_wp>=0)
		{	// we've not already done a MoveTo()
			save_wp=cur_wp;	// save the current waypoint
			cur_wp=-1;		// flag this move as quest controlled
		}
		Log.Out(Logs::Detail, Logs::AI, "MoveTo %s, pausing regular grid wandering. Grid %d, save_wp %d",to_string(static_cast<glm::vec3>(position)).c_str(), -GetGrid(), save_wp);
	}
	else
	{	// not on a grid
		roamer=true;
		save_wp=0;
		cur_wp=-2;		// flag as quest controlled w/no grid
		Log.Out(Logs::Detail, Logs::AI, "MoveTo %s without a grid.", to_string(static_cast<glm::vec3>(position)).c_str());
	}
	if (saveguardspot)
	{
		m_GuardPoint = position;

		if(m_GuardPoint.w == 0)
			m_GuardPoint.w  = 0.0001;		//hack to make IsGuarding simpler

		if(m_GuardPoint.w  == -1)
			m_GuardPoint.w  = this->CalculateHeadingToTarget(position.x, position.y);

		Log.Out(Logs::Detail, Logs::AI, "Setting guard position to %s", to_string(static_cast<glm::vec3>(m_GuardPoint)).c_str());
	}

	m_CurrentWayPoint = position;
	cur_wp_pause = 0;
	pLastFightingDelayMoving = 0;
	if(AIwalking_timer->Enabled())
		AIwalking_timer->Start(100);
}

void NPC::UpdateWaypoint(int wp_index)
{
	if(wp_index >= static_cast<int>(Waypoints.size())) {
		Log.Out(Logs::Detail, Logs::AI, "Update to waypoint %d failed. Not found.", wp_index);
		return;
	}
	std::vector<wplist>::iterator cur;
	cur = Waypoints.begin();
	cur += wp_index;

	m_CurrentWayPoint = glm::vec4(cur->x, cur->y, cur->z, cur->heading);
	cur_wp_pause = cur->pause;
	Log.Out(Logs::Detail, Logs::AI, "Next waypoint %d: (%.3f, %.3f, %.3f, %.3f)", wp_index, m_CurrentWayPoint.x, m_CurrentWayPoint.y, m_CurrentWayPoint.z, m_CurrentWayPoint.w);

	//fix up pathing Z
	if(zone->HasMap() && RuleB(Map, FixPathingZAtWaypoints) && !IsBoat())
	{

		if(!RuleB(Watermap, CheckForWaterAtWaypoints) || !zone->HasWaterMap() ||
		   (zone->HasWaterMap() && !zone->watermap->InWater(glm::vec3(m_CurrentWayPoint))))
		{
			glm::vec3 dest(m_CurrentWayPoint.x, m_CurrentWayPoint.y, m_CurrentWayPoint.z);

			float newz = zone->zonemap->FindBestZ(dest, nullptr);

			if ((newz > -2000) && std::abs(newz + GetZOffset() - dest.z) < (RuleR(Map, FixPathingZMaxDeltaWaypoint) + GetZOffset()))
			{
				m_CurrentWayPoint.z = SetBestZ(newz);
			}
		}
	}

}

void NPC::CalculateNewWaypoint()
{
	int old_wp = cur_wp;
	bool reached_end = false;
	bool reached_beginning = false;
	if (cur_wp == max_wp)
		reached_end = true;
	if (cur_wp == 0)
		reached_beginning = true;

	switch(wandertype)
	{
	case 0: //circle
	{
		if (reached_end)
			cur_wp = 0;
		else
			cur_wp = cur_wp + 1;
		break;
	}
	case 1: //10 closest
	{
		std::list<wplist> closest;
		GetClosestWaypoint(closest, 10, glm::vec3(GetPosition()));
		std::list<wplist>::iterator iter = closest.begin();
		if(!closest.empty())
		{
			iter = closest.begin();
			std::advance(iter, zone->random.Int(0, closest.size() - 1));
			cur_wp = (*iter).index;
		}

		break;
	}
	case 2: //random
	{
		cur_wp = zone->random.Int(0, Waypoints.size() - 1);
		if(cur_wp == old_wp)
		{
			if(cur_wp == (Waypoints.size() - 1))
			{
				if(cur_wp > 0)
				{
					cur_wp--;
				}
			}
			else if(cur_wp == 0)
			{
				if((Waypoints.size() - 1) > 0)
				{
					cur_wp++;
				}
			}
		}

		break;
	}
	case 3: //patrol
	{
		if(reached_end)
			patrol = 1;
		else if(reached_beginning)
			patrol = 0;
		if(patrol == 1)
			cur_wp = cur_wp - 1;
		else
			cur_wp = cur_wp + 1;

		break;
	}
	case 4: //goto the end and depop with spawn timer
	case 6: //goto the end and depop without spawn timer
	{
		cur_wp = cur_wp + 1;
		break;
	}
	case 5: //pick random closest 5 and pick one that's in sight
	{
		std::list<wplist> closest;
		GetClosestWaypoint(closest, 5, glm::vec3(GetPosition()));

		std::list<wplist>::iterator iter = closest.begin();
		while(iter != closest.end())
		{
			if(CheckLosFN((*iter).x, (*iter).y, (*iter).z, GetSize()))
			{
				++iter;
			}
			else
			{
				iter = closest.erase(iter);
			}
		}

		if (closest.size() != 0)
		{
			iter = closest.begin();
			std::advance(iter, zone->random.Int(0, closest.size() - 1));
			cur_wp = (*iter).index;
		}
		break;
	}
	}

	tar_ndx = 20;

	// Preserve waypoint setting for quest controlled NPCs
	if (cur_wp < 0)
		cur_wp = old_wp;

	// Check to see if we need to update the waypoint.
	if (cur_wp != old_wp)
		UpdateWaypoint(cur_wp);

	if(IsNPC() && GetClass() == MERCHANT)
		entity_list.SendMerchantEnd(this);
}

bool wp_distance_pred(const wp_distance& left, const wp_distance& right)
{
	return left.dist < right.dist;
}

void NPC::GetClosestWaypoint(std::list<wplist> &wp_list, int count, const glm::vec3& location)
{
	wp_list.clear();
	if(Waypoints.size() <= count)
	{
		for(int i = 0; i < Waypoints.size(); ++i)
		{
			wp_list.push_back(Waypoints[i]);
		}
		return;
	}

	std::list<wp_distance> distances;
	for(int i = 0; i < Waypoints.size(); ++i)
	{
		float cur_x = (Waypoints[i].x - location.x);
		cur_x *= cur_x;
		float cur_y = (Waypoints[i].y - location.y);
		cur_y *= cur_y;
		float cur_z = (Waypoints[i].z - location.z);
		cur_z *= cur_z;
		float cur_dist = cur_x + cur_y + cur_z;
		wp_distance w_dist;
		w_dist.dist = cur_dist;
		w_dist.index = i;
		distances.push_back(w_dist);
	}
	distances.sort([](const wp_distance& a, const wp_distance& b) {
		return a.dist < b.dist;
	});

	std::list<wp_distance>::iterator iter = distances.begin();
	for(int i = 0; i < count; ++i)
	{
		wp_list.push_back(Waypoints[(*iter).index]);
		++iter;
	}
}

void NPC::SetWaypointPause()
{
	//Declare time to wait on current WP

	if (cur_wp_pause == 0) {
		AIwalking_timer->Start(100);
		AIwalking_timer->Trigger();
	}
	else
	{

		switch (pausetype)
		{
			case 0: //Random Half
				AIwalking_timer->Start((cur_wp_pause - zone->random.Int(0, cur_wp_pause-1)/2)*1000);
				break;
			case 1: //Full
				AIwalking_timer->Start(cur_wp_pause*1000);
				break;
			case 2: //Random Full
				AIwalking_timer->Start(zone->random.Int(0, cur_wp_pause-1)*1000);
				break;
		}
	}
}

void NPC::SaveGuardSpot(bool iClearGuardSpot) {
	if (iClearGuardSpot) {
		Log.Out(Logs::Detail, Logs::AI, "Clearing guard order.");
		m_GuardPoint = glm::vec4(0, 0, 0, 0);
	}
	else {
		m_GuardPoint = m_Position;

		if(m_GuardPoint.w == 0)
			m_GuardPoint.w = 0.0001;		//hack to make IsGuarding simpler
		Log.Out(Logs::Detail, Logs::AI, "Setting guard position to %s", to_string(static_cast<glm::vec3>(m_GuardPoint)).c_str());
	}
}

void NPC::NextGuardPosition() {
	float walksp = GetMovespeed();
	SetCurrentSpeed(walksp);
	if(walksp <= 0.0f)
		return;

	bool CP2Moved;
	if(!RuleB(Pathing, Guard) || !zone->pathing)
		CP2Moved = CalculateNewPosition2( m_GuardPoint.x, m_GuardPoint.y, m_GuardPoint.z, walksp);
	else
	{
		if(!((m_Position.x == m_GuardPoint.x) && (m_Position.y == m_GuardPoint.y) && (m_Position.z == m_GuardPoint.z)))
		{
			bool WaypointChanged, NodeReached;
			glm::vec3 Goal = UpdatePath(m_GuardPoint.x, m_GuardPoint.y, m_GuardPoint.z, walksp, WaypointChanged, NodeReached);
			if(WaypointChanged)
				tar_ndx = 20;

			if(NodeReached)
				entity_list.OpenDoorsNear(CastToNPC());

			CP2Moved = CalculateNewPosition2(Goal.x, Goal.y, Goal.z, walksp);
		}
		else
			CP2Moved = false;

	}
	if (!CP2Moved)
	{
		if(moved) {
			Log.Out(Logs::Detail, Logs::AI, "Reached guard point (%.3f,%.3f,%.3f)", m_GuardPoint.x, m_GuardPoint.y, m_GuardPoint.z);
			moved=false;
			SetMoving(false);
			if (GetTarget() == nullptr || DistanceSquared(m_Position, GetTarget()->GetPosition()) >= 5*5 )
			{
				SetHeading(m_GuardPoint.w);
			} else {
				FaceTarget(GetTarget());
			}
			SendPosition();
			SetAppearance(GetGuardPointAnim());
		}
	}



	//if (!CalculateNewPosition2(m_GuardPoint.x, m_GuardPoint.y, m_GuardPoint.z, GetMovespeed())) {
	//	SetHeading(m_GuardPoint.w);
	//	Log.Out(Logs::Detail, Logs::AI, "Unable to move to next guard position. Probably rooted.");
	//}
	//else if((m_Position.x == m_GuardPoint.x) && (m_Position.y == m_GuardPoint.y) && (m_Position.z == m_GuardPoint.z))
	//{
	//	if(moved)
	//	{
	//		moved=false;
	//		SetMoving(false);
	//		SendPosition();
	//	}
	//}
}

/*
// we need this for charmed NPCs
void Mob::SaveSpawnSpot() {
	spawn_x = x_pos;
	spawn_y = y_pos;
	spawn_z = z_pos;
	spawn_heading = heading;
}*/




/*float Mob::CalculateDistanceToNextWaypoint() {
	return CalculateDistance(cur_wp_x, cur_wp_y, cur_wp_z);
}*/

float Mob::CalculateDistance(float x, float y, float z) {
	return (float)sqrtf( ((m_Position.x-x)*(m_Position.x-x)) + ((m_Position.y-y)*(m_Position.y-y)) + ((m_Position.z-z)*(m_Position.z-z)) );
}

/*
uint8 NPC::CalculateHeadingToNextWaypoint() {
	return CalculateHeadingToTarget(cur_wp_x, cur_wp_y);
}
*/
float Mob::CalculateHeadingToTarget(float in_x, float in_y) {
	float angle;

	if (in_x-m_Position.x > 0)
		angle = - 90 + atan((float)(in_y-m_Position.y) / (float)(in_x-m_Position.x)) * 180 / M_PI;
	else if (in_x-m_Position.x < 0)
		angle = + 90 + atan((float)(in_y-m_Position.y) / (float)(in_x-m_Position.x)) * 180 / M_PI;
	else // Added?
	{
		if (in_y-m_Position.y > 0)
			angle = 0;
		else
			angle = 180;
	}
	if (angle < 0)
		angle += 360;
	if (angle > 360)
		angle -= 360;
	return (256*(360-angle)/360.0f);
}

bool Mob::MakeNewPositionAndSendUpdate(float x, float y, float z, float speed, bool checkZ) {
	if(GetID()==0)
		return true;

	if ((m_Position.x-x == 0) && (m_Position.y-y == 0)) {//spawn is at target coords
		if(m_Position.z-z != 0) {
			m_Position.z = z;
			Log.Out(Logs::Detail, Logs::AI, "Calc Position2 (%.3f, %.3f, %.3f): Jumping pure Z.", x, y, z);
			return true;
		}
		Log.Out(Logs::Detail, Logs::AI, "Calc Position2 (%.3f, %.3f, %.3f) inWater=%d: We are there.", x, y, z, inWater);
		return false;
	}
	else if ((std::abs(m_Position.x - x) < 0.1) && (std::abs(m_Position.y - y) < 0.1))
	{
		Log.Out(Logs::Detail, Logs::AI, "Calc Position2 (%.3f, %.3f, %.3f): X/Y difference <0.1, Jumping to target.", x, y, z);

		if(IsNPC()) {
			entity_list.ProcessMove(CastToNPC(), x, y, z);
		}

		m_Position.x = x;
		m_Position.y = y;
		m_Position.z = z;
		return true;
	}
	bool send_update = false;
	int compare_steps = IsBoat() ? 1 : 20;

	if(tar_ndx < compare_steps && m_TargetLocation.x==x && m_TargetLocation.y==y) {

		float new_x = m_Position.x + m_TargetV.x*tar_vector;
		float new_y = m_Position.y + m_TargetV.y*tar_vector;
		float new_z = m_Position.z + m_TargetV.z*tar_vector;
		if(IsNPC()) {
			entity_list.ProcessMove(CastToNPC(), new_x, new_y, new_z);
		}

		m_Position.x = new_x;
		m_Position.y = new_y;
		m_Position.z = new_z;

		Log.Out(Logs::Detail, Logs::AI, "Calculating new position2 to (%.3f, %.3f, %.3f), old vector (%.3f, %.3f, %.3f)", x, y, z, m_TargetV.x, m_TargetV.y, m_TargetV.z);

		uint8 NPCFlyMode = 0;

		if(IsNPC()) {
			if(CastToNPC()->GetFlyMode() == 1 || CastToNPC()->GetFlyMode() == 2)
				NPCFlyMode = 1;
		}

		//fix up pathing Z
		if(!NPCFlyMode && !IsBoat() && checkZ && zone->HasMap() && RuleB(Map, FixPathingZWhenMoving))
		{
			if(!RuleB(Watermap, CheckForWaterWhenMoving) || !zone->HasWaterMap() ||
			   (zone->HasWaterMap() && !zone->watermap->InWater(glm::vec3(m_Position))))
			{
				glm::vec3 dest(m_Position.x, m_Position.y, m_Position.z);

				float newz = zone->zonemap->FindBestZ(dest, nullptr);

				Log.Out(Logs::Detail, Logs::AI, "BestZ returned %4.3f at %4.3f, %4.3f, %4.3f", newz,m_Position.x,m_Position.y,m_Position.z);

				if ((newz > -2000) &&
				    std::abs(newz + GetZOffset() - dest.z) < RuleR(Map, FixPathingZMaxDeltaMoving)) // Sanity check.
				{
					if((std::abs(x - m_Position.x) < 0.5) && (std::abs(y - m_Position.y) < 0.5))
					{
						if (std::abs(z - m_Position.z) <= RuleR(Map, FixPathingZMaxDeltaMoving))
							m_Position.z = z;
						else
						{
							m_Position.z = SetBestZ(newz);
						}
					}
					else
					{
						m_Position.z = SetBestZ(newz);
					}
				}
			}
		}

		tar_ndx++;
		return true;
	}


	if (tar_ndx>50) {
		tar_ndx--;
	} else {
		tar_ndx=0;
	}
	m_TargetLocation = glm::vec3(x, y, z);

	float nx = this->m_Position.x;
	float ny = this->m_Position.y;
	float nz = this->m_Position.z;
//	float nh = this->heading;

	m_TargetV.x = x - nx;
	m_TargetV.y = y - ny;
	m_TargetV.z = z - nz;

	//pRunAnimSpeed = (int8)(speed*RuleI(NPC, RunAnimRatio));
	//speed *= RuleR(NPC, SpeedMultiplier);

	Log.Out(Logs::Detail, Logs::AI, "Calculating new position2 to (%.3f, %.3f, %.3f), new vector (%.3f, %.3f, %.3f) rate %.3f, RAS %d", x, y, z, m_TargetV.x, m_TargetV.y, m_TargetV.z, speed, pRunAnimSpeed);

	// --------------------------------------------------------------------------
	// 2: get unit vector
	// --------------------------------------------------------------------------
	float mag = sqrtf (m_TargetV.x*m_TargetV.x + m_TargetV.y*m_TargetV.y + m_TargetV.z*m_TargetV.z);
	tar_vector = speed / mag;

// mob move fix
	int numsteps = (int) ( mag * 20.0f / speed);


// mob move fix

	if (numsteps<20)
	{
		if (numsteps>1)
		{
			tar_vector=1.0f ;
			m_TargetV.x = m_TargetV.x/numsteps;
			m_TargetV.y = m_TargetV.y/numsteps;
			m_TargetV.z = m_TargetV.z/numsteps;

			float new_x = m_Position.x + m_TargetV.x;
			float new_y = m_Position.y + m_TargetV.y;
			float new_z = m_Position.z + m_TargetV.z;

			m_Position.x = new_x;
			m_Position.y = new_y;
			m_Position.z = new_z;
			m_Position.w = CalculateHeadingToTarget(x, y);
			tar_ndx=20-numsteps;
			Log.Out(Logs::Detail, Logs::AI, "Next position2 (%.3f, %.3f, %.3f) (%d steps)", m_Position.x, m_Position.y, m_Position.z, numsteps);
		}
		else
		{
			if(IsNPC()) {
				entity_list.ProcessMove(CastToNPC(), x, y, z);
			}

			m_Position.x = x;
			m_Position.y = y;
			m_Position.z = z;
			tar_ndx = 20;
			Log.Out(Logs::Detail, Logs::AI, "Only a single step to get there... jumping.");
		}
	}

	else {
		tar_vector/=20.0f;

		float new_x = m_Position.x + m_TargetV.x*tar_vector;
		float new_y = m_Position.y + m_TargetV.y*tar_vector;
		float new_z = m_Position.z + m_TargetV.z*tar_vector;
		if(IsNPC()) {
			entity_list.ProcessMove(CastToNPC(), new_x, new_y, new_z);
		}

		m_Position.x = new_x;
		m_Position.y = new_y;
		m_Position.z = new_z;
		m_Position.w = CalculateHeadingToTarget(x, y);
		Log.Out(Logs::Detail, Logs::AI, "Next position2 (%.3f, %.3f, %.3f) (%d steps)", m_Position.x, m_Position.y, m_Position.z, numsteps);
	}


	uint8 NPCFlyMode = 0;

	if(IsNPC()) {
		if(CastToNPC()->GetFlyMode() == 1 || CastToNPC()->GetFlyMode() == 2)
			NPCFlyMode = 1;

	}

	//fix up pathing Z
	if(!NPCFlyMode && !IsBoat() && checkZ && zone->HasMap() && RuleB(Map, FixPathingZWhenMoving)) {

		if(!RuleB(Watermap, CheckForWaterWhenMoving) || !zone->HasWaterMap() ||
		   (zone->HasWaterMap() && !zone->watermap->InWater(glm::vec3(m_Position))))
		{
			glm::vec3 dest(m_Position.x, m_Position.y, m_Position.z);

			float newz = zone->zonemap->FindBestZ(dest, nullptr);

			Log.Out(Logs::Detail, Logs::AI, "BestZ returned %4.3f at %4.3f, %4.3f, %4.3f", newz,m_Position.x,m_Position.y,m_Position.z);

			if ((newz > -2000) &&
			    std::abs(newz + GetZOffset() - dest.z) < (RuleR(Map, FixPathingZMaxDeltaMoving) + GetZOffset())) // Sanity check.
			{
				if(std::abs(x - m_Position.x) < 0.5 && std::abs(y - m_Position.y) < 0.5)
				{
					if(std::abs(z - m_Position.z) <= (RuleR(Map, FixPathingZMaxDeltaMoving) + GetZOffset()))
						m_Position.z = z;
					else
					{
						m_Position.z = SetBestZ(newz);
					}
				}
				else
				{
					m_Position.z = SetBestZ(newz);
				}
			}
		}
	}

	SetMoving(true);
	moved=true;


	m_Delta = glm::vec4(floorf((m_Position.x - nx)*6.6666666f), floorf((m_Position.y - ny)*6.6666666f), floorf((m_Position.z - nz)*6.6666666f), 0.0f);

	if (IsClient()) {
		SendPosUpdate(1);
		CastToClient()->ResetPositionTimer();
	}
	else
	{
		SendPosUpdate();
		SetAppearance(eaStanding, false);
	}		
	SetChanged();
	
	return true;
}

bool Mob::CalculateNewPosition2(float x, float y, float z, float speed, bool checkZ) {

	float newspeed = SetRunAnimation(speed);

	bool moved = false;
	if (MakeNewPositionAndSendUpdate(x, y, z, newspeed, checkZ))
	{
		if(IsNPC() && GetClass() == MERCHANT)
			entity_list.SendMerchantEnd(this);

		moved = true;
	}

	return moved;

}

float Mob::SetRunAnimation(float speed)
{
	SetCurrentSpeed(speed);
	float newspeed = speed * 50.0f;
	pRunAnimSpeed = static_cast<uint8>(speed * 10.0f);
	if (IsClient()) {
		newspeed = speed * 100.0f;
		animation = static_cast<uint16>(speed * 10.0f);
	}

	return newspeed;
}

bool Mob::CalculateNewPosition(float x, float y, float z, float speed, bool checkZ) {
	if(GetID()==0)
		return true;

	float nx = m_Position.x;
	float ny = m_Position.y;
	float nz = m_Position.z;

	// if NPC is rooted
	if (speed <= 0.0) {
		SetHeading(CalculateHeadingToTarget(x, y));
		if(moved){
			SendPosition();
			SetMoving(false);
			moved=false;
		}
		SetRunAnimSpeed(0);
		Log.Out(Logs::Detail, Logs::AI, "Rooted while calculating new position to (%.3f, %.3f, %.3f)", x, y, z);
		return true;
	}

	float old_test_vector=test_vector;
	m_TargetV.x = x - nx;
	m_TargetV.y = y - ny;
	m_TargetV.z = z - nz;

	if (m_TargetV.x == 0 && m_TargetV.y == 0)
		return false;
	float tmpspeed = speed;
	speed = SetRunAnimation(tmpspeed);

	Log.Out(Logs::Detail, Logs::AI, "Calculating new position to (%.3f, %.3f, %.3f) vector (%.3f, %.3f, %.3f) rate %.3f RAS %d", x, y, z, m_TargetV.x, m_TargetV.y, m_TargetV.z, speed, pRunAnimSpeed);

	// --------------------------------------------------------------------------
	// 2: get unit vector
	// --------------------------------------------------------------------------
	test_vector=sqrtf (x*x + y*y + z*z);
	tar_vector = speed / sqrtf (m_TargetV.x*m_TargetV.x + m_TargetV.y*m_TargetV.y + m_TargetV.z*m_TargetV.z);
	m_Position.w = CalculateHeadingToTarget(x, y);

	if (tar_vector >= 1.0) {
		if(IsNPC()) {
			entity_list.ProcessMove(CastToNPC(), x, y, z);
		}

		m_Position.x = x;
		m_Position.y = y;
		m_Position.z = z;
		Log.Out(Logs::Detail, Logs::AI, "Close enough, jumping to waypoint");
	}
	else {
		float new_x = m_Position.x + m_TargetV.x*tar_vector;
		float new_y = m_Position.y + m_TargetV.y*tar_vector;
		float new_z = m_Position.z + m_TargetV.z*tar_vector;
		if(IsNPC()) {
			entity_list.ProcessMove(CastToNPC(), new_x, new_y, new_z);
		}

		m_Position.x = new_x;
		m_Position.y = new_y;
		m_Position.z = new_z;
		Log.Out(Logs::Detail, Logs::AI, "Next position (%.3f, %.3f, %.3f)", m_Position.x, m_Position.y, m_Position.z);
	}

	uint8 NPCFlyMode = 0;

	if(IsNPC()) {
		if(CastToNPC()->GetFlyMode() == 1 || CastToNPC()->GetFlyMode() == 2)
			NPCFlyMode = 1;
	}

	//fix up pathing Z
	if(!NPCFlyMode && !IsBoat() && checkZ && zone->HasMap() && RuleB(Map, FixPathingZWhenMoving))
	{
		if(!RuleB(Watermap, CheckForWaterWhenMoving) || !zone->HasWaterMap() ||
			(zone->HasWaterMap() && !zone->watermap->InWater(glm::vec3(m_Position))))
		{
			glm::vec3 dest(m_Position.x, m_Position.y, m_Position.z);

			float newz = zone->zonemap->FindBestZ(dest, nullptr);

			Log.Out(Logs::Detail, Logs::AI, "BestZ returned %4.3f at %4.3f, %4.3f, %4.3f", newz,m_Position.x,m_Position.y,m_Position.z);

			if ((newz > -2000) &&
			    std::abs(newz + GetZOffset() - dest.z) < (RuleR(Map, FixPathingZMaxDeltaMoving) + GetZOffset())) // Sanity check.
			{
				if (std::abs(x - m_Position.x) < 0.5 && std::abs(y - m_Position.y) < 0.5)
				{
					if(std::abs(z - m_Position.z) <= RuleR(Map, FixPathingZMaxDeltaMoving))
						m_Position.z = z;
					else
					{
						m_Position.z = SetBestZ(newz);
					}
				}
				else
				{
					m_Position.z = SetBestZ(newz);
				}
			}
		}
	}

	//OP_MobUpdate
	if((old_test_vector!=test_vector) || tar_ndx>20){ //send update
		tar_ndx=0;
		this->SetMoving(true);
		moved=true;
		m_Delta = glm::vec4(m_Position.x - nx, m_Position.y - ny, m_Position.z - nz, 0.0f);
		SendPosUpdate();
	}
	tar_ndx++;

	// now get new heading
	SetAppearance(eaStanding, false); // make sure they're standing
	SetChanged();
	return true;
}

void NPC::AssignWaypoints(int32 grid)
{
	if (grid == 0)
		return; // grid ID 0 not supported

	if (grid < 0) {
		// Allow setting negative grid values for pausing pathing
		this->CastToNPC()->SetGrid(grid);
		return;
	}

	Waypoints.clear();
	roamer = false;

	// Retrieve the wander and pause types for this grid
	std::string query = StringFormat("SELECT `type`, `type2` FROM `grid` WHERE `id` = %i AND `zoneid` = %i", grid,
					 zone->GetZoneID());
	auto results = database.QueryDatabase(query);
	if (!results.Success()) {
		Log.Out(Logs::General, Logs::Error, "MySQL Error while trying to assign grid %u to mob %s: %s", grid,
			name, results.ErrorMessage().c_str());
		return;
	}

	if (results.RowCount() == 0)
		return;

	auto row = results.begin();

	wandertype = atoi(row[0]);
	pausetype = atoi(row[1]);

	this->CastToNPC()->SetGrid(grid); // Assign grid number

	// Retrieve all waypoints for this grid
	query = StringFormat("SELECT `x`,`y`,`z`,`pause`,`heading` "
		"FROM grid_entries WHERE `gridid` = %i AND `zoneid` = %i "
		"ORDER BY `number`", grid, zone->GetZoneID());
	results = database.QueryDatabase(query);
	if (!results.Success()) {
		Log.Out(Logs::General, Logs::Error, "MySQL Error while trying to assign waypoints from grid %u to mob %s: %s", grid, name, results.ErrorMessage().c_str());
		return;
	}

	roamer = true;
	max_wp = 0;	// Initialize it; will increment it for each waypoint successfully added to the list

	for (auto row = results.begin(); row != results.end(); ++row, ++max_wp)
	{
		wplist newwp;
		newwp.index = max_wp;
		newwp.x = atof(row[0]);
		newwp.y = atof(row[1]);
		newwp.z = atof(row[2]);

		if(zone->HasMap() && RuleB(Map, FixPathingZWhenLoading) && !IsBoat())
		{
			auto positon = glm::vec3(newwp.x,newwp.y,newwp.z);
			if(!RuleB(Watermap, CheckWaypointsInWaterWhenLoading) || !zone->HasWaterMap() ||
				(zone->HasWaterMap() && !zone->watermap->InWater(positon)))
			{
				glm::vec3 dest(newwp.x, newwp.y, newwp.z);

				float newz = zone->zonemap->FindBestZ(dest, nullptr);

				if( (newz > -2000) && std::abs(newz+GetZOffset()-dest.z) < (RuleR(Map, FixPathingZMaxDeltaLoading)+GetZOffset()))
				{
					newwp.z = SetBestZ(newz);
				}
			}
		}

		newwp.pause = atoi(row[3]);
		newwp.heading = atof(row[4]);
		Waypoints.push_back(newwp);
	}

	if(Waypoints.size() < 2) {
		roamer = false;
	}

	UpdateWaypoint(0);
	SetWaypointPause();

	if (wandertype == 1 || wandertype == 2 || wandertype == 5)
		CalculateNewWaypoint();

}

void Mob::SendTo(float new_x, float new_y, float new_z) {
	if(IsNPC()) {
		entity_list.ProcessMove(CastToNPC(), new_x, new_y, new_z);
	}

	m_Position.x = new_x;
	m_Position.y = new_y;
	m_Position.z = new_z;
	Log.Out(Logs::Detail, Logs::AI, "Sent To (%.3f, %.3f, %.3f)", new_x, new_y, new_z);

	if(flymode == FlyMode1)
		return;

	//fix up pathing Z, this shouldent be needed IF our waypoints
	//are corrected instead
	if(zone->HasMap() && RuleB(Map, FixPathingZOnSendTo) && !IsBoat())
	{
		if(!RuleB(Watermap, CheckForWaterOnSendTo) || !zone->HasWaterMap() ||
			(zone->HasWaterMap() && !zone->watermap->InWater(glm::vec3(m_Position))))
		{
			glm::vec3 dest(m_Position.x, m_Position.y, m_Position.z);

			float newz = zone->zonemap->FindBestZ(dest, nullptr);

			Log.Out(Logs::Detail, Logs::AI, "BestZ returned %4.3f at %4.3f, %4.3f, %4.3f", newz,m_Position.x,m_Position.y,m_Position.z);

			if ((newz > -2000) &&
			    std::abs(newz - dest.z) < RuleR(Map, FixPathingZMaxDeltaSendTo)) // Sanity check.
			{
				m_Position.z = SetBestZ(newz);
			}
		}
	}
	else
		m_Position.z += 0.1;
}

void Mob::SendToFixZ(float new_x, float new_y, float new_z) {
	if(IsNPC()) {
		entity_list.ProcessMove(CastToNPC(), new_x, new_y, new_z + 0.5f);
	}

	m_Position.x = new_x;
	m_Position.y = new_y;
	m_Position.z = new_z + 0.5f;

	//fix up pathing Z, this shouldent be needed IF our waypoints
	//are corrected instead

	if(zone->HasMap() && RuleB(Map, FixPathingZOnSendTo) && !IsBoat())
	{
		if(!RuleB(Watermap, CheckForWaterOnSendTo) || !zone->HasWaterMap() ||
			(zone->HasWaterMap() && !zone->watermap->InWater(glm::vec3(m_Position))))
		{
			glm::vec3 dest(m_Position.x, m_Position.y, m_Position.z);

			float newz = zone->zonemap->FindBestZ(dest, nullptr);

			Log.Out(Logs::Detail, Logs::AI, "BestZ returned %4.3f at %4.3f, %4.3f, %4.3f", newz,m_Position.x,m_Position.y,m_Position.z);

			if ((newz > -2000) &&
			    std::abs(newz - dest.z) < RuleR(Map, FixPathingZMaxDeltaSendTo)) // Sanity check.
			{
				m_Position.z = SetBestZ(newz);
			}
		}
	}
}

int	ZoneDatabase::GetHighestGrid(uint32 zoneid) {

	std::string query = StringFormat("SELECT COALESCE(MAX(id), 0) FROM grid WHERE zoneid = %i", zoneid);
	auto results = QueryDatabase(query);
    if (!results.Success()) {
        return 0;
    }

	if (results.RowCount() != 1)
		return 0;

	auto row = results.begin();
	return atoi(row[0]);
}

uint8 ZoneDatabase::GetGridType2(uint32 grid, uint16 zoneid) {

	int type2 = 0;
	std::string query = StringFormat("SELECT type2 FROM grid WHERE id = %i AND zoneid = %i", grid, zoneid);
	auto results = QueryDatabase(query);
    if (!results.Success()) {
        return 0;
    }

	if (results.RowCount() != 1)
		return 0;

	auto row = results.begin();

	return atoi(row[0]);
}

bool ZoneDatabase::GetWaypoints(uint32 grid, uint16 zoneid, uint32 num, wplist* wp) {

	if (wp == nullptr)
		return false;

	std::string query = StringFormat("SELECT x, y, z, pause, heading FROM grid_entries "
                                    "WHERE gridid = %i AND number = %i AND zoneid = %i", grid, num, zoneid);
    auto results = QueryDatabase(query);
    if (!results.Success()) {
        return false;
    }

	if (results.RowCount() != 1)
		return false;

	auto row = results.begin();

	wp->x = atof(row[0]);
	wp->y = atof(row[1]);
	wp->z = atof(row[2]);
	wp->pause = atoi(row[3]);
	wp->heading = atof(row[4]);

	return true;
}

void ZoneDatabase::AssignGrid(Client *client, int grid, int spawn2id) {
	std::string query = StringFormat("UPDATE spawn2 SET pathgrid = %d WHERE id = %d", grid, spawn2id);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return;
	
	if (results.RowsAffected() != 1) {
		return;
	}

	client->Message(CC_Default, "Grid assign: spawn2 id = %d updated", spawn2id);
}


/******************
* ModifyGrid - Either adds an empty grid, or removes a grid and all its waypoints, for a particular zone.
*	remove:		TRUE if we are deleting the specified grid, FALSE if we are adding it
*	id:		The ID# of the grid to add or delete
*	type,type2:	The type and type2 values for the grid being created (ignored if grid is being deleted)
*	zoneid:		The ID number of the zone the grid is being created/deleted in
*/
void ZoneDatabase::ModifyGrid(Client *client, bool remove, uint32 id, uint8 type, uint8 type2, uint16 zoneid) {

	if (!remove)
	{
        std::string query = StringFormat("INSERT INTO grid(id, zoneid, type, type2) "
                                            "VALUES (%i, %i, %i, %i)", id, zoneid, type, type2);
        auto results = QueryDatabase(query);
        if (!results.Success()) {
            return;
        }

		return;
	}

    std::string query = StringFormat("DELETE FROM grid where id=%i", id);
    auto results = QueryDatabase(query);

    query = StringFormat("DELETE FROM grid_entries WHERE zoneid = %i AND gridid = %i", zoneid, id);
    results = QueryDatabase(query);

}

/**************************************
* AddWP - Adds a new waypoint to a specific grid for a specific zone.
*/
void ZoneDatabase::AddWP(Client *client, uint32 gridid, uint32 wpnum, const glm::vec4& position, uint32 pause, uint16 zoneid)
{
	std::string query = StringFormat("INSERT INTO grid_entries (gridid, zoneid, `number`, x, y, z, pause, heading) "
									"VALUES (%i, %i, %i, %f, %f, %f, %i, %f)",
									gridid, zoneid, wpnum, position.x, position.y, position.z, pause, position.w);
	auto results = QueryDatabase(query);
	if (!results.Success()) {
		return;
	}

}


/**********
* ModifyWP() has been obsoleted. The #wp command either uses AddWP() or DeleteWaypoint()
***********/

/******************
* DeleteWaypoint - Removes a specific waypoint from the grid
*	grid_id:	The ID number of the grid whose wp is being deleted
*	wp_num:		The number of the waypoint being deleted
*	zoneid:		The ID number of the zone that contains the waypoint being deleted
*/
void ZoneDatabase::DeleteWaypoint(Client *client, uint32 grid_num, uint32 wp_num, uint16 zoneid)
{
	std::string query = StringFormat("DELETE FROM grid_entries WHERE "
									"gridid = %i AND zoneid = %i AND `number` = %i",
									grid_num, zoneid, wp_num);
	auto results = QueryDatabase(query);
	if(!results.Success()) {
		return;
	}

}


/******************
* AddWPForSpawn - Used by the #wpadd command - for a given spawn, this will add a new waypoint to whatever grid that spawn is assigned to.
* If there is currently no grid assigned to the spawn, a new grid will be created using the next available Grid ID number for the zone
* the spawn is in.
* Returns 0 if the function didn't have to create a new grid. If the function had to create a new grid for the spawn, then the ID of
* the created grid is returned.
*/
uint32 ZoneDatabase::AddWPForSpawn(Client *client, uint32 spawn2id, const glm::vec4& position, uint32 pause, int type1, int type2, uint16 zoneid) {

	uint32 grid_num;	 // The grid number the spawn is assigned to (if spawn has no grid, will be the grid number we end up creating)
	uint32 next_wp_num;	 // The waypoint number we should be assigning to the new waypoint
	bool createdNewGrid; // Did we create a new grid in this function?

	// See what grid number our spawn is assigned
	std::string query = StringFormat("SELECT pathgrid FROM spawn2 WHERE id = %i", spawn2id);
	auto results = QueryDatabase(query);
	if (!results.Success()) {
		// Query error
		return 0;
	}

	if (results.RowCount() == 0)
		return 0;

	auto row = results.begin();
	grid_num = atoi(row[0]);

	if (grid_num == 0)
	{ // Our spawn doesn't have a grid assigned to it -- we need to create a new grid and assign it to the spawn
		createdNewGrid = true;
		grid_num = GetFreeGrid(zoneid);
		if(grid_num == 0)	// There are no grids for the current zone -- create Grid #1
			grid_num = 1;

		query = StringFormat("INSERT INTO grid SET id = '%i', zoneid = %i, type ='%i', type2 = '%i'",
							grid_num, zoneid, type1, type2);
		QueryDatabase(query);

		query = StringFormat("UPDATE spawn2 SET pathgrid = '%i' WHERE id = '%i'", grid_num, spawn2id);
		QueryDatabase(query);

	}
	else	// NPC had a grid assigned to it
		createdNewGrid = false;

	// Find out what the next waypoint is for this grid
	query = StringFormat("SELECT max(`number`) FROM grid_entries WHERE zoneid = '%i' AND gridid = '%i'", zoneid, grid_num);

	results = QueryDatabase(query);
	if(!results.Success()) { // Query error
		return 0;
	}

	row = results.begin();
	if(row[0] != 0)
		next_wp_num = atoi(row[0]) + 1;
	else	// No waypoints in this grid yet
		next_wp_num = 1;

	query = StringFormat("INSERT INTO grid_entries(gridid, zoneid, `number`, x, y, z, pause, heading) "
						"VALUES (%i, %i, %i, %f, %f, %f, %i, %f)",
						grid_num, zoneid, next_wp_num, position.x, position.y, position.z, pause, position.w);
	results = QueryDatabase(query);

	return createdNewGrid? grid_num: 0;
}

uint32 ZoneDatabase::GetFreeGrid(uint16 zoneid) {

	std::string query = StringFormat("SELECT max(id) FROM grid WHERE zoneid = %i", zoneid);
	auto results = QueryDatabase(query);
	if (!results.Success()) {
        return 0;
	}

	if (results.RowCount() != 1)
		return 0;

    auto row = results.begin();

	uint32 freeGridID = atoi(row[0]) + 1;

    return freeGridID;
}

int ZoneDatabase::GetHighestWaypoint(uint32 zoneid, uint32 gridid) {

	std::string query = StringFormat("SELECT COALESCE(MAX(number), 0) FROM grid_entries "
									"WHERE zoneid = %i AND gridid = %i", zoneid, gridid);
	auto results = QueryDatabase(query);
	if (!results.Success()) {
        return 0;
	}

	if (results.RowCount() != 1)
		return 0;

	auto row = results.begin();
	return atoi(row[0]);
}

void NPC::SaveGuardSpotCharm()
{
	m_GuardPointSaved = m_GuardPoint;
}

void NPC::RestoreGuardSpotCharm()
{
	m_GuardPoint = m_GuardPointSaved;
}
