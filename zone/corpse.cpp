/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2003 EQEMu Development Team (http://eqemulator.net)

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
/*
New class for handling corpses and everything associated with them.
Child of the Mob class.
-Quagmire
*/

#ifdef _WINDOWS
    #define snprintf	_snprintf
	#define vsnprintf	_vsnprintf
    #define strncasecmp	_strnicmp
    #define strcasecmp	_stricmp
#endif

#include "../common/global_define.h"
#include "../common/eqemu_logsys.h"
#include "../common/rulesys.h"
#include "../common/string_util.h"

#include "client.h"
#include "corpse.h"
#include "entity.h"
#include "groups.h"
#include "mob.h"
#include "raids.h"

#ifdef BOTS
#include "bot.h"
#endif

#include "quest_parser_collection.h"
#include "string_ids.h"
#include "worldserver.h"
#include "queryserv.h"
#include <iostream>
#include "remote_call_subscribe.h"


extern EntityList entity_list;
extern Zone* zone;
extern WorldServer worldserver;
extern QueryServ* QServ;
extern npcDecayTimes_Struct npcCorpseDecayTimes[100];

void Corpse::SendEndLootErrorPacket(Client* client) {
	EQApplicationPacket* outapp = new EQApplicationPacket(OP_LootComplete, 0);
	client->QueuePacket(outapp);
	safe_delete(outapp);
}

void Corpse::SendLootReqErrorPacket(Client* client, uint8 response) {
	EQApplicationPacket* outapp = new EQApplicationPacket(OP_MoneyOnCorpse, sizeof(moneyOnCorpseStruct));
	moneyOnCorpseStruct* d = (moneyOnCorpseStruct*)outapp->pBuffer;
	d->response = response;
	d->unknown1 = 0x5a;
	d->unknown2 = 0x40;
	client->QueuePacket(outapp);
	safe_delete(outapp);
}

Corpse* Corpse::LoadCharacterCorpseEntity(uint32 in_dbid, uint32 in_charid, std::string in_charname, const glm::vec4& position, std::string time_of_death, bool rezzed, bool was_at_graveyard){
	uint32 item_count = database.GetCharacterCorpseItemCount(in_dbid);
	char *buffer = new char[sizeof(PlayerCorpse_Struct) + (item_count * sizeof(player_lootitem::ServerLootItem_Struct))];
	PlayerCorpse_Struct *pcs = (PlayerCorpse_Struct*)buffer;
	database.LoadCharacterCorpseData(in_dbid, pcs);

	/* Load Items */ 
	ItemList itemlist;
	ServerLootItem_Struct* tmp = 0;
	for (unsigned int i = 0; i < pcs->itemcount; i++) {
		tmp = new ServerLootItem_Struct;
		memcpy(tmp, &pcs->items[i], sizeof(player_lootitem::ServerLootItem_Struct));
		itemlist.push_back(tmp);
	}

	/* Create Corpse Entity */
	Corpse* pc = new Corpse(
		in_dbid,			   // uint32 in_dbid
		in_charid,			   // uint32 in_charid
		in_charname.c_str(),   // char* in_charname
		&itemlist,			   // ItemList* in_itemlist
		pcs->copper,		   // uint32 in_copper
		pcs->silver,		   // uint32 in_silver
		pcs->gold,			   // uint32 in_gold
		pcs->plat,			   // uint32 in_plat
		position,
		pcs->size,			   // float in_size
		pcs->gender,		   // uint8 in_gender
		pcs->race,			   // uint16 in_race
		pcs->class_,		   // uint8 in_class
		pcs->deity,			   // uint8 in_deity
		pcs->level,			   // uint8 in_level
		pcs->texture,		   // uint8 in_texture
		pcs->helmtexture,	   // uint8 in_helmtexture
		pcs->exp,			   // uint32 in_rezexp
		pcs->gmexp,			   // uint32 in_gmrezexp
		pcs->killedby,		   // uint8 in_killedby
		pcs->rezzable,		   // bool rezzable
		pcs->rez_time,		   // uint32 rez_time
		was_at_graveyard	   // bool wasAtGraveyard
	);
	if (pcs->locked){
		pc->Lock();
	}

	/* Load Item Tints */
	pc->item_tint[0].color = pcs->item_tint[0].color;
	pc->item_tint[1].color = pcs->item_tint[1].color;
	pc->item_tint[2].color = pcs->item_tint[2].color;
	pc->item_tint[3].color = pcs->item_tint[3].color;
	pc->item_tint[4].color = pcs->item_tint[4].color;
	pc->item_tint[5].color = pcs->item_tint[5].color;
	pc->item_tint[6].color = pcs->item_tint[6].color;
	pc->item_tint[7].color = pcs->item_tint[7].color;
	pc->item_tint[8].color = pcs->item_tint[8].color; 

	/* Load Physical Appearance */
	pc->haircolor = pcs->haircolor;
	pc->beardcolor = pcs->beardcolor;
	pc->eyecolor1 = pcs->eyecolor1;
	pc->eyecolor2 = pcs->eyecolor2;
	pc->hairstyle = pcs->hairstyle;
	pc->luclinface = pcs->face;
	pc->beard = pcs->beard;
	pc->IsRezzed(rezzed);
	pc->become_npc = false;

	pc->m_Light.Level.Innate = pc->m_Light.Type.Innate = 0;
	pc->UpdateEquipmentLight(); // itemlist populated above..need to determine actual values
	pc->m_Light.Level.Spell = pc->m_Light.Type.Spell = 0;

	safe_delete_array(pcs);

	return pc;
}

Corpse::Corpse(NPC* in_npc, ItemList* in_itemlist, uint32 in_npctypeid, const NPCType** in_npctypedata, uint32 in_decaytime)
	: Mob("Unnamed_Corpse",		// const char* in_name,
	"",							// const char* in_lastname,
	0,							// int32		in_cur_hp,
	0,							// int32		in_max_hp,
	in_npc->GetGender(),		// uint8		in_gender,
	in_npc->GetRace(),			// uint16		in_race,
	in_npc->GetClass(),			// uint8		in_class,
	BT_Humanoid,				// bodyType	in_bodytype,
	in_npc->GetDeity(),			// uint8		in_deity,
	in_npc->GetLevel(),			// uint8		in_level,
	in_npc->GetNPCTypeID(),		// uint32		in_npctype_id,
	in_npc->GetSize(),			// float		in_size,
	0,							// float		in_runspeed,
	in_npc->GetPosition(),		// float		in_position
	in_npc->GetInnateLightType(),	// uint8		in_light,
	in_npc->GetTexture(),		// uint8		in_texture,
	in_npc->GetHelmTexture(),	// uint8		in_helmtexture,
	0,							// uint16		in_ac,
	0,							// uint16		in_atk,
	0,							// uint16		in_str,
	0,							// uint16		in_sta,
	0,							// uint16		in_dex,
	0,							// uint16		in_agi,
	0,							// uint16		in_int,
	0,							// uint16		in_wis,
	0,							// uint16		in_cha,
	0,							// uint8		in_haircolor,
	0,							// uint8		in_beardcolor,
	0,							// uint8		in_eyecolor1, // the eyecolors always seem to be the same, maybe left and right eye?
	0,							// uint8		in_eyecolor2,
	0,							// uint8		in_hairstyle,
	0,							// uint8		in_luclinface,
	0,							// uint8		in_beard,
	0,							// uint32		in_armor_tint[_MaterialCount], 
	0xff,						// uint8		in_aa_title,
	0,							// uint8		in_see_invis, // see through invis/ivu
	0,							// uint8		in_see_invis_undead,
	0,							// uint8		in_see_hide,
	0,							// uint8		in_see_improved_hide,
	0,							// int32		in_hp_regen,
	0,							// int32		in_mana_regen,
	0,							// uint8		in_qglobal,
	0,							// uint8		in_maxlevel,
	0,							// uint32		in_scalerate
	0,							// uint8		in_armtexture,
	0,							// uint8		in_bracertexture,
	0,							// uint8		in_handtexture,
	0,							// uint8		in_legtexture,
	0,							// uint8		in_feettexture,
	0							// uint8		in_chesttexture,
),									  
	corpse_decay_timer(in_decaytime),
	corpse_rez_timer(0),
	corpse_delay_timer(RuleI(NPC, CorpseUnlockTimer)),
	corpse_graveyard_timer(0),
	loot_cooldown_timer(10)
{
	corpse_graveyard_timer.Disable();

	memset(item_tint, 0, sizeof(item_tint));

	is_corpse_changed = false;
	is_player_corpse = false;
	is_locked = false;
	being_looted_by = 0xFFFFFFFF;
	if (in_itemlist) {
		itemlist = *in_itemlist;
		in_itemlist->clear();
	}

	SetCash(in_npc->GetCopper(), in_npc->GetSilver(), in_npc->GetGold(), in_npc->GetPlatinum());

	npctype_id = in_npctypeid;
	SetPlayerKillItemID(0);
	char_id = 0;
	corpse_db_id = 0;
	player_corpse_depop = false;
	strcpy(corpse_name, in_npc->GetName());
	strcpy(name, in_npc->GetName());
	
	for (int count = 0; count < 100; count++) {
		if ((level >= npcCorpseDecayTimes[count].minlvl) && (level <= npcCorpseDecayTimes[count].maxlvl)) {
			corpse_decay_timer.SetTimer(npcCorpseDecayTimes[count].seconds * 1000);
			break;
		}
	}
	if(IsEmpty()) {
		corpse_decay_timer.SetTimer(RuleI(NPC, EmptyNPCCorpseDecayTimeMS) + 1000);
	}

	
	if(in_npc->HasPrivateCorpse()) {
		corpse_delay_timer.SetTimer(corpse_decay_timer.GetRemainingTime() + 1000);
	}

	for (int i = 0; i < MAX_LOOTERS; i++){
		allowed_looters[i] = 0;
	}
	this->rez_experience = 0;
	this->gm_rez_experience = 0;
	UpdateEquipmentLight();
	UpdateActiveLight();
}

Corpse::Corpse(Client* client, int32 in_rezexp, uint8 in_killedby) : Mob (
	"Unnamed_Corpse",				  // const char*	in_name,
	"",								  // const char*	in_lastname,
	0,								  // int32		in_cur_hp,
	0,								  // int32		in_max_hp,
	client->GetGender(),			  // uint8		in_gender,
	client->GetRace(),				  // uint16		in_race,
	client->GetClass(),				  // uint8		in_class,
	BT_Humanoid,					  // bodyType	in_bodytype,
	client->GetDeity(),				  // uint8		in_deity,
	client->GetLevel(),				  // uint8		in_level,
	0,								  // uint32		in_npctype_id,
	client->GetSize(),				  // float		in_size,
	0,								  // float		in_runspeed,
	client->GetPosition(),
	client->GetInnateLightType(),	  // uint8		in_light, - verified for client innate_light value
	client->GetTexture(),			  // uint8		in_texture,
	client->GetHelmTexture(),		  // uint8		in_helmtexture,
	0,								  // uint16		in_ac,
	0,								  // uint16		in_atk,
	0,								  // uint16		in_str,
	0,								  // uint16		in_sta,
	0,								  // uint16		in_dex,
	0,								  // uint16		in_agi,
	0,								  // uint16		in_int,
	0,								  // uint16		in_wis,
	0,								  // uint16		in_cha,
	client->GetPP().haircolor,		  // uint8		in_haircolor,
	client->GetPP().beardcolor,		  // uint8		in_beardcolor,
	client->GetPP().eyecolor1,		  // uint8		in_eyecolor1, // the eye colors always seem to be the same, maybe left and right eye?
	client->GetPP().eyecolor2,		  // uint8		in_eyecolor2,
	client->GetPP().hairstyle,		  // uint8		in_hairstyle,
	client->GetPP().face,			  // uint8		in_luclinface,
	client->GetPP().beard,			  // uint8		in_beard,
	0,								  // uint32		in_armor_tint[_MaterialCount],
	0xff,							  // uint8		in_aa_title,
	0,								  // uint8		in_see_invis, // see through invis
	0,								  // uint8		in_see_invis_undead, // see through invis vs. un dead
	0,								  // uint8		in_see_hide,
	0,								  // uint8		in_see_improved_hide,
	0,								  // int32		in_hp_regen,
	0,								  // int32		in_mana_regen,
	0,								  // uint8		in_qglobal,
	0,								  // uint8		in_maxlevel,
	0,								  // uint32		in_scalerate
	0,								  // uint8		in_armtexture,
	0,								  // uint8		in_bracertexture,
	0,								  // uint8		in_handtexture,
	0,								  // uint8		in_legtexture,
	0,								  // uint8		in_feettexture,
	0								  // uint8		in_chesttexture,
	),
	corpse_decay_timer(RuleI(Character, EmptyCorpseDecayTimeMS)),
	corpse_rez_timer(RuleI(Character, CorpseResTimeMS)),
	corpse_delay_timer(RuleI(NPC, CorpseUnlockTimer)),
	corpse_graveyard_timer(RuleI(Zone, GraveyardTimeMS)),
	loot_cooldown_timer(10)
{
	int i;

	PlayerProfile_Struct *pp = &client->GetPP();
	ItemInst *item;

	/* Check if Zone has Graveyard First */
	if(!zone->HasGraveyard()) {
		corpse_graveyard_timer.Disable();
	}

	memset(item_tint, 0, sizeof(item_tint));

	for (i = 0; i < MAX_LOOTERS; i++){
		allowed_looters[i] = 0;
	}

	is_corpse_changed		= true;
	rez_experience			= in_rezexp;
	gm_rez_experience		= in_rezexp;
	is_player_corpse	= true;
	is_locked			= false;
	being_looted_by	= 0xFFFFFFFF;
	char_id			= client->CharacterID();
	corpse_db_id	= 0;
	player_corpse_depop			= false;
	copper			= 0;
	silver			= 0;
	gold			= 0;
	platinum		= 0;
	killedby		= in_killedby;
	rezzable		= true;
	rez_time		= 0;
	is_owner_online = false; // We can't assume they are online just because they just died. Perhaps they rage smashed their router.

	owner_online_timer.SetTimer(RuleI(Character, CorpseOwnerOnlineTimeMS));

	corpse_rez_timer.Disable();
	SetRezTimer(true);

	strcpy(corpse_name, pp->name);
	strcpy(name, pp->name);

	/* become_npc was not being initialized which led to some pretty funky things with newly created corpses */
	become_npc = false;

	SetPlayerKillItemID(0);

	/* Check Rule to see if we can leave corpses */
	if(!RuleB(Character, LeaveNakedCorpses) ||
		RuleB(Character, LeaveCorpses) &&
		GetLevel() >= RuleI(Character, DeathItemLossLevel)) {
		// cash
		SetCash(pp->copper, pp->silver, pp->gold, pp->platinum);
		pp->copper = 0;
		pp->silver = 0;
		pp->gold = 0;
		pp->platinum = 0;

		// get their tints
		memcpy(item_tint, &client->GetPP().item_tint, sizeof(item_tint));

		// worn + inventory + cursor
		// Todo: Handle soulbound bags.
		std::list<uint32> removed_list;
		bool cursor = false;
		for (i = MAIN_BEGIN; i < EmuConstants::MAP_POSSESSIONS_SIZE; i++) {
			item = client->GetInv().GetItem(i);
			if(item)
			{
				if ((!client->IsBecomeNPC() && (!item->GetItem()->Soulbound)) || 
					(client->IsBecomeNPC() && !item->GetItem()->NoRent && !item->GetItem()->Soulbound)) {
						std::list<uint32> slot_list = MoveItemToCorpse(client, item, i);
						removed_list.merge(slot_list);
				}
				else if(item->GetItem()->Soulbound)
					Log.Out(Logs::Moderate, Logs::Inventory, "Skipping Soulbound item %s in slot %d", item->GetItem()->Name, i);
			}

		}

		database.TransactionBegin();
		if (removed_list.size() != 0) {
			std::stringstream ss("");
			ss << "DELETE FROM character_inventory WHERE id=" << client->CharacterID();
			ss << " AND (";
			std::list<uint32>::const_iterator iter = removed_list.begin();
			bool first = true;
			while (iter != removed_list.end()) {
				if (first) {
					first = false;
				}
				else {
					ss << " OR ";
				}
				ss << "slotid=" << (*iter);
				++iter;
			}
			ss << ")";
			database.QueryDatabase(ss.str().c_str());
		}

		auto start = client->GetInv().cursor_cbegin();
		auto finish = client->GetInv().cursor_cend();
		// If soulbound items were moved to the cursor, they need to be moved to a primary inventory slot.
		database.SaveSoulboundItems(client, start, finish);

		client->CalcBonuses(); // will only affect offline profile viewing of dead characters..unneeded overhead
		client->Save();

		IsRezzed(false);
		Save();
		database.TransactionCommit();

		if(!IsEmpty()) {
			corpse_decay_timer.SetTimer(RuleI(Character, CorpseDecayTimeMS));
		}
		return;
	} //end "not leaving naked corpses"

	UpdateEquipmentLight();
	UpdateActiveLight();

	IsRezzed(false);
	Save();
}

std::list<uint32> Corpse::MoveItemToCorpse(Client *client, ItemInst *item, int16 equipslot)
{
	int bagindex;
	int16 interior_slot;
	ItemInst *interior_item;
	std::list<uint32> returnlist;

	AddItem(item->GetItem()->ID, item->GetCharges(), equipslot);
	returnlist.push_back(equipslot);

	// Qualified bag slot iterations. processing bag slots that don't exist is probably not a good idea.
	if (item->IsType(ItemClassContainer) && ((equipslot >= EmuConstants::GENERAL_BEGIN && equipslot <= EmuConstants::GENERAL_END))){ // Limit the bag check to inventory and cursor slots.
		for (bagindex = SUB_BEGIN; bagindex <= EmuConstants::ITEM_CONTAINER_SIZE; bagindex++) {
			// For empty bags in cursor queue, slot was previously being resolved as SLOT_INVALID (-1)
			interior_slot = Inventory::CalcSlotId(equipslot, bagindex);
			interior_item = client->GetInv().GetItem(interior_slot);

			if (interior_item && !interior_item->GetItem()->Soulbound) {
				AddItem(interior_item->GetItem()->ID, interior_item->GetCharges(), interior_slot);
				returnlist.push_back(Inventory::CalcSlotId(equipslot, bagindex));
				client->DeleteItemInInventory(interior_slot, 0, true, false);
			}
			else if(interior_item && interior_item->GetItem()->Soulbound)
			{
				client->PushItemOnCursor(*interior_item, true); // Push to cursor for now, since parent bag is about to be deleted.
				client->DeleteItemInInventory(interior_slot);
				Log.Out(Logs::Moderate, Logs::Inventory, "Skipping Soulbound item %s in slot %d", interior_item->GetItem()->Name, interior_slot);
			}
		}
	}
	client->DeleteItemInInventory(equipslot, 0, true, false);
	return returnlist;
}

/* Called from Database Load */

Corpse::Corpse(uint32 in_dbid, uint32 in_charid, const char* in_charname, ItemList* in_itemlist, uint32 in_copper, uint32 in_silver, uint32 in_gold, uint32 in_plat, const glm::vec4& position, float in_size, uint8 in_gender, uint16 in_race, uint8 in_class, uint8 in_deity, uint8 in_level, uint8 in_texture, uint8 in_helmtexture, uint32 in_rezexp, uint32 in_gmrezexp, uint8 in_killedby, bool in_rezzable, uint32 in_rez_time, bool wasAtGraveyard)
	: Mob("Unnamed_Corpse", // const char* in_name,
	"",						// const char* in_lastname,
	0,						// int32		in_cur_hp,
	0,						// int32		in_max_hp,
	in_gender,				// uint8		in_gender,
	in_race,				// uint16		in_race,
	in_class,				// uint8		in_class,
	BT_Humanoid,			// bodyType	in_bodytype,
	in_deity,				// uint8		in_deity,
	in_level,				// uint8		in_level,
	0,						// uint32		in_npctype_id,
	in_size,				// float		in_size,
	0,						// float		in_runspeed,
	position,
	0,						// uint8		in_light,
	in_texture,				// uint8		in_texture,
	in_helmtexture,			// uint8		in_helmtexture,
	0,						// uint16		in_ac,
	0,						// uint16		in_atk,
	0,						// uint16		in_str,
	0,						// uint16		in_sta,
	0,						// uint16		in_dex,
	0,						// uint16		in_agi,
	0,						// uint16		in_int,
	0,						// uint16		in_wis,
	0,						// uint16		in_cha,
	0,						// uint8		in_haircolor,
	0,						// uint8		in_beardcolor,
	0,						// uint8		in_eyecolor1, // the eyecolors always seem to be the same, maybe left and right eye?
	0,						// uint8		in_eyecolor2,
	0,						// uint8		in_hairstyle,
	0,						// uint8		in_luclinface,
	0,						// uint8		in_beard,
	0,						// uint32		in_armor_tint[_MaterialCount], 
	0xff,					// uint8		in_aa_title,
	0,						// uint8		in_see_invis, // see through invis/ivu
	0,						// uint8		in_see_invis_undead,
	0,						// uint8		in_see_hide,
	0,						// uint8		in_see_improved_hide,
	0,						// int32		in_hp_regen,
	0,						// int32		in_mana_regen,
	0,						// uint8		in_qglobal,
	0,						// uint8		in_maxlevel,
	0,						// uint32		in_scalerate
	0,						// uint8		in_armtexture,
	0,						// uint8		in_bracertexture,
	0,						// uint8		in_handtexture,
	0,						// uint8		in_legtexture,
	0,						// uint8		in_feettexture,
	0						// uint8		in_chesttexture,
	),						
	corpse_decay_timer(RuleI(Character, EmptyCorpseDecayTimeMS)),
	corpse_rez_timer(RuleI(Character, CorpseResTimeMS)),
	corpse_delay_timer(RuleI(NPC, CorpseUnlockTimer)),
	corpse_graveyard_timer(RuleI(Zone, GraveyardTimeMS)),
	loot_cooldown_timer(10)
{
	// The timer needs to know if the corpse has items or not before we actually apply the items
	// to the corpse. The corpse seems to poof shortly after the timer is applied if it is done so
	// after items are loaded.
	bool empty = true;
	if (!in_itemlist->empty() || in_copper != 0 || in_silver != 0 || in_gold != 0 || in_plat != 0)
		empty = false;

	LoadPlayerCorpseDecayTime(in_dbid, empty);

	if (!zone->HasGraveyard() || wasAtGraveyard){
		corpse_graveyard_timer.Disable();
	}

	memset(item_tint, 0, sizeof(item_tint));

	is_corpse_changed = false;
	is_player_corpse = true;
	is_locked = false;
	being_looted_by = 0xFFFFFFFF;
	corpse_db_id = in_dbid;
	player_corpse_depop = false;
	char_id = in_charid;
	itemlist = *in_itemlist;
	in_itemlist->clear();

	strcpy(corpse_name, in_charname);
	strcpy(name, in_charname);

	this->copper = in_copper;
	this->silver = in_silver;
	this->gold = in_gold;
	this->platinum = in_plat;

	rez_experience = in_rezexp;
	gm_rez_experience = in_gmrezexp;
	killedby = in_killedby;
	rezzable = in_rezzable;
	rez_time = in_rez_time;
	is_owner_online = false;

	owner_online_timer.SetTimer(RuleI(Character, CorpseOwnerOnlineTimeMS));

	corpse_rez_timer.Disable();
	SetRezTimer();

	for (int i = 0; i < MAX_LOOTERS; i++){
		allowed_looters[i] = 0;
	}
	SetPlayerKillItemID(0);

	UpdateEquipmentLight();
	m_Light.Level.Spell = m_Light.Type.Spell = 0;
	UpdateActiveLight();
}

Corpse::~Corpse() {
	if (is_player_corpse && !(player_corpse_depop && corpse_db_id == 0)) {
		Save();
	}
	ItemList::iterator cur, end;
	cur = itemlist.begin();
	end = itemlist.end();
	for (; cur != end; ++cur) {
		ServerLootItem_Struct* item = *cur;
		safe_delete(item);
	}
	itemlist.clear();
}

/*
this needs to be called AFTER the entity_id is set
the client does this too, so it's unchangeable
*/
void Corpse::CalcCorpseName() {
	EntityList::RemoveNumbers(name);
	char tmp[64];
	if (is_player_corpse){
		snprintf(tmp, sizeof(tmp), "'s corpse%d", GetID());
	}
	else{
		snprintf(tmp, sizeof(tmp), "`s_corpse%d", GetID());
	}
	name[(sizeof(name) - 1) - strlen(tmp)] = 0;
	strcat(name, tmp);
}

bool Corpse::Save() {
	if (!is_player_corpse)
		return true;
	if (!is_corpse_changed)
		return true;

	uint32 tmp = this->CountItems();
	uint32 tmpsize = sizeof(PlayerCorpse_Struct) + (tmp * sizeof(player_lootitem::ServerLootItem_Struct));

	PlayerCorpse_Struct* dbpc = (PlayerCorpse_Struct*) new uchar[tmpsize];
	memset(dbpc, 0, tmpsize);
	dbpc->itemcount = tmp;
	dbpc->size = this->size;
	dbpc->locked = is_locked;
	dbpc->copper = this->copper;
	dbpc->silver = this->silver;
	dbpc->gold = this->gold;
	dbpc->plat = this->platinum;
	dbpc->race = this->race;
	dbpc->class_ = class_;
	dbpc->gender = gender;
	dbpc->deity = deity;
	dbpc->level = level;
	dbpc->texture = this->texture;
	dbpc->helmtexture = this->helmtexture;
	dbpc->exp = rez_experience;
	dbpc->gmexp = gm_rez_experience;
	dbpc->killedby = killedby;
	dbpc->rezzable = rezzable;
	dbpc->rez_time = rez_time;

	memcpy(dbpc->item_tint, item_tint, sizeof(dbpc->item_tint));
	dbpc->haircolor = haircolor;
	dbpc->beardcolor = beardcolor;
	dbpc->eyecolor2 = eyecolor1;
	dbpc->hairstyle = hairstyle;
	dbpc->face = luclinface;
	dbpc->beard = beard;

	uint32 x = 0;
	ItemList::iterator cur, end;
	cur = itemlist.begin();
	end = itemlist.end();
	for (; cur != end; ++cur) {
		ServerLootItem_Struct* item = *cur;
		memcpy((char*)&dbpc->items[x++], (char*)item, sizeof(ServerLootItem_Struct));
	}

	/* Create New Corpse*/
	if (corpse_db_id == 0) {
		corpse_db_id = database.SaveCharacterCorpse(char_id, corpse_name, zone->GetZoneID(), zone->GetInstanceID(), dbpc, m_Position);
		if(!IsEmpty() && RuleB(Character, UsePlayerCorpseBackups))
		{
			database.SaveCharacterCorpseBackup(corpse_db_id, char_id, corpse_name, zone->GetZoneID(), zone->GetInstanceID(), dbpc, m_Position);
		}
	}
	/* Update Corpse Data */
	else{
		corpse_db_id = database.UpdateCharacterCorpse(corpse_db_id, char_id, corpse_name, zone->GetZoneID(), zone->GetInstanceID(), dbpc, m_Position, IsRezzed());
		if(!IsEmpty() && RuleB(Character, UsePlayerCorpseBackups))
		{
			database.UpdateCharacterCorpseBackup(corpse_db_id, char_id, corpse_name, zone->GetZoneID(), zone->GetInstanceID(), dbpc, m_Position, IsRezzed());
		}
	}

	safe_delete_array(dbpc);

	return true;
}

void Corpse::Delete() {
	if (IsPlayerCorpse() && corpse_db_id != 0)
		database.DeleteCharacterCorpse(corpse_db_id);

	corpse_db_id = 0;
	player_corpse_depop = true;
}

void Corpse::Bury() {
	if (IsPlayerCorpse() && corpse_db_id != 0){
		database.BuryCharacterCorpse(corpse_db_id);
	}
	corpse_db_id = 0;
	player_corpse_depop = true;
}

void Corpse::DepopNPCCorpse() {
	/* Web Interface: Corpse Depop */
	if (RemoteCallSubscriptionHandler::Instance()->IsSubscribed("Entity.Events")) {
		std::vector<std::string> params;
		params.push_back(std::to_string((long)EntityEvents::Entity_Corpse_Bury));
		params.push_back(std::to_string((long)GetID()));
		RemoteCallSubscriptionHandler::Instance()->OnEvent("Entity.Events", params);
	}

	if (IsNPCCorpse())
		player_corpse_depop = true;
}

void Corpse::DepopPlayerCorpse() {
	player_corpse_depop = true;
}

uint32 Corpse::CountItems() {
	return itemlist.size();
}

void Corpse::AddItem(uint32 itemnum, uint16 charges, int16 slot) {
	if (!database.GetItem(itemnum))
		return;

	is_corpse_changed = true;

	ServerLootItem_Struct* item = new ServerLootItem_Struct;
	
	memset(item, 0, sizeof(ServerLootItem_Struct));
	item->item_id = itemnum;
	item->charges = charges;
	item->equip_slot = slot;
	itemlist.push_back(item);

	UpdateEquipmentLight();
}

ServerLootItem_Struct* Corpse::GetItem(uint16 lootslot, ServerLootItem_Struct** bag_item_data) {
	ServerLootItem_Struct *sitem = 0, *sitem2;

	ItemList::iterator cur, end;
	cur = itemlist.begin();
	end = itemlist.end();
	for(; cur != end; ++cur) {
		if((*cur)->lootslot == lootslot) {
			sitem = *cur;
			break;
		}
	}

	if (sitem && bag_item_data && Inventory::SupportsContainers(sitem->equip_slot)) {
		int16 bagstart = Inventory::CalcSlotId(sitem->equip_slot, SUB_BEGIN);

		cur = itemlist.begin();
		end = itemlist.end();
		for (; cur != end; ++cur) {
			sitem2 = *cur;
			if (sitem2->equip_slot >= bagstart && sitem2->equip_slot < bagstart + 10) {
				bag_item_data[sitem2->equip_slot - bagstart] = sitem2;
			}
		}
	}

	return sitem;
}

uint32 Corpse::GetWornItem(int16 equipSlot) const {
	ItemList::const_iterator cur, end;
	cur = itemlist.begin();
	end = itemlist.end();
	for (; cur != end; ++cur) {
		ServerLootItem_Struct* item = *cur;
		if (item->equip_slot == equipSlot) {
			return item->item_id;
		}
	}

	return 0;
}

void Corpse::RemoveItem(uint16 lootslot) {
	if (lootslot == 0xFFFF)
		return;

	ItemList::iterator cur, end;
	cur = itemlist.begin();
	end = itemlist.end();
	for (; cur != end; ++cur) {
		ServerLootItem_Struct* sitem = *cur;
		if (sitem->lootslot == lootslot) {
			RemoveItem(sitem);
			return;
		}
	}
}

void Corpse::RemoveItem(ServerLootItem_Struct* item_data)
{
	for (auto iter = itemlist.begin(); iter != itemlist.end(); ++iter) {
		auto sitem = *iter;
		if (sitem != item_data) { continue; }

		is_corpse_changed = true;
		itemlist.erase(iter);

		uint8 material = Inventory::CalcMaterialFromSlot(sitem->equip_slot); // autos to unsigned char
		if (material != _MaterialInvalid)
			SendWearChange(material);

		UpdateEquipmentLight();
		if (UpdateActiveLight())
			SendAppearancePacket(AT_Light, GetActiveLightType());

		safe_delete(sitem);
		return;
	}
}

void Corpse::SetCash(uint32 in_copper, uint32 in_silver, uint32 in_gold, uint32 in_platinum) {
	this->copper = in_copper;
	this->silver = in_silver;
	this->gold = in_gold;
	this->platinum = in_platinum;
	is_corpse_changed = true;
}

void Corpse::RemoveCash() {
	this->copper = 0;
	this->silver = 0;
	this->gold = 0;
	this->platinum = 0;
	is_corpse_changed = true;
}

bool Corpse::IsEmpty() const {
	if (copper != 0 || silver != 0 || gold != 0 || platinum != 0)
		return false;

	return itemlist.empty();
}

bool Corpse::Process() {
	if (player_corpse_depop){
		return false;
	}

	if(owner_online_timer.Check() || rezzable) 
	{
		IsOwnerOnline();
	}

	if (corpse_delay_timer.Check()) {
		for (int i = 0; i < MAX_LOOTERS; i++){
			allowed_looters[i] = 0;
		}
		corpse_delay_timer.Disable();
		return true;
	}

	if (corpse_graveyard_timer.Check()) {
		if (zone->HasGraveyard()) {
			Save();
			player_corpse_depop = true;
			database.SendCharacterCorpseToGraveyard(corpse_db_id, zone->graveyard_zoneid(),
				(zone->GetZoneID() == zone->graveyard_zoneid()) ? zone->GetInstanceID() : 0, zone->GetGraveyardPoint());
			corpse_graveyard_timer.Disable();
			ServerPacket* pack = new ServerPacket(ServerOP_SpawnPlayerCorpse, sizeof(SpawnPlayerCorpse_Struct));
			SpawnPlayerCorpse_Struct* spc = (SpawnPlayerCorpse_Struct*)pack->pBuffer;
			spc->player_corpse_id = corpse_db_id;
			spc->zone_id = zone->graveyard_zoneid();
			worldserver.SendPacket(pack);
			safe_delete(pack);
			Log.Out(Logs::General, Logs::Corpse, "Moved %s player corpse to the designated graveyard in zone %s.", this->GetName(), database.GetZoneName(zone->graveyard_zoneid()));
			corpse_db_id = 0;
		}

		corpse_graveyard_timer.Disable();
		return false;
	}

	//Player is offline. If rez timer is enabled, disable it and save corpse.
	if(!is_owner_online && rezzable)
	{
		if(corpse_rez_timer.Enabled())
		{
			rez_time = corpse_rez_timer.GetRemainingTime();
			corpse_rez_timer.Disable();
			is_corpse_changed = true;
			Save();
		}
	}
	//Player is online. If rez timer is disabled, enable it.
	else if(is_owner_online && rezzable)
	{
		if(corpse_rez_timer.Enabled())
		{
			rez_time = corpse_rez_timer.GetRemainingTime();
		}
		else
		{
			SetRezTimer();
		}
	}

	if(corpse_rez_timer.Check()) 
	{
		CompleteResurrection(true);
	}	

	/* This is when a corpse hits decay timer and does checks*/
	if (corpse_decay_timer.Check()) {
		/* NPC */
		if (IsNPCCorpse()){
			corpse_decay_timer.Disable();
			return false;
		}
		/* Client */
		if (!RuleB(Zone, EnableShadowrest)){
			Delete();
		}
		else {
			if (database.BuryCharacterCorpse(corpse_db_id)) {
				Save();
				player_corpse_depop = true;
				corpse_db_id = 0;
				Log.Out(Logs::General, Logs::Corpse, "Tagged %s player corpse has buried.", this->GetName());
			}
			else {
				Log.Out(Logs::General, Logs::Error, "Unable to bury %s player corpse.", this->GetName());
				return true;
			}
		}
		corpse_decay_timer.Disable();
		return false;
	}

	return true;
}

void Corpse::SetDecayTimer(uint32 decaytime) {
	if (decaytime == 0)
		corpse_decay_timer.Trigger();
	else
		corpse_decay_timer.Start(decaytime);
}

bool Corpse::CanPlayerLoot(int charid) {
	uint8 looters = 0;
	for (int i = 0; i < MAX_LOOTERS; i++) {
		if (allowed_looters[i] != 0){
			looters++;
		}

		if (allowed_looters[i] == charid){
			return true;
		}
	}
	/* If we have no looters, obviously client can loot */
	if (looters == 0){
		return true;
	}
	return false;
}

void Corpse::AllowPlayerLoot(Mob *them, uint8 slot) {
	if (slot >= MAX_LOOTERS)
		return;
	if (them == nullptr || !them->IsClient())
		return;

	allowed_looters[slot] = them->CastToClient()->CharacterID();
}

void Corpse::MakeLootRequestPackets(Client* client, const EQApplicationPacket* app) {
	// Added 12/08. Started compressing loot struct on live.
	char tmp[10];
	if(player_corpse_depop) {
		SendLootReqErrorPacket(client, 0);
		return;
	}

	if(IsPlayerCorpse() && corpse_db_id == 0) {
		// SendLootReqErrorPacket(client, 0);
		client->Message(CC_Red, "Warning: Corpse's dbid = 0! Corpse will not survive zone shutdown!");
		std::cout << "Error: PlayerCorpse::MakeLootRequestPackets: dbid = 0!" << std::endl;
		// return;
	}

	if(is_locked && client->Admin() < 100) {
		SendLootReqErrorPacket(client, 0);
		client->Message(CC_Red, "Error: Corpse locked by GM.");
		return;
	}

	if(being_looted_by == 0) { 
		being_looted_by = 0xFFFFFFFF; 
	}

	if(this->being_looted_by != 0xFFFFFFFF) {
		// lets double check....
		Entity* looter = entity_list.GetID(this->being_looted_by);
		if(looter == 0) { 
			this->being_looted_by = 0xFFFFFFFF; 
		}
	}

	uint8 Loot_Request_Type = 1;
	bool loot_coin = false;
	if(database.GetVariable("LootCoin", tmp, 9)) { loot_coin = (atoi(tmp) == 1); }

	if (this->being_looted_by != 0xFFFFFFFF && this->being_looted_by != client->GetID()) {
		SendLootReqErrorPacket(client, 0);
		Loot_Request_Type = 0;
	}
	else if (IsPlayerCorpse() && char_id == client->CharacterID()) {
		Loot_Request_Type = 2;
	}
	else if ((IsNPCCorpse() || become_npc) && CanPlayerLoot(client->CharacterID())) {
		Loot_Request_Type = 2;
	}
	else if (GetPlayerKillItem() == -1 && CanPlayerLoot(client->CharacterID())) { /* PVP loot all items, variable cash */
		Loot_Request_Type = 3;
	}
	else if (GetPlayerKillItem() == 1 && CanPlayerLoot(client->CharacterID())) { /* PVP loot 1 item, variable cash */
		Loot_Request_Type = 4;
	}
	else if (GetPlayerKillItem() > 1 && CanPlayerLoot(client->CharacterID())) { /* PVP loot 1 set item, variable cash */
		Loot_Request_Type = 5;
	}

	if (Loot_Request_Type == 1) {
		if (client->Admin() < 100 || !client->GetGM()) {
			SendLootReqErrorPacket(client, 2);
		}
	}

	if(Loot_Request_Type >= 2 || (Loot_Request_Type == 1 && client->Admin() >= 100 && client->GetGM())) {
		this->being_looted_by = client->GetID();
		EQApplicationPacket* outapp = new EQApplicationPacket(OP_MoneyOnCorpse, sizeof(moneyOnCorpseStruct));
		moneyOnCorpseStruct* d = (moneyOnCorpseStruct*)outapp->pBuffer;

		d->response		= 1;
		d->unknown1		= 0x42;
		d->unknown2		= 0xef;

		/* Don't take the coin off if it's a gm peeking at the corpse */
		if(Loot_Request_Type == 2 || (Loot_Request_Type >= 3 && loot_coin)) { 
			if(!IsPlayerCorpse() && client->IsGrouped() && client->AutoSplitEnabled() && client->GetGroup()) {
				d->copper		= 0;
				d->silver		= 0;
				d->gold			= 0;
				d->platinum		= 0;
				Group *cgroup = client->GetGroup();
				cgroup->SplitMoney(GetCopper(), GetSilver(), GetGold(), GetPlatinum(), client);
				/* QS: Player_Log_Looting */
				if (RuleB(QueryServ, PlayerLogLoot))
				{
					QServ->QSLootRecords(client->CharacterID(), corpse_name, "CASH-SPLIT", client->GetZoneID(), 0, "null", 0, GetPlatinum(), GetGold(), GetSilver(), GetCopper());
				}
			}
			else {
				d->copper = this->GetCopper();
				d->silver = this->GetSilver();
				d->gold = this->GetGold();
				d->platinum = this->GetPlatinum();
				client->AddMoneyToPP(GetCopper(), GetSilver(), GetGold(), GetPlatinum(), false);
				/* QS: Player_Log_Looting */
				if (RuleB(QueryServ, PlayerLogLoot))
				{
					QServ->QSLootRecords(client->CharacterID(), corpse_name, "CASH", client->GetZoneID(), 0, "null", 0, GetPlatinum(), GetGold(), GetSilver(), GetCopper());
				}
			}

			RemoveCash();
			Save();
		}

		outapp->priority = 6;
		client->QueuePacket(outapp);
		safe_delete(outapp);
		if(Loot_Request_Type == 5) {
			int pkitem = GetPlayerKillItem();
			const Item_Struct* item = database.GetItem(pkitem);
			ItemInst* inst = database.CreateItem(item, item->MaxCharges);
			if (inst) {
				client->SendItemPacket(EmuConstants::CORPSE_BEGIN, inst, ItemPacketLoot);
				safe_delete(inst);
			}
			else { client->Message(CC_Red, "Could not find item number %i to send!!", GetPlayerKillItem()); }

			client->QueuePacket(app);
			return;
		}

		int i = 0;
		const Item_Struct* item = 0;
		ItemList::iterator cur, end;
		cur = itemlist.begin();
		end = itemlist.end();

		int corpselootlimit = EQLimits::InventoryMapSize(MapCorpse, client->GetClientVersion());

		for (; cur != end; ++cur) {
			ServerLootItem_Struct* item_data = *cur;
			item_data->lootslot = 0xFFFF;

			// Don't display the item if it's in a bag
			// Added cursor queue slots to corpse item visibility list. Nothing else should be making it to corpse.
			if (!IsPlayerCorpse() || item_data->equip_slot <= EmuConstants::GENERAL_END || item_data->equip_slot == MainPowerSource || Loot_Request_Type >= 3 ||
				(item_data->equip_slot >= EmuConstants::CURSOR_QUEUE_BEGIN && item_data->equip_slot <= EmuConstants::CURSOR_QUEUE_END)) {
				if (i < corpselootlimit) 
				{
					if(!RuleB(NPC, IgnoreQuestLoot) || (RuleB(NPC, IgnoreQuestLoot) && item_data->quest == 0))
					{
						item = database.GetItem(item_data->item_id);
						if(client && item && (item_data->quest == 0 || (item_data->quest == 1 && item->NoDrop != 0))) 
						{
							ItemInst* inst = database.CreateItem(item, item_data->charges);
							if(inst) {
								// MainGeneral1 is the corpse inventory start offset for Ti(EMu) - CORPSE_END = MainGeneral1 + MainCursor
								client->SendItemPacket(i, inst, ItemPacketLoot);
								safe_delete(inst);
							}
						}

						item_data->lootslot = i;
					}
				}

				i++;
			}
		}

		if(IsPlayerCorpse() && (char_id == client->CharacterID() || client->GetGM())) {
			if(i > corpselootlimit) {
				client->Message(CC_Yellow, "*** This corpse contains more items than can be displayed! ***");
				client->Message(CC_Default, "Remove items and re-loot corpse to access remaining inventory.");
				client->Message(CC_Default, "(%s contains %i additional %s.)", GetName(), (i - corpselootlimit), (i - corpselootlimit) == 1 ? "item" : "items");
			}

			if (IsPlayerCorpse() && i == 0 && itemlist.size() > 0) { // somehow, player corpse contains items, but client doesn't see them...
				client->Message(CC_Red, "This corpse contains items that are inaccessible!");
				client->Message(CC_Yellow, "Contact a GM for item replacement, if necessary.");
				client->Message(CC_Yellow, "BUGGED CORPSE [DBID: %i, Name: %s, Item Count: %i]", GetCorpseDBID(), GetName(), itemlist.size());

				cur = itemlist.begin();
				end = itemlist.end();
				for (; cur != end; ++cur) {
					ServerLootItem_Struct* item_data = *cur;
					item = database.GetItem(item_data->item_id);
					Log.Out(Logs::General, Logs::Corpse, "Corpse Looting: %s was not sent to client loot window (corpse_dbid: %i, charname: %s(%s))", item->Name, GetCorpseDBID(), client->GetName(), client->GetGM() ? "GM" : "Owner");
					client->Message(CC_Default, "Inaccessible Corpse Item: %s", item->Name);
				}
			}
		}
	}

	// Disgrace: Client seems to require that we send the packet back...
	client->QueuePacket(app);
}

void Corpse::LootItem(Client* client, const EQApplicationPacket* app) {
	/* This gets sent no matter what as a sort of ACK */
	client->QueuePacket(app);

	if (!loot_cooldown_timer.Check()) {
		SendEndLootErrorPacket(client);
		//unlock corpse for others
		if (this->being_looted_by = client->GetID()) {
			being_looted_by = 0xFFFFFFFF;
		}
		return;
	}

	/* To prevent item loss for a player using 'Loot All' who doesn't have inventory space for all their items. */
	if (RuleB(Character, CheckCursorEmptyWhenLooting) && !client->GetInv().CursorEmpty()) {
		client->Message(CC_Red, "You may not loot an item while you have an item on your cursor.");
		SendEndLootErrorPacket(client);
		/* Unlock corpse for others */
		if (this->being_looted_by = client->GetID()) {
			being_looted_by = 0xFFFFFFFF;
		}
		return;
	}

	LootingItem_Struct* lootitem = (LootingItem_Struct*)app->pBuffer;

	if (this->being_looted_by != client->GetID()) {
		client->Message(CC_Red, "Error: Corpse::LootItem: BeingLootedBy != client");
		SendEndLootErrorPacket(client);
		return;
	}
	if (IsPlayerCorpse() && !CanPlayerLoot(client->CharacterID()) && !become_npc && (char_id != client->CharacterID() && client->Admin() < 150)) {
		client->Message(CC_Red, "Error: This is a player corpse and you don't own it.");
		SendEndLootErrorPacket(client);
		return;
	}
	if (is_locked && client->Admin() < 100) {
		SendLootReqErrorPacket(client, 0);
		client->Message(CC_Red, "Error: Corpse locked by GM.");
		return;
	}
	if (IsPlayerCorpse() && (char_id != client->CharacterID()) && CanPlayerLoot(client->CharacterID()) && GetPlayerKillItem() == 0){
		client->Message(CC_Red, "Error: You cannot loot any more items from this corpse.");
		SendEndLootErrorPacket(client);
		being_looted_by = 0xFFFFFFFF;
		return;
	}
	const Item_Struct* item = 0;
	ItemInst *inst = 0;
	ServerLootItem_Struct* item_data = nullptr, *bag_item_data[10];

	memset(bag_item_data, 0, sizeof(bag_item_data));
	if (GetPlayerKillItem() > 1){
		item = database.GetItem(GetPlayerKillItem());
	}
	else if (GetPlayerKillItem() == -1 || GetPlayerKillItem() == 1){
		item_data = GetItem(lootitem->slot_id); //don't allow them to loot entire bags of items as pvp reward
	}
	else{
		item_data = GetItem(lootitem->slot_id, bag_item_data);
	}

	if (GetPlayerKillItem()<=1 && item_data != 0) {
		item = database.GetItem(item_data->item_id);
	}

	if (item != 0) {
		if (item_data){
			inst = database.CreateItem(item, item_data ? item_data->charges : 0);
		}
		else {
			inst = database.CreateItem(item);
		}
	}

	if (client && inst) {
		if (client->CheckLoreConflict(item)) {
			client->Message_StringID(0, LOOT_LORE_ERROR);
			SendEndLootErrorPacket(client);
			being_looted_by = 0;
			delete inst;
			return;
		}
		// search through bags for lore items
		if (item && item->ItemClass == ItemClassContainer) {
			for (int i = 0; i < 10; i++) {
				if(bag_item_data[i])
				{
					const Item_Struct* bag_item = 0;
					bag_item = database.GetItem(bag_item_data[i]->item_id);
					if (bag_item && client->CheckLoreConflict(bag_item))
					{
						client->Message(CC_Default, "You cannot loot this container. The %s inside is a Lore Item and you already have one.", bag_item->Name);
						SendEndLootErrorPacket(client);
						being_looted_by = 0;
						delete inst;
						return;
					}
				}
			}
		}

		char buf[88];
		char corpse_name[64];
		strcpy(corpse_name, GetName());
		snprintf(buf, 87, "%d %d %s", inst->GetItem()->ID, inst->GetCharges(), EntityList::RemoveNumbers(corpse_name));
		buf[87] = '\0';
		std::vector<EQEmu::Any> args;
		args.push_back(inst);
		args.push_back(this);
		parse->EventPlayer(EVENT_LOOT, client, buf, 0, &args);
		parse->EventItem(EVENT_LOOT, client, inst, this, buf, 0);

		if ((RuleB(Character, EnableDiscoveredItems))) {
			if (client && !client->GetGM() && !client->IsDiscovered(inst->GetItem()->ID))
				client->DiscoverItem(inst->GetItem()->ID);
		}


		/* First add it to the looter - this will do the bag contents too */
		if (lootitem->auto_loot) {
			if (!client->AutoPutLootInInventory(*inst, true, true, bag_item_data))
				client->PutLootInInventory(MainCursor, *inst, bag_item_data);
		}
		else {
			client->PutLootInInventory(MainCursor, *inst, bag_item_data);
		}

		/* Remove it from Corpse */
		if (item_data){
			/* Delete needs to be before RemoveItem because its deletes the pointer for item_data/bag_item_data */
			database.DeleteItemOffCharacterCorpse(this->corpse_db_id, item_data->equip_slot, item_data->item_id);
			/* Delete Item Instance */
			RemoveItem(item_data->lootslot);
		}

		/* Remove Bag Contents */
		if (item->ItemClass == ItemClassContainer && (GetPlayerKillItem() != -1 || GetPlayerKillItem() != 1)) {
			for (int i = SUB_BEGIN; i < EmuConstants::ITEM_CONTAINER_SIZE; i++) {
				if (bag_item_data[i]) {
					/* Delete needs to be before RemoveItem because its deletes the pointer for item_data/bag_item_data */
					database.DeleteItemOffCharacterCorpse(this->corpse_db_id, bag_item_data[i]->equip_slot, bag_item_data[i]->item_id);
					/* Delete Item Instance */
					RemoveItem(bag_item_data[i]);
				}
			}
		}

		if (GetPlayerKillItem() != -1){
			SetPlayerKillItemID(0);
		}

		/* Send message with item link to groups and such */
		char *link = 0, *link2 = 0; //just like a db query :-)
		client->MakeItemLink(link2, inst);
		MakeAnyLenString(&link, "%c" "%s" "%s" "%c",
			0x12,
			link2,
			item->Name,
			0x12);
		safe_delete_array(link2);

		if (!IsPlayerCorpse()) 
		{
			client->Message_StringID(MT_LootMessages, LOOTED_MESSAGE, link);

			Group *g = client->GetGroup();
			if (g != nullptr) 
			{
				g->GroupMessage_StringID(client, MT_LootMessages, OTHER_LOOTED_MESSAGE, client->GetName(), link);
			}
			else
			{
				Raid *r = client->GetRaid();
				if (r != nullptr) 
				{
					r->RaidMessage_StringID(client, MT_LootMessages, OTHER_LOOTED_MESSAGE, client->GetName(), link);
				}
			}
		}
		safe_delete_array(link);
	}
	else {
		SendEndLootErrorPacket(client);
		safe_delete(inst);
		return;
	}

	/* QS: Player_Log_Looting */
	if (RuleB(QueryServ, PlayerLogLoot))
	{
		QServ->QSLootRecords(client->CharacterID(), corpse_name, "ITEM", client->GetZoneID(), item->ID, item->Name, inst->GetCharges(), GetPlatinum(), GetGold(), GetSilver(), GetCopper());
	}

	safe_delete(inst);
}

void Corpse::EndLoot(Client* client, const EQApplicationPacket* app) {
	EQApplicationPacket* outapp = new EQApplicationPacket;
	outapp->SetOpcode(OP_LootComplete);
	outapp->size = 0;
	client->QueuePacket(outapp);
	safe_delete(outapp);

	this->being_looted_by = 0xFFFFFFFF;
	if (this->IsEmpty())
		Delete();
	else
		Save();
}

void Corpse::FillSpawnStruct(NewSpawn_Struct* ns, Mob* ForWho) {
	Mob::FillSpawnStruct(ns, ForWho);

	ns->spawn.max_hp = 120;

	if (IsPlayerCorpse())
		ns->spawn.NPC = 3;
	else
		ns->spawn.NPC = 2;

	UpdateActiveLight();
	ns->spawn.light = m_Light.Type.Active;
}

void Corpse::QueryLoot(Client* to) {
	int x = 0, y = 0; // x = visible items, y = total items
	to->Message(CC_Default, "Coin: %ip, %ig, %is, %ic", platinum, gold, silver, copper);

	ItemList::iterator cur, end;
	cur = itemlist.begin();
	end = itemlist.end();

	int corpselootlimit = EQLimits::InventoryMapSize(MapCorpse, to->GetClientVersion());

	for (; cur != end; ++cur) {
		ServerLootItem_Struct* sitem = *cur;

		if (IsPlayerCorpse()) {
			if (sitem->equip_slot >= EmuConstants::GENERAL_BAGS_BEGIN && sitem->equip_slot <= EmuConstants::CURSOR_BAG_END)
				sitem->lootslot = 0xFFFF;
			else
				x < corpselootlimit ? sitem->lootslot = x : sitem->lootslot = 0xFFFF;

			const Item_Struct* item = database.GetItem(sitem->item_id);

			if (item)
				to->Message((sitem->lootslot == 0xFFFF), "LootSlot: %i (EquipSlot: %i) Item: %s (%d), Count: %i", static_cast<int16>(sitem->lootslot), sitem->equip_slot, item->Name, item->ID, sitem->charges);
			else
				to->Message((sitem->lootslot == 0xFFFF), "Error: 0x%04x", sitem->item_id);

			if (sitem->lootslot != 0xFFFF)
				x++;

			y++;
		}
		else {
			sitem->lootslot = y;
			const Item_Struct* item = database.GetItem(sitem->item_id);

			if (item)
				to->Message(CC_Default, "LootSlot: %i Item: %s (%d), Count: %i", sitem->lootslot, item->Name, item->ID, sitem->charges);
			else
				to->Message(CC_Default, "Error: 0x%04x", sitem->item_id);

			y++;
		}
	}

	if (IsPlayerCorpse()) {
		to->Message(CC_Default, "%i visible %s (%i total) on %s (DBID: %i).", x, x==1?"item":"items", y, this->GetName(), this->GetCorpseDBID());
	}
	else {
		to->Message(CC_Default, "%i %s on %s.", y, y == 1 ? "item" : "items", this->GetName());
	}
}

bool Corpse::Summon(Client* client, bool spell, bool CheckDistance) {
	uint32 dist2 = 10000; // pow(100, 2);
	if (!spell) {
		if (this->GetCharID() == client->CharacterID()) {
			if (IsLocked() && client->Admin() < 100) {
				client->Message(CC_Red, "That corpse is locked by a GM.");
				return false;
			}
			if (!CheckDistance || (DistanceSquaredNoZ(m_Position, client->GetPosition()) <= dist2)) {
				GMMove(client->GetX(), client->GetY(), client->GetZ());
				is_corpse_changed = true;
			}
			else {
				client->Message_StringID(CC_Default, CORPSE_TOO_FAR);
				return false;
			}
		}
		else
		{
			bool consented = false;
			std::list<std::string>::iterator itr;
			for(itr = client->consent_list.begin(); itr != client->consent_list.end(); ++itr) {
				if(strcmp(this->GetOwnerName(), itr->c_str()) == 0) {
					if (!CheckDistance || (DistanceSquaredNoZ(m_Position, client->GetPosition()) <= dist2)) {
						GMMove(client->GetX(), client->GetY(), client->GetZ());
						is_corpse_changed = true;
					}
					else {
						client->Message_StringID(CC_Default, CORPSE_TOO_FAR);
						return false;
					}
					consented = true;
				}
			}
			if(!consented) {
				client->Message_StringID(CC_Default, CONSENT_NONE);
				return false;
			}
		}
	}
	else {
		GMMove(client->GetX(), client->GetY(), client->GetZ());
		is_corpse_changed = true;
	}
	Save();
	return true;
}

void Corpse::CompleteResurrection(bool timer_expired)
{
	if(timer_expired)
	{
		rez_time = 0;
		rezzable = false; // Players can no longer rez this corpse.
		corpse_rez_timer.Disable();
	}
	else
	{
		rez_time = corpse_rez_timer.GetRemainingTime();
	}

	IsRezzed(true); // Players can rez this corpse for no XP (corpse gate) provided rezzable is true.
	rez_experience = 0;
	is_corpse_changed = true;
	this->Save();
}

void Corpse::Spawn() {
	EQApplicationPacket* app = new EQApplicationPacket;
	this->CreateSpawnPacket(app, this);
	entity_list.QueueClients(this, app);
	safe_delete(app);
}

uint32 Corpse::GetEquipment(uint8 material_slot) const {
	int invslot;

	if(material_slot > EmuConstants::MATERIAL_END) {
		return NO_ITEM;
	}

	invslot = Inventory::CalcSlotFromMaterial(material_slot);
	if (invslot == INVALID_INDEX) // GetWornItem() should be returning a NO_ITEM for any invalid index...
		return NO_ITEM;

	return GetWornItem(invslot);
}

uint32 Corpse::GetEquipmentColor(uint8 material_slot) const {
	const Item_Struct *item;

	if(material_slot > EmuConstants::MATERIAL_END) {
		return 0;
	}

	item = database.GetItem(GetEquipment(material_slot));
	if(item != NO_ITEM) {
		return item_tint[material_slot].rgb.use_tint ?
			item_tint[material_slot].color :
			item->Color;
	}

	return 0;
}

void Corpse::UpdateEquipmentLight()
{
	m_Light.Type.Equipment = 0;
	m_Light.Level.Equipment = 0;

	for (auto iter = itemlist.begin(); iter != itemlist.end(); ++iter) {
		if (((*iter)->equip_slot < EmuConstants::EQUIPMENT_BEGIN || (*iter)->equip_slot > EmuConstants::EQUIPMENT_END) && (*iter)->equip_slot != MainPowerSource) { continue; }
		if ((*iter)->equip_slot == MainAmmo) { continue; }
		
		auto item = database.GetItem((*iter)->item_id);
		if (item == nullptr) { continue; }
		
		if (m_Light.IsLevelGreater(item->Light, m_Light.Type.Equipment))
			m_Light.Type.Equipment = item->Light;
	}
	
	uint8 general_light_type = 0;
	for (auto iter = itemlist.begin(); iter != itemlist.end(); ++iter) {
		if ((*iter)->equip_slot < EmuConstants::GENERAL_BEGIN || (*iter)->equip_slot > EmuConstants::GENERAL_END) { continue; }
		
		auto item = database.GetItem((*iter)->item_id);
		if (item == nullptr) { continue; }
		
		if (item->ItemClass != ItemClassCommon) { continue; }
		if (item->Light < 9 || item->Light > 13) { continue; }

		if (m_Light.TypeToLevel(item->Light))
			general_light_type = item->Light;
	}

	if (m_Light.IsLevelGreater(general_light_type, m_Light.Type.Equipment))
		m_Light.Type.Equipment = general_light_type;

	m_Light.Level.Equipment = m_Light.TypeToLevel(m_Light.Type.Equipment);
}

void Corpse::AddLooter(Mob* who) {
	for (int i = 0; i < MAX_LOOTERS; i++) {
		if (allowed_looters[i] == 0) {
			allowed_looters[i] = who->CastToClient()->CharacterID();
			break;
		}
	}
}

void Corpse::LoadPlayerCorpseDecayTime(uint32 corpse_db_id, bool empty){
	if(!corpse_db_id)
		return;

	uint32 active_corpse_decay_timer = database.GetCharacterCorpseDecayTimer(corpse_db_id);
	int32 corpse_decay;
	if(empty)
	{
		corpse_decay = RuleI(Character, EmptyCorpseDecayTimeMS);
	}
	else
	{
		corpse_decay = RuleI(Character, CorpseDecayTimeMS);
	}

	if (active_corpse_decay_timer > 0 && corpse_decay > (active_corpse_decay_timer * 1000)) {
		corpse_decay_timer.SetTimer(corpse_decay - (active_corpse_decay_timer * 1000));
	}
	else {
		corpse_decay_timer.SetTimer(2000);
	}
	if (active_corpse_decay_timer > 0 && RuleI(Zone, GraveyardTimeMS) > (active_corpse_decay_timer * 1000)) {
		corpse_graveyard_timer.SetTimer(RuleI(Zone, GraveyardTimeMS) - (active_corpse_decay_timer * 1000));
	}
	else {
		corpse_graveyard_timer.SetTimer(3000);
	}
}

void Corpse::SetRezTimer(bool initial_timer)
{

	if(!rezzable)
	{
		if(corpse_rez_timer.Enabled())
			corpse_rez_timer.Disable();

		return;
	}

	IsOwnerOnline();
	if(!is_owner_online && !initial_timer)
	{
		if(corpse_rez_timer.Enabled())
			corpse_rez_timer.Disable();

		return;
    }

	if(corpse_rez_timer.Enabled() && !initial_timer)
	{
		return;
	}

	if(initial_timer)
	{
		uint32 timer = RuleI(Character, CorpseResTimeMS);
		if(killedby == Killed_DUEL)
		{
			timer = RuleI(Character, DuelCorpseResTimeMS);
		}
	
		rez_time = timer;
	}

	if(rez_time < 1)
	{
		// Corpse is no longer rezzable
		CompleteResurrection(true);
		return;
	}

	corpse_rez_timer.SetTimer(rez_time);

}

void Corpse::IsOwnerOnline()
{
	Client* client = entity_list.GetClientByCharID(GetCharID());

	if(!client)
	{
		// Client is not in the corpse's zone, send a packet to world to have it check.
		ServerPacket* pack = new ServerPacket(ServerOP_IsOwnerOnline, sizeof(ServerIsOwnerOnline_Struct));
		ServerIsOwnerOnline_Struct* online = (ServerIsOwnerOnline_Struct*)pack->pBuffer;
		strncpy(online->name, GetOwnerName(), 64);
		online->corpseid = this->GetID();
		online->zoneid = zone->GetZoneID();
		online->online = 0;
		worldserver.SendPacket(pack);
	}
	else
	{
		SetOwnerOnline(true);
	}
}
