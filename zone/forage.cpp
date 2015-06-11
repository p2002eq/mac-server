/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2002 EQEMu Development Team (http://eqemu.org)

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
#include "../common/eqemu_logsys.h"
#include "../common/misc_functions.h"
#include "../common/rulesys.h"
#include "../common/string_util.h"

#include "entity.h"
#include "forage.h"
#include "npc.h"
#include "quest_parser_collection.h"
#include "string_ids.h"
#include "titles.h"
#include "water_map.h"
#include "zonedb.h"

#include <iostream>

#ifdef _WINDOWS
#define snprintf	_snprintf
#endif

struct NPCType;

//max number of items which can be in the foraging table
//for a given zone.
#define FORAGE_ITEM_LIMIT 50

uint32 ZoneDatabase::GetZoneForage(uint32 ZoneID, uint8 skill) {

	uint32 item[FORAGE_ITEM_LIMIT];
	uint32 chance[FORAGE_ITEM_LIMIT];
	uint32 ret;

	for (int c=0; c < FORAGE_ITEM_LIMIT; c++) {
		item[c] = 0;
	}

	uint32 chancepool = 0;
    std::string query = StringFormat("SELECT itemid, chance FROM "
                                    "forage WHERE zoneid = '%i' and level <= '%i' "
                                    "LIMIT %i", ZoneID, skill, FORAGE_ITEM_LIMIT);
    auto results = QueryDatabase(query);
	if (!results.Success()) {
		return 0;
	}

	uint8 index = 0;
    for (auto row = results.begin(); row != results.end(); ++row, ++index) {
        if (index >= FORAGE_ITEM_LIMIT)
            break;

        item[index] = atoi(row[0]);
        chance[index] = atoi(row[1]) + chancepool;
		Log.Out(Logs::General, Logs::General, "Possible Forage: %d with a %d chance total to %d chancepool", item[index], chance[index] - chancepool, chance[index]);
        chancepool = chance[index];
    }

	if(chancepool == 0 || index < 1)
		return 0;

	if(index == 1) {
		return item[0];
	}

	ret = 0;

	uint32 rindex = zone->random.Int(1, chancepool);

	for(int i = 0; i < index; i++) {
		if(rindex <= chance[i]) {
			ret = item[i];
			break;
		}
	}

	return ret;
}

uint32 ZoneDatabase::GetZoneFishing(uint32 ZoneID, uint8 skill, uint32 &npc_id, uint8 &npc_chance)
{
	uint32 item[50];
	uint32 chance[50];
	uint32 npc_ids[50];
	uint32 npc_chances[50];
	uint32 chancepool = 0;
	uint32 ret = 0;

	for (int c=0; c<50; c++) {
		item[c]=0;
		chance[c]=0;
	}

    std::string query = StringFormat("SELECT itemid, chance, npc_id, npc_chance "
                                    "FROM fishing WHERE (zoneid = '%i' || zoneid = 0) AND skill_level <= '%i'",
                                    ZoneID, skill);
    auto results = QueryDatabase(query);
    if (!results.Success()) {
		return 0;
    }

    uint8 index = 0;
    for (auto row = results.begin(); row != results.end(); ++row, ++index) {
        if (index >= 50)
            break;

        item[index] = atoi(row[0]);
        chance[index] = atoi(row[1])+chancepool;
        chancepool = chance[index];

        npc_ids[index] = atoi(row[2]);
        npc_chances[index] = atoi(row[3]);
    }

	npc_id = 0;
	npc_chance = 0;
	if (index <= 0)
        return 0;

    uint32 random = zone->random.Int(1, chancepool);
    for (int i = 0; i < index; i++)
    {
        if (random > chance[i])
            continue;

        ret = item[i];
        npc_id = npc_ids[i];
        npc_chance = npc_chances[i];
        break;
    }

	return ret;
}

//we need this function to immediately determine, after we receive OP_Fishing, if we can even try to fish, otherwise we have to wait a while to get the failure
bool Client::CanFish() {

	if(fishing_timer.Enabled())
	{
		Message_StringID(CC_Default, ALREADY_FISHING);	//You are already fishing!
		return false;
	}

	if(m_inv.GetItem(MainCursor))
	{
		Message_StringID(CC_User_Skills, FISHING_HANDS_FULL);
		return false;
	}

	//make sure we still have a fishing pole on:
	const ItemInst* Pole = m_inv[MainPrimary];
	int32 bslot = m_inv.HasItemByUse(ItemTypeFishingBait, 1, invWhereWorn|invWherePersonal);
	const ItemInst* Bait = nullptr;
	if (bslot != INVALID_INDEX)
		Bait = m_inv.GetItem(bslot);

	if(!Pole || !Pole->IsType(ItemClassCommon) || Pole->GetItem()->ItemType != ItemTypeFishingPole) {
		if (m_inv.HasItemByUse(ItemTypeFishingPole, 1, invWhereWorn|invWherePersonal|invWhereBank|invWhereTrading|invWhereCursor))	//We have a fishing pole somewhere, just not equipped
			Message_StringID(MT_Skills, FISHING_EQUIP_POLE);	//You need to put your fishing pole in your primary hand.
		else	//We don't have a fishing pole anywhere
			Message_StringID(MT_Skills, FISHING_NO_POLE);	//You can't fish without a fishing pole, go buy one.
		return false;
	}

	if (!Bait || !Bait->IsType(ItemClassCommon) || Bait->GetItem()->ItemType != ItemTypeFishingBait) {
		Message_StringID(MT_Skills, FISHING_NO_BAIT);	//You can't fish without fishing bait, go buy some.
		return false;
	}

	if(zone->zonemap != nullptr && zone->watermap != nullptr && RuleB(Watermap, CheckForWaterWhenFishing)) {

		glm::vec3 rodPosition;
		// Tweak Rod and LineLength if required
		const float RodLength = RuleR(Watermap, FishingRodLength);
		const float LineLength = RuleR(Watermap, FishingLineLength);
		const float LineExtension = RuleR(Watermap, FishingLineExtension);
		int HeadingDegrees;

		HeadingDegrees = (int) ((GetHeading()*360)/256);
		HeadingDegrees = HeadingDegrees % 360;

		rodPosition.x = m_Position.x + RodLength * sin(HeadingDegrees * M_PI/180.0f);
		rodPosition.y = m_Position.y + RodLength * cos(HeadingDegrees * M_PI/180.0f);

		glm::vec3 dest;
		dest.x = rodPosition.x;
		dest.y = rodPosition.y;
		dest.z = m_Position.z;
		rodPosition.z = dest.z - LineLength;

		bool in_lava = zone->watermap->InLava(rodPosition);
		bool in_water = zone->watermap->InWater(rodPosition) || zone->watermap->InVWater(rodPosition);
		Log.Out(Logs::General, Logs::Maps, "Fishing Rod is at %4.3f, %4.3f, %4.3f (dest.z: %4.3f), InWater says %d, InLava says %d Region is: %d RodLength: %f LineLength: %f", rodPosition.x, rodPosition.y, rodPosition.z, dest.z, in_water, in_lava, zone->watermap->ReturnRegionType(rodPosition), RodLength, LineLength);
		if (in_lava) {
			Message_StringID(MT_Skills, FISHING_LAVA);	//Trying to catch a fire elemental or something?
			return false;
		}
		if(!in_water) {
			// Our line may be too long, and we are going underworld. Reel our line in, and try again.
			rodPosition.z = dest.z - (LineLength/2);
			in_water = zone->watermap->InWater(rodPosition) || zone->watermap->InVWater(rodPosition);
			Log.Out(Logs::General, Logs::Maps, "Trying again with new Z %4.3f InWater now says %d", rodPosition.z, in_water);

			if(!in_water)
			{
				// Our line may be too short. Reel our line out using extension, and try again
				rodPosition.z = dest.z - (LineLength+LineExtension);
				in_water = zone->watermap->InWater(rodPosition) || zone->watermap->InVWater(rodPosition);
				Log.Out(Logs::General, Logs::Maps, "Trying again with new Z %4.3f InWater now says %d", rodPosition.z, in_water);

				if(!in_water) 
				{
					Message_StringID(MT_Skills, FISHING_LAND);	//Trying to catch land sharks perhaps?
					return false;
				}
			}
		}
	}
	return true;
}

void Client::GoFish()
{

	fishing_timer.Disable();

	//we're doing this a second time (1st in Client::Handle_OP_Fishing) to make sure that, between when we started fishing & now, we're still able to fish (in case we move, change equip, etc)
	if (!CanFish())	//if we can't fish here, we don't need to bother with the rest
		return;

	//multiple entries yeilds higher probability of dropping...
	uint32 common_fish_ids[MAX_COMMON_FISH_IDS] = {
		1038, // Tattered Cloth Sandals
		1038, // Tattered Cloth Sandals
		1038, // Tattered Cloth Sandals
		13019, // Fresh Fish
		13076, // Fish Scales
		13076, // Fish Scales
		7007, // Rusty Dagger
		7007, // Rusty Dagger
		7007 // Rusty Dagger

	};

	//success formula is not researched at all

	int fishing_skill = GetSkill(SkillFishing);	//will take into account skill bonuses on pole & bait

	//make sure we still have a fishing pole on:
	int32 bslot = m_inv.HasItemByUse(ItemTypeFishingBait, 1, invWhereWorn|invWherePersonal);
	const ItemInst* Bait = nullptr;
	if (bslot != INVALID_INDEX)
		Bait = m_inv.GetItem(bslot);

	//if the bait isnt equipped, need to add its skill bonus
	if(bslot >= EmuConstants::GENERAL_BEGIN && Bait != nullptr && Bait->GetItem()->SkillModType == SkillFishing) {
		fishing_skill += Bait->GetItem()->SkillModValue;
	}

	if (fishing_skill > 100)
	{
		fishing_skill = 100+((fishing_skill-100)/2);
	}

	if (zone->random.Int(0,175) < fishing_skill) {
		uint32 food_id = 0;

		if (zone->random.Int(0, 299) <= fishing_skill ) {
			uint32 npc_id = 0;
			uint8 npc_chance = 0;
			food_id = database.GetZoneFishing(m_pp.zone_id, fishing_skill, npc_id, npc_chance);

			//check for add NPC
			if(npc_chance > 0 && npc_id) {
				if(npc_chance < zone->random.Int(0, 99)) {
					const NPCType* tmp = database.GetNPCType(npc_id);
					if(tmp != nullptr) {
                        auto positionNPC = GetPosition();
                        positionNPC.x = positionNPC.x + 3;
						NPC* npc = new NPC(tmp, nullptr, positionNPC, FlyMode3);
						npc->AddLootTable();

						npc->AddToHateList(this, 1, 0, false);	//no help yelling

						entity_list.AddNPC(npc);

						Message(MT_Emote, "You fish up a little more than you bargained for...");
					}
				}
			}
		}

		//consume bait, should we always consume bait on success?
		DeleteItemInInventory(bslot, 1, true);	//do we need client update?

		if(food_id == 0) {
			int index = zone->random.Int(0, MAX_COMMON_FISH_IDS-1);
			food_id = common_fish_ids[index];
		}

		const Item_Struct* food_item = database.GetItem(food_id);

		ItemInst* inst = database.CreateItem(food_item, 1);
		if(inst != nullptr) {
			if(CheckLoreConflict(inst->GetItem()))
			{
				Message_StringID(CC_Default, DUP_LORE);
				safe_delete(inst);
			}
			else
			{
				Message_StringID(MT_Skills, FISHING_SUCCESS);
				PushItemOnCursor(*inst);
				SendItemPacket(MainCursor, inst, ItemPacketSummonItem);

				safe_delete(inst);
				inst = m_inv.GetItem(MainCursor);
			}

			if(inst) {
				std::vector<EQEmu::Any> args;
				args.push_back(inst);
				parse->EventPlayer(EVENT_FISH_SUCCESS, this, "", inst->GetID(), &args);
			}
		}
	}
	else
	{
		//chance to use bait when you dont catch anything...
		if (zone->random.Int(0, 4) == 1) {
			DeleteItemInInventory(bslot, 1, true);	//do we need client update?
			Message_StringID(MT_Skills, FISHING_LOST_BAIT);	//You lost your bait!
		} else {
			if (zone->random.Int(0, 15) == 1)	//give about a 1 in 15 chance to spill your beer. we could make this a rule, but it doesn't really seem worth it
				//TODO: check for & consume an alcoholic beverage from inventory when this triggers, and set it as a rule that's disabled by default
				Message_StringID(MT_Skills, FISHING_SPILL_BEER);	//You spill your beer while bringing in your line.
			else
				Message_StringID(MT_Skills, FISHING_FAILED);	//You didn't catch anything.
		}

		parse->EventPlayer(EVENT_FISH_FAILURE, this, "", 0);
	}

	//chance to break fishing pole...
	uint16 break_chance = 49;
	if(fishing_skill > 49)
		break_chance = fishing_skill;
	if (zone->random.Int(0, break_chance) == 1) {
		Message_StringID(MT_Skills, FISHING_POLE_BROKE);	//Your fishing pole broke!
		DeleteItemInInventory(MainPrimary, 0, true);
	}

	if(CheckIncreaseSkill(SkillFishing, nullptr, 5))
	{
		if(title_manager.IsNewTradeSkillTitleAvailable(SkillFishing, GetRawSkill(SkillFishing)))
			NotifyNewTitlesAvailable();
	}
}

void Client::ForageItem(bool guarantee) {

	int skill_level = GetSkill(SkillForage);

	//be wary of the string ids in switch below when changing this.
	uint32 common_food_ids[MAX_COMMON_FOOD_IDS] = {
		13046, // Fruit
		13045, // Berries
		13419, // Vegetables
		13048, // Rabbit Meat
		13047, // Roots
		13044, // Pod Of Water
		13106 // Fishing Grubs
	};

	// these may need to be fine tuned, I am just guessing here
	if (guarantee || zone->random.Int(0,199) < skill_level) 
	{
		uint32 foragedfood = 0;
		uint32 stringid = FORAGE_NOEAT;

		if (RuleB(Character, ForageNeedFoodorDrink))
		{
			// If we're hungry or thirsty, we want to prefer food or water.
			if(Hungry() && Thirsty())
			{
				if(m_pp.hunger_level <= m_pp.thirst_level)
					foragedfood = 13047;
				else
					foragedfood = 13044;
			}
			// We only need food.
			else if(Hungry())
			{
				foragedfood = 13047;
			}
			// We only need water.
			else if(Thirsty())
			{
				foragedfood = 13044;
			}
		}


		if (RuleB(Character, ForageCommonFoodorDrink))
		{
			// Do a roll to check if we pull an item from the DB.
			if (zone->random.Roll(25) && foragedfood == 0)
				foragedfood = database.GetZoneForage(m_pp.zone_id, skill_level);

			//not an else in case theres no DB food
			if (foragedfood == 0) 
			{
				uint8 index = 0;
				index = zone->random.Int(0, MAX_COMMON_FOOD_IDS - 1);
				if (GetZoneID() == poknowledge)
					foragedfood = 13047; //PoK only has roots from the above array.
				else
					foragedfood = common_food_ids[index];
			}
		}
		else
		{
			if (foragedfood == 0)
				foragedfood = database.GetZoneForage(m_pp.zone_id, skill_level);
		}

		const Item_Struct* food_item = database.GetItem(foragedfood);

		if(!food_item)
		{
			Log.Out(Logs::General, Logs::Error, "nullptr returned from database.GetItem in ClientForageItem");
			return;
		}

		if(foragedfood == 13106)
			stringid = FORAGE_GRUBS;
		else
		{
			switch(food_item->ItemType) 
			{
				case ItemTypeFood:
					stringid = FORAGE_FOOD;
					break;

				case ItemTypeDrink:
					if(strstr(food_item->Name, "ater"))
						stringid = FORAGE_WATER;
					else
						stringid = FORAGE_DRINK;
					break;

				default:
					break;
			}
		}

		ItemInst* inst = database.CreateItem(food_item, 1);
		if(inst != nullptr) 
		{
			// check to make sure it isn't a foraged lore item
			if(CheckLoreConflict(inst->GetItem()))
			{
				Message_StringID(CC_Default, DUP_LORE);
				safe_delete(inst);
			}
			else
			{
				Message_StringID(MT_Skills, stringid);
				SummonItem(inst->GetID(), inst->GetCharges());
				safe_delete(inst);
				inst = m_inv.GetItem(MainCursor);
			}

			if(inst) {
				std::vector<EQEmu::Any> args;
				args.push_back(inst);
				parse->EventPlayer(EVENT_FORAGE_SUCCESS, this, "", inst->GetID(), &args);
			}
		}

		int ChanceSecondForage = aabonuses.ForageAdditionalItems + itembonuses.ForageAdditionalItems + spellbonuses.ForageAdditionalItems;
		if(!guarantee && zone->random.Roll(ChanceSecondForage))
		{
			Message_StringID(MT_Skills, FORAGE_MASTERY);
			ForageItem(true);
		}

	} 
	else
	{
		Message_StringID(MT_Skills, FORAGE_FAILED);
		parse->EventPlayer(EVENT_FORAGE_FAILURE, this, "", 0);
	}

	CheckIncreaseSkill(SkillForage, nullptr, 5);

}

