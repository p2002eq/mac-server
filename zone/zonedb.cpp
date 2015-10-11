
#include "../common/eqemu_logsys.h"
#include "../common/extprofile.h"
#include "../common/item.h"
#include "../common/rulesys.h"
#include "../common/string_util.h"

#include "client.h"
#include "corpse.h"
#include "groups.h"
#include "zone.h"
#include "zonedb.h"

#include <ctime>
#include <iostream>

extern Zone* zone;

ZoneDatabase database;

ZoneDatabase::ZoneDatabase()
: SharedDatabase()
{
	ZDBInitVars();
}

ZoneDatabase::ZoneDatabase(const char* host, const char* user, const char* passwd, const char* database, uint32 port)
: SharedDatabase(host, user, passwd, database, port)
{
	ZDBInitVars();
}

void ZoneDatabase::ZDBInitVars() {
	memset(door_isopen_array, 0, sizeof(door_isopen_array));
	npc_spells_maxid = 0;
	npc_spellseffects_maxid = 0;
	npc_spells_cache = 0;
	npc_spellseffects_cache = 0;
	npc_spells_loadtried = 0;
	npc_spellseffects_loadtried = 0;
	max_faction = 0;
	faction_array = nullptr;
}

ZoneDatabase::~ZoneDatabase() {
	unsigned int x;
	if (npc_spells_cache) {
		for (x=0; x<=npc_spells_maxid; x++) {
			safe_delete_array(npc_spells_cache[x]);
		}
		safe_delete_array(npc_spells_cache);
	}
	safe_delete_array(npc_spells_loadtried);

	if (npc_spellseffects_cache) {
		for (x=0; x<=npc_spellseffects_maxid; x++) {
			safe_delete_array(npc_spellseffects_cache[x]);
		}
		safe_delete_array(npc_spellseffects_cache);
	}
	safe_delete_array(npc_spellseffects_loadtried);

	if (faction_array != nullptr) {
		for (x=0; x <= max_faction; x++) {
			if (faction_array[x] != 0)
				safe_delete(faction_array[x]);
		}
		safe_delete_array(faction_array);
	}
}

bool ZoneDatabase::SaveZoneCFG(uint32 zoneid, uint16 instance_id, NewZone_Struct* zd) {

	std::string query = StringFormat("UPDATE zone SET underworld = %f, minclip = %f, "
                                    "maxclip = %f, fog_minclip = %f, fog_maxclip = %f, "
                                    "fog_blue = %i, fog_red = %i, fog_green = %i, "
                                    "sky = %i, ztype = %i, zone_exp_multiplier = %f, "
                                    "safe_x = %f, safe_y = %f, safe_z = %f "
                                    "WHERE zoneidnumber = %i AND version = %i",
                                    zd->underworld, zd->minclip,
                                    zd->maxclip, zd->fog_minclip[0], zd->fog_maxclip[0],
                                    zd->fog_blue[0], zd->fog_red[0], zd->fog_green[0],
                                    zd->sky, zd->ztype, zd->zone_exp_multiplier,
                                    zd->safe_x, zd->safe_y, zd->safe_z,
                                    zoneid, instance_id);
	auto results = QueryDatabase(query);
	if (!results.Success()) {
        return false;
	}

	return true;
}

bool ZoneDatabase::GetZoneCFG(uint32 zoneid, uint16 instance_id, NewZone_Struct *zone_data, bool &can_bind, bool &can_combat, bool &can_levitate, bool &can_castoutdoor, bool &is_city, bool &is_hotzone, uint8 &zone_type, int &ruleset, char **map_filename, bool &can_bind_others, bool &skip_los) {

	*map_filename = new char[100];
	zone_data->zone_id = zoneid;

	std::string query = StringFormat("SELECT ztype, fog_red, fog_green, fog_blue, fog_minclip, fog_maxclip, " // 5
                                    "fog_red2, fog_green2, fog_blue2, fog_minclip2, fog_maxclip2, " // 5
                                    "fog_red3, fog_green3, fog_blue3, fog_minclip3, fog_maxclip3, " // 5
                                    "fog_red4, fog_green4, fog_blue4, fog_minclip4, fog_maxclip4, " // 5
                                    "fog_density, sky, zone_exp_multiplier, safe_x, safe_y, safe_z, underworld, " // 7
                                    "minclip, maxclip, time_type, canbind, cancombat, canlevitate, " // 6
                                    "castoutdoor, hotzone, ruleset, suspendbuffs, map_file_name, short_name, " // 6
                                    "rain_chance1, rain_chance2, rain_chance3, rain_chance4, " // 4
                                    "rain_duration1, rain_duration2, rain_duration3, rain_duration4, " // 4
                                    "snow_chance1, snow_chance2, snow_chance3, snow_chance4, " // 4
                                    "snow_duration1, snow_duration2, snow_duration3, snow_duration4, " // 4
									"skylock, skip_los, music " // 3
                                    "FROM zone WHERE zoneidnumber = %i AND version = %i", zoneid, instance_id);
    auto results = QueryDatabase(query);
    if (!results.Success()) {
        strcpy(*map_filename, "default");
		return false;
    }

	if (results.RowCount() == 0) {
        strcpy(*map_filename, "default");
        return false;
    }

    auto row = results.begin();

    memset(zone_data, 0, sizeof(NewZone_Struct));
    zone_data->ztype = atoi(row[0]);
    zone_type = zone_data->ztype;

    int index;
    for(index = 0; index < 4; index++) {
        zone_data->fog_red[index]=atoi(row[1 + index * 5]);
        zone_data->fog_green[index]=atoi(row[2 + index * 5]);
        zone_data->fog_blue[index]=atoi(row[3 + index * 5]);
        zone_data->fog_minclip[index]=atof(row[4 + index * 5]);
        zone_data->fog_maxclip[index]=atof(row[5 + index * 5]);
    }

    zone_data->fog_density = atof(row[21]);
    zone_data->sky=atoi(row[22]);
    zone_data->zone_exp_multiplier=atof(row[23]);
    zone_data->safe_x=atof(row[24]);
    zone_data->safe_y=atof(row[25]);
    zone_data->safe_z=atof(row[26]);
    zone_data->underworld=atof(row[27]);
    zone_data->minclip=atof(row[28]);
    zone_data->maxclip=atof(row[29]);
    zone_data->time_type=atoi(row[30]);
	zone_data->skylock = atoi(row[56]);
	zone_data->normal_music_day = atoi(row[58]);

    //not in the DB yet:
    zone_data->gravity = 0.4;

    int bindable = 0;
    bindable = atoi(row[31]);

    can_bind = bindable == 0? false: true;
    is_city = bindable == 2? true: false;
	can_bind_others = bindable == 3? true: false;
    can_combat = atoi(row[32]) == 0? false: true;
    can_levitate = atoi(row[33]) == 0? false: true;
    can_castoutdoor = atoi(row[34]) == 0? false: true;
    is_hotzone = atoi(row[35]) == 0? false: true;


    ruleset = atoi(row[36]);
    zone_data->SuspendBuffs = atoi(row[37]);

    char *file = row[38];
    if(file)
        strcpy(*map_filename, file);
    else
        strcpy(*map_filename, row[39]);

    for(index = 0; index < 4; index++)
        zone_data->rain_chance[index]=atoi(row[40 + index]);

	for(index = 0; index < 4; index++)
        zone_data->rain_duration[index]=atoi(row[44 + index]);

	for(index = 0; index < 4; index++)
        zone_data->snow_chance[index]=atoi(row[48 + index]);

	for(index = 0; index < 4; index++)
        zone_data->snow_duration[index]=atof(row[52 + index]);

	skip_los = atoi(row[57]) == 0? false: true;

	return true;
}

void ZoneDatabase::UpdateRespawnTime(uint32 spawn2_id, uint16 instance_id, uint32 time_left)
{

	timeval tv;
	gettimeofday(&tv, nullptr);
	uint32 current_time = tv.tv_sec;

	/*	If we pass timeleft as 0 that means we clear from respawn time
			otherwise we update with a REPLACE INTO
	*/

	if(time_left == 0) {
        std::string query = StringFormat("DELETE FROM `respawn_times` WHERE `id` = %u AND `instance_id` = %u", spawn2_id, instance_id);
        QueryDatabase(query); 
		return;
	}

    std::string query = StringFormat(
		"REPLACE INTO `respawn_times` "
		"(id, "
		"start, "
		"duration, "
		"instance_id) "
		"VALUES " 
		"(%u, "
		"%u, "
		"%u, "
		"%u)",
		spawn2_id, 
		current_time,
		time_left, 
		instance_id
	);
    QueryDatabase(query);

	return;
}

//Gets the respawn time left in the database for the current spawn id
uint32 ZoneDatabase::GetSpawnTimeLeft(uint32 id, uint16 instance_id)
{
	std::string query = StringFormat("SELECT start, duration FROM respawn_times "
                                    "WHERE id = %lu AND instance_id = %lu",
                                    (unsigned long)id, (unsigned long)zone->GetInstanceID());
    auto results = QueryDatabase(query);
    if (!results.Success()) {
		return 0;
    }

    if (results.RowCount() != 1)
        return 0;

    auto row = results.begin();

    timeval tv;
    gettimeofday(&tv, nullptr);
    uint32 resStart = atoi(row[0]);
    uint32 resDuration = atoi(row[1]);

    //compare our values to current time
    if((resStart + resDuration) <= tv.tv_sec) {
        //our current time was expired
        return 0;
    }

    //we still have time left on this timer
    return ((resStart + resDuration) - tv.tv_sec);

}

void ZoneDatabase::UpdateSpawn2Status(uint32 id, uint8 new_status)
{
	std::string query = StringFormat("UPDATE spawn2 SET enabled = %i WHERE id = %lu", new_status, (unsigned long)id);
	QueryDatabase(query);
}

bool ZoneDatabase::logevents(const char* accountname,uint32 accountid,uint8 status,const char* charname, const char* target,const char* descriptiontype, const char* description,int event_nid){

	uint32 len = strlen(description);
	uint32 len2 = strlen(target);
	char* descriptiontext = new char[2*len+1];
	char* targetarr = new char[2*len2+1];
	memset(descriptiontext, 0, 2*len+1);
	memset(targetarr, 0, 2*len2+1);
	DoEscapeString(descriptiontext, description, len);
	DoEscapeString(targetarr, target, len2);

	std::string query = StringFormat("INSERT INTO eventlog (accountname, accountid, status, "
                                    "charname, target, descriptiontype, description, event_nid) "
                                    "VALUES('%s', %i, %i, '%s', '%s', '%s', '%s', '%i')",
                                    accountname, accountid, status, charname, targetarr,
                                    descriptiontype, descriptiontext, event_nid);
    safe_delete_array(descriptiontext);
	safe_delete_array(targetarr);
	auto results = QueryDatabase(query);
	if (!results.Success())	{
		return false;
	}

	return true;
}

void ZoneDatabase::UpdateBug(BugStruct* bug, uint32 clienttype) {

	uint32 len = strlen(bug->bug);
	char* bugtext = nullptr;
	if (len > 0)
	{
		bugtext = new char[2 * len + 1];
		memset(bugtext, 0, 2 * len + 1);
		DoEscapeString(bugtext, bug->bug, len);
	}

	len = strlen(bug->target_name);
	char* targettext = nullptr;
	if (len > 0)
	{
		targettext = new char[2 * len + 1];
		memset(targettext, 0, 2 * len + 1);
		DoEscapeString(targettext, bug->target_name, len);
	}

	char uitext[16];
	if(clienttype == BIT_MacPC)
		strcpy(uitext, "PC");
	else if(clienttype == BIT_MacIntel)
		strcpy(uitext, "Intel");

	std::string query = StringFormat("INSERT INTO bugs (zone, name, ui, x, y, z, type, flag, target, bug, date) "
		"VALUES('%s', '%s', '%s', '%.2f', '%.2f', '%.2f', '%s', %d, '%s', '%s', CURDATE())",
		zone->GetShortName(), bug->name, uitext, bug->x, bug->y, bug->z, bug->chartype, bug->type, 
		targettext == nullptr ? "Unknown Target" : targettext,
		bugtext == nullptr ? "" : bugtext);
	safe_delete_array(bugtext);
	safe_delete_array(targettext);
	QueryDatabase(query);
}

void ZoneDatabase::UpdateFeedback(Feedback_Struct* feedback) {

	uint32 len = strlen(feedback->name);
	char* name = nullptr;
	if (len > 0)
	{
		name = new char[2 * len + 1];
		memset(name, 0, 2 * len + 1);
		DoEscapeString(name, feedback->name, len);
	}

	len = strlen(feedback->message);
	char* message = nullptr;
	if (len > 0)
	{
		message = new char[2 * len + 1];
		memset(message, 0, 2 * len + 1);
		DoEscapeString(message, feedback->message, len);
	}

	std::string query = StringFormat("INSERT INTO feedback (name, message, zone, date) "
		"VALUES('%s', '%s', '%s', CURDATE())",
		name, message, zone->GetShortName());

	QueryDatabase(query);

}

void ZoneDatabase::AddSoulMark(uint32 charid, const char* charname, const char* accname, const char* gmname, const char* gmacctname, uint32 utime, uint32 type, const char* desc) {

	std::string query = StringFormat("INSERT INTO character_soulmarks (charid, charname, acctname, gmname, gmacctname, utime, type, `desc`) "
		"VALUES(%i, '%s', '%s','%s','%s', %i, %i, '%s')", charid, charname, accname, gmname, gmacctname, utime, type, desc);
	auto results = QueryDatabase(query);
	if (!results.Success())
		std::cerr << "Error in AddSoulMark '" << query << "' " << results.ErrorMessage() << std::endl;

}

int ZoneDatabase::RemoveSoulMark(uint32 charid) {

	std::string query = StringFormat("DELETE FROM character_soulmarks where charid=%i", charid);
	auto results = QueryDatabase(query);
	if (!results.Success())
	{
		std::cerr << "Error in DeleteSoulMark '" << query << "' " << results.ErrorMessage() << std::endl;
		return 0;
	}
	int res = 0;

	if(results.RowsAffected() >= 0 && results.RowsAffected() <= 11)
	{
		res = results.RowsAffected();
	}

	return res;

}

bool ZoneDatabase::SetSpecialAttkFlag(uint8 id, const char* flag) {

	std::string query = StringFormat("UPDATE npc_types SET npcspecialattks='%s' WHERE id = %i;", flag, id);
    auto results = QueryDatabase(query);
	if (!results.Success())
		return false;

	return results.RowsAffected() != 0;
}

bool ZoneDatabase::DoorIsOpen(uint8 door_id,const char* zone_name)
{
	if(door_isopen_array[door_id] == 0) {
		SetDoorPlace(1,door_id,zone_name);
		return false;
	}
	else {
		SetDoorPlace(0,door_id,zone_name);
		return true;
	}
}

void ZoneDatabase::SetDoorPlace(uint8 value,uint8 door_id,const char* zone_name)
{
	door_isopen_array[door_id] = value;
}

void ZoneDatabase::GetEventLogs(const char* name,char* target,uint32 account_id,uint8 eventid,char* detail,char* timestamp, CharacterEventLog_Struct* cel)
{
	char modifications[200];
	if(strlen(name) != 0)
		sprintf(modifications,"charname=\'%s\'",name);
	else if(account_id != 0)
		sprintf(modifications,"accountid=%i",account_id);

	if(strlen(target) != 0)
		sprintf(modifications,"%s AND target LIKE \'%%%s%%\'",modifications,target);

	if(strlen(detail) != 0)
		sprintf(modifications,"%s AND description LIKE \'%%%s%%\'",modifications,detail);

	if(strlen(timestamp) != 0)
		sprintf(modifications,"%s AND time LIKE \'%%%s%%\'",modifications,timestamp);

	if(eventid == 0)
		eventid =1;
	sprintf(modifications,"%s AND event_nid=%i",modifications,eventid);

    std::string query = StringFormat("SELECT id, accountname, accountid, status, charname, target, "
                                    "time, descriptiontype, description FROM eventlog WHERE %s", modifications);
    auto results = QueryDatabase(query);
    if (!results.Success())
        return;

	int index = 0;
    for (auto row = results.begin(); row != results.end(); ++row, ++index) {
        if(index == 255)
            break;

        cel->eld[index].id = atoi(row[0]);
        strn0cpy(cel->eld[index].accountname,row[1],64);
        cel->eld[index].account_id = atoi(row[2]);
        cel->eld[index].status = atoi(row[3]);
        strn0cpy(cel->eld[index].charactername,row[4],64);
        strn0cpy(cel->eld[index].targetname,row[5],64);
        sprintf(cel->eld[index].timestamp,"%s",row[6]);
        strn0cpy(cel->eld[index].descriptiontype,row[7],64);
        strn0cpy(cel->eld[index].details,row[8],128);
        cel->eventid = eventid;
        cel->count = index + 1;
    }

}

// Load child objects for a world container (i.e., forge, bag dropped to ground, etc)
void ZoneDatabase::LoadWorldContainer(uint32 parentid, ItemInst* container)
{
	if (!container) {
		Log.Out(Logs::General, Logs::Error, "Programming error: LoadWorldContainer passed nullptr pointer");
		return;
	}

	std::string query = StringFormat("SELECT bagidx, itemid, charges "
                                    "FROM object_contents WHERE parentid = %i", parentid);
    auto results = QueryDatabase(query);
    if (!results.Success()) {
        Log.Out(Logs::General, Logs::Error, "Error in DB::LoadWorldContainer: %s", results.ErrorMessage().c_str());
        return;
    }

    for (auto row = results.begin(); row != results.end(); ++row) {
        uint8 index = (uint8)atoi(row[0]);
        uint32 item_id = (uint32)atoi(row[1]);
        int8 charges = (int8)atoi(row[2]);

        ItemInst* inst = database.CreateItem(item_id, charges);
        if (inst) {
            // Put item inside world container
            container->PutItem(index, *inst);
            safe_delete(inst);
        }
    }

}

// Save child objects for a world container (i.e., forge, bag dropped to ground, etc)
void ZoneDatabase::SaveWorldContainer(uint32 zone_id, uint32 parent_id, const ItemInst* container)
{
	// Since state is not saved for each world container action, we'll just delete
	// all and save from scratch .. we may come back later to optimize
	if (!container)
		return;

	//Delete all items from container
	DeleteWorldContainer(parent_id,zone_id);

	// Save all 10 items, if they exist
	for (uint8 index = SUB_BEGIN; index < EmuConstants::ITEM_CONTAINER_SIZE; index++) {

		ItemInst* inst = container->GetItem(index);
		if (!inst)
            continue;
		else
		{
			if(inst->GetItem()->NoDrop == 0)
				continue;
		}

        uint32 item_id = inst->GetItem()->ID;

        std::string query = StringFormat("REPLACE INTO object_contents "
                                        "(zoneid, parentid, bagidx, itemid, charges, droptime) "
                                        "VALUES (%i, %i, %i, %i, %i, now())",
                                        zone_id, parent_id, index, item_id, inst->GetCharges());
        auto results = QueryDatabase(query);
        if (!results.Success())
            Log.Out(Logs::General, Logs::Error, "Error in ZoneDatabase::SaveWorldContainer: %s", results.ErrorMessage().c_str());

    }

}

// Remove all child objects inside a world container (i.e., forge, bag dropped to ground, etc)
void ZoneDatabase::DeleteWorldContainer(uint32 parent_id, uint32 zone_id)
{
	std::string query = StringFormat("DELETE FROM object_contents WHERE parentid = %i AND zoneid = %i", parent_id, zone_id);
    auto results = QueryDatabase(query);
	if (!results.Success())
		Log.Out(Logs::General, Logs::Error, "Error in ZoneDatabase::DeleteWorldContainer: %s", results.ErrorMessage().c_str());

}

Trader_Struct* ZoneDatabase::LoadTraderItem(uint32 char_id)
{
	Trader_Struct* loadti = new Trader_Struct;
	memset(loadti,0,sizeof(Trader_Struct));

	std::string query = StringFormat("SELECT * FROM trader WHERE char_id = %i ORDER BY slot_id LIMIT 80", char_id);
	auto results = QueryDatabase(query);
	if (!results.Success()) {
		Log.Out(Logs::Detail, Logs::Trading, "Failed to load trader information!\n");
		return loadti;
	}

	loadti->Code = BazaarTrader_ShowItems;
	for (auto row = results.begin(); row != results.end(); ++row) {
		if (atoi(row[5]) >= 80 || atoi(row[4]) < 0) {
			Log.Out(Logs::Detail, Logs::Trading, "Bad Slot number when trying to load trader information!\n");
			continue;
		}

		loadti->Items[atoi(row[5])] = atoi(row[1]);
		loadti->ItemCost[atoi(row[5])] = atoi(row[4]);
	}
	return loadti;
}

TraderCharges_Struct* ZoneDatabase::LoadTraderItemWithCharges(uint32 char_id)
{
	TraderCharges_Struct* loadti = new TraderCharges_Struct;
	memset(loadti,0,sizeof(TraderCharges_Struct));

	std::string query = StringFormat("SELECT * FROM trader WHERE char_id=%i ORDER BY slot_id LIMIT 80", char_id);
	auto results = QueryDatabase(query);
	if (!results.Success()) {
		Log.Out(Logs::Detail, Logs::Trading, "Failed to load trader information!\n");
		return loadti;
	}

	for (auto row = results.begin(); row != results.end(); ++row) {
		if (atoi(row[5]) >= 80 || atoi(row[5]) < 0) {
			Log.Out(Logs::Detail, Logs::Trading, "Bad Slot number when trying to load trader information!\n");
			continue;
		}

		loadti->ItemID[atoi(row[5])] = atoi(row[1]);
		loadti->SerialNumber[atoi(row[5])] = atoi(row[2]);
		loadti->Charges[atoi(row[5])] = atoi(row[3]);
		loadti->ItemCost[atoi(row[5])] = atoi(row[4]);
	}
	return loadti;
}

ItemInst* ZoneDatabase::LoadSingleTraderItem(uint32 CharID, int SerialNumber) {
	std::string query = StringFormat("SELECT * FROM trader WHERE char_id = %i AND serialnumber = %i "
                                    "ORDER BY slot_id LIMIT 80", CharID, SerialNumber);
    auto results = QueryDatabase(query);
    if (!results.Success())
        return nullptr;

	if (results.RowCount() == 0) {
        Log.Out(Logs::Detail, Logs::Trading, "Bad result from query\n"); fflush(stdout);
        return nullptr;
    }

    auto row = results.begin();

    int ItemID = atoi(row[1]);
	int Charges = atoi(row[3]);
	int Cost = atoi(row[4]);

    const Item_Struct *item = database.GetItem(ItemID);

	if(!item) {
		Log.Out(Logs::Detail, Logs::Trading, "Unable to create item\n");
		fflush(stdout);
		return nullptr;
	}

    if (item->NoDrop == 0)
        return nullptr;

    ItemInst* inst = database.CreateItem(item);
	if(!inst) {
		Log.Out(Logs::Detail, Logs::Trading, "Unable to create item instance\n");
		fflush(stdout);
		return nullptr;
	}

    inst->SetCharges(Charges);
	inst->SetSerialNumber(SerialNumber);
	inst->SetMerchantSlot(SerialNumber);
	inst->SetPrice(Cost);

	if(inst->IsStackable())
		inst->SetMerchantCount(Charges);

	return inst;
}

void ZoneDatabase::SaveTraderItem(uint32 CharID, uint32 ItemID, uint32 SerialNumber, int32 Charges, uint32 ItemCost, uint8 Slot){

	std::string query = StringFormat("REPLACE INTO trader VALUES(%i, %i, %i, %i, %i, %i)",
                                    CharID, ItemID, SerialNumber, Charges, ItemCost, Slot);
    auto results = QueryDatabase(query);
    if (!results.Success())
        Log.Out(Logs::Detail, Logs::None, "[CLIENT] Failed to save trader item: %i for char_id: %i, the error was: %s\n", ItemID, CharID, results.ErrorMessage().c_str());

}

void ZoneDatabase::UpdateTraderItemCharges(int CharID, uint32 SerialNumber, int32 Charges) {
	Log.Out(Logs::Detail, Logs::Trading, "ZoneDatabase::UpdateTraderItemCharges(%i, %i, %i)", CharID, SerialNumber, Charges);

	std::string query = StringFormat("UPDATE trader SET charges = %i WHERE char_id = %i AND serialnumber = %i",
                                    Charges, CharID, SerialNumber);
    auto results = QueryDatabase(query);
    if (!results.Success())
		Log.Out(Logs::Detail, Logs::None, "[CLIENT] Failed to update charges for trader item: %i for char_id: %i, the error was: %s\n",
                                SerialNumber, CharID, results.ErrorMessage().c_str());

}

void ZoneDatabase::UpdateTraderItemPrice(int CharID, uint32 ItemID, uint32 Charges, uint32 NewPrice) {

	Log.Out(Logs::Detail, Logs::Trading, "ZoneDatabase::UpdateTraderPrice(%i, %i, %i, %i)", CharID, ItemID, Charges, NewPrice);

	const Item_Struct *item = database.GetItem(ItemID);

	if(!item)
		return;

	if(NewPrice == 0) {
		Log.Out(Logs::Detail, Logs::Trading, "Removing Trader items from the DB for CharID %i, ItemID %i", CharID, ItemID);

        std::string query = StringFormat("DELETE FROM trader WHERE char_id = %i AND item_id = %i",CharID, ItemID);
        auto results = QueryDatabase(query);
        if (!results.Success())
			Log.Out(Logs::Detail, Logs::None, "[CLIENT] Failed to remove trader item(s): %i for char_id: %i, the error was: %s\n", ItemID, CharID, results.ErrorMessage().c_str());

		return;
	}

    if(!item->Stackable) {
        std::string query = StringFormat("UPDATE trader SET item_cost = %i "
                                        "WHERE char_id = %i AND item_id = %i AND charges=%i",
                                        NewPrice, CharID, ItemID, Charges);
        auto results = QueryDatabase(query);
        if (!results.Success())
            Log.Out(Logs::Detail, Logs::None, "[CLIENT] Failed to update price for trader item: %i for char_id: %i, the error was: %s\n", ItemID, CharID, results.ErrorMessage().c_str());

        return;
    }

    std::string query = StringFormat("UPDATE trader SET item_cost = %i "
                                    "WHERE char_id = %i AND item_id = %i",
                                    NewPrice, CharID, ItemID);
    auto results = QueryDatabase(query);
    if (!results.Success())
            Log.Out(Logs::Detail, Logs::None, "[CLIENT] Failed to update price for trader item: %i for char_id: %i, the error was: %s\n", ItemID, CharID, results.ErrorMessage().c_str());
}

void ZoneDatabase::DeleteTraderItem(uint32 char_id){

	if(char_id==0) {
        const std::string query = "DELETE FROM trader";
        auto results = QueryDatabase(query);
		if (!results.Success())
			Log.Out(Logs::Detail, Logs::None, "[CLIENT] Failed to delete all trader items data, the error was: %s\n", results.ErrorMessage().c_str());

        return;
	}

	std::string query = StringFormat("DELETE FROM trader WHERE char_id = %i", char_id);
	auto results = QueryDatabase(query);
    if (!results.Success())
        Log.Out(Logs::Detail, Logs::None, "[CLIENT] Failed to delete trader item data for char_id: %i, the error was: %s\n", char_id, results.ErrorMessage().c_str());

}
void ZoneDatabase::DeleteTraderItem(uint32 CharID,uint16 SlotID) {

	std::string query = StringFormat("DELETE FROM trader WHERE char_id = %i And slot_id = %i", CharID, SlotID);
	auto results = QueryDatabase(query);
	if (!results.Success())
		Log.Out(Logs::Detail, Logs::None, "[CLIENT] Failed to delete trader item data for char_id: %i, the error was: %s\n",CharID, results.ErrorMessage().c_str());
}

bool ZoneDatabase::LoadCharacterData(uint32 character_id, PlayerProfile_Struct* pp, ExtendedProfile_Struct* m_epp){
	std::string query = StringFormat(
		"SELECT                     "
		"`name`,                    "
		"last_name,                 "
		"gender,                    "
		"race,                      "
		"class,                     "
		"`level`,                   "
		"deity,                     "
		"birthday,                  "
		"last_login,                "
		"time_played,               "
		"pvp_status,                "
		"level2,                    "
		"anon,                      "
		"gm,                        "
		"intoxication,              "
		"hair_color,                "
		"beard_color,               "
		"eye_color_1,               "
		"eye_color_2,               "
		"hair_style,                "
		"beard,                     "
		"title,                     "
		"suffix,                    "
		"exp,                       "
		"points,                    "
		"mana,                      "
		"cur_hp,                    "
		"str,                       "
		"sta,                       "
		"cha,                       "
		"dex,                       "
		"`int`,                     "
		"agi,                       "
		"wis,                       "
		"face,                      "
		"y,                         "
		"x,                         "
		"z,                         "
		"heading,                   "
		"autosplit_enabled,         "
		"zone_change_count,         "
		"hunger_level,              "
		"thirst_level,              "
		"zone_id,                   "
		"zone_instance,             "
		"endurance,                 "
		"air_remaining,             "
		"aa_points_spent,           "
		"aa_exp,                    "
		"aa_points,                 "
		"boatid,					"
		"`boatname`,				"
		"famished,					"
		"`e_aa_effects`,			"
		"`e_percent_to_aa`,			"
		"`e_expended_aa_spent`		"
		"FROM                       "
		"character_data             "
		"WHERE `id` = %i         ", character_id);
	auto results = database.QueryDatabase(query); int r = 0;
	for (auto row = results.begin(); row != results.end(); ++row) {
		strcpy(pp->name, row[r]); r++;											 // "`name`,                    "
		strcpy(pp->last_name, row[r]); r++;										 // "last_name,                 "
		pp->gender = atoi(row[r]); r++;											 // "gender,                    "
		pp->race = atoi(row[r]); r++;											 // "race,                      "
		pp->class_ = atoi(row[r]); r++;											 // "class,                     "
		pp->level = atoi(row[r]); r++;											 // "`level`,                   "
		pp->deity = atoi(row[r]); r++;											 // "deity,                     "
		pp->birthday = atoi(row[r]); r++;										 // "birthday,                  "
		pp->lastlogin = atoi(row[r]); r++;										 // "last_login,                "
		pp->timePlayedMin = atoi(row[r]); r++;									 // "time_played,               "
		pp->pvp = atoi(row[r]); r++;											 // "pvp_status,                "
		pp->level2 = atoi(row[r]); r++;											 // "level2,                    "
		pp->anon = atoi(row[r]); r++;											 // "anon,                      "
		pp->gm = atoi(row[r]); r++;												 // "gm,                        "
		pp->intoxication = atoi(row[r]); r++;									 // "intoxication,              "
		pp->haircolor = atoi(row[r]); r++;										 // "hair_color,                "
		pp->beardcolor = atoi(row[r]); r++;										 // "beard_color,               "
		pp->eyecolor1 = atoi(row[r]); r++;										 // "eye_color_1,               "
		pp->eyecolor2 = atoi(row[r]); r++;										 // "eye_color_2,               "
		pp->hairstyle = atoi(row[r]); r++;										 // "hair_style,                "
		pp->beard = atoi(row[r]); r++;											 // "beard,                     "
		strcpy(pp->title, row[r]); r++;											 // "title,                     "
		strcpy(pp->suffix, row[r]); r++;										 // "suffix,                    "
		pp->exp = atoi(row[r]); r++;											 // "exp,                       "
		pp->points = atoi(row[r]); r++;											 // "points,                    "
		pp->mana = atoi(row[r]); r++;											 // "mana,                      "
		pp->cur_hp = atoi(row[r]); r++;											 // "cur_hp,                    "
		pp->STR = atoi(row[r]); r++;											 // "str,                       "
		pp->STA = atoi(row[r]); r++;											 // "sta,                       "
		pp->CHA = atoi(row[r]); r++;											 // "cha,                       "
		pp->DEX = atoi(row[r]); r++;											 // "dex,                       "
		pp->INT = atoi(row[r]); r++;											 // "`int`,                     "
		pp->AGI = atoi(row[r]); r++;											 // "agi,                       "
		pp->WIS = atoi(row[r]); r++;											 // "wis,                       "
		pp->face = atoi(row[r]); r++;											 // "face,                      "
		pp->y = atof(row[r]); r++;												 // "y,                         "
		pp->x = atof(row[r]); r++;												 // "x,                         "
		pp->z = atof(row[r]); r++;												 // "z,                         "
		pp->heading = atof(row[r]); r++;										 // "heading,                   "
		pp->autosplit = atoi(row[r]); r++;										 // "autosplit_enabled,         "
		pp->zone_change_count = atoi(row[r]); r++;								 // "zone_change_count,         "
		pp->hunger_level = atoi(row[r]); r++;									 // "hunger_level,              "
		pp->thirst_level = atoi(row[r]); r++;									 // "thirst_level,              "
		pp->zone_id = atoi(row[r]); r++;										 // "zone_id,                   "
		pp->zoneInstance = atoi(row[r]); r++;									 // "zone_instance,             "
		pp->endurance = atoi(row[r]); r++;										 // "endurance,                 "
		pp->air_remaining = atoi(row[r]); r++;									 // "air_remaining,             "
		pp->aapoints_spent = atoi(row[r]); r++;									 // "aa_points_spent,           "
		pp->expAA = atoi(row[r]); r++;											 // "aa_exp,                    "
		pp->aapoints = atoi(row[r]); r++;										 // "aa_points,                 "
		pp->boatid = atoi(row[r]); r++;											 // "boatid,					"
		strncpy(pp->boat, row[r], 32); r++;										 // "boatname					"
		pp->famished = atoi(row[r]); r++;										 // "famished,					"
		m_epp->aa_effects = atoi(row[r]); r++;									 // "`e_aa_effects`,			"
		m_epp->perAA = atoi(row[r]); r++;										 // "`e_percent_to_aa`,			"
		m_epp->expended_aa = atoi(row[r]); r++;									 // "`e_expended_aa_spent`,		"
	}
	return true;
}

bool ZoneDatabase::LoadCharacterFactionValues(uint32 character_id, faction_map & val_list) {
	std::string query = StringFormat("SELECT `faction_id`, `current_value` FROM `character_faction_values` WHERE `id` = %i", character_id);
	auto results = database.QueryDatabase(query);
	for (auto row = results.begin(); row != results.end(); ++row) 
	{ 
		val_list[atoi(row[0])] = atoi(row[1]); 
	}
	return true;
}

bool ZoneDatabase::LoadCharacterMemmedSpells(uint32 character_id, PlayerProfile_Struct* pp){
	std::string query = StringFormat(
		"SELECT							"
		"slot_id,						"
		"`spell_id`						"
		"FROM							"
		"`character_memmed_spells`		"
		"WHERE `id` = %u ORDER BY `slot_id`", character_id);
	auto results = database.QueryDatabase(query);
	int i = 0;
	/* Initialize Spells */
	for (i = 0; i < MAX_PP_MEMSPELL; i++){
		pp->mem_spells[i] = 0xFFFFFFFF;
	}
	for (auto row = results.begin(); row != results.end(); ++row) {
		i = atoi(row[0]);
		if (i < MAX_PP_MEMSPELL && atoi(row[1]) <= SPDAT_RECORDS){
			pp->mem_spells[i] = atoi(row[1]);
		}
	}
	return true;
}

bool ZoneDatabase::LoadCharacterSpellBook(uint32 character_id, PlayerProfile_Struct* pp){
	std::string query = StringFormat(
		"SELECT					"
		"slot_id,				"
		"`spell_id`				"
		"FROM					"
		"`character_spells`		"
		"WHERE `id` = %u ORDER BY `slot_id`", character_id);
	auto results = database.QueryDatabase(query);
	int i = 0;
	/* Initialize Spells */
	for (i = 0; i < MAX_PP_SPELLBOOK; i++){
		pp->spell_book[i] = 0xFFFFFFFF;
	}
	for (auto row = results.begin(); row != results.end(); ++row) {
		i = atoi(row[0]);
		if (i < MAX_PP_SPELLBOOK && atoi(row[1]) <= SPDAT_RECORDS){
			pp->spell_book[i] = atoi(row[1]);
		}
	}
	return true;
}

bool ZoneDatabase::LoadCharacterLanguages(uint32 character_id, PlayerProfile_Struct* pp){
	std::string query = StringFormat(
		"SELECT					"
		"lang_id,				"
		"`value`				"
		"FROM					"
		"`character_languages`	"
		"WHERE `id` = %u ORDER BY `lang_id`", character_id);
	auto results = database.QueryDatabase(query); int i = 0;
	/* Initialize Languages */
	for (i = 0; i < MAX_PP_LANGUAGE; i++){
		pp->languages[i] = 0;
	}
	for (auto row = results.begin(); row != results.end(); ++row) {
		i = atoi(row[0]);
		if (i < MAX_PP_LANGUAGE){
			pp->languages[i] = atoi(row[1]);
		}
	}
	return true;
}

bool ZoneDatabase::LoadCharacterSkills(uint32 character_id, PlayerProfile_Struct* pp){
	std::string query = StringFormat(
		"SELECT				"
		"skill_id,			"
		"`value`			"
		"FROM				"
		"`character_skills` "
		"WHERE `id` = %u ORDER BY `skill_id`", character_id);
	auto results = database.QueryDatabase(query); int i = 0;
	/* Initialize Skill */
	for (i = 0; i < MAX_PP_SKILL; i++){
		pp->skills[i] = 0;
	}
	for (auto row = results.begin(); row != results.end(); ++row) {
		i = atoi(row[0]);
		if (i < MAX_PP_SKILL){
			pp->skills[i] = atoi(row[1]);
		}
	}
	return true;
}

bool ZoneDatabase::LoadCharacterCurrency(uint32 character_id, PlayerProfile_Struct* pp){
	std::string query = StringFormat(
		"SELECT                  "
		"platinum,               "
		"gold,                   "
		"silver,                 "
		"copper,                 "
		"platinum_bank,          "
		"gold_bank,              "
		"silver_bank,            "
		"copper_bank,            "
		"platinum_cursor,        "
		"gold_cursor,            "
		"silver_cursor,          "
		"copper_cursor           "
		"FROM                    "
		"character_currency      "
		"WHERE `id` = %i         ", character_id);
	auto results = database.QueryDatabase(query);
	for (auto row = results.begin(); row != results.end(); ++row) {
		pp->platinum = atoi(row[0]);
		pp->gold = atoi(row[1]);
		pp->silver = atoi(row[2]);
		pp->copper = atoi(row[3]);
		pp->platinum_bank = atoi(row[4]);
		pp->gold_bank = atoi(row[5]);
		pp->silver_bank = atoi(row[6]);
		pp->copper_bank = atoi(row[7]);
		pp->platinum_cursor = atoi(row[8]);
		pp->gold_cursor = atoi(row[9]);
		pp->silver_cursor = atoi(row[10]);
		pp->copper_cursor = atoi(row[11]);

	}
	return true;
}

bool ZoneDatabase::LoadCharacterMaterialColor(uint32 character_id, PlayerProfile_Struct* pp){
	std::string query = StringFormat("SELECT slot, blue, green, red, use_tint, color FROM `character_material` WHERE `id` = %u LIMIT 9", character_id);
	auto results = database.QueryDatabase(query); int i = 0; int r = 0;
	for (auto row = results.begin(); row != results.end(); ++row) {
		r = 0;
		i = atoi(row[r]); /* Slot */ r++;
		pp->item_tint[i].rgb.blue = atoi(row[r]); r++;
		pp->item_tint[i].rgb.green = atoi(row[r]); r++;
		pp->item_tint[i].rgb.red = atoi(row[r]); r++;
		pp->item_tint[i].rgb.use_tint = atoi(row[r]);
	}
	return true;
}

bool ZoneDatabase::LoadCharacterBindPoint(uint32 character_id, PlayerProfile_Struct* pp){
	std::string query = StringFormat("SELECT `zone_id`, `instance_id`, `x`, `y`, `z`, `heading`, `is_home` FROM `character_bind` WHERE `id` = %u LIMIT 2", character_id);
	auto results = database.QueryDatabase(query); int i = 0;
	for (auto row = results.begin(); row != results.end(); ++row) {
		i = 0;
		/* Is home bind */
		if (atoi(row[6]) == 1){
			pp->binds[4].zoneId = atoi(row[i++]);
			pp->binds[4].instance_id = atoi(row[i++]);
			pp->binds[4].x = atoi(row[i++]);
			pp->binds[4].y = atoi(row[i++]);
			pp->binds[4].z = atoi(row[i++]);
			pp->binds[4].heading = atoi(row[i++]);
		}
		/* Is regular bind point */
		else{
			pp->binds[0].zoneId = atoi(row[i++]);
			pp->binds[0].instance_id = atoi(row[i++]);
			pp->binds[0].x = atoi(row[i++]);
			pp->binds[0].y = atoi(row[i++]);
			pp->binds[0].z = atoi(row[i++]);
			pp->binds[0].heading = atoi(row[i++]);
		}
	}
	return true;
}

bool ZoneDatabase::SaveCharacterLanguage(uint32 character_id, uint32 lang_id, uint32 value){
	std::string query = StringFormat("REPLACE INTO `character_languages` (id, lang_id, value) VALUES (%u, %u, %u)", character_id, lang_id, value); QueryDatabase(query);
	Log.Out(Logs::General, Logs::Character, "ZoneDatabase::SaveCharacterLanguage for character ID: %i, lang_id:%u value:%u done", character_id, lang_id, value);
	return true;
}

bool ZoneDatabase::SaveCharacterBindPoint(uint32 character_id, uint32 zone_id, uint32 instance_id, const glm::vec4& position, uint8 is_home){
	if (zone_id <= 0) {
		return false;
	}

	/* Save Home Bind Point */
	std::string query = StringFormat("REPLACE INTO `character_bind` (id, zone_id, instance_id, x, y, z, heading, is_home)"
		" VALUES (%u, %u, %u, %f, %f, %f, %f, %i)", character_id, zone_id, instance_id, position.x, position.y, position.z, position.w, is_home);
	Log.Out(Logs::General, Logs::Character, "ZoneDatabase::SaveCharacterBindPoint for character ID: %i zone_id: %u instance_id: %u position: %s ishome: %u", character_id, zone_id, instance_id, to_string(position).c_str(), is_home);
	auto results = QueryDatabase(query);
	if (!results.RowsAffected()) {
	}
	return true;
}

bool ZoneDatabase::SaveCharacterMaterialColor(uint32 character_id, uint32 slot_id, uint32 color){
	uint8 red = (color & 0x00FF0000) >> 16;
	uint8 green = (color & 0x0000FF00) >> 8;
	uint8 blue = (color & 0x000000FF);

	std::string query = StringFormat("REPLACE INTO `character_material` (id, slot, red, green, blue, color, use_tint) VALUES (%u, %u, %u, %u, %u, %u, 255)", character_id, slot_id, red, green, blue, color); auto results = QueryDatabase(query);
	Log.Out(Logs::General, Logs::Character, "ZoneDatabase::SaveCharacterMaterialColor for character ID: %i, slot_id: %u color: %u done", character_id, slot_id, color);
	return true;
}

bool ZoneDatabase::SaveCharacterSkill(uint32 character_id, uint32 skill_id, uint32 value){
	std::string query = StringFormat("REPLACE INTO `character_skills` (id, skill_id, value) VALUES (%u, %u, %u)", character_id, skill_id, value); auto results = QueryDatabase(query);
	Log.Out(Logs::General, Logs::Character, "ZoneDatabase::SaveCharacterSkill for character ID: %i, skill_id:%u value:%u done", character_id, skill_id, value);
	return true;
}

bool ZoneDatabase::SaveCharacterData(uint32 character_id, uint32 account_id, PlayerProfile_Struct* pp, ExtendedProfile_Struct* m_epp){
	clock_t t = std::clock(); /* Function timer start */
	std::string query = StringFormat(
		"REPLACE INTO `character_data` ("
		" id,                        "
		" account_id,                "
		" `name`,                    "
		" last_name,                 "
		" gender,                    "
		" race,                      "
		" class,                     "
		" `level`,                   "
		" deity,                     "
		" birthday,                  "
		" last_login,                "
		" time_played,               "
		" pvp_status,                "
		" level2,                    "
		" anon,                      "
		" gm,                        "
		" intoxication,              "
		" hair_color,                "
		" beard_color,               "
		" eye_color_1,               "
		" eye_color_2,               "
		" hair_style,                "
		" beard,                     "
		" title,                     "
		" suffix,                    "
		" exp,                       "
		" points,                    "
		" mana,                      "
		" cur_hp,                    "
		" str,                       "
		" sta,                       "
		" cha,                       "
		" dex,                       "
		" `int`,                     "
		" agi,                       "
		" wis,                       "
		" face,                      "
		" y,                         "
		" x,                         "
		" z,                         "
		" heading,                   "
		" autosplit_enabled,         "
		" zone_change_count,         "
		" hunger_level,              "
		" thirst_level,              "
		" zone_id,                   "
		" zone_instance,             "
		" endurance,                 "
		" air_remaining,             "
		" aa_points_spent,           "
		" aa_exp,                    "
		" aa_points,                 "
		" boatid,					 "
		" `boatname`,				 "
		" famished,					 "
		" e_aa_effects,				 "
		" e_percent_to_aa,			 "
		" e_expended_aa_spent		 "
		")							 "
		"VALUES ("
		"%u,"  // id																" id,                        "
		"%u,"  // account_id														" account_id,                "
		"'%s',"  // `name`					  pp->name,								" `name`,                    "
		"'%s',"  // last_name					pp->last_name,						" last_name,                 "
		"%u,"  // gender					  pp->gender,							" gender,                    "
		"%u,"  // race						  pp->race,								" race,                      "
		"%u,"  // class						  pp->class_,							" class,                     "
		"%u,"  // `level`					  pp->level,							" `level`,                   "
		"%u,"  // deity						  pp->deity,							" deity,                     "
		"%u,"  // birthday					  pp->birthday,							" birthday,                  "
		"%u,"  // last_login				  pp->lastlogin,						" last_login,                "
		"%u,"  // time_played				  pp->timePlayedMin,					" time_played,               "
		"%u,"  // pvp_status				  pp->pvp,								" pvp_status,                "
		"%u,"  // level2					  pp->level2,							" level2,                    "
		"%u,"  // anon						  pp->anon,								" anon,                      "
		"%u,"  // gm						  pp->gm,								" gm,                        "
		"%u,"  // intoxication				  pp->intoxication,						" intoxication,              "
		"%u,"  // hair_color				  pp->haircolor,						" hair_color,                "
		"%u,"  // beard_color				  pp->beardcolor,						" beard_color,               "
		"%u,"  // eye_color_1				  pp->eyecolor1,						" eye_color_1,               "
		"%u,"  // eye_color_2				  pp->eyecolor2,						" eye_color_2,               "
		"%u,"  // hair_style				  pp->hairstyle,						" hair_style,                "
		"%u,"  // beard						  pp->beard,							" beard,                     "
		"'%s',"  // title						  pp->title,						" title,                     "   "
		"'%s',"  // suffix					  pp->suffix,							" suffix,                    "
		"%u,"  // exp						  pp->exp,								" exp,                       "
		"%u,"  // points					  pp->points,							" points,                    "
		"%u,"  // mana						  pp->mana,								" mana,                      "
		"%u,"  // cur_hp					  pp->cur_hp,							" cur_hp,                    "
		"%u,"  // str						  pp->STR,								" str,                       "
		"%u,"  // sta						  pp->STA,								" sta,                       "
		"%u,"  // cha						  pp->CHA,								" cha,                       "
		"%u,"  // dex						  pp->DEX,								" dex,                       "
		"%u,"  // `int`						  pp->INT,								" `int`,                     "
		"%u,"  // agi						  pp->AGI,								" agi,                       "
		"%u,"  // wis						  pp->WIS,								" wis,                       "
		"%u,"  // face						  pp->face,								" face,                      "
		"%f,"  // y							  pp->y,								" y,                         "
		"%f,"  // x							  pp->x,								" x,                         "
		"%f,"  // z							  pp->z,								" z,                         "
		"%f,"  // heading					  pp->heading,							" heading,                   "
		"%u,"  // autosplit_enabled			  pp->autosplit,						" autosplit_enabled,         "
		"%u,"  // zone_change_count			  pp->zone_change_count,				" zone_change_count,         "
		"%i,"  // hunger_level				  pp->hunger_level,						" hunger_level,              "
		"%i,"  // thirst_level				  pp->thirst_level,						" thirst_level,              "
		"%u,"  // zone_id					  pp->zone_id,							" zone_id,                   "
		"%u,"  // zone_instance				  pp->zoneInstance,						" zone_instance,             "
		"%u,"  // endurance					  pp->endurance,						" endurance,                 "
		"%u,"  // air_remaining				  pp->air_remaining,					" air_remaining,             "
		"%u,"  // aa_points_spent			  pp->aapoints_spent,					" aa_points_spent,           "
		"%u,"  // aa_exp					  pp->expAA,							" aa_exp,                    "
		"%u,"  // aa_points					  pp->aapoints,							" aa_points,                 "
		"%u,"  // boatid					  pp->boatid,							" boatid					 "
		"'%s'," // `boatname`				  pp->boat,								" `boatname`,                "
		"%u,"	//famished					  pp->famished							" famished					 "
		"%u,"  // e_aa_effects
		"%u,"  // e_percent_to_aa
		"%u"   // e_expended_aa_spent
		")",
		character_id,					  // " id,                        "
		account_id,						  // " account_id,                "
		EscapeString(pp->name).c_str(),						  // " `name`,                    "
		EscapeString(pp->last_name).c_str(),					  // " last_name,                 "
		pp->gender,						  // " gender,                    "
		pp->race,						  // " race,                      "
		pp->class_,						  // " class,                     "
		pp->level,						  // " `level`,                   "
		pp->deity,						  // " deity,                     "
		pp->birthday,					  // " birthday,                  "
		pp->lastlogin,					  // " last_login,                "
		pp->timePlayedMin,				  // " time_played,               "
		pp->pvp,						  // " pvp_status,                "
		pp->level2,						  // " level2,                    "
		pp->anon,						  // " anon,                      "
		pp->gm,							  // " gm,                        "
		pp->intoxication,				  // " intoxication,              "
		pp->haircolor,					  // " hair_color,                "
		pp->beardcolor,					  // " beard_color,               "
		pp->eyecolor1,					  // " eye_color_1,               "
		pp->eyecolor2,					  // " eye_color_2,               "
		pp->hairstyle,					  // " hair_style,                "
		pp->beard,						  // " beard,                     "
		EscapeString(pp->title).c_str(),						  // " title,                     "
		EscapeString(pp->suffix).c_str(),						  // " suffix,                    "
		pp->exp,						  // " exp,                       "
		pp->points,						  // " points,                    "
		pp->mana,						  // " mana,                      "
		pp->cur_hp,						  // " cur_hp,                    "
		pp->STR,						  // " str,                       "
		pp->STA,						  // " sta,                       "
		pp->CHA,						  // " cha,                       "
		pp->DEX,						  // " dex,                       "
		pp->INT,						  // " `int`,                     "
		pp->AGI,						  // " agi,                       "
		pp->WIS,						  // " wis,                       "
		pp->face,						  // " face,                      "
		pp->y,							  // " y,                         "
		pp->x,							  // " x,                         "
		pp->z,							  // " z,                         "
		pp->heading,					  // " heading,                   "
		pp->autosplit,					  // " autosplit_enabled,         "
		pp->zone_change_count,			  // " zone_change_count,         "
		pp->hunger_level,				  // " hunger_level,              "
		pp->thirst_level,				  // " thirst_level,              "
		pp->zone_id,					  // " zone_id,                   "
		pp->zoneInstance,				  // " zone_instance,             "
		pp->endurance,					  // " endurance,                 "
		pp->air_remaining,				  // " air_remaining,             "
		pp->aapoints_spent,				  // " aa_points_spent,           "
		pp->expAA,						  // " aa_exp,                    "
		pp->aapoints,					  // " aa_points,                 "
		pp->boatid,						  // "boatid,					  "
		EscapeString(pp->boat).c_str(),	  // " boatname                   "
		pp->famished,					  // " famished					  "
		m_epp->aa_effects,
		m_epp->perAA,
		m_epp->expended_aa
	);
	auto results = database.QueryDatabase(query);
	Log.Out(Logs::General, Logs::Character, "ZoneDatabase::SaveCharacterData %i, done... Took %f seconds", character_id, ((float)(std::clock() - t)) / CLOCKS_PER_SEC);
	return true;
}

bool ZoneDatabase::SaveCharacterCurrency(uint32 character_id, PlayerProfile_Struct* pp){
	if (pp->copper < 0) { pp->copper = 0; }
	if (pp->silver < 0) { pp->silver = 0; }
	if (pp->gold < 0) { pp->gold = 0; }
	if (pp->platinum < 0) { pp->platinum = 0; }
	if (pp->copper_bank < 0) { pp->copper_bank = 0; }
	if (pp->silver_bank < 0) { pp->silver_bank = 0; }
	if (pp->gold_bank < 0) { pp->gold_bank = 0; }
	if (pp->platinum_bank < 0) { pp->platinum_bank = 0; }
	if (pp->platinum_cursor < 0) { pp->platinum_cursor = 0; }
	if (pp->gold_cursor < 0) { pp->gold_cursor = 0; }
	if (pp->silver_cursor < 0) { pp->silver_cursor = 0; }
	if (pp->copper_cursor < 0) { pp->copper_cursor = 0; }
	std::string query = StringFormat(
		"REPLACE INTO `character_currency` (id, platinum, gold, silver, copper,"
		"platinum_bank, gold_bank, silver_bank, copper_bank,"
		"platinum_cursor, gold_cursor, silver_cursor, copper_cursor)"
		"VALUES (%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u)",
		character_id,
		pp->platinum,
		pp->gold,
		pp->silver,
		pp->copper,
		pp->platinum_bank,
		pp->gold_bank,
		pp->silver_bank,
		pp->copper_bank,
		pp->platinum_cursor,
		pp->gold_cursor,
		pp->silver_cursor,
		pp->copper_cursor);
	auto results = database.QueryDatabase(query);
	Log.Out(Logs::General, Logs::Character, "Saving Currency for character ID: %i, done", character_id);
	return true;
}

bool ZoneDatabase::SaveCharacterAA(uint32 character_id, uint32 aa_id, uint32 current_level){
	std::string rquery = StringFormat("REPLACE INTO `character_alternate_abilities` (id, aa_id, aa_value)"
		" VALUES (%u, %u, %u)",
		character_id, aa_id, current_level);
	auto results = QueryDatabase(rquery);
	Log.Out(Logs::General, Logs::Character, "Saving AA for character ID: %u, aa_id: %u current_level: %u", character_id, aa_id, current_level);
	return true;
}

bool ZoneDatabase::SaveCharacterMemorizedSpell(uint32 character_id, uint32 spell_id, uint32 slot_id){
	if (spell_id > SPDAT_RECORDS){ return false; }
	std::string query = StringFormat("REPLACE INTO `character_memmed_spells` (id, slot_id, spell_id) VALUES (%u, %u, %u)", character_id, slot_id, spell_id);
	QueryDatabase(query);
	return true;
}

bool ZoneDatabase::SaveCharacterSpell(uint32 character_id, uint32 spell_id, uint32 slot_id){
	if (spell_id > SPDAT_RECORDS){ return false; }
	std::string query = StringFormat("REPLACE INTO `character_spells` (id, slot_id, spell_id) VALUES (%u, %u, %u)", character_id, slot_id, spell_id);
	QueryDatabase(query);
	return true;
}

bool ZoneDatabase::SaveCharacterConsent(uint32 character_id, char name[64]){
	std::string query = StringFormat("REPLACE INTO `character_consent` (id, consented_name) VALUES (%u, '%s')", character_id, name);
	QueryDatabase(query);
	return true;
}

bool ZoneDatabase::DeleteCharacterSpell(uint32 character_id, uint32 spell_id, uint32 slot_id){
	std::string query = StringFormat("DELETE FROM `character_spells` WHERE `slot_id` = %u AND `id` = %u", slot_id, character_id);
	QueryDatabase(query);
	return true;
}

bool ZoneDatabase::DeleteCharacterAAs(uint32 character_id){
	std::string query = StringFormat("DELETE FROM `character_alternate_abilities` WHERE `id` = %u", character_id);
	QueryDatabase(query);
	return true;
}

bool ZoneDatabase::DeleteCharacterDye(uint32 character_id){
	std::string query = StringFormat("DELETE FROM `character_material` WHERE `id` = %u", character_id);
	QueryDatabase(query);
	return true;
}

bool ZoneDatabase::DeleteCharacterMemorizedSpell(uint32 character_id, uint32 spell_id, uint32 slot_id){
	std::string query = StringFormat("DELETE FROM `character_memmed_spells` WHERE `slot_id` = %u AND `id` = %u", slot_id, character_id);
	QueryDatabase(query);
	return true;
}

bool ZoneDatabase::DeleteCharacterConsent(uint32 character_id, char name[64]){
	std::string query = StringFormat("DELETE FROM `character_consent` WHERE `consented_name` = '%s' AND `id` = %u", name, character_id);
	QueryDatabase(query);
	return true;
}

bool ZoneDatabase::NoRentExpired(const char* name){
	std::string query = StringFormat("SELECT (UNIX_TIMESTAMP(NOW()) - last_login) FROM `character_data` WHERE name = '%s'", name);
	auto results = QueryDatabase(query);
	if (!results.Success())
        return false;

    if (results.RowCount() != 1)
        return false;

	auto row = results.begin();
	uint32 seconds = atoi(row[0]);

	return (seconds>1800);
}

/* Searches npctable for matching id, and returns the item if found,
 * or nullptr otherwise. If id passed is 0, loads all npc_types for
 * the current zone, returning the last item added.
 */
const NPCType* ZoneDatabase::GetNPCType (uint32 id) {
	const NPCType *npc=nullptr;

	// If NPC is already in tree, return it.
	auto itr = zone->npctable.find(id);
	if(itr != zone->npctable.end())
		return itr->second;

    // Otherwise, get NPCs from database.


    // If id is 0, load all npc_types for the current zone,
    // according to spawn2.
    std::string query = StringFormat("SELECT npc_types.id, npc_types.name, npc_types.level, npc_types.race, "
                        "npc_types.class, npc_types.hp, npc_types.mana, npc_types.gender, "
                        "npc_types.texture, npc_types.helmtexture, npc_types.size, "
                        "npc_types.loottable_id, npc_types.merchant_id, "
                        "npc_types.trap_template, "
                        "npc_types.STR, npc_types.STA, npc_types.DEX, npc_types.AGI, npc_types._INT, "
                        "npc_types.WIS, npc_types.CHA, npc_types.MR, npc_types.CR, npc_types.DR, "
                        "npc_types.FR, npc_types.PR, npc_types.Corrup, npc_types.PhR, "
                        "npc_types.mindmg, npc_types.maxdmg, npc_types.attack_count, npc_types.special_abilities, "
                        "npc_types.npc_spells_id, npc_types.npc_spells_effects_id, npc_types.d_melee_texture1, "
                        "npc_types.d_melee_texture2, npc_types.ammo_idfile, npc_types.prim_melee_type, "
                        "npc_types.sec_melee_type, npc_types.ranged_type, npc_types.runspeed, npc_types.findable, "
                        "npc_types.trackable, npc_types.hp_regen_rate, npc_types.mana_regen_rate, "
                        "npc_types.aggroradius, npc_types.assistradius, npc_types.bodytype, npc_types.npc_faction_id, "
                        "npc_types.face, npc_types.luclin_hairstyle, npc_types.luclin_haircolor, "
                        "npc_types.luclin_eyecolor, npc_types.luclin_eyecolor2, npc_types.luclin_beardcolor,"
                        "npc_types.luclin_beard, npc_types.armortint_id, "
                        "npc_types.armortint_red, npc_types.armortint_green, npc_types.armortint_blue, "
                        "npc_types.see_invis, npc_types.see_invis_undead, npc_types.lastname, "
                        "npc_types.qglobal, npc_types.AC, npc_types.npc_aggro, npc_types.spawn_limit, "
                        "npc_types.see_hide, npc_types.see_improved_hide, npc_types.ATK, npc_types.Accuracy, "
                        "npc_types.Avoidance, npc_types.slow_mitigation, npc_types.maxlevel, npc_types.scalerate, "
                        "npc_types.private_corpse, npc_types.unique_spawn_by_name, npc_types.underwater, "
                        "npc_types.emoteid, npc_types.spellscale, npc_types.healscale, npc_types.no_target_hotkey,"
                        "npc_types.raid_target, npc_types.attack_delay, npc_types.walkspeed, npc_types.combat_hp_regen, "
						"npc_types.combat_mana_regen, npc_types.light, npc_types.aggro_pc, "
						"npc_types.armtexture, npc_types.bracertexture, npc_types.handtexture, npc_types.legtexture, "
						"npc_types.feettexture, npc_types.chesttexture, npc_types.ignore_distance FROM npc_types WHERE id = %d", id);

    auto results = QueryDatabase(query);
    if (!results.Success()) {
        return nullptr;
    }


    for (auto row = results.begin(); row != results.end(); ++row) {
		NPCType *tmpNPCType;
		tmpNPCType = new NPCType;
		memset (tmpNPCType, 0, sizeof *tmpNPCType);

		tmpNPCType->npc_id = atoi(row[0]);

		strn0cpy(tmpNPCType->name, row[1], 50);

		tmpNPCType->level = atoi(row[2]);
		tmpNPCType->race = atoi(row[3]);
		tmpNPCType->class_ = atoi(row[4]);
		tmpNPCType->max_hp = atoi(row[5]);
		tmpNPCType->cur_hp = tmpNPCType->max_hp;
		tmpNPCType->Mana = atoi(row[6]);
		tmpNPCType->gender = atoi(row[7]);
		tmpNPCType->texture = atoi(row[8]);
		tmpNPCType->helmtexture = atoi(row[9]);
		tmpNPCType->size = atof(row[10]);
        tmpNPCType->loottable_id = atoi(row[11]);
		tmpNPCType->merchanttype = atoi(row[12]);
		tmpNPCType->trap_template = atoi(row[13]);
		tmpNPCType->STR = atoi(row[14]);
		tmpNPCType->STA = atoi(row[15]);
		tmpNPCType->DEX = atoi(row[16]);
		tmpNPCType->AGI = atoi(row[17]);
		tmpNPCType->INT = atoi(row[18]);
		tmpNPCType->WIS = atoi(row[19]);
		tmpNPCType->CHA = atoi(row[20]);
		tmpNPCType->MR = atoi(row[21]);
		tmpNPCType->CR = atoi(row[22]);
		tmpNPCType->DR = atoi(row[23]);
		tmpNPCType->FR = atoi(row[24]);
		tmpNPCType->PR = atoi(row[25]);
		tmpNPCType->Corrup = atoi(row[26]);
		tmpNPCType->PhR = atoi(row[27]);
		tmpNPCType->min_dmg = atoi(row[28]);
		tmpNPCType->max_dmg = atoi(row[29]);
		tmpNPCType->attack_count = atoi(row[30]);
		if (row[31] != nullptr)
			strn0cpy(tmpNPCType->special_abilities, row[31], 512);
		else
			tmpNPCType->special_abilities[0] = '\0';
		tmpNPCType->npc_spells_id = atoi(row[32]);
		tmpNPCType->npc_spells_effects_id = atoi(row[33]);
		tmpNPCType->d_melee_texture1 = atoi(row[34]);
		tmpNPCType->d_melee_texture2 = atoi(row[35]);
		strn0cpy(tmpNPCType->ammo_idfile, row[36], 30);
		tmpNPCType->prim_melee_type = atoi(row[37]);
		tmpNPCType->sec_melee_type = atoi(row[38]);
		tmpNPCType->ranged_type = atoi(row[39]);
		tmpNPCType->runspeed= atof(row[40]);
		tmpNPCType->findable = atoi(row[41]) == 0? false : true;
		tmpNPCType->trackable = atoi(row[42]) == 0? false : true;
		tmpNPCType->hp_regen = atoi(row[43]);
		tmpNPCType->mana_regen = atoi(row[44]);

		// set defaultvalue for aggroradius
        tmpNPCType->aggroradius = (int32)atoi(row[45]);
		if (tmpNPCType->aggroradius <= 0)
			tmpNPCType->aggroradius = 70;

		tmpNPCType->assistradius = (int32)atoi(row[46]);
		if (tmpNPCType->assistradius <= 0)
			tmpNPCType->assistradius = tmpNPCType->aggroradius;

		if (row[47] && strlen(row[47]))
            tmpNPCType->bodytype = (uint8)atoi(row[47]);
        else
            tmpNPCType->bodytype = 0;

		tmpNPCType->npc_faction_id = atoi(row[48]);

		tmpNPCType->luclinface = atoi(row[49]);
		tmpNPCType->hairstyle = atoi(row[50]);
		tmpNPCType->haircolor = atoi(row[51]);
		tmpNPCType->eyecolor1 = atoi(row[52]);
		tmpNPCType->eyecolor2 = atoi(row[53]);
		tmpNPCType->beardcolor = atoi(row[54]);
		tmpNPCType->beard = atoi(row[55]);

		uint32 armor_tint_id = atoi(row[56]);

		tmpNPCType->armor_tint[0] = (atoi(row[57]) & 0xFF) << 16;
        tmpNPCType->armor_tint[0] |= (atoi(row[58]) & 0xFF) << 8;
		tmpNPCType->armor_tint[0] |= (atoi(row[59]) & 0xFF);
		tmpNPCType->armor_tint[0] |= (tmpNPCType->armor_tint[0]) ? (0xFF << 24) : 0;

		if (armor_tint_id == 0)
			for (int index = MaterialChest; index <= EmuConstants::MATERIAL_END; index++)
				tmpNPCType->armor_tint[index] = tmpNPCType->armor_tint[0];
		else if (tmpNPCType->armor_tint[0] == 0)
        {
			std::string armortint_query = StringFormat("SELECT red1h, grn1h, blu1h, "
                                                    "red2c, grn2c, blu2c, "
                                                    "red3a, grn3a, blu3a, "
                                                    "red4b, grn4b, blu4b, "
                                                    "red5g, grn5g, blu5g, "
                                                    "red6l, grn6l, blu6l, "
                                                    "red7f, grn7f, blu7f, "
                                                    "red8x, grn8x, blu8x, "
                                                    "red9x, grn9x, blu9x "
                                                    "FROM npc_types_tint WHERE id = %d",
                                                    armor_tint_id);
            auto armortint_results = QueryDatabase(armortint_query);
            if (!armortint_results.Success() || armortint_results.RowCount() == 0)
                armor_tint_id = 0;
            else {
                auto armorTint_row = armortint_results.begin();

                for (int index = EmuConstants::MATERIAL_BEGIN; index <= EmuConstants::MATERIAL_END; index++) {
                    tmpNPCType->armor_tint[index] = atoi(armorTint_row[index * 3]) << 16;
					tmpNPCType->armor_tint[index] |= atoi(armorTint_row[index * 3 + 1]) << 8;
					tmpNPCType->armor_tint[index] |= atoi(armorTint_row[index * 3 + 2]);
					tmpNPCType->armor_tint[index] |= (tmpNPCType->armor_tint[index]) ? (0xFF << 24) : 0;
                }
            }
        } else
            armor_tint_id = 0;

		tmpNPCType->see_invis = atoi(row[60]);
		tmpNPCType->see_invis_undead = atoi(row[61]) == 0? false: true;	// Set see_invis_undead flag
		if (row[62] != nullptr)
			strn0cpy(tmpNPCType->lastname, row[62], 32);

		tmpNPCType->qglobal = atoi(row[63]) == 0? false: true;	// qglobal
		tmpNPCType->AC = atoi(row[64]);
		tmpNPCType->npc_aggro = atoi(row[65]) == 0? false: true;
		tmpNPCType->spawn_limit = atoi(row[66]);
		tmpNPCType->see_hide = atoi(row[67]) == 0? false: true;
		tmpNPCType->see_improved_hide = atoi(row[68]) == 0? false: true;
		tmpNPCType->ATK = atoi(row[69]);
		tmpNPCType->accuracy_rating = atoi(row[70]);
		tmpNPCType->avoidance_rating = atoi(row[71]);
		tmpNPCType->slow_mitigation = atoi(row[72]);
		tmpNPCType->maxlevel = atoi(row[73]);
		tmpNPCType->scalerate = atoi(row[74]);
		tmpNPCType->private_corpse = atoi(row[75]) == 1 ? true: false;
		tmpNPCType->unique_spawn_by_name = atoi(row[76]) == 1 ? true: false;
		tmpNPCType->underwater = atoi(row[77]) == 1 ? true: false;
		tmpNPCType->emoteid = atoi(row[78]);
		tmpNPCType->spellscale = atoi(row[79]);
		tmpNPCType->healscale = atoi(row[80]);
		tmpNPCType->no_target_hotkey = atoi(row[81]) == 1 ? true: false;
		tmpNPCType->raid_target = atoi(row[82]) == 0 ? false: true;
		tmpNPCType->attack_delay = atoi(row[83]);
		tmpNPCType->walkspeed= atof(row[84]);
		tmpNPCType->combat_hp_regen = atoi(row[85]);
		tmpNPCType->combat_mana_regen = atoi(row[86]);
		tmpNPCType->light = (atoi(row[87]) & 0x0F);
		tmpNPCType->aggro_pc = atoi(row[88]) == 1 ? true : false;
		tmpNPCType->armtexture = atoi(row[89]);
		tmpNPCType->bracertexture = atoi(row[90]);
		tmpNPCType->handtexture = atoi(row[91]);
		tmpNPCType->legtexture = atoi(row[92]);
		tmpNPCType->feettexture = atoi(row[93]);
		tmpNPCType->chesttexture = atoi(row[94]);
		tmpNPCType->ignore_distance = atof(row[95]);
		// If NPC with duplicate NPC id already in table,
		// free item we attempted to add.
		if (zone->npctable.find(tmpNPCType->npc_id) != zone->npctable.end()) {
			std::cerr << "Error loading duplicate NPC " << tmpNPCType->npc_id << std::endl;
			delete tmpNPCType;
			return nullptr;
		}

        zone->npctable[tmpNPCType->npc_id]=tmpNPCType;
        npc = tmpNPCType;
    }

	return npc;
}

NPCType* ZoneDatabase::GetNPCTypeTemp (uint32 id) {
	NPCType *npc=nullptr;

	// If NPC is already in tree, return it.
	auto itr = zone->npctable.find(id);
	if(itr != zone->npctable.end())
		return itr->second;

    // Otherwise, get NPCs from database.


    // If id is 0, load all npc_types for the current zone,
    // according to spawn2.
    std::string query = StringFormat("SELECT npc_types.id, npc_types.name, npc_types.level, npc_types.race, "
                        "npc_types.class, npc_types.hp, npc_types.mana, npc_types.gender, "
                        "npc_types.texture, npc_types.helmtexture, npc_types.size, "
                        "npc_types.loottable_id, npc_types.merchant_id, "
                        "npc_types.trap_template, "
                        "npc_types.STR, npc_types.STA, npc_types.DEX, npc_types.AGI, npc_types._INT, "
                        "npc_types.WIS, npc_types.CHA, npc_types.MR, npc_types.CR, npc_types.DR, "
                        "npc_types.FR, npc_types.PR, npc_types.Corrup, npc_types.PhR,"
                        "npc_types.mindmg, npc_types.maxdmg, npc_types.attack_count, npc_types.special_abilities,"
                        "npc_types.npc_spells_id, npc_types.npc_spells_effects_id, npc_types.d_melee_texture1,"
                        "npc_types.d_melee_texture2, npc_types.ammo_idfile, npc_types.prim_melee_type,"
                        "npc_types.sec_melee_type, npc_types.ranged_type, npc_types.runspeed, npc_types.findable,"
                        "npc_types.trackable, npc_types.hp_regen_rate, npc_types.mana_regen_rate, "
                        "npc_types.aggroradius, npc_types.assistradius, npc_types.bodytype, npc_types.npc_faction_id, "
                        "npc_types.face, npc_types.luclin_hairstyle, npc_types.luclin_haircolor, "
                        "npc_types.luclin_eyecolor, npc_types.luclin_eyecolor2, npc_types.luclin_beardcolor,"
                        "npc_types.luclin_beard, npc_types.armortint_id, "
                        "npc_types.armortint_red, npc_types.armortint_green, npc_types.armortint_blue, "
                        "npc_types.see_invis, npc_types.see_invis_undead, npc_types.lastname, "
                        "npc_types.qglobal, npc_types.AC, npc_types.npc_aggro, npc_types.spawn_limit, "
                        "npc_types.see_hide, npc_types.see_improved_hide, npc_types.ATK, npc_types.Accuracy, "
                        "npc_types.Avoidance, npc_types.slow_mitigation, npc_types.maxlevel, npc_types.scalerate, "
                        "npc_types.private_corpse, npc_types.unique_spawn_by_name, npc_types.underwater, "
                        "npc_types.emoteid, npc_types.spellscale, npc_types.healscale, npc_types.no_target_hotkey,"
                        "npc_types.raid_target, npc_types.attack_delay, npc_types.walkspeed, npc_types.combat_hp_regen, "
						"npc_types.combat_mana_regen, npc_types.light, npc_types.aggro_pc, npc_types.armtexture, "
						"npc_types.bracertexture, npc_types.handtexture, npc_types.legtexture, npc_types.feettexture, "
						"npc_types.chesttexture, npc_types.ignore_distance FROM npc_types WHERE id = %d", id);

    auto results = QueryDatabase(query);
    if (!results.Success()) {
        return nullptr;
    }


    for (auto row = results.begin(); row != results.end(); ++row) {
		NPCType *tmpNPCType;
		tmpNPCType = new NPCType;
		memset (tmpNPCType, 0, sizeof *tmpNPCType);

		tmpNPCType->npc_id = atoi(row[0]);

		strn0cpy(tmpNPCType->name, row[1], 50);

		tmpNPCType->level = atoi(row[2]);
		tmpNPCType->race = atoi(row[3]);
		tmpNPCType->class_ = atoi(row[4]);
		tmpNPCType->max_hp = atoi(row[5]);
		tmpNPCType->cur_hp = tmpNPCType->max_hp;
		tmpNPCType->Mana = atoi(row[6]);
		tmpNPCType->gender = atoi(row[7]);
		tmpNPCType->texture = atoi(row[8]);
		tmpNPCType->helmtexture = atoi(row[9]);
		tmpNPCType->size = atof(row[10]);
        tmpNPCType->loottable_id = atoi(row[11]);
		tmpNPCType->merchanttype = atoi(row[12]);
		tmpNPCType->trap_template = atoi(row[13]);
		tmpNPCType->STR = atoi(row[14]);
		tmpNPCType->STA = atoi(row[15]);
		tmpNPCType->DEX = atoi(row[16]);
		tmpNPCType->AGI = atoi(row[17]);
		tmpNPCType->INT = atoi(row[18]);
		tmpNPCType->WIS = atoi(row[19]);
		tmpNPCType->CHA = atoi(row[20]);
		tmpNPCType->MR = atoi(row[21]);
		tmpNPCType->CR = atoi(row[22]);
		tmpNPCType->DR = atoi(row[23]);
		tmpNPCType->FR = atoi(row[24]);
		tmpNPCType->PR = atoi(row[25]);
		tmpNPCType->Corrup = atoi(row[26]);
		tmpNPCType->PhR = atoi(row[27]);
		tmpNPCType->min_dmg = atoi(row[28]);
		tmpNPCType->max_dmg = atoi(row[29]);
		tmpNPCType->attack_count = atoi(row[30]);
		if (row[31] != nullptr)
			strn0cpy(tmpNPCType->special_abilities, row[31], 512);
		else
			tmpNPCType->special_abilities[0] = '\0';
		tmpNPCType->npc_spells_id = atoi(row[32]);
		tmpNPCType->npc_spells_effects_id = atoi(row[33]);
		tmpNPCType->d_melee_texture1 = atoi(row[34]);
		tmpNPCType->d_melee_texture2 = atoi(row[35]);
		strn0cpy(tmpNPCType->ammo_idfile, row[36], 30);
		tmpNPCType->prim_melee_type = atoi(row[37]);
		tmpNPCType->sec_melee_type = atoi(row[38]);
		tmpNPCType->ranged_type = atoi(row[39]);
		tmpNPCType->runspeed= atof(row[40]);
		tmpNPCType->findable = atoi(row[41]) == 0? false : true;
		tmpNPCType->trackable = atoi(row[42]) == 0? false : true;
		tmpNPCType->hp_regen = atoi(row[43]);
		tmpNPCType->mana_regen = atoi(row[44]);

		// set defaultvalue for aggroradius
        tmpNPCType->aggroradius = (int32)atoi(row[45]);
		if (tmpNPCType->aggroradius <= 0)
			tmpNPCType->aggroradius = 70;

		tmpNPCType->assistradius = (int32)atoi(row[46]);
		if (tmpNPCType->assistradius <= 0)
			tmpNPCType->assistradius = tmpNPCType->aggroradius;

		if (row[47] && strlen(row[47]))
            tmpNPCType->bodytype = (uint8)atoi(row[47]);
        else
            tmpNPCType->bodytype = 0;

		tmpNPCType->npc_faction_id = atoi(row[48]);

		tmpNPCType->luclinface = atoi(row[49]);
		tmpNPCType->hairstyle = atoi(row[50]);
		tmpNPCType->haircolor = atoi(row[51]);
		tmpNPCType->eyecolor1 = atoi(row[52]);
		tmpNPCType->eyecolor2 = atoi(row[53]);
		tmpNPCType->beardcolor = atoi(row[54]);
		tmpNPCType->beard = atoi(row[55]);

		uint32 armor_tint_id = atoi(row[56]);

		tmpNPCType->armor_tint[0] = (atoi(row[57]) & 0xFF) << 16;
        tmpNPCType->armor_tint[0] |= (atoi(row[58]) & 0xFF) << 8;
		tmpNPCType->armor_tint[0] |= (atoi(row[59]) & 0xFF);
		tmpNPCType->armor_tint[0] |= (tmpNPCType->armor_tint[0]) ? (0xFF << 24) : 0;

		if (armor_tint_id == 0)
			for (int index = MaterialChest; index <= EmuConstants::MATERIAL_END; index++)
				tmpNPCType->armor_tint[index] = tmpNPCType->armor_tint[0];
		else if (tmpNPCType->armor_tint[0] == 0)
        {
			std::string armortint_query = StringFormat("SELECT red1h, grn1h, blu1h, "
                                                    "red2c, grn2c, blu2c, "
                                                    "red3a, grn3a, blu3a, "
                                                    "red4b, grn4b, blu4b, "
                                                    "red5g, grn5g, blu5g, "
                                                    "red6l, grn6l, blu6l, "
                                                    "red7f, grn7f, blu7f, "
                                                    "red8x, grn8x, blu8x, "
                                                    "red9x, grn9x, blu9x "
                                                    "FROM npc_types_tint WHERE id = %d",
                                                    armor_tint_id);
            auto armortint_results = QueryDatabase(armortint_query);
            if (!armortint_results.Success() || armortint_results.RowCount() == 0)
                armor_tint_id = 0;
            else {
                auto armorTint_row = armortint_results.begin();

                for (int index = EmuConstants::MATERIAL_BEGIN; index <= EmuConstants::MATERIAL_END; index++) {
                    tmpNPCType->armor_tint[index] = atoi(armorTint_row[index * 3]) << 16;
					tmpNPCType->armor_tint[index] |= atoi(armorTint_row[index * 3 + 1]) << 8;
					tmpNPCType->armor_tint[index] |= atoi(armorTint_row[index * 3 + 2]);
					tmpNPCType->armor_tint[index] |= (tmpNPCType->armor_tint[index]) ? (0xFF << 24) : 0;
                }
            }
        } else
            armor_tint_id = 0;

		tmpNPCType->see_invis = atoi(row[60]);
		tmpNPCType->see_invis_undead = atoi(row[61]) == 0? false: true;	// Set see_invis_undead flag
		if (row[62] != nullptr)
			strn0cpy(tmpNPCType->lastname, row[62], 32);

		tmpNPCType->qglobal = atoi(row[63]) == 0? false: true;	// qglobal
		tmpNPCType->AC = atoi(row[64]);
		tmpNPCType->npc_aggro = atoi(row[65]) == 0? false: true;
		tmpNPCType->spawn_limit = atoi(row[66]);
		tmpNPCType->see_hide = atoi(row[67]) == 0? false: true;
		tmpNPCType->see_improved_hide = atoi(row[68]) == 0? false: true;
		tmpNPCType->ATK = atoi(row[69]);
		tmpNPCType->accuracy_rating = atoi(row[70]);
		tmpNPCType->avoidance_rating = atoi(row[71]);
		tmpNPCType->slow_mitigation = atoi(row[72]);
		tmpNPCType->maxlevel = atoi(row[73]);
		tmpNPCType->scalerate = atoi(row[74]);
		tmpNPCType->private_corpse = atoi(row[75]) == 1 ? true: false;
		tmpNPCType->unique_spawn_by_name = atoi(row[76]) == 1 ? true: false;
		tmpNPCType->underwater = atoi(row[77]) == 1 ? true: false;
		tmpNPCType->emoteid = atoi(row[78]);
		tmpNPCType->spellscale = atoi(row[79]);
		tmpNPCType->healscale = atoi(row[80]);
		tmpNPCType->no_target_hotkey = atoi(row[81]) == 1 ? true: false;
		tmpNPCType->raid_target = atoi(row[82]) == 0 ? false: true;
		tmpNPCType->attack_delay = atoi(row[83]);
		tmpNPCType->walkspeed= atof(row[84]);
		tmpNPCType->combat_hp_regen = atoi(row[85]);
		tmpNPCType->combat_mana_regen = atoi(row[86]);
		tmpNPCType->light = (atoi(row[87]) & 0x0F);
		tmpNPCType->aggro_pc = atoi(row[88]) == 1 ? true : false;
		tmpNPCType->armtexture = atoi(row[89]);
		tmpNPCType->bracertexture = atoi(row[90]);
		tmpNPCType->handtexture = atoi(row[91]);
		tmpNPCType->legtexture = atoi(row[92]);
		tmpNPCType->feettexture = atoi(row[93]);
		tmpNPCType->chesttexture = atoi(row[94]);
		tmpNPCType->ignore_distance = atof(row[95]);

		// If NPC with duplicate NPC id already in table,
		// free item we attempted to add.
		if (zone->npctable.find(tmpNPCType->npc_id) != zone->npctable.end()) {
			std::cerr << "Error loading duplicate NPC " << tmpNPCType->npc_id << std::endl;
			delete tmpNPCType;
			return nullptr;
		}

        zone->npctable[tmpNPCType->npc_id]=tmpNPCType;
        npc = tmpNPCType;
    }

	return npc;
}

uint8 ZoneDatabase::GetGridType(uint32 grid, uint32 zoneid) {

	std::string query = StringFormat("SELECT type FROM grid WHERE id = %i AND zoneid = %i", grid, zoneid);
	auto results = QueryDatabase(query);
	if (!results.Success()) {
        return 0;
	}

	if (results.RowCount() != 1)
		return 0;

	auto row = results.begin();

	return atoi(row[0]);
}

void ZoneDatabase::SaveMerchantTemp(uint32 npcid, uint32 slot, uint32 item, uint32 charges, uint32 quantity){

	std::string query = StringFormat("REPLACE INTO merchantlist_temp (npcid, slot, itemid, charges, quantity) "
                                    "VALUES(%d, %d, %d, %d, %d)", npcid, slot, item, charges, quantity);
    QueryDatabase(query);
}

void ZoneDatabase::DeleteMerchantTemp(uint32 npcid, uint32 slot){
	std::string query = StringFormat("DELETE FROM merchantlist_temp WHERE npcid=%d AND slot=%d", npcid, slot);
	QueryDatabase(query);
}

bool ZoneDatabase::UpdateZoneSafeCoords(const char* zonename, const glm::vec3& location) {

	std::string query = StringFormat("UPDATE zone SET safe_x='%f', safe_y='%f', safe_z='%f' "
                                    "WHERE short_name='%s';",
                                    location.x, location.y, location.z, zonename);
	auto results = QueryDatabase(query);
	if (!results.Success() || results.RowsAffected() == 0)
		return false;

	return true;
}

uint8 ZoneDatabase::GetUseCFGSafeCoords()
{
	const std::string query = "SELECT value FROM variables WHERE varname='UseCFGSafeCoords'";
	auto results = QueryDatabase(query);
	if (!results.Success()) {
		return 0;
	}

	if (results.RowCount() != 1)
        return 0;

	auto row = results.begin();

    return atoi(row[0]);
}

//New functions for timezone
uint32 ZoneDatabase::GetZoneTZ(uint32 zoneid, uint32 version) {

	std::string query = StringFormat("SELECT timezone FROM zone WHERE zoneidnumber = %i "
                                    "AND (version = %i OR version = 0) ORDER BY version DESC",
                                    zoneid, version);
    auto results = QueryDatabase(query);
    if (!results.Success()) {
        return 0;
    }

    if (results.RowCount() == 0)
        return 0;

    auto row = results.begin();
    return atoi(row[0]);
}

bool ZoneDatabase::SetZoneTZ(uint32 zoneid, uint32 version, uint32 tz) {

	std::string query = StringFormat("UPDATE zone SET timezone = %i "
                                    "WHERE zoneidnumber = %i AND version = %i",
                                    tz, zoneid, version);
    auto results = QueryDatabase(query);
    if (!results.Success()) {
		return false;
    }

    return results.RowsAffected() == 1;
}

void ZoneDatabase::RefreshGroupFromDB(Client *client){
	if(!client)
		return;

	Group *group = client->GetGroup();

	if(!group)
		return;

	EQApplicationPacket* outapp = new EQApplicationPacket(OP_GroupUpdate,sizeof(GroupUpdate_Struct));
	GroupUpdate_Struct* gu = (GroupUpdate_Struct*)outapp->pBuffer;
	gu->action = groupActUpdate;

	strcpy(gu->yourname, client->GetName());
	GetGroupLeadershipInfo(group->GetID(), gu->leadersname);

	int index = 0;

	std::string query = StringFormat("SELECT name FROM group_id WHERE groupid = %d", group->GetID());
	auto results = QueryDatabase(query);
	if (!results.Success())
	{
	}
	else
	{
		for (auto row = results.begin(); row != results.end(); ++row) {
			if(index >= 6)
				continue;

            if(strcmp(client->GetName(), row[0]) == 0)
				continue;

			strcpy(gu->membername[index], row[0]);
			index++;
		}
	}

	client->QueuePacket(outapp);
	safe_delete(outapp);
}

uint8 ZoneDatabase::GroupCount(uint32 groupid) {

	std::string query = StringFormat("SELECT count(charid) FROM group_id WHERE groupid = %d", groupid);
	auto results = QueryDatabase(query);
    if (!results.Success()) {
        return 0;
    }

    if (results.RowCount() == 0)
        return 0;

    auto row = results.begin();

	return atoi(row[0]);
}

uint8 ZoneDatabase::RaidGroupCount(uint32 raidid, uint32 groupid) {

	std::string query = StringFormat("SELECT count(charid) FROM raid_members "
                                    "WHERE raidid = %d AND groupid = %d;", raidid, groupid);
    auto results = QueryDatabase(query);

    if (!results.Success()) {
        return 0;
    }

    if (results.RowCount() == 0)
        return 0;

    auto row = results.begin();

	return atoi(row[0]);
 }

int32 ZoneDatabase::GetBlockedSpellsCount(uint32 zoneid)
{
	std::string query = StringFormat("SELECT count(*) FROM blocked_spells WHERE zoneid = %d", zoneid);
	auto results = QueryDatabase(query);
	if (!results.Success()) {
		return -1;
	}

	if (results.RowCount() == 0)
        return -1;

    auto row = results.begin();

	return atoi(row[0]);
}

bool ZoneDatabase::LoadBlockedSpells(int32 blockedSpellsCount, ZoneSpellsBlocked* into, uint32 zoneid)
{
	Log.Out(Logs::General, Logs::Status, "Loading Blocked Spells from database...");

	std::string query = StringFormat("SELECT id, spellid, type, x, y, z, x_diff, y_diff, z_diff, message "
                                    "FROM blocked_spells WHERE zoneid = %d ORDER BY id ASC", zoneid);
    auto results = QueryDatabase(query);
    if (!results.Success()) {
		return false;
    }

    if (results.RowCount() == 0)
		return true;

    int32 index = 0;
    for(auto row = results.begin(); row != results.end(); ++row, ++index) {
        if(index >= blockedSpellsCount) {
            std::cerr << "Error, Blocked Spells Count of " << blockedSpellsCount << " exceeded." << std::endl;
            break;
        }

        memset(&into[index], 0, sizeof(ZoneSpellsBlocked));
        into[index].spellid = atoi(row[1]);
        into[index].type = atoi(row[2]);
        into[index].m_Location = glm::vec3(atof(row[3]), atof(row[4]), atof(row[5]));
        into[index].m_Difference = glm::vec3(atof(row[6]), atof(row[7]), atof(row[8]));
        strn0cpy(into[index].message, row[9], 255);
    }

	return true;
}

int ZoneDatabase::getZoneShutDownDelay(uint32 zoneID, uint32 version)
{
	std::string query = StringFormat("SELECT shutdowndelay FROM zone "
                                    "WHERE zoneidnumber = %i AND (version=%i OR version=0) "
                                    "ORDER BY version DESC", zoneID, version);
    auto results = QueryDatabase(query);
    if (!results.Success()) {
        return (RuleI(Zone, AutoShutdownDelay));
    }

    if (results.RowCount() == 0) {
        std::cerr << "Error in getZoneShutDownDelay no result '" << query << "' " << std::endl;
        return (RuleI(Zone, AutoShutdownDelay));
    }

    auto row = results.begin();

    return atoi(row[0]);
}

uint32 ZoneDatabase::GetKarma(uint32 acct_id)
{
    std::string query = StringFormat("SELECT `karma` FROM `account` WHERE `id` = '%i' LIMIT 1", acct_id);
    auto results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	auto row = results.begin();

	return atoi(row[0]);
}

void ZoneDatabase::UpdateKarma(uint32 acct_id, uint32 amount)
{
	std::string query = StringFormat("UPDATE account SET karma = %i WHERE id = %i", amount, acct_id);
    QueryDatabase(query);
}

void ZoneDatabase::ListAllInstances(Client* client, uint32 charid)
{
	if(!client)
		return;

	std::string query = StringFormat("SELECT instance_list.id, zone, version "
                                    "FROM instance_list JOIN instance_list_player "
                                    "ON instance_list.id = instance_list_player.id "
                                    "WHERE instance_list_player.charid = %lu",
                                    (unsigned long)charid);
    auto results = QueryDatabase(query);
    if (!results.Success())
        return;

    char name[64];
    database.GetCharName(charid, name);
    client->Message(CC_Default, "%s is part of the following instances:", name);

    for (auto row = results.begin(); row != results.end(); ++row) {
        client->Message(CC_Default, "%s - id: %lu, version: %lu", database.GetZoneName(atoi(row[1])),
				(unsigned long)atoi(row[0]), (unsigned long)atoi(row[2]));
    }
}

void ZoneDatabase::QGlobalPurge()
{
	const std::string query = "DELETE FROM quest_globals WHERE expdate < UNIX_TIMESTAMP()";
	database.QueryDatabase(query);
}

void ZoneDatabase::InsertDoor(uint32 ddoordbid, uint16 ddoorid, const char* ddoor_name, const glm::vec4& position, uint8 dopentype, uint16 dguildid, uint32 dlockpick, uint32 dkeyitem, uint8 ddoor_param, uint8 dinvert, int dincline, uint16 dsize){

	std::string query = StringFormat("REPLACE INTO doors (id, doorid, zone, version, name, "
                                    "pos_x, pos_y, pos_z, heading, opentype, guild, lockpick, "
                                    "keyitem, door_param, invert_state, incline, size) "
                                    "VALUES('%i', '%i', '%s', '%i', '%s', '%f', '%f', "
                                    "'%f', '%f', '%i', '%i', '%i', '%i', '%i', '%i', '%i', '%i')",
                                    ddoordbid, ddoorid, zone->GetShortName(), zone->GetInstanceVersion(),
                                    ddoor_name, position.x, position.y, position.z, position.w,
                                    dopentype, dguildid, dlockpick, dkeyitem, ddoor_param, dinvert, dincline, dsize);
    QueryDatabase(query);
}

void ZoneDatabase::LogCommands(const char* char_name, const char* acct_name, float y, float x, float z, const char* command, const char* targetType, const char* target, float tar_y, float tar_x, float tar_z, uint32 zone_id, const char* zone_name)
{

	std::string new_char_name = std::string(char_name);
	replace_all(new_char_name, "'", "_");
	std::string new_target = std::string(target);
	replace_all(new_target, "'", "_");

	std::string rquery = StringFormat("SHOW TABLES LIKE 'commands_log'");
	auto results = QueryDatabase(rquery);
	if (results.RowCount() == 0){
		rquery = StringFormat(
			"CREATE TABLE	`commands_log` (								"
			"`entry_id`		int(11) NOT NULL AUTO_INCREMENT,				"
			"`char_name`	varchar(64) DEFAULT NULL,						"
			"`acct_name`	varchar(64) DEFAULT NULL,						"
			"`y`			float NOT NULL DEFAULT '0',						"
			"`x`			float NOT NULL DEFAULT '0',						"
			"`z`			float NOT NULL DEFAULT '0',						"
			"`command`		varchar(100) DEFAULT NULL,						"
			"`target_type`	varchar(30) DEFAULT NULL,						"
			"`target`		varchar(64) DEFAULT NULL,						"
			"`tar_y`		float NOT NULL DEFAULT '0',						"
			"`tar_x`		float NOT NULL DEFAULT '0',						"
			"`tar_z`		float NOT NULL DEFAULT '0',						"
			"`zone_id`		int(11) DEFAULT NULL,							"
			"`zone_name`	varchar(30) DEFAULT NULL,						"
			"`time`			datetime DEFAULT NULL,							"
			"PRIMARY KEY(`entry_id`)										"
			") ENGINE = InnoDB AUTO_INCREMENT = 8 DEFAULT CHARSET = latin1;	"
			);
		auto results = QueryDatabase(rquery);
	}
	std::string query = StringFormat("INSERT INTO `commands_log` (char_name, acct_name, y, x, z, command, target_type, target, tar_y, tar_x, tar_z, zone_id, zone_name, time) "
									"VALUES('%s', '%s', '%f', '%f', '%f', '%s', '%s', '%s', '%f', '%f', '%f', '%i', '%s', now())",
									new_char_name.c_str(), acct_name, y, x, z, command, targetType, new_target.c_str(), tar_y, tar_x, tar_z, zone_id, zone_name, time);
	auto log_results = QueryDatabase(query);
	if (!log_results.Success())
		Log.Out(Logs::General, Logs::Error, "Error in LogCommands query '%s': %s", query.c_str(), results.ErrorMessage().c_str());
}

uint8 ZoneDatabase::GetCommandAccess(const char* command) {
	std::string check_query = StringFormat("SELECT command FROM `commands` WHERE `command`='%s'", command);
	auto check_results = QueryDatabase(check_query);
	if (check_results.RowCount() == 0)
	{
		std::string insert_query = StringFormat("INSERT INTO `commands` (`command`, `access`) VALUES ('%s', %i)", command, 250);
		auto insert_results = QueryDatabase(insert_query);
		if (!insert_results.Success())
		{
			Log.Out(Logs::Detail, Logs::Error, "Error creating command %s in commands table.", command);
			return 250;
		}
	}

	std::string query = StringFormat("SELECT access FROM commands WHERE command = '%s'", command);
	auto results = QueryDatabase(query);
	if (!results.Success()) {
		return 250;
	}

	if (results.RowCount() != 1)
		return 250;

	auto row = results.begin();

	return atoi(row[0]);
}

void ZoneDatabase::SaveBuffs(Client *client) {

	std::string query = StringFormat("DELETE FROM `character_buffs` WHERE `id` = '%u'", client->CharacterID());
	database.QueryDatabase(query);

	uint32 buff_count = client->GetMaxBuffSlots();
	Buffs_Struct *buffs = client->GetBuffs();

	for (int index = 0; index < buff_count; index++) {
		if(buffs[index].spellid == SPELL_UNKNOWN)
            continue;

		query = StringFormat("INSERT INTO `character_buffs` (id, slot_id, spell_id, "
                            "caster_level, caster_name, ticsremaining, counters, numhits, melee_rune, "
                            "magic_rune, persistent, dot_rune, caston_x, caston_y, caston_z, ExtraDIChance) "
                            "VALUES('%u', '%u', '%u', '%u', '%s', '%u', '%u', '%u', '%u', '%u', '%u', '%u', "
                            "'%i', '%i', '%i', '%i')", client->CharacterID(), index, buffs[index].spellid,
                            buffs[index].casterlevel, buffs[index].caster_name, buffs[index].ticsremaining,
                            buffs[index].counters, buffs[index].numhits, buffs[index].melee_rune,
                            buffs[index].magic_rune, buffs[index].persistant_buff, buffs[index].dot_rune,
                            buffs[index].caston_x, buffs[index].caston_y, buffs[index].caston_z,
                            buffs[index].ExtraDIChance);
       QueryDatabase(query);
	}
}

void ZoneDatabase::LoadBuffs(Client *client) {

	Buffs_Struct *buffs = client->GetBuffs();
	uint32 max_slots = client->GetMaxBuffSlots();

	for(int index = 0; index < max_slots; ++index)
		buffs[index].spellid = SPELL_UNKNOWN;

	std::string query = StringFormat("SELECT spell_id, slot_id, caster_level, caster_name, ticsremaining, "
                                    "counters, numhits, melee_rune, magic_rune, persistent, dot_rune, "
                                    "caston_x, caston_y, caston_z, ExtraDIChance "
                                    "FROM `character_buffs` WHERE `id` = '%u'", client->CharacterID());
    auto results = QueryDatabase(query);
    if (!results.Success()) {
		return;
    }

    for (auto row = results.begin(); row != results.end(); ++row) {
        uint32 slot_id = atoul(row[1]);
		if(slot_id >= client->GetMaxBuffSlots())
			continue;

        uint32 spell_id = atoul(row[0]);
		if(!IsValidSpell(spell_id))
            continue;

        Client *caster = entity_list.GetClientByName(row[3]);
		uint32 caster_level = atoi(row[2]);
		uint32 ticsremaining = atoul(row[4]);
		uint32 counters = atoul(row[5]);
		uint32 numhits = atoul(row[6]);
		uint32 melee_rune = atoul(row[7]);
		uint32 magic_rune = atoul(row[8]);
		uint8 persistent = atoul(row[9]);
		uint32 dot_rune = atoul(row[10]);
		int32 caston_x = atoul(row[11]);
		int32 caston_y = atoul(row[12]);
		int32 caston_z = atoul(row[13]);
		int32 ExtraDIChance = atoul(row[14]);

		buffs[slot_id].spellid = spell_id;
        buffs[slot_id].casterlevel = caster_level;

        if(caster) {
            buffs[slot_id].casterid = caster->GetID();
            strcpy(buffs[slot_id].caster_name, caster->GetName());
            buffs[slot_id].client = true;
        } else {
            buffs[slot_id].casterid = 0;
			strcpy(buffs[slot_id].caster_name, "");
			buffs[slot_id].client = false;
        }

        buffs[slot_id].ticsremaining = ticsremaining;
		buffs[slot_id].counters = counters;
		buffs[slot_id].numhits = numhits;
		buffs[slot_id].melee_rune = melee_rune;
		buffs[slot_id].magic_rune = magic_rune;
		buffs[slot_id].persistant_buff = persistent? true: false;
		buffs[slot_id].dot_rune = dot_rune;
		buffs[slot_id].caston_x = caston_x;
        buffs[slot_id].caston_y = caston_y;
        buffs[slot_id].caston_z = caston_z;
        buffs[slot_id].ExtraDIChance = ExtraDIChance;
        buffs[slot_id].RootBreakChance = 0;
        buffs[slot_id].UpdateClient = false;
		buffs[slot_id].isdisc = IsDisc(spell_id);

    }

	max_slots = client->GetMaxBuffSlots();
	for(int index = 0; index < max_slots; ++index) {
		if(!IsValidSpell(buffs[index].spellid))
			continue;

		for(int effectIndex = 0; effectIndex < 12; ++effectIndex) {

			if (spells[buffs[index].spellid].effectid[effectIndex] == SE_Charm) {
                buffs[index].spellid = SPELL_UNKNOWN;
                break;
            }

            if (spells[buffs[index].spellid].effectid[effectIndex] == SE_Illusion) {
                if(buffs[index].persistant_buff)
                    break;

                buffs[index].spellid = SPELL_UNKNOWN;
				break;
			}
		}
	}
}

void ZoneDatabase::SavePetInfo(Client *client)
{
	PetInfo *petinfo = nullptr;

	std::string query = StringFormat("DELETE FROM `character_pet_buffs` WHERE `char_id` = %u", client->CharacterID());
	auto results = database.QueryDatabase(query);
	if (!results.Success())
		return;

	query = StringFormat("DELETE FROM `character_pet_inventory` WHERE `char_id` = %u", client->CharacterID());
	results = database.QueryDatabase(query);
	if (!results.Success())
		return;

	for (int pet = 0; pet < 2; pet++) {
		petinfo = client->GetPetInfo(pet);
		if (!petinfo)
			continue;

		query = StringFormat("INSERT INTO `character_pet_info` "
				"(`char_id`, `pet`, `petname`, `petpower`, `spell_id`, `hp`, `mana`, `size`) "
				"VALUES (%u, %u, '%s', %i, %u, %u, %u, %f) "
				"ON DUPLICATE KEY UPDATE `petname` = '%s', `petpower` = %i, `spell_id` = %u, "
				"`hp` = %u, `mana` = %u, `size` = %f",
				client->CharacterID(), pet, petinfo->Name, petinfo->petpower, petinfo->SpellID,
				petinfo->HP, petinfo->Mana, petinfo->size, // and now the ON DUPLICATE ENTRIES
				petinfo->Name, petinfo->petpower, petinfo->SpellID, petinfo->HP, petinfo->Mana, petinfo->size);
		results = database.QueryDatabase(query);
		if (!results.Success())
			return;
		query.clear();

		// pet buffs!
		for (int index = 0; index < RuleI(Spells, MaxTotalSlotsPET); index++) {
			if (petinfo->Buffs[index].spellid == SPELL_UNKNOWN || petinfo->Buffs[index].spellid == 0)
				continue;
			if (query.length() == 0)
				query = StringFormat("INSERT INTO `character_pet_buffs` "
						"(`char_id`, `pet`, `slot`, `spell_id`, `caster_level`, "
						"`ticsremaining`, `counters`) "
						"VALUES (%u, %u, %u, %u, %u, %u, %d)",
						client->CharacterID(), pet, index, petinfo->Buffs[index].spellid,
						petinfo->Buffs[index].level, petinfo->Buffs[index].duration,
						petinfo->Buffs[index].counters);
			else
				query += StringFormat(", (%u, %u, %u, %u, %u, %u, %d)",
						client->CharacterID(), pet, index, petinfo->Buffs[index].spellid,
						petinfo->Buffs[index].level, petinfo->Buffs[index].duration,
						petinfo->Buffs[index].counters);
		}
		database.QueryDatabase(query);
		query.clear();

		// pet inventory!
		for (int index = EmuConstants::EQUIPMENT_BEGIN; index < EmuConstants::EQUIPMENT_END; index++) {
			if (!petinfo->Items[index])
				continue;

			if (query.length() == 0)
				query = StringFormat("INSERT INTO `character_pet_inventory` "
						"(`char_id`, `pet`, `slot`, `item_id`) "
						"VALUES (%u, %u, %u, %u)",
						client->CharacterID(), pet, index, petinfo->Items[index]);
			else
				query += StringFormat(", (%u, %u, %u, %u)", client->CharacterID(), pet, index, petinfo->Items[index]);
		}
		database.QueryDatabase(query);
	}
}


void ZoneDatabase::RemoveTempFactions(Client *client) {

	std::string query = StringFormat("DELETE FROM character_faction_values "
                                    "WHERE temp = 1 AND id = %u",
                                    client->CharacterID());
	QueryDatabase(query);
}

void ZoneDatabase::LoadPetInfo(Client *client) {

	// Load current pet and suspended pet
	PetInfo *petinfo = client->GetPetInfo(0);
	PetInfo *suspended = client->GetPetInfo(1);

	memset(petinfo, 0, sizeof(PetInfo));
	memset(suspended, 0, sizeof(PetInfo));

    std::string query = StringFormat("SELECT `pet`, `petname`, `petpower`, `spell_id`, "
                                    "`hp`, `mana`, `size` FROM `character_pet_info` "
                                    "WHERE `char_id` = %u", client->CharacterID());
    auto results = database.QueryDatabase(query);
	if(!results.Success()) {
		return;
	}

    PetInfo *pi;
	for (auto row = results.begin(); row != results.end(); ++row) {
        uint16 pet = atoi(row[0]);

		if (pet == 0)
			pi = petinfo;
		else if (pet == 1)
			pi = suspended;
		else
			continue;

		strncpy(pi->Name,row[1],64);
		pi->petpower = atoi(row[2]);
		pi->SpellID = atoi(row[3]);
		pi->HP = atoul(row[4]);
		pi->Mana = atoul(row[5]);
		pi->size = atof(row[6]);
	}

    query = StringFormat("SELECT `pet`, `slot`, `spell_id`, `caster_level`, `castername`, "
                        "`ticsremaining`, `counters` FROM `character_pet_buffs` "
                        "WHERE `char_id` = %u", client->CharacterID());
    results = QueryDatabase(query);
    if (!results.Success()) {
		return;
    }

    for (auto row = results.begin(); row != results.end(); ++row) {
        uint16 pet = atoi(row[0]);
        if (pet == 0)
            pi = petinfo;
        else if (pet == 1)
            pi = suspended;
        else
            continue;

        uint32 slot_id = atoul(row[1]);
        if(slot_id >= RuleI(Spells, MaxTotalSlotsPET))
				continue;

        uint32 spell_id = atoul(row[2]);
        if(!IsValidSpell(spell_id))
            continue;

        uint32 caster_level = atoi(row[3]);
        int caster_id = 0;
        // The castername field is currently unused
        uint32 ticsremaining = atoul(row[5]);
        uint32 counters = atoul(row[6]);

        pi->Buffs[slot_id].spellid = spell_id;
        pi->Buffs[slot_id].level = caster_level;
        pi->Buffs[slot_id].player_id = caster_id;
        pi->Buffs[slot_id].slotid = 2;	// Always 2 in buffs struct for real buffs

        pi->Buffs[slot_id].duration = ticsremaining;
        pi->Buffs[slot_id].counters = counters;
    }

    query = StringFormat("SELECT `pet`, `slot`, `item_id` "
                        "FROM `character_pet_inventory` "
                        "WHERE `char_id`=%u",client->CharacterID());
    results = database.QueryDatabase(query);
    if (!results.Success()) {
		return;
	}

    for(auto row = results.begin(); row != results.end(); ++row) {
        uint16 pet = atoi(row[0]);
		if (pet == 0)
			pi = petinfo;
		else if (pet == 1)
			pi = suspended;
		else
			continue;

		int slot = atoi(row[1]);
		if (slot < EmuConstants::EQUIPMENT_BEGIN || slot > EmuConstants::EQUIPMENT_END)
            continue;

        pi->Items[slot] = atoul(row[2]);
    }

}

bool ZoneDatabase::GetFactionData(FactionMods* fm, uint32 class_mod, uint32 race_mod, uint32 deity_mod, int32 faction_id, uint8 texture_mod, uint8 gender_mod) {
	if (faction_id <= 0 || faction_id > (int32) max_faction)
		return false;

	if (faction_array[faction_id] == 0){
		return false;
	}

	fm->base = faction_array[faction_id]->base;

	if(class_mod > 0 && GetRaceBitmask(race_mod) & allraces_1) 
	{
		char str[32];
		sprintf(str, "c%u", class_mod);
		fm->class_mod = 0;

		std::map<std::string, int16>::const_iterator iter = faction_array[faction_id]->mods.find(str);
		if(iter != faction_array[faction_id]->mods.end()) 
		{
			fm->class_mod = iter->second;
		}
	} 
	else
	{
		fm->class_mod = 0;
	}

	if(race_mod > 0) 
	{
		char str[32];
		sprintf(str, "r%u", race_mod);

		if(race_mod == WOLF)
		{
			sprintf(str, "r%um%u", race_mod, gender_mod);
		}
		else if(race_mod == ELEMENTAL)
		{
			sprintf(str, "r%um%u", race_mod, texture_mod);
		}
		fm->race_mod = 0;

		std::map<std::string, int16>::iterator iter = faction_array[faction_id]->mods.find(str);
		if(iter != faction_array[faction_id]->mods.end())
		{
			fm->race_mod = iter->second;
		}
		else if(race_mod == ELEMENTAL || race_mod == WOLF)
		{
			sprintf(str, "r%u", race_mod);
			std::map<std::string, int16>::iterator iter = faction_array[faction_id]->mods.find(str);
			if(iter != faction_array[faction_id]->mods.end())
			{
				fm->race_mod = iter->second;
			}
		}
	}

	if(deity_mod > 0 && (GetRaceBitmask(race_mod) & allraces_1 || race_mod == ELEMENTAL)) 
	{
		char str[32];
		sprintf(str, "d%u", deity_mod);
		fm->deity_mod = 0;

		std::map<std::string, int16>::iterator iter = faction_array[faction_id]->mods.find(str);
		if(iter != faction_array[faction_id]->mods.end()) 
		{
			fm->deity_mod = iter->second;
		}
	} 
	else
	{
		fm->deity_mod = 0;
	}

	Log.Out(Logs::Detail, Logs::Faction, "Race: %d RaceBit: %d Class: %d Deity: %d BaseMod: %i RaceMod: %d ClassMod: %d DeityMod: %d", race_mod, GetRaceBitmask(race_mod), class_mod, deity_mod, fm->base, fm->race_mod, fm->class_mod, fm->deity_mod);  
	return true;
}

//o--------------------------------------------------------------
//| Name: GetFactionName; Dec. 16
//o--------------------------------------------------------------
//| Notes: Retrieves the name of the specified faction .Returns false on failure.
//o--------------------------------------------------------------
bool ZoneDatabase::GetFactionName(int32 faction_id, char* name, uint32 buflen) {
	if ((faction_id <= 0) || faction_id > int32(max_faction) ||(faction_array[faction_id] == 0))
		return false;
	if (faction_array[faction_id]->name[0] != 0) {
		strn0cpy(name, faction_array[faction_id]->name, buflen);
		return true;
	}
	return false;

}

//o--------------------------------------------------------------
//| Name: GetNPCFactionList; Dec. 16, 2001
//o--------------------------------------------------------------
//| Purpose: Gets a list of faction_id's and values bound to the npc_id. Returns false on failure.
//o--------------------------------------------------------------
bool ZoneDatabase::GetNPCFactionList(uint32 npcfaction_id, int32* faction_id, int32* value, uint8* temp, int32* primary_faction) {
	if (npcfaction_id <= 0) {
		if (primary_faction)
			*primary_faction = npcfaction_id;
		return true;
	}
	const NPCFactionList* nfl = GetNPCFactionEntry(npcfaction_id);
	if (!nfl)
		return false;
	if (primary_faction)
		*primary_faction = nfl->primaryfaction;
	for (int i=0; i<MAX_NPC_FACTIONS; i++) {
		faction_id[i] = nfl->factionid[i];
		value[i] = nfl->factionvalue[i];
		temp[i] = nfl->factiontemp[i];
	}
	return true;
}

//o--------------------------------------------------------------
//| Name: SetCharacterFactionLevel; Dec. 20, 2001
//o--------------------------------------------------------------
//| Purpose: Update characters faction level with specified faction_id to specified value. Returns false on failure.
//o--------------------------------------------------------------
bool ZoneDatabase::SetCharacterFactionLevel(uint32 char_id, int32 faction_id, int32 value, uint8 temp, faction_map &val_list)
{

	std::string query;

	if(temp == 2)
		temp = 0;

	if(temp == 3)
		temp = 1;

	query = StringFormat("INSERT INTO `character_faction_values` "
						"(`id`, `faction_id`, `current_value`, `temp`) "
						"VALUES (%i, %i, %i, %i) "
						"ON DUPLICATE KEY UPDATE `current_value`=%i,`temp`=%i",
						char_id, faction_id, value, temp, value, temp);
    auto results = QueryDatabase(query);
	
	if (!results.Success())
		return false;
	else
		val_list[faction_id] = value;

	return true;
}

bool ZoneDatabase::LoadFactionData()
{
	std::string query = "SELECT MAX(id) FROM faction_list";
	auto results = QueryDatabase(query);
	if (!results.Success()) {
		return false;
	}

    if (results.RowCount() == 0)
        return false;

    auto row = results.begin();

	max_faction = row[0] ? atoi(row[0]) : 0;
    faction_array = new Faction*[max_faction+1];
    for(unsigned int index=0; index<max_faction; index++)
        faction_array[index] = nullptr;

    query = "SELECT id, name, base FROM faction_list";
    results = QueryDatabase(query);
    if (!results.Success()) {
        return false;
    }

    for (row = results.begin(); row != results.end(); ++row) {
        uint32 index = atoi(row[0]);
		faction_array[index] = new Faction;
		strn0cpy(faction_array[index]->name, row[1], 50);
		faction_array[index]->base = atoi(row[2]);

        query = StringFormat("SELECT `mod`, `mod_name` FROM `faction_list_mod` WHERE faction_id = %u", index);
        auto modResults = QueryDatabase(query);
        if (!modResults.Success())
            continue;

		for (auto modRow = modResults.begin(); modRow != modResults.end(); ++modRow)
		{
            faction_array[index]->mods[modRow[1]] = atoi(modRow[0]);
		}

    }

	return true;
}

bool ZoneDatabase::GetFactionIdsForNPC(uint32 nfl_id, std::list<struct NPCFaction*> *faction_list, int32* primary_faction) {
	if (nfl_id <= 0) {
		std::list<struct NPCFaction*>::iterator cur,end;
		cur = faction_list->begin();
		end = faction_list->end();
		for(; cur != end; ++cur) {
			struct NPCFaction* tmp = *cur;
			safe_delete(tmp);
		}

		faction_list->clear();
		if (primary_faction)
			*primary_faction = nfl_id;
		return true;
	}
	const NPCFactionList* nfl = GetNPCFactionEntry(nfl_id);
	if (!nfl)
		return false;
	if (primary_faction)
		*primary_faction = nfl->primaryfaction;

	std::list<struct NPCFaction*>::iterator cur,end;
	cur = faction_list->begin();
	end = faction_list->end();
	for(; cur != end; ++cur) {
		struct NPCFaction* tmp = *cur;
		safe_delete(tmp);
	}
	faction_list->clear();
	for (int i=0; i<MAX_NPC_FACTIONS; i++) {
		struct NPCFaction *pFac;
		if (nfl->factionid[i]) {
			pFac = new struct NPCFaction;
			pFac->factionID = nfl->factionid[i];
			pFac->value_mod = nfl->factionvalue[i];
			pFac->npc_value = nfl->factionnpcvalue[i];
			pFac->temp = nfl->factiontemp[i];
			faction_list->push_back(pFac);
		}
	}
	return true;
}

/*  Corpse Queries */

bool ZoneDatabase::DeleteGraveyard(uint32 zone_id, uint32 graveyard_id) {
	std::string query = StringFormat( "UPDATE `zone` SET `graveyard_id` = 0 WHERE `zone_idnumber` = %u AND `version` = 0", zone_id);
	auto results = QueryDatabase(query);

	query = StringFormat("DELETE FROM `graveyard` WHERE `id` = %u",  graveyard_id);
	auto results2 = QueryDatabase(query);

	if (results.Success() && results2.Success()){
		return true;
	}

	return false;
}

uint32 ZoneDatabase::AddGraveyardIDToZone(uint32 zone_id, uint32 graveyard_id) {
	std::string query = StringFormat(
		"UPDATE `zone` SET `graveyard_id` = %u WHERE `zone_idnumber` = %u AND `version` = 0",
		graveyard_id, zone_id
	);
	auto results = QueryDatabase(query);
	return zone_id;
}

uint32 ZoneDatabase::CreateGraveyardRecord(uint32 graveyard_zone_id, const glm::vec4& position) {
	std::string query = StringFormat("INSERT INTO `graveyard` "
                                    "SET `zone_id` = %u, `x` = %1.1f, `y` = %1.1f, `z` = %1.1f, `heading` = %1.1f",
                                    graveyard_zone_id, position.x, position.y, position.z, position.w);
	auto results = QueryDatabase(query);
	if (results.Success())
		return results.LastInsertedID();

	return 0;
}
uint32 ZoneDatabase::SendCharacterCorpseToGraveyard(uint32 dbid, uint32 zone_id, uint16 instance_id, const glm::vec4& position) {
	std::string query = StringFormat("UPDATE `character_corpses` "
                                    "SET `zone_id` = %u, `instance_id` = 0, "
                                    "`x` = %1.1f, `y` = %1.1f, `z` = %1.1f, `heading` = %1.1f, "
                                    "`was_at_graveyard` = 1 "
                                    "WHERE `id` = %d",
                                    zone_id, position.x, position.y, position.z, position.w, dbid);
	QueryDatabase(query);
	return dbid;
}

uint32 ZoneDatabase::GetCharacterCorpseDecayTimer(uint32 corpse_db_id){
	std::string query = StringFormat("SELECT(UNIX_TIMESTAMP() - UNIX_TIMESTAMP(time_of_death)) FROM `character_corpses` WHERE `id` = %d AND NOT `time_of_death` = 0", corpse_db_id);
	auto results = QueryDatabase(query);
	auto row = results.begin();
	if (results.Success() && results.RowsAffected() != 0){
		return atoul(row[0]); 
	}
	return 0;
}

uint32 ZoneDatabase::UpdateCharacterCorpse(uint32 db_id, uint32 char_id, const char* char_name, uint32 zone_id, uint16 instance_id, PlayerCorpse_Struct* dbpc, const glm::vec4& position, bool is_rezzed) {
	std::string query = StringFormat("UPDATE `character_corpses` SET \n"
		"`charname` =		  '%s',\n"
		"`zone_id` =				%u,\n"
		"`instance_id` =			%u,\n"
		"`charid` =				%d,\n"
		"`x` =					%1.1f,\n"
		"`y` =					%1.1f,\n"
		"`z` =					%1.1f,\n"
		"`heading` =			%1.1f,\n"
		"`is_locked` =          %d,\n"
		"`exp` =                 %u,\n"
		"`gmexp` =				%u,\n"
		"`size` =               %f,\n"
		"`level` =              %u,\n"
		"`race` =               %u,\n"
		"`gender` =             %u,\n"
		"`class` =              %u,\n"
		"`deity` =              %u,\n"
		"`texture` =            %u,\n"
		"`helm_texture` =       %u,\n"
		"`copper` =             %u,\n"
		"`silver` =             %u,\n"
		"`gold` =               %u,\n"
		"`platinum` =           %u,\n"
		"`hair_color`  =        %u,\n"
		"`beard_color` =        %u,\n"
		"`eye_color_1` =        %u,\n"
		"`eye_color_2` =        %u,\n"
		"`hair_style`  =        %u,\n"
		"`face` =               %u,\n"
		"`beard` =              %u,\n"
		"`wc_1` =               %u,\n"
		"`wc_2` =               %u,\n"
		"`wc_3` =               %u,\n"
		"`wc_4` =               %u,\n"
		"`wc_5` =               %u,\n"
		"`wc_6` =               %u,\n"
		"`wc_7` =               %u,\n"
		"`wc_8` =               %u,\n"
		"`wc_9`	=               %u,\n"
		"`killedby` =			%u,\n"
		"`rezzable` =			%d,\n"
		"`rez_time` =			%u,\n"
		"`is_rezzed` =			%u \n"
		"WHERE `id` = %u",
		EscapeString(char_name).c_str(),
		zone_id,
		instance_id,
		char_id,
		position.x,
		position.y,
		position.z,
		position.w,
		dbpc->locked,
		dbpc->exp,
		dbpc->gmexp,
		dbpc->size,
		dbpc->level,
		dbpc->race,
		dbpc->gender,
		dbpc->class_,
		dbpc->deity,
		dbpc->texture,
		dbpc->helmtexture,
		dbpc->copper,
		dbpc->silver,
		dbpc->gold,
		dbpc->plat,
		dbpc->haircolor,
		dbpc->beardcolor,
		dbpc->eyecolor1,
		dbpc->eyecolor2,
		dbpc->hairstyle,
		dbpc->face,
		dbpc->beard,
		dbpc->item_tint[0].color,
		dbpc->item_tint[1].color,
		dbpc->item_tint[2].color,
		dbpc->item_tint[3].color,
		dbpc->item_tint[4].color,
		dbpc->item_tint[5].color,
		dbpc->item_tint[6].color,
		dbpc->item_tint[7].color,
		dbpc->item_tint[8].color,
		dbpc->killedby,
		dbpc->rezzable,
		dbpc->rez_time,
		is_rezzed,
		db_id
	);
	auto results = QueryDatabase(query);

	return db_id;
}

bool ZoneDatabase::UpdateCharacterCorpseBackup(uint32 db_id, uint32 char_id, const char* char_name, uint32 zone_id, uint16 instance_id, PlayerCorpse_Struct* dbpc, const glm::vec4& position, bool is_rezzed) {
	std::string query = StringFormat("UPDATE `character_corpses_backup` SET \n"
		"`charname` =		  '%s',\n"
		"`charid` =				%d,\n"
		"`exp` =                 %u,\n"
		"`gmexp` =				%u,\n"
		"`copper` =             %u,\n"
		"`silver` =             %u,\n"
		"`gold` =               %u,\n"
		"`platinum` =           %u,\n"
		"`rezzable` =           %d,\n"
		"`rez_time` =           %u,\n"
		"`is_rezzed` =          %u \n"
		"WHERE `id` = %u",
		EscapeString(char_name).c_str(), 
		char_id, 
		dbpc->exp,
		dbpc->gmexp,
		dbpc->copper,
		dbpc->silver,
		dbpc->gold,
		dbpc->plat,
		dbpc->rezzable,
		dbpc->rez_time,
		is_rezzed,
		db_id
	);
	auto results = QueryDatabase(query);
	if (!results.Success()){
		Log.Out(Logs::Detail, Logs::Error, "UpdateCharacterCorpseBackup query '%s' %s", query.c_str(), results.ErrorMessage().c_str());
		return false;
	}

	return true;
}


void ZoneDatabase::MarkCorpseAsRezzed(uint32 db_id) {
	std::string query = StringFormat("UPDATE `character_corpses` SET `is_rezzed` = 1 WHERE `id` = %i", db_id);
	auto results = QueryDatabase(query);
}

uint32 ZoneDatabase::SaveCharacterCorpse(uint32 charid, const char* charname, uint32 zoneid, uint16 instanceid, PlayerCorpse_Struct* dbpc, const glm::vec4& position) {
	/* Dump Basic Corpse Data */
	std::string query = StringFormat("INSERT INTO `character_corpses` SET \n"
		"`charname` =		  '%s',\n"
		"`zone_id` =				%u,\n"
		"`instance_id` =			%u,\n"
		"`charid` =				%d,\n"
		"`x` =					%1.1f,\n"
		"`y` =					%1.1f,\n"
		"`z` =					%1.1f,\n"
		"`heading` =			%1.1f,\n"
		"`time_of_death` =		NOW(),\n"
		"`is_buried` =				0,"
		"`is_locked` =          %d,\n"
		"`exp` =                 %u,\n"
		"`gmexp` =				%u,\n"
		"`size` =               %f,\n"
		"`level` =              %u,\n"
		"`race` =               %u,\n"
		"`gender` =             %u,\n"
		"`class` =              %u,\n"
		"`deity` =              %u,\n"
		"`texture` =            %u,\n"
		"`helm_texture` =       %u,\n"
		"`copper` =             %u,\n"
		"`silver` =             %u,\n"
		"`gold` =               %u,\n"
		"`platinum` =           %u,\n"
		"`hair_color`  =        %u,\n"
		"`beard_color` =        %u,\n"
		"`eye_color_1` =        %u,\n"
		"`eye_color_2` =        %u,\n"
		"`hair_style`  =        %u,\n"
		"`face` =               %u,\n"
		"`beard` =              %u,\n"
		"`wc_1` =               %u,\n"
		"`wc_2` =               %u,\n"
		"`wc_3` =               %u,\n"
		"`wc_4` =               %u,\n"
		"`wc_5` =               %u,\n"
		"`wc_6` =               %u,\n"
		"`wc_7` =               %u,\n"
		"`wc_8` =               %u,\n"
		"`wc_9`	=               %u,\n"
		"`killedby` =			%u,\n"
		"`rezzable` =			%d,\n"
		"`rez_time` =			%u \n",
		EscapeString(charname).c_str(),
		zoneid,
		instanceid,
		charid,
		position.x,
		position.y,
		position.z,
		position.w,
		dbpc->locked,
		dbpc->exp,
		dbpc->gmexp,
		dbpc->size,
		dbpc->level,
		dbpc->race,
		dbpc->gender,
		dbpc->class_,
		dbpc->deity,
		dbpc->texture,
		dbpc->helmtexture,
		dbpc->copper,
		dbpc->silver,
		dbpc->gold,
		dbpc->plat,
		dbpc->haircolor,
		dbpc->beardcolor,
		dbpc->eyecolor1,
		dbpc->eyecolor2,
		dbpc->hairstyle,
		dbpc->face,
		dbpc->beard,
		dbpc->item_tint[0].color,
		dbpc->item_tint[1].color,
		dbpc->item_tint[2].color,
		dbpc->item_tint[3].color,
		dbpc->item_tint[4].color,
		dbpc->item_tint[5].color,
		dbpc->item_tint[6].color,
		dbpc->item_tint[7].color,
		dbpc->item_tint[8].color,
		dbpc->killedby,
		dbpc->rezzable,
		dbpc->rez_time
	);
	auto results = QueryDatabase(query);
	uint32 last_insert_id = results.LastInsertedID();

	std::string corpse_items_query;
	/* Dump Items from Inventory */
	uint8 first_entry = 0;
	for (unsigned int i = 0; i < dbpc->itemcount; i++) {
		if (first_entry != 1){
			corpse_items_query = StringFormat("REPLACE INTO `character_corpse_items` \n"
				" (corpse_id, equip_slot, item_id, charges, attuned) \n"
				" VALUES (%u, %u, %u, %u, 0) \n",
				last_insert_id,
				dbpc->items[i].equip_slot,
				dbpc->items[i].item_id,
				dbpc->items[i].charges
			);
			first_entry = 1;
		}
		else{
			corpse_items_query = corpse_items_query + StringFormat(", (%u, %u, %u, %u, 0) \n",
				last_insert_id,
				dbpc->items[i].equip_slot,
				dbpc->items[i].item_id,
				dbpc->items[i].charges
			);
		}
	}
	auto sc_results = QueryDatabase(corpse_items_query);
	return last_insert_id;
}

bool ZoneDatabase::SaveCharacterCorpseBackup(uint32 corpse_id, uint32 charid, const char* charname, uint32 zoneid, uint16 instanceid, PlayerCorpse_Struct* dbpc, const glm::vec4& position) {
	/* Dump Basic Corpse Data */
	std::string query = StringFormat("INSERT INTO `character_corpses_backup` SET \n"
		"`id` =						%u,\n"
		"`charname` =		  '%s',\n"
		"`zone_id` =				%u,\n"
		"`instance_id` =			%u,\n"
		"`charid` =				%d,\n"
		"`x` =					%1.1f,\n"
		"`y` =					%1.1f,\n"
		"`z` =					%1.1f,\n"
		"`heading` =			%1.1f,\n"
		"`time_of_death` =		NOW(),\n"
		"`is_buried` =				0,"
		"`is_locked` =          %d,\n"
		"`exp` =                 %u,\n"
		"`gmexp` =				%u,\n"
		"`size` =               %f,\n"
		"`level` =              %u,\n"
		"`race` =               %u,\n"
		"`gender` =             %u,\n"
		"`class` =              %u,\n"
		"`deity` =              %u,\n"
		"`texture` =            %u,\n"
		"`helm_texture` =       %u,\n"
		"`copper` =             %u,\n"
		"`silver` =             %u,\n"
		"`gold` =               %u,\n"
		"`platinum` =           %u,\n"
		"`hair_color`  =        %u,\n"
		"`beard_color` =        %u,\n"
		"`eye_color_1` =        %u,\n"
		"`eye_color_2` =        %u,\n"
		"`hair_style`  =        %u,\n"
		"`face` =               %u,\n"
		"`beard` =              %u,\n"
		"`wc_1` =               %u,\n"
		"`wc_2` =               %u,\n"
		"`wc_3` =               %u,\n"
		"`wc_4` =               %u,\n"
		"`wc_5` =               %u,\n"
		"`wc_6` =               %u,\n"
		"`wc_7` =               %u,\n"
		"`wc_8` =               %u,\n"
		"`wc_9`	=               %u,\n"
		"`killedby` =			%u,\n"
		"`rezzable` =			%d,\n"
		"`rez_time` =			%u \n",
		corpse_id,
		EscapeString(charname).c_str(),
		zoneid,
		instanceid,
		charid,
		position.x,
		position.y,
		position.z,
		position.w,
		dbpc->locked,
		dbpc->exp,
		dbpc->gmexp,
		dbpc->size,
		dbpc->level,
		dbpc->race,
		dbpc->gender,
		dbpc->class_,
		dbpc->deity,
		dbpc->texture,
		dbpc->helmtexture,
		dbpc->copper,
		dbpc->silver,
		dbpc->gold,
		dbpc->plat,
		dbpc->haircolor,
		dbpc->beardcolor,
		dbpc->eyecolor1,
		dbpc->eyecolor2,
		dbpc->hairstyle,
		dbpc->face,
		dbpc->beard,
		dbpc->item_tint[0].color,
		dbpc->item_tint[1].color,
		dbpc->item_tint[2].color,
		dbpc->item_tint[3].color,
		dbpc->item_tint[4].color,
		dbpc->item_tint[5].color,
		dbpc->item_tint[6].color,
		dbpc->item_tint[7].color,
		dbpc->item_tint[8].color,
		dbpc->killedby,
		dbpc->rezzable,
		dbpc->rez_time
	);
	auto results = QueryDatabase(query); 
	if (!results.Success()){
		Log.Out(Logs::Detail, Logs::Error, "Error inserting character_corpses_backup.");
		return false;
	}

	/* Dump Items from Inventory */
	uint8 first_entry = 0;
	for (unsigned int i = 0; i < dbpc->itemcount; i++) { 
		if (first_entry != 1){
			query = StringFormat("REPLACE INTO `character_corpse_items_backup` \n"
				" (corpse_id, equip_slot, item_id, charges, attuned) \n"
				" VALUES (%u, %u, %u, %u, 0) \n",
				corpse_id, 
				dbpc->items[i].equip_slot,
				dbpc->items[i].item_id,  
				dbpc->items[i].charges
			);
			first_entry = 1;
		}
		else{ 
			query = query + StringFormat(", (%u, %u, %u, %u, 0) \n",
				corpse_id,
				dbpc->items[i].equip_slot,
				dbpc->items[i].item_id,
				dbpc->items[i].charges
			);
		}
	}
	auto sc_results = QueryDatabase(query); 
	if (!sc_results.Success()){
		Log.Out(Logs::Detail, Logs::Error, "Error inserting character_corpse_items_backup.");
		return false;
	}
	return true;
}

uint32 ZoneDatabase::GetCharacterBuriedCorpseCount(uint32 char_id) {
	std::string query = StringFormat("SELECT COUNT(*) FROM `character_corpses` WHERE `charid` = '%u' AND `is_buried` = 1", char_id);
	auto results = QueryDatabase(query);

	for (auto row = results.begin(); row != results.end(); ++row) {
		return atoi(row[0]);
	}
	return 0;
}

uint32 ZoneDatabase::GetCharacterCorpseCount(uint32 char_id) {
	std::string query = StringFormat("SELECT COUNT(*) FROM `character_corpses` WHERE `charid` = '%u'", char_id);
	auto results = QueryDatabase(query);

	for (auto row = results.begin(); row != results.end(); ++row) {
		return atoi(row[0]);
	}
	return 0;
}

uint32 ZoneDatabase::GetCharacterCorpseID(uint32 char_id, uint8 corpse) {
	std::string query = StringFormat("SELECT `id` FROM `character_corpses` WHERE `charid` = '%u'", char_id);
	auto results = QueryDatabase(query);

	for (auto row = results.begin(); row != results.end(); ++row) {
		for (int i = 0; i < corpse; i++) {
			return atoul(row[0]);
		}
	}
	return 0;
}

uint32 ZoneDatabase::GetCharacterCorpseItemCount(uint32 corpse_id){
	std::string query = StringFormat("SELECT COUNT(*) FROM character_corpse_items WHERE `corpse_id` = %u",
		corpse_id
	);
	auto results = QueryDatabase(query);
	auto row = results.begin();
	if (results.Success() && results.RowsAffected() != 0){
		return atoi(row[0]);
	}
	return 0;
}

uint32 ZoneDatabase::GetCharacterCorpseItemAt(uint32 corpse_id, uint16 slotid) {
	Corpse* tmp = LoadCharacterCorpse(corpse_id);
	uint32 itemid = 0;

	if (tmp) {
		itemid = tmp->GetWornItem(slotid);
		tmp->DepopPlayerCorpse();
	}
	return itemid;
}

bool ZoneDatabase::LoadCharacterCorpseData(uint32 corpse_id, PlayerCorpse_Struct* pcs){
	std::string query = StringFormat(
		"SELECT           \n"
		"is_locked,       \n"
		"exp,             \n"
		"gmexp,			  \n"
		"size,            \n"
		"`level`,         \n"
		"race,            \n"
		"gender,          \n"
		"class,           \n"
		"deity,           \n"
		"texture,         \n"
		"helm_texture,    \n"
		"copper,          \n"
		"silver,          \n"
		"gold,            \n"
		"platinum,        \n"
		"hair_color,      \n"
		"beard_color,     \n"
		"eye_color_1,     \n"
		"eye_color_2,     \n"
		"hair_style,      \n"
		"face,            \n"
		"beard,           \n"
		"wc_1,            \n"
		"wc_2,            \n"
		"wc_3,            \n"
		"wc_4,            \n"
		"wc_5,            \n"
		"wc_6,            \n"
		"wc_7,            \n"
		"wc_8,            \n"
		"wc_9,             \n"
		"killedby,		  \n"
		"rezzable,		  \n"
		"rez_time		  \n"
		"FROM             \n"
		"character_corpses\n"
		"WHERE `id` = %u  LIMIT 1\n",
		corpse_id
	);
	auto results = QueryDatabase(query);
	uint16 i = 0;
	for (auto row = results.begin(); row != results.end(); ++row) {
		pcs->locked = atoi(row[i++]);						// is_locked,
		pcs->exp = atoul(row[i++]);							// exp,
		pcs->gmexp = atoul(row[i++]);						// gmexp,
		pcs->size = atoi(row[i++]);							// size,
		pcs->level = atoi(row[i++]);						// `level`,
		pcs->race = atoi(row[i++]);							// race,
		pcs->gender = atoi(row[i++]);						// gender,
		pcs->class_ = atoi(row[i++]);						// class,
		pcs->deity = atoi(row[i++]);						// deity,
		pcs->texture = atoi(row[i++]);						// texture,
		pcs->helmtexture = atoi(row[i++]);					// helm_texture,
		pcs->copper = atoul(row[i++]);						// copper,
		pcs->silver = atoul(row[i++]);						// silver,
		pcs->gold = atoul(row[i++]);						// gold,
		pcs->plat = atoul(row[i++]);						// platinum,
		pcs->haircolor = atoi(row[i++]);					// hair_color,
		pcs->beardcolor = atoi(row[i++]);					// beard_color,
		pcs->eyecolor1 = atoi(row[i++]);					// eye_color_1,
		pcs->eyecolor2 = atoi(row[i++]);					// eye_color_2,
		pcs->hairstyle = atoi(row[i++]);					// hair_style,
		pcs->face = atoi(row[i++]);							// face,
		pcs->beard = atoi(row[i++]);						// beard,
		pcs->item_tint[0].color = atoul(row[i++]);			// wc_1,
		pcs->item_tint[1].color = atoul(row[i++]);			// wc_2,
		pcs->item_tint[2].color = atoul(row[i++]);			// wc_3,
		pcs->item_tint[3].color = atoul(row[i++]);			// wc_4,
		pcs->item_tint[4].color = atoul(row[i++]);			// wc_5,
		pcs->item_tint[5].color = atoul(row[i++]);			// wc_6,
		pcs->item_tint[6].color = atoul(row[i++]);			// wc_7,
		pcs->item_tint[7].color = atoul(row[i++]);			// wc_8,
		pcs->item_tint[8].color = atoul(row[i++]);			// wc_9
		pcs->killedby = atoi(row[i++]);						// killedby
		pcs->rezzable = atoi(row[i++]);						// rezzable
		pcs->rez_time = atoul(row[i++]);					// rez_time
	}
	query = StringFormat(
		"SELECT                       \n"
		"equip_slot,                  \n"
		"item_id,                     \n"
		"charges,                     \n"
		"attuned                      \n"
		"FROM                         \n"
		"character_corpse_items       \n"
		"WHERE `corpse_id` = %u\n"
		,
		corpse_id
	);
	results = QueryDatabase(query);

	i = 0;
	pcs->itemcount = results.RowCount();
	uint16 r = 0;
	for (auto row = results.begin(); row != results.end(); ++row) {
		memset(&pcs->items[i], 0, sizeof (player_lootitem::ServerLootItem_Struct));
		pcs->items[i].equip_slot = atoi(row[r++]);		// equip_slot,
		pcs->items[i].item_id = atoul(row[r++]); 		// item_id,
		pcs->items[i].charges = atoi(row[r++]); 		// charges,
		r = 0;
		i++;
	}

	return true;
}

Corpse* ZoneDatabase::SummonBuriedCharacterCorpses(uint32 char_id, uint32 dest_zone_id, uint16 dest_instance_id, const glm::vec4& position) {
	Corpse* corpse = nullptr;
	std::string query = StringFormat("SELECT `id`, `charname`, `time_of_death`, `is_rezzed` "
                                    "FROM `character_corpses` "
                                    "WHERE `charid` = '%u' AND `is_buried` = 1 "
                                    "ORDER BY `time_of_death` LIMIT 1",
                                    char_id);
	auto results = QueryDatabase(query);

	for (auto row = results.begin(); row != results.end(); ++row) {
		corpse = Corpse::LoadCharacterCorpseEntity(
			atoul(row[0]), 			 // uint32 in_dbid
			char_id, 				 // uint32 in_charid
			row[1], 				 // char* in_charname
			position,
			row[2], 				 // char* time_of_death
			atoi(row[3]) == 1, 		 // bool rezzed
			false					 // bool was_at_graveyard
		);
		if (corpse) {
			entity_list.AddCorpse(corpse);
			int32 corpse_decay = 0;
			if(corpse->IsEmpty())
			{
				corpse_decay = RuleI(Character, EmptyCorpseDecayTimeMS);
			}
			else
			{
				corpse_decay = RuleI(Character, CorpseDecayTimeMS);
			}
			corpse->SetDecayTimer(corpse_decay);
			corpse->Spawn();
			if (!UnburyCharacterCorpse(corpse->GetCorpseDBID(), dest_zone_id, dest_instance_id, position))
				Log.Out(Logs::Detail, Logs::Error, "Unable to unbury a summoned player corpse for character id %u.", char_id);
		}
	}

	return corpse;
}

Corpse* ZoneDatabase::SummonCharacterCorpse(uint32 corpse_id, uint32 char_id, uint32 dest_zone_id, uint16 dest_instance_id, const glm::vec4& position) {
	Corpse* NewCorpse = 0;
	std::string query = StringFormat(
		"SELECT `id`, `charname`, `time_of_death`, `is_rezzed` FROM `character_corpses` WHERE `charid` = '%u' AND `id` = %u", 
		char_id, corpse_id
	);
	auto results = QueryDatabase(query);

	for (auto row = results.begin(); row != results.end(); ++row) {
		NewCorpse = Corpse::LoadCharacterCorpseEntity(
			atoul(row[0]), 			 // uint32 in_dbid
			char_id, 				 // uint32 in_charid
			row[1], 				 // char* in_charname
			position,
			row[2], 				 // char* time_of_death
			atoi(row[3]) == 1, 		 // bool rezzed
			false					 // bool was_at_graveyard
		);
		if (NewCorpse) { 
			entity_list.AddCorpse(NewCorpse);
			int32 corpse_decay = 0;
			if(NewCorpse->IsEmpty())
			{
				corpse_decay = RuleI(Character, EmptyCorpseDecayTimeMS);
			}
			else
			{
				corpse_decay = RuleI(Character, CorpseDecayTimeMS);
			}
			NewCorpse->SetDecayTimer(corpse_decay);
			NewCorpse->Spawn();
		}
	}

	return NewCorpse;
}

bool ZoneDatabase::SummonAllCharacterCorpses(uint32 char_id, uint32 dest_zone_id, uint16 dest_instance_id, const glm::vec4& position) {
	Corpse* NewCorpse = 0;
	int CorpseCount = 0;

	std::string update_query = StringFormat(
		"UPDATE character_corpses SET zone_id = %i, instance_id = %i, x = %f, y = %f, z = %f, heading = %f, is_buried = 0, was_at_graveyard = 0 WHERE charid = %i",
		dest_zone_id, dest_instance_id, position.x, position.y, position.z, position.w, char_id
	);
	auto results = QueryDatabase(update_query);

	std::string select_query = StringFormat(
		"SELECT `id`, `charname`, `time_of_death`, `is_rezzed` FROM `character_corpses` WHERE `charid` = '%u'"
		"ORDER BY time_of_death",
		char_id);
	results = QueryDatabase(select_query);

	for (auto row = results.begin(); row != results.end(); ++row) {
		NewCorpse = Corpse::LoadCharacterCorpseEntity(
			atoul(row[0]),
			char_id,
			row[1],
			position,
			row[2],
			atoi(row[3]) == 1,
			false);
		if (NewCorpse) {
			entity_list.AddCorpse(NewCorpse);
			int32 corpse_decay = 0;
			if(NewCorpse->IsEmpty())
			{
				corpse_decay = RuleI(Character, EmptyCorpseDecayTimeMS);
			}
			else
			{
				corpse_decay = RuleI(Character, CorpseDecayTimeMS);
			}
			NewCorpse->SetDecayTimer(corpse_decay);
			NewCorpse->Spawn();
			++CorpseCount;
		}
		else{
			Log.Out(Logs::General, Logs::Error, "Unable to construct a player corpse for character id %u.", char_id);
		}
	}

	return (CorpseCount > 0);
}

bool ZoneDatabase::UnburyCharacterCorpse(uint32 db_id, uint32 new_zone_id, uint16 new_instance_id, const glm::vec4& position) {
	std::string query = StringFormat("UPDATE `character_corpses` "
                                    "SET `is_buried` = 0, `zone_id` = %u, `instance_id` = %u, "
                                    "`x` = %f, `y` = %f, `z` = %f, `heading` = %f, "
                                    "`time_of_death` = Now(), `was_at_graveyard` = 0 "
                                    "WHERE `id` = %u",
                                    new_zone_id, new_instance_id,
                                    position.x, position.y, position.z, position.w, db_id);
	auto results = QueryDatabase(query);
	if (results.Success() && results.RowsAffected() != 0)
		return true;

	return false;
}

Corpse* ZoneDatabase::LoadCharacterCorpse(uint32 player_corpse_id) {
	Corpse* NewCorpse = 0;
	std::string query = StringFormat(
		"SELECT `id`, `charid`, `charname`, `x`, `y`, `z`, `heading`, `time_of_death`, `is_rezzed`, `was_at_graveyard` FROM `character_corpses` WHERE `id` = '%u' LIMIT 1",
		player_corpse_id
	);
	auto results = QueryDatabase(query);
	for (auto row = results.begin(); row != results.end(); ++row) {
        auto position = glm::vec4(atof(row[3]), atof(row[4]), atof(row[5]), atof(row[6]));
		NewCorpse = Corpse::LoadCharacterCorpseEntity(
				atoul(row[0]), 		 // id					  uint32 in_dbid
				atoul(row[1]),		 // charid				  uint32 in_charid
				row[2], 			 //	char_name
				position,
				row[7],				 // time_of_death		  char* time_of_death
				atoi(row[8]) == 1, 	 // is_rezzed			  bool rezzed
				atoi(row[9])		 // was_at_graveyard	  bool was_at_graveyard
			);
		entity_list.AddCorpse(NewCorpse);
	}
	return NewCorpse;
}

bool ZoneDatabase::LoadCharacterCorpses(uint32 zone_id, uint16 instance_id) {
	std::string query;
	if (!RuleB(Zone, EnableShadowrest)){
		query = StringFormat("SELECT id, charid, charname, x, y, z, heading, time_of_death, is_rezzed, was_at_graveyard FROM character_corpses WHERE zone_id='%u' AND instance_id='%u'", zone_id, instance_id);
	}
	else{
		query = StringFormat("SELECT id, charid, charname, x, y, z, heading, time_of_death, is_rezzed, 0 as was_at_graveyard FROM character_corpses WHERE zone_id='%u' AND instance_id='%u' AND is_buried=0", zone_id, instance_id);
	}

	auto results = QueryDatabase(query);
	for (auto row = results.begin(); row != results.end(); ++row) {
        auto position = glm::vec4(atof(row[3]), atof(row[4]), atof(row[5]), atof(row[6]));
		entity_list.AddCorpse(
			 Corpse::LoadCharacterCorpseEntity(
				atoul(row[0]), 		  // id					  uint32 in_dbid
				atoul(row[1]), 		  // charid				  uint32 in_charid
				row[2], 			  //					  char_name
				position,
				row[7], 			  // time_of_death		  char* time_of_death
				atoi(row[8]) == 1, 	  // is_rezzed			  bool rezzed
				atoi(row[9]))
		);
	}

	return true;
}

uint32 ZoneDatabase::GetFirstCorpseID(uint32 char_id) {
	std::string query = StringFormat("SELECT `id` FROM `character_corpses` WHERE `charid` = '%u' AND `is_buried` = 0 ORDER BY `time_of_death` LIMIT 1", char_id);
	auto results = QueryDatabase(query);
	for (auto row = results.begin(); row != results.end(); ++row) {
		return atoi(row[0]);
	}
	return 0;
}

bool ZoneDatabase::DeleteItemOffCharacterCorpse(uint32 db_id, uint32 equip_slot, uint32 item_id){
	std::string query = StringFormat("DELETE FROM `character_corpse_items` WHERE `corpse_id` = %u AND equip_slot = %u AND item_id = %u", db_id, equip_slot, item_id);
	auto results = QueryDatabase(query);
	if (!results.Success()){
		return false;
	}
	if(RuleB(Character, UsePlayerCorpseBackups))
	{
		std::string query = StringFormat("DELETE FROM `character_corpse_items_backup` WHERE `corpse_id` = %u AND equip_slot = %u AND item_id = %u", db_id, equip_slot, item_id);
		auto results = QueryDatabase(query);
		if (!results.Success()){
			return false;
		}
	}
	return true;
}

bool ZoneDatabase::BuryCharacterCorpse(uint32 db_id) {
	std::string query = StringFormat("UPDATE `character_corpses` SET `is_buried` = 1 WHERE `id` = %u", db_id);
	auto results = QueryDatabase(query);
	if (results.Success() && results.RowsAffected() != 0){
		return true;
	}
	return false;
}

bool ZoneDatabase::BuryAllCharacterCorpses(uint32 char_id) {
	std::string query = StringFormat("SELECT `id` FROM `character_corpses` WHERE `charid` = %u", char_id);
	auto results = QueryDatabase(query);
	for (auto row = results.begin(); row != results.end(); ++row) {
		BuryCharacterCorpse(atoi(row[0]));
		return true;
	}
	return false;
}

bool ZoneDatabase::DeleteCharacterCorpse(uint32 db_id) {
	std::string query = StringFormat("DELETE FROM `character_corpses` WHERE `id` = %d", db_id);
	auto results = QueryDatabase(query);
	if (!results.Success()){ 
		return false;
	}
	std::string ci_query = StringFormat("DELETE FROM `character_corpse_items` WHERE `corpse_id` = %d", db_id);
	auto ci_results = QueryDatabase(ci_query);
	if (!ci_results.Success()){ 
		return false;
	}
	return true;
}

bool ZoneDatabase::IsValidCorpseBackup(uint32 corpse_id) {
	std::string query = StringFormat("SELECT COUNT(*) FROM `character_corpses_backup` WHERE `id` = %d", corpse_id);
	auto results = QueryDatabase(query);
	auto row = results.begin();
	if(atoi(row[0]) == 1)
		return true;

	return false;
}

bool ZoneDatabase::IsValidCorpse(uint32 corpse_id) {
	std::string query = StringFormat("SELECT COUNT(*) FROM `character_corpses` WHERE `id` = %d", corpse_id);
	auto results = QueryDatabase(query);
	auto row = results.begin();
	if(atoi(row[0]) == 1)
		return true;

	return false;
}

bool ZoneDatabase::CopyBackupCorpse(uint32 corpse_id) {
	std::string query = StringFormat("INSERT INTO `character_corpses` SELECT * from `character_corpses_backup` WHERE `id` = %d", corpse_id);
	auto results = QueryDatabase(query);
	if (!results.Success()){ 
		return false;
	}
	std::string tod_query = StringFormat("UPDATE `character_corpses` SET `time_of_death` = NOW() WHERE `id` = %d", corpse_id);
	auto tod_results = QueryDatabase(tod_query);
	if (!tod_results.Success()){ 
		return false;
	}
	std::string ci_query = StringFormat("REPLACE INTO `character_corpse_items` SELECT * from `character_corpse_items_backup` WHERE `corpse_id` = %d", corpse_id);
	auto ci_results = QueryDatabase(ci_query);
	if (!ci_results.Success()){ 
		Log.Out(Logs::Detail, Logs::Error, "CopyBackupCorpse() Error replacing items.");
	}

	return true;
}

bool ZoneDatabase::IsCorpseBackupOwner(uint32 corpse_id, uint32 char_id) {
	std::string query = StringFormat("SELECT COUNT(*) FROM `character_corpses_backup` WHERE `id` = %d AND `charid` = %d", corpse_id, char_id);
	auto results = QueryDatabase(query);
	auto row = results.begin();
	if(atoi(row[0]) == 1)
		return true;

	return false;
}

int8 ZoneDatabase::ItemQuantityType(int16 item_id)
{
	const Item_Struct* item = database.GetItem(item_id);
	if(item)
	{
		//Item does not have quantity and is not stackable (Normal item.)
		if (item->MaxCharges < 1 && (item->StackSize < 1 || !item->Stackable)) 
		{ 
			return Quantity_Normal;
		}
		//Item is not stackable, and uses charges.
		else if(item->StackSize < 1 || !item->Stackable) 
		{
			return Quantity_Charges;
		}
		//Due to the previous checks, item has to stack.
		else
		{
			return Quantity_Stacked;
		}
	}

	return Quantity_Unknown;
}

bool ZoneDatabase::RetrieveMBMessages(uint16 category, std::vector<MBMessageRetrievalGen_Struct>& outData) {
	std::string query = StringFormat("SELECT id, date, language, author, subject, category from mb_messages WHERE category=%i ORDER BY time DESC LIMIT %i",category, 500);
	auto results = QueryDatabase(query);
	if(!results.Success())
	{
		return false; // no messages
	}

	for (auto row = results.begin(); row != results.end(); ++row) {
		MBMessageRetrievalGen_Struct message;
		message.id = atoi(row[0]);			
		strncpy(message.date, row[1], sizeof(message.date));		
		message.language = atoi(row[2]);
		strncpy(message.author, row[3],  sizeof(message.author));
		strncpy(message.subject, row[4], sizeof(message.subject));
		message.category = atoi(row[5]);
		outData.push_back(message);
	}
	return false;
}

bool ZoneDatabase::PostMBMessage(uint32 charid, const char* charName, MBMessageRetrievalGen_Struct* inData) {
	std::string query = StringFormat("INSERT INTO mb_messages (charid, date, language, author, subject, category, message) VALUES (%i, '%s', %i, '%s', '%s', %i, '%s')", charid, EscapeString(inData->date, sizeof(inData->date)).c_str(), inData->language, charName, EscapeString(inData->subject, strlen(inData->subject)).c_str(), inData->category, EscapeString(inData->message, strlen(inData->message)).c_str());
	auto results = QueryDatabase(query);
	if(!results.Success())
	{
		return false; // no messages
	}
	return true;
}

bool ZoneDatabase::EraseMBMessage(uint32 id, uint32 charid)
{
	std::string query = StringFormat("DELETE FROM mb_messages WHERE id=%i AND charid=%i", id, charid);
	auto results = QueryDatabase(query);
	if(!results.Success())
	{
		return false; // no messages
	}
	return true;
}

bool ZoneDatabase::ViewMBMessage(uint32 id, char* outData) {
	std::string query = StringFormat("SELECT message from mb_messages WHERE id=%i", id);
	auto results = QueryDatabase(query);
	if(!results.Success())
	{
		return false; // no messages
	}

	if(results.RowCount() == 0 || results.RowCount() > 1)
	{
		return false;
	}
	auto row = results.begin();
	strncpy(outData, row[0], 2048);
	return true;
}


bool ZoneDatabase::SaveSoulboundItems(Client* client, std::list<ItemInst*>::const_iterator &start, std::list<ItemInst*>::const_iterator &end)
{
		// Delete cursor items
	std::string query = StringFormat("DELETE FROM character_inventory WHERE id = %i "
                                    "AND ((slotid >= %i AND slotid <= %i) "
                                    "OR slotid = %i OR (slotid >= %i AND slotid <= %i) )",
                                    client->CharacterID(), EmuConstants::CURSOR_QUEUE_BEGIN, EmuConstants::CURSOR_QUEUE_END, 
									MainCursor, EmuConstants::CURSOR_BAG_BEGIN, EmuConstants::CURSOR_BAG_END);
    auto results = QueryDatabase(query);
    if (!results.Success()) {
        std::cout << "Clearing cursor failed: " << results.ErrorMessage() << std::endl;
        return false;
    }

	int8 count = 0;
	int i = EmuConstants::CURSOR_QUEUE_BEGIN;
    for(auto it = start; it != end; ++it) {
        ItemInst *inst = *it;
		if(inst && inst->GetItem()->Soulbound)
		{
			int16 newslot = client->GetInv().FindFreeSlot(false, false, inst->GetItem()->Size);
			if(newslot != INVALID_INDEX)
			{
				client->PutItemInInventory(newslot, *inst);
				++count;
			}
		}
		else if(inst)
		{
			int16 use_slot = (i == EmuConstants::CURSOR_QUEUE_BEGIN) ? MainCursor : i;
			if (SaveInventory(client->CharacterID(), inst, use_slot)) 
			{
				++i;
			}
		}
    }

	if(count > 0)
		return true;

	return false;
}

bool ZoneDatabase::UpdateSkillDifficulty(uint16 skillid, float difficulty)
{
		std::string query = StringFormat("UPDATE skill_difficulty SET difficulty = %0.2f "
                                    "WHERE skillid = %u;", difficulty, skillid);
    auto results = QueryDatabase(query);
	if (!results.Success())
		return false;

	return results.RowsAffected() > 0;
}
