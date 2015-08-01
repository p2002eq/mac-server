/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2015 EQEMu Development Team (http://eqemulator.net)

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
#include "../common/rulesys.h"

#include <ctype.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <mysqld_error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Disgrace: for windows compile
#ifdef _WINDOWS
#include <windows.h>
#define snprintf	_snprintf
#define strncasecmp	_strnicmp
#define strcasecmp	_stricmp
#else
#include "unix.h"
#include <netinet/in.h>
#include <sys/time.h>
#endif

#include "database.h"
#include "eq_packet_structs.h"
#include "extprofile.h"
#include "string_util.h"
#include "random.h"

extern Client client;
EQEmu::Random emudb_random;

#ifdef _WINDOWS
#if _MSC_VER > 1700 // greater than 2012 (2013+)
#	define _ISNAN_(a) std::isnan(a)
#else
#	include <float.h>
#	define _ISNAN_(a) _isnan(a)
#endif
#else
#	define _ISNAN_(a) std::isnan(a)
#endif

#pragma warning( disable : 4267 4244 4018 )

Database::Database () {
	DBInitVars();
}

/*
Establish a connection to a mysql database with the supplied parameters
*/

Database::Database(const char* host, const char* user, const char* passwd, const char* database, uint32 port)
{
	DBInitVars();
	Connect(host, user, passwd, database, port);
}

bool Database::Connect(const char* host, const char* user, const char* passwd, const char* database, uint32 port) {
	uint32 errnum= 0;
	char errbuf[MYSQL_ERRMSG_SIZE];
	if (!Open(host, user, passwd, database, port, &errnum, errbuf)) {
		Log.Out(Logs::General, Logs::Error, "Failed to connect to database: Error: %s", errbuf); 
		return false; 
	}
	else {
		Log.Out(Logs::General, Logs::Status, "Using database '%s' at %s:%d", database, host,port);
		return true;
	}
}

void Database::DBInitVars() { 
	varcache_array = 0;
	varcache_max = 0;
	varcache_lastupdate = 0;
}

/*
	Close the connection to the database
*/

Database::~Database()
{
	unsigned int x;
	if (varcache_array) {
		for (x=0; x<varcache_max; x++) {
			safe_delete_array(varcache_array[x]);
		}
		safe_delete_array(varcache_array);
	}
}

/*
	Check if there is an account with name "name" and password "password"
	Return the account id or zero if no account matches.
	Zero will also be returned if there is a database error.
*/
uint32 Database::CheckLogin(const char* name, const char* password, int16* oStatus) {

	if(strlen(name) >= 50 || strlen(password) >= 50)
		return(0);

	char tmpUN[100];
	char tmpPW[100];

	DoEscapeString(tmpUN, name, strlen(name));
	DoEscapeString(tmpPW, password, strlen(password));

	std::string query = StringFormat("SELECT id, status FROM account WHERE name='%s' AND password is not null "
		"and length(password) > 0 and (password='%s' or password=MD5('%s'))",
		tmpUN, tmpPW, tmpPW);
	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		return 0;
	}

	if(results.RowCount() == 0)
		return 0;

	auto row = results.begin();

	uint32 id = atoi(row[0]);

	if (oStatus)
		*oStatus = atoi(row[1]);

	return id;
}

//Get Banned IP Address List - Only return false if the incoming connection's IP address is not present in the banned_ips table.
bool Database::CheckBannedIPs(const char* loginIP)
{
	std::string query = StringFormat("SELECT ip_address FROM Banned_IPs WHERE ip_address='%s'", loginIP);

	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		return true;
	}

	if (results.RowCount() != 0)
		return true;

	return false;
}

bool Database::AddBannedIP(char* bannedIP, const char* notes) {
	std::string query = StringFormat("INSERT into Banned_IPs SET ip_address='%s', notes='%s'", bannedIP, notes); 
	auto results = QueryDatabase(query); 
	if (!results.Success()) {
		return false;
	} 
	return true;
}

 bool Database::CheckGMIPs(const char* ip_address, uint32 account_id) {
	std::string query = StringFormat("SELECT * FROM `gm_ips` WHERE `ip_address` = '%s' AND `account_id` = %i", ip_address, account_id); 
	auto results = QueryDatabase(query);

	if (!results.Success())
		return false;

	if (results.RowCount() == 1)
		return true;

	return false;
}

bool Database::AddGMIP(char* ip_address, char* name) {
	std::string query = StringFormat("INSERT into `gm_ips` SET `ip_address` = '%s', `name` = '%s'", ip_address, name); 
	auto results = QueryDatabase(query); 
	return results.Success();
}

void Database::LoginIP(uint32 AccountID, const char* LoginIP) {
	std::string query = StringFormat("INSERT INTO account_ip SET accid=%i, ip='%s' ON DUPLICATE KEY UPDATE count=count+1, lastused=now()", AccountID, LoginIP); 
	QueryDatabase(query); 
}

int16 Database::CheckStatus(uint32 account_id) {
	std::string query = StringFormat("SELECT `status`, UNIX_TIMESTAMP(`suspendeduntil`) as `suspendeduntil`, UNIX_TIMESTAMP() as `current`"
							" FROM `account` WHERE `id` = %i", account_id);

	auto results = QueryDatabase(query); 
	if (!results.Success()) {
		return 0;
	}

	if (results.RowCount() != 1)
		return 0;
	
	auto row = results.begin(); 
	int16 status = atoi(row[0]); 
	int32 suspendeduntil = 0;

	// MariaDB initalizes with NULL if unix_timestamp() is out of range
	if (row[1] != nullptr) {
		suspendeduntil = atoi(row[1]);
	}

	int32 current = atoi(row[2]);

	if(suspendeduntil > current)
		return -1;

	return status;
}

uint32 Database::CreateAccount(const char* name, const char* password, int16 status, uint32 lsaccount_id) {
	std::string query;

	if (password)
		query = StringFormat("INSERT INTO account SET name='%s', password='%s', status=%i, lsaccount_id=%i, time_creation=UNIX_TIMESTAMP();",name,password,status, lsaccount_id);
	else
		query = StringFormat("INSERT INTO account SET name='%s', status=%i, lsaccount_id=%i, time_creation=UNIX_TIMESTAMP();",name, status, lsaccount_id);

	Log.Out(Logs::General, Logs::World_Server, "Account Attempting to be created: '%s' status: %i", name, status);
	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return 0;
	}

	if (results.LastInsertedID() == 0)
	{
		return 0;
	}

	return results.LastInsertedID();
}

bool Database::DeleteAccount(const char* name) {
	std::string query = StringFormat("DELETE FROM account WHERE name='%s';",name); 
	Log.Out(Logs::General, Logs::World_Server, "Account Attempting to be deleted:'%s'", name);

	auto results = QueryDatabase(query); 
	if (!results.Success()) {
		return false;
	}

	return results.RowsAffected() == 1;
}

bool Database::SetLocalPassword(uint32 accid, const char* password) {
	std::string query = StringFormat("UPDATE account SET password=MD5('%s') where id=%i;", EscapeString(password).c_str(), accid);

	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return false;
	}

	return true;
}

bool Database::SetAccountStatus(const char* name, int16 status) {
	std::string query = StringFormat("UPDATE account SET status=%i WHERE name='%s';", status, name);

	std::cout << "Account being GM Flagged:" << name << ", Level: " << (int16) status << std::endl;

	auto results = QueryDatabase(query);

	if (!results.Success())
		return false;

	if (results.RowsAffected() == 0)
	{
		std::cout << "Account: " << name << " does not exist, therefore it cannot be flagged\n";
		return false; 
	}

	return true;
}

/* This initially creates the character during character create */
bool Database::ReserveName(uint32 account_id, char* name) {
	std::string query = StringFormat("SELECT `account_id`, `name` FROM `character_data` WHERE `name` = '%s'", name);
	auto results = QueryDatabase(query);
	for (auto row = results.begin(); row != results.end(); ++row) {
		if (row[0] && atoi(row[0]) > 0){
			Log.Out(Logs::General, Logs::World_Server, "Account: %i tried to request name: %s, but it is already taken...", account_id, name);
			return false;
		}
	}

	query = StringFormat("INSERT INTO `character_data` SET `account_id` = %i, `name` = '%s'", account_id, name); 
	results = QueryDatabase(query);
	if (!results.Success() || results.ErrorMessage() != ""){ return false; } 
	return true;
}

bool Database::MarkCharacterDeleted(char *name) {
	std::string query = StringFormat("UPDATE character_data SET is_deleted = 1 where name='%s'", EscapeString(name).c_str());

	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return false;
	}

	if (results.RowsAffected() == 0) {
		return false;
	}

	return true;
}

bool Database::UnDeleteCharacter(const char *name) {
	std::string query = StringFormat("UPDATE character_data SET is_deleted = 0 where name='%s'", EscapeString(name).c_str());

	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return false;
	}

	if (results.RowsAffected() == 0) {
		return false;
	}

	return true;
}

/*
	Delete the character with the name "name"
	returns false on failure, true otherwise
*/
bool Database::DeleteCharacter(char *name) {
	uint32 charid = 0;
	if(!name ||	!strlen(name)) {
		Log.Out(Logs::General, Logs::World_Server, "DeleteCharacter: request to delete without a name (empty char slot)");
		return false;
	}
	Log.Out(Logs::General, Logs::World_Server, "Database::DeleteCharacter name : '%s'", name);

	/* Get id from character_data before deleting record so we can clean up the rest of the tables */
	std::string query = StringFormat("SELECT `id` from `character_data` WHERE `name` = '%s'", name);
	auto results = QueryDatabase(query);
	for (auto row = results.begin(); row != results.end(); ++row) { charid = atoi(row[0]); }
	if (charid <= 0){ std::cerr << "Database::DeleteCharacter :: Character not found, stopping delete...\n"; return false; }

	query = StringFormat("DELETE FROM `quest_globals` WHERE `charid` = '%d'", charid); QueryDatabase(query);			   
	query = StringFormat("DELETE FROM `friends` WHERE `charid` = '%d'", charid); QueryDatabase(query);				  
	query = StringFormat("DELETE FROM `mail` WHERE `charid` = '%d'", charid); QueryDatabase(query);					  
	query = StringFormat("DELETE FROM `timers` WHERE `char_id` = '%d'", charid); QueryDatabase(query);				  
	query = StringFormat("DELETE FROM `inventory` WHERE `charid` = '%d'", charid); QueryDatabase(query);				  
	query = StringFormat("DELETE FROM `char_recipe_list` WHERE `char_id` = '%d'", charid); QueryDatabase(query);		  
	query = StringFormat("DELETE FROM `zone_flags` WHERE `charID` = '%d'", charid); QueryDatabase(query);				  
	query = StringFormat("DELETE FROM `titles` WHERE `char_id` = '%d'", charid); QueryDatabase(query);				  
	query = StringFormat("DELETE FROM `player_titlesets` WHERE `char_id` = '%d'", charid); QueryDatabase(query);		  		  
	query = StringFormat("DELETE FROM `faction_values` WHERE `char_id` = '%d'", charid); QueryDatabase(query);		  
	query = StringFormat("DELETE FROM `instance_list_player` WHERE `charid` = '%d'", charid); QueryDatabase(query);	  
	query = StringFormat("DELETE FROM `character_data` WHERE `id` = '%d'", charid); QueryDatabase(query);				  
	query = StringFormat("DELETE FROM `character_skills` WHERE `id` = %u", charid); QueryDatabase(query);				  
	query = StringFormat("DELETE FROM `character_languages` WHERE `id` = %u", charid); QueryDatabase(query);			  
	query = StringFormat("DELETE FROM `character_bind` WHERE `id` = %u", charid); QueryDatabase(query);				  
	query = StringFormat("DELETE FROM `character_alternate_abilities` WHERE `id` = %u", charid); QueryDatabase(query);  
	query = StringFormat("DELETE FROM `character_currency` WHERE `id` = %u", charid); QueryDatabase(query);			  
	query = StringFormat("DELETE FROM `character_data` WHERE `id` = %u", charid); QueryDatabase(query);				  
	query = StringFormat("DELETE FROM `character_spells` WHERE `id` = %u", charid); QueryDatabase(query);				  
	query = StringFormat("DELETE FROM `character_memmed_spells` WHERE `id` = %u", charid); QueryDatabase(query);		  	  
	query = StringFormat("DELETE FROM `character_material` WHERE `id` = %u", charid); QueryDatabase(query);			  
	query = StringFormat("DELETE FROM `character_inspect_messages` WHERE `id` = %u", charid); QueryDatabase(query);	  
	query = StringFormat("DELETE FROM `guild_members` WHERE `char_id` = '%d'", charid);
	QueryDatabase(query);
	
	return true;
}

bool Database::SaveCharacterCreate(uint32 character_id, uint32 account_id, PlayerProfile_Struct* pp){
	std::string query = StringFormat(
		"REPLACE INTO `character_data` ("
		"id,"
		"account_id,"
		"`name`,"
		"last_name,"
		"gender,"
		"race,"
		"class,"
		"`level`,"
		"deity,"
		"birthday,"
		"last_login,"
		"time_played,"
		"pvp_status,"
		"level2,"
		"anon,"
		"gm,"
		"intoxication,"
		"hair_color,"
		"beard_color,"
		"eye_color_1,"
		"eye_color_2,"
		"hair_style,"
		"beard,"
		"ability_time_seconds,"
		"ability_number,"
		"ability_time_minutes,"
		"ability_time_hours,"
		"title,"
		"suffix,"
		"exp,"
		"points,"
		"mana,"
		"cur_hp,"
		"str,"
		"sta,"
		"cha,"
		"dex,"
		"`int`,"
		"agi,"
		"wis,"
		"face,"
		"y,"
		"x,"
		"z,"
		"heading,"
		"autosplit_enabled,"
		"zone_change_count,"
		"toxicity,"
		"hunger_level,"
		"thirst_level,"
		"ability_up,"
		"zone_id,"
		"zone_instance,"
		"endurance,"
		"air_remaining,"
		"aa_points_spent,"
		"aa_exp,"
		"aa_points,"
		"boatid,"
		"boatname,"
		"famished) "
		"VALUES ("
		"%u,"  // id					
		"%u,"  // account_id			
		"'%s',"  // `name`				
		"'%s',"  // last_name			
		"%u,"  // gender				
		"%u,"  // race					
		"%u,"  // class					
		"%u,"  // `level`				
		"%u,"  // deity					
		"%u,"  // birthday				
		"%u,"  // last_login			
		"%u,"  // time_played			
		"%u,"  // pvp_status			
		"%u,"  // level2				
		"%u,"  // anon					
		"%u,"  // gm					
		"%u,"  // intoxication			
		"%u,"  // hair_color			
		"%u,"  // beard_color			
		"%u,"  // eye_color_1			
		"%u,"  // eye_color_2			
		"%u,"  // hair_style			
		"%u,"  // beard					
		"%u,"  // ability_time_seconds	
		"%u,"  // ability_number		
		"%u,"  // ability_time_minutes	
		"%u,"  // ability_time_hours	
		"'%s',"  // title				
		"'%s',"  // suffix				
		"%u,"  // exp					
		"%u,"  // points				
		"%u,"  // mana					
		"%u,"  // cur_hp				
		"%u,"  // str					
		"%u,"  // sta					
		"%u,"  // cha					
		"%u,"  // dex					
		"%u,"  // `int`					
		"%u,"  // agi					
		"%u,"  // wis					
		"%u,"  // face					
		"%f,"  // y						
		"%f,"  // x						
		"%f,"  // z						
		"%f,"  // heading				
		"%u,"  // autosplit_enabled		
		"%u,"  // zone_change_count		
		"%i,"  // toxicity				
		"%i,"  // hunger_level			
		"%i,"  // thirst_level			
		"%u,"  // ability_up			
		"%u,"  // zone_id				
		"%u,"  // zone_instance			
		"%u,"  // endurance				
		"%u,"  // air_remaining			
		"%u,"  // aa_points_spent		
		"%u,"  // aa_exp				
		"%u,"  // aa_points				
		"%u,"   // boatid		
		"'%s',"  // boatname	
		"%u"
		")",
		character_id,					  // " id,                        "
		account_id,						  // " account_id,                "
		EscapeString(pp->name).c_str(),	  // " `name`,                    "
		EscapeString(pp->last_name).c_str(), // " last_name,              "
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
		pp->ability_time_seconds,		  // " ability_time_seconds,      "
		pp->ability_number,				  // " ability_number,            "
		pp->ability_time_minutes,		  // " ability_time_minutes,      "
		pp->ability_time_hours,			  // " ability_time_hours,        "
		EscapeString(pp->title).c_str(),  // " title,                     "
		EscapeString(pp->suffix).c_str(), // " suffix,                    "
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
		pp->toxicity,					  // " toxicity,                  "
		pp->hunger_level,				  // " hunger_level,              "
		pp->thirst_level,				  // " thirst_level,              "
		pp->ability_up,					  // " ability_up,                "
		pp->zone_id,					  // " zone_id,                   "
		pp->zoneInstance,				  // " zone_instance,             "
		pp->endurance,					  // " endurance,                 "
		pp->air_remaining,				  // " air_remaining,             "
		pp->aapoints_spent,				  // " aa_points_spent,           "
		pp->expAA,						  // " aa_exp,                    "
		pp->aapoints,					  // " aa_points,                 "
		pp->boatid,						  // " boatid,					  "
		EscapeString(pp->boat).c_str(),	  // " boatname                   "
		pp->famished					  // " famished)				  "
	);
	auto results = QueryDatabase(query);
	/* Save Bind Points */
	query = StringFormat("REPLACE INTO `character_bind` (id, zone_id, instance_id, x, y, z, heading, is_home)"
		" VALUES (%u, %u, %u, %f, %f, %f, %f, %i), "
		"(%u, %u, %u, %f, %f, %f, %f, %i)",
		character_id, pp->binds[0].zoneId, 0, pp->binds[0].x, pp->binds[0].y, pp->binds[0].z, pp->binds[0].heading, 0,
		character_id, pp->binds[4].zoneId, 0, pp->binds[4].x, pp->binds[4].y, pp->binds[4].z, pp->binds[4].heading, 1
	); results = QueryDatabase(query); 

	/* Save Skills */
	int firstquery = 0;
	for (int i = 0; i < MAX_PP_SKILL; i++){
		if (pp->skills[i] > 0){
			if (firstquery != 1){
				firstquery = 1;
				query = StringFormat("REPLACE INTO `character_skills` (id, skill_id, value) VALUES (%u, %u, %u)", character_id, i, pp->skills[i]);
			}
			else{
				query = query + StringFormat(", (%u, %u, %u)", character_id, i, pp->skills[i]);
			}
		}
	}
	results = QueryDatabase(query); 

	/* Save Language */
	firstquery = 0;
	for (int i = 0; i < MAX_PP_LANGUAGE; i++){
		if (pp->languages[i] > 0){
			if (firstquery != 1){
				firstquery = 1;
				query = StringFormat("REPLACE INTO `character_languages` (id, lang_id, value) VALUES (%u, %u, %u)", character_id, i, pp->languages[i]);
			}
			else{
				query = query + StringFormat(", (%u, %u, %u)", character_id, i, pp->languages[i]);
			}
		}
	}
	results = QueryDatabase(query); 

	return true;
}

/* This only for new Character creation storing */
bool Database::StoreCharacter(uint32 account_id, PlayerProfile_Struct* pp, Inventory* inv) {
	uint32 charid = 0; 
	char zone[50]; 
	float x, y, z; 
	charid = GetCharacterID(pp->name);

	if(!charid) {
		Log.Out(Logs::General, Logs::Error, "StoreCharacter: no character id");
		return false;
	}

	const char *zname = GetZoneName(pp->zone_id);
	if(zname == nullptr) {
		/* Zone not in the DB, something to prevent crash... */
		strn0cpy(zone, "qeynos", 49);
		pp->zone_id = qeynos;
	}
	else{ strn0cpy(zone, zname, 49); }

	x = pp->x;
	y = pp->y;
	z = pp->z;

	/* Saves Player Profile Data */
	SaveCharacterCreate(charid, account_id, pp); 

	/* Insert starting inventory... */
	std::string invquery;
	for (int16 i=MAIN_BEGIN; i<=EmuConstants::BANK_BAGS_END;)
	{
		const ItemInst* newinv = inv->GetItem(i);
		if (newinv) {
			invquery = StringFormat("INSERT INTO `inventory` (charid, slotid, itemid, charges, color) VALUES (%u, %i, %u, %i, %u)",
				charid, i, newinv->GetItem()->ID, newinv->GetCharges(), newinv->GetColor()); 
			
			auto results = QueryDatabase(invquery); 
		}

		if (i == EmuConstants::GENERAL_END) {
			i = EmuConstants::GENERAL_BAGS_BEGIN;
			continue;
		}
		else if (i == EmuConstants::CURSOR_BAG_END) { 
			i = EmuConstants::BANK_BEGIN; 
			continue; 
		}
		else if (i == EmuConstants::BANK_END) { 
			i = EmuConstants::BANK_BAGS_BEGIN; 
			continue; 
		} 
		i++;
	}
	return true;
}

uint32 Database::GetCharacterID(const char *name) {
	std::string query = StringFormat("SELECT `id` FROM `character_data` WHERE `name` = '%s'", name);
	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		std::cerr << "Error in GetAccountIDByChar query '" << query << "' " << results.ErrorMessage() << std::endl;
		return 0;
	}

	if (results.RowCount() != 1)
		return 0; 

	auto row = results.begin();
	if (row[0]){ return atoi(row[0]); }
	return 0; 
}

/*
	This function returns the account_id that owns the character with
	the name "name" or zero if no character with that name was found
	Zero will also be returned if there is a database error.
*/
uint32 Database::GetAccountIDByChar(const char* charname, uint32* oCharID) {
	std::string query = StringFormat("SELECT `account_id`, `id` FROM `character_data` WHERE name='%s'", EscapeString(charname).c_str());

	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		return 0;
	}

	if (results.RowCount() != 1)
		return 0; 

	auto row = results.begin();

	uint32 accountId = atoi(row[0]);

	if (oCharID)
		*oCharID = atoi(row[1]);

	return accountId;
}

uint32 Database::GetLevelByChar(const char* charname) {
	std::string query = StringFormat("SELECT `level` FROM `character_data` WHERE name='%s'", EscapeString(charname).c_str()); 
	auto results = QueryDatabase(query); 
	if (!results.Success()) {
		return 0;
	}

	if (results.RowCount() != 1)
		return 0;

	auto row = results.begin(); 
	return atoi(row[0]);
}

// Retrieve account_id for a given char_id
uint32 Database::GetAccountIDByChar(uint32 char_id) {
	std::string query = StringFormat("SELECT `account_id` FROM `character_data` WHERE `id` = %i LIMIT 1", char_id); 
	auto results = QueryDatabase(query); 
	if (!results.Success()) {
		return 0;
	}

	if (results.RowCount() != 1)
		return 0;

	auto row = results.begin(); 
	return atoi(row[0]);
}

uint32 Database::GetAccountIDByName(const char* accname, int16* status, uint32* lsid) {
	if (!isAlphaNumeric(accname))
		return 0;

	std::string query = StringFormat("SELECT `id`, `status`, `lsaccount_id` FROM `account` WHERE `name` = '%s' LIMIT 1", accname);
	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return 0;
	}

	if (results.RowCount() != 1)
		return 0;

	auto row = results.begin();

	uint32 id = atoi(row[0]);

	if (status)
		*status = atoi(row[1]);

	if (lsid) {
		if (row[2])
			*lsid = atoi(row[2]);
		else
			*lsid = 0;
	}

	return id;
}

void Database::GetAccountName(uint32 accountid, char* name, uint32* oLSAccountID) {
	std::string query = StringFormat("SELECT `name`, `lsaccount_id` FROM `account` WHERE `id` = '%i'", accountid); 
	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return;
	}

	if (results.RowCount() != 1)
		return;

	auto row = results.begin();

	strcpy(name, row[0]);
	if (row[1] && oLSAccountID) {
		*oLSAccountID = atoi(row[1]);
	}

}

void Database::GetCharName(uint32 char_id, char* name) { 
	std::string query = StringFormat("SELECT `name` FROM `character_data` WHERE id='%i'", char_id);
	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return; 
	}

	auto row = results.begin();
	for (auto row = results.begin(); row != results.end(); ++row) {
		strcpy(name, row[0]);
	}
}

static inline void loadbar(unsigned int x, unsigned int n, unsigned int w = 50) {
	if ((x != n) && (x % (n / 100 + 1) != 0)) return; 
	float ratio = x / (float)n;
	int   c = ratio * w; 
	std::cout << std::setw(3) << (int)(ratio * 100) << "% [";
	for (int x = 0; x<c; x++) std::cout << "=";
	for (int x = c; x<w; x++) std::cout << " ";
	std::cout << "]\r" << std::flush;
}

bool Database::LoadVariables() {
	char *query = nullptr;

	auto results = QueryDatabase(query, LoadVariables_MQ(&query));

	if (!results.Success())
	{
		safe_delete_array(query);
		return false;
	}

	safe_delete_array(query);
	return LoadVariables_result(std::move(results));
}

uint32 Database::LoadVariables_MQ(char** query)
{
	return MakeAnyLenString(query, "SELECT varname, value, unix_timestamp() FROM variables where unix_timestamp(ts) >= %d", varcache_lastupdate);
}

// always returns true? not sure about this.
bool Database::LoadVariables_result(MySQLRequestResult results) {
	uint32 i = 0;
	LockMutex lock(&Mvarcache);

	if (results.RowCount() == 0)
		return true;

	if (!varcache_array) {
		varcache_max = results.RowCount();
		varcache_array = new VarCache_Struct*[varcache_max];
		for (i=0; i<varcache_max; i++)
			varcache_array[i] = 0;
	}
	else {
		uint32 tmpnewmax = varcache_max + results.RowCount();
		VarCache_Struct** tmp = new VarCache_Struct*[tmpnewmax];
		for (i=0; i<tmpnewmax; i++)
			tmp[i] = 0;
		for (i=0; i<varcache_max; i++)
			tmp[i] = varcache_array[i];
		VarCache_Struct** tmpdel = varcache_array;
		varcache_array = tmp;
		varcache_max = tmpnewmax;
		delete [] tmpdel;
	}

	for (auto row = results.begin(); row != results.end(); ++row)
	{
		varcache_lastupdate = atoi(row[2]);
		for (i=0; i<varcache_max; i++) {
			if (varcache_array[i]) {
				if (strcasecmp(varcache_array[i]->varname, row[0]) == 0) {
					delete varcache_array[i];
					varcache_array[i] = (VarCache_Struct*) new uint8[sizeof(VarCache_Struct) + strlen(row[1]) + 1];
					strn0cpy(varcache_array[i]->varname, row[0], sizeof(varcache_array[i]->varname));
					strcpy(varcache_array[i]->value, row[1]);
					break;
				}
			}
			else {
				varcache_array[i] = (VarCache_Struct*) new uint8[sizeof(VarCache_Struct) + strlen(row[1]) + 1];
				strcpy(varcache_array[i]->varname, row[0]);
				strcpy(varcache_array[i]->value, row[1]);
				break;
			}
		}
	}

	uint32 max_used = 0;
	for (i=0; i<varcache_max; i++) {
		if (varcache_array[i]) {
			if (i > max_used)
				max_used = i;
		}
	}

	varcache_max = max_used + 1;

	return true;
}

// Gets variable from 'variables' table
bool Database::GetVariable(const char* varname, char* varvalue, uint16 varvalue_len) {
	varvalue[0] = '\0';

	LockMutex lock(&Mvarcache);
	if (strlen(varname) <= 1)
		return false;
	for (uint32 i=0; i<varcache_max; i++) {

		if (varcache_array[i]) {
			if (strcasecmp(varcache_array[i]->varname, varname) == 0) {
				snprintf(varvalue, varvalue_len, "%s", varcache_array[i]->value);
				varvalue[varvalue_len-1] = 0;
				return true;
			}
		}
		else
			return false;
	}
	return false;
}

bool Database::SetVariable(const char* varname_in, const char* varvalue_in) {
	
	char *varname,*varvalue;

	varname=(char *)malloc(strlen(varname_in)*2+1);
	varvalue=(char *)malloc(strlen(varvalue_in)*2+1);
	DoEscapeString(varname, varname_in, strlen(varname_in));
	DoEscapeString(varvalue, varvalue_in, strlen(varvalue_in));

	std::string query = StringFormat("Update variables set value='%s' WHERE varname like '%s'", varvalue, varname);
	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		free(varname);
		free(varvalue);
		return false;
	}

	if (results.RowsAffected() == 1)
	{
		LoadVariables(); // refresh cache
		free(varname);
		free(varvalue);
		return true;
	}

	query = StringFormat("Insert Into variables (varname, value) values ('%s', '%s')", varname, varvalue);
	results = QueryDatabase(query);
	free(varname);
	free(varvalue);

	if (results.RowsAffected() != 1)
		return false;
	
	LoadVariables(); // refresh cache
	return true;
}

// Get zone starting points from DB
bool Database::GetSafePoints(const char* short_name, uint32 version, float* safe_x, float* safe_y, float* safe_z, int16* minstatus, uint8* minlevel, char *flag_needed) {
	
	std::string query = StringFormat("SELECT safe_x, safe_y, safe_z, min_status, min_level, flag_needed FROM zone "
		" WHERE short_name='%s' AND (version=%i OR version=0) ORDER BY version DESC", short_name, version);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return false;

	if (results.RowCount() == 0)
		return false;

	auto row = results.begin();

	if (safe_x != nullptr)
		*safe_x = atof(row[0]);
	if (safe_y != nullptr)
		*safe_y = atof(row[1]);
	if (safe_z != nullptr)
		*safe_z = atof(row[2]);
	if (minstatus != nullptr)
		*minstatus = atoi(row[3]);
	if (minlevel != nullptr)
		*minlevel = atoi(row[4]);
	if (flag_needed != nullptr)
		strcpy(flag_needed, row[5]);

	return true;
}

bool Database::GetZoneLongName(const char* short_name, char** long_name, char* file_name, float* safe_x, float* safe_y, float* safe_z, uint32* graveyard_id, uint32* maxclients) {
	
	std::string query = StringFormat("SELECT long_name, file_name, safe_x, safe_y, safe_z, graveyard_id, maxclients FROM zone WHERE short_name='%s' AND version=0", short_name);
	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return false;
	}

	if (results.RowCount() == 0)
		return false;

	auto row = results.begin();

	if (long_name != nullptr)
		*long_name = strcpy(new char[strlen(row[0])+1], row[0]);

	if (file_name != nullptr) {
		if (row[1] == nullptr)
			strcpy(file_name, short_name);
		else
			strcpy(file_name, row[1]);
	}

	if (safe_x != nullptr)
		*safe_x = atof(row[2]);
	if (safe_y != nullptr)
		*safe_y = atof(row[3]);
	if (safe_z != nullptr)
		*safe_z = atof(row[4]);
	if (graveyard_id != nullptr)
		*graveyard_id = atoi(row[5]);
	if (maxclients != nullptr)
		*maxclients = atoi(row[6]);

	return true;
}

uint32 Database::GetZoneGraveyardID(uint32 zone_id, uint32 version) {

	std::string query = StringFormat("SELECT graveyard_id FROM zone WHERE zoneidnumber='%u' AND (version=%i OR version=0) ORDER BY version DESC", zone_id, version);
	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		return 0;
	}

	if (results.RowCount() == 0)
		return 0;

	auto row = results.begin();
	return atoi(row[0]);
}

bool Database::GetZoneGraveyard(const uint32 graveyard_id, uint32* graveyard_zoneid, float* graveyard_x, float* graveyard_y, float* graveyard_z, float* graveyard_heading) {
	
	std::string query = StringFormat("SELECT zone_id, x, y, z, heading FROM graveyard WHERE id=%i", graveyard_id);
	auto results = QueryDatabase(query);

	if (!results.Success()){
		return false;
	}

	if (results.RowCount() != 1)
		return false;

	auto row = results.begin();

	if(graveyard_zoneid != nullptr)
		*graveyard_zoneid = atoi(row[0]);
	if(graveyard_x != nullptr)
		*graveyard_x = atof(row[1]);
	if(graveyard_y != nullptr)
		*graveyard_y = atof(row[2]);
	if(graveyard_z != nullptr)
		*graveyard_z = atof(row[3]);
	if(graveyard_heading != nullptr)
		*graveyard_heading = atof(row[4]);

	return true;
}

bool Database::LoadZoneNames() {
	std::string query("SELECT zoneidnumber, short_name FROM zone");

	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		return false;
	}

	for (auto row= results.begin();row != results.end();++row)
	{
		uint32 zoneid = atoi(row[0]);
		std::string zonename = row[1];
		zonename_array.insert(std::pair<uint32,std::string>(zoneid,zonename));
	}

	return true;
}

uint32 Database::GetZoneID(const char* zonename) {

	if (zonename == nullptr)
		return 0;

	for (auto iter = zonename_array.begin(); iter != zonename_array.end(); ++iter)
		if (strcasecmp(iter->second.c_str(), zonename) == 0)
			return iter->first;

	return 0;
}

const char* Database::GetZoneName(uint32 zoneID, bool ErrorUnknown) {
	auto iter = zonename_array.find(zoneID);

	if (iter != zonename_array.end())
		return iter->second.c_str();

	if (ErrorUnknown)
		return "UNKNOWN";

	return 0;
}

uint8 Database::GetPEQZone(uint32 zoneID, uint32 version){
	
	std::string query = StringFormat("SELECT peqzone from zone where zoneidnumber='%i' AND (version=%i OR version=0) ORDER BY version DESC", zoneID, version);
	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return 0;
	}

	if (results.RowCount() == 0)
		return 0;

	auto row = results.begin();

	return atoi(row[0]);
}

bool Database::CheckNameFilter(const char* name, bool surname)
{
	std::string str_name = name;

	if(surname)
	{
		if(!name || strlen(name) < 3)
		{
			return false;
		}
	}
	else
	{
		// the minimum 4 is enforced by the client
		if(!name || strlen(name) < 4 || strlen(name) > 15)
		{
			return false;
		}
	}

	for (size_t i = 0; i < str_name.size(); i++)
	{
		if(!isalpha(str_name[i]))
		{
			return false;
		}
	}

	for(size_t x = 0; x < str_name.size(); ++x)
	{
		str_name[x] = tolower(str_name[x]);
	}

	char c = '\0';
	uint8 num_c = 0;
	for(size_t x = 0; x < str_name.size(); ++x)
	{
		if(str_name[x] == c)
		{
			num_c++;
		}
		else
		{
			num_c = 1;
			c = str_name[x];
		}
		if(num_c > 2)
		{
			return false;
		}
	}

	
	std::string query("SELECT name FROM name_filter");
	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		// false through to true? shouldn't it be falls through to false?
		return true;
	}

	for (auto row = results.begin();row != results.end();++row)
	{
		std::string current_row = row[0];

		for(size_t x = 0; x < current_row.size(); ++x)
			current_row[x] = tolower(current_row[x]);

		if(str_name.find(current_row) != std::string::npos)
			return false;
	}

	return true;
}

bool Database::AddToNameFilter(const char* name) {
	
	std::string query = StringFormat("INSERT INTO name_filter (name) values ('%s')", name);
	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		return false;
	}

	if (results.RowsAffected() == 0)
		return false;

	return true;
}

uint32 Database::GetAccountIDFromLSID(uint32 iLSID, char* oAccountName, int16* oStatus) {
	uint32 account_id = 0;
	std::string query = StringFormat("SELECT id, name, status FROM account WHERE lsaccount_id=%i", iLSID);
	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return 0;
	}

	if (results.RowCount() != 1)
		return 0;

	for (auto row = results.begin(); row != results.end(); ++row) {
		account_id = atoi(row[0]);

		if (oAccountName)
			strcpy(oAccountName, row[1]);
		if (oStatus)
			*oStatus = atoi(row[2]);
	}

	return account_id;
}

void Database::GetAccountFromID(uint32 id, char* oAccountName, int16* oStatus) {
	
	std::string query = StringFormat("SELECT name, status FROM account WHERE id=%i", id);
	auto results = QueryDatabase(query);

	if (!results.Success()){
		return;
	}

	if (results.RowCount() != 1)
		return;

	auto row = results.begin();

	if (oAccountName)
		strcpy(oAccountName, row[0]);
	if (oStatus)
		*oStatus = atoi(row[1]);
}

void Database::ClearMerchantTemp(){
	QueryDatabase("DELETE FROM merchantlist_temp");
}

bool Database::UpdateName(const char* oldname, const char* newname) { 
	std::cout << "Renaming " << oldname << " to " << newname << "..." << std::endl; 
	std::string query = StringFormat("UPDATE `character_data` SET `name` = '%s' WHERE `name` = '%s';", newname, oldname);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return false;

	if (results.RowsAffected() == 0)
		return false;

	return true;
}

// If the name is used or an error occurs, it returns false, otherwise it returns true
bool Database::CheckUsedName(const char* name) {
	std::string query = StringFormat("SELECT `id` FROM `character_data` WHERE `name` = '%s'", name);
	auto results = QueryDatabase(query); 
	if (!results.Success()) {
		return false;
	}

	if (results.RowCount() > 0)
		return false;

	return true;
}

uint8 Database::GetServerType() {
	std::string query("SELECT `value` FROM `variables` WHERE `varname` = 'ServerType' LIMIT 1");
	auto results = QueryDatabase(query); 
	if (!results.Success()) {
		return 0;
	}

	if (results.RowCount() != 1)
		return 0;

	auto row = results.begin();
	return atoi(row[0]);
}

bool Database::MoveCharacterToZone(const char* charname, const char* zonename, uint32 zoneid) {
	if(zonename == nullptr || strlen(zonename) == 0)
		return false;

	std::string query = StringFormat("UPDATE `character_data` SET `zone_id` = %i, `x` = -1, `y` = -1, `z` = -1 WHERE `name` = '%s'", zoneid, charname);
	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return false;
	}

	if (results.RowsAffected() == 0)
		return false;

	return true;
}

bool Database::MoveCharacterToZone(const char* charname, const char* zonename) {
	return MoveCharacterToZone(charname, zonename, GetZoneID(zonename));
}

bool Database::MoveCharacterToZone(uint32 iCharID, const char* iZonename) { 
	std::string query = StringFormat("UPDATE `character_data` SET `zone_id` = %i, `x` = -1, `y` = -1, `z` = -1 WHERE `id` = %i", iZonename, GetZoneID(iZonename), iCharID);
	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return false;
	}

	return results.RowsAffected() != 0;
}

bool Database::SetHackerFlag(const char* accountname, const char* charactername, const char* hacked) { 
	std::string new_hacked = std::string(hacked);
	replace_all(new_hacked, "'", "_");
	std::string query = StringFormat("INSERT INTO `hackers` (account, name, hacked) values('%s','%s','%s')", accountname, charactername, new_hacked.c_str());
	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return false;
	}

	return results.RowsAffected() != 0;
}

bool Database::SetMQDetectionFlag(const char* accountname, const char* charactername, const char* hacked, const char* zone) { 
	//Utilize the "hacker" table, but also give zone information.
	std::string new_hacked = std::string(hacked);
	replace_all(new_hacked, "'", "_");
	std::string query = StringFormat("INSERT INTO hackers(account,name,hacked,zone) values('%s','%s','%s','%s')", accountname, charactername, new_hacked.c_str(), zone);
	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		return false;
	}

	return results.RowsAffected() != 0;
}

uint8 Database::GetRaceSkill(uint8 skillid, uint8 in_race)
{
	//Check for a racial cap!
	std::string query = StringFormat("SELECT skillcap from race_skillcaps where skill = %i && race = %i", skillid, in_race);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return 0;

	if (results.RowCount() == 0)
		return 0;

	auto row = results.begin();
	return atoi(row[0]);
}

uint8 Database::GetSkillCap(uint8 skillid, uint8 in_race, uint8 in_class, uint16 in_level)
{
	uint8 skill_level = 0, skill_formula = 0;
	uint16 base_cap = 0, skill_cap = 0, skill_cap2 = 0, skill_cap3 = 0;
	

	//Fetch the data from DB.
	std::string query = StringFormat("SELECT level, formula, pre50cap, post50cap, post60cap from skillcaps where skill = %i && class = %i", skillid, in_class);
	auto results = QueryDatabase(query);

	if (results.Success() && results.RowsAffected() != 0)
	{
		auto row = results.begin();
		skill_level = atoi(row[0]);
		skill_formula = atoi(row[1]);
		skill_cap = atoi(row[2]);
		if (atoi(row[3]) > skill_cap)
			skill_cap2 = (atoi(row[3])-skill_cap)/10; //Split the post-50 skill cap into difference between pre-50 cap and post-50 cap / 10 to determine amount of points per level.
		skill_cap3 = atoi(row[4]);
	}

	int race_skill = GetRaceSkill(skillid,in_race);

	if (race_skill > 0 && (race_skill > skill_cap || skill_cap == 0 || in_level < skill_level))
		return race_skill;

	if (skill_cap == 0) //Can't train this skill at all.
		return 255; //Untrainable

	if (in_level < skill_level)
		return 254; //Untrained

	//Determine pre-51 level-based cap
	if (skill_formula > 0)
		base_cap = in_level*skill_formula+skill_formula;
	if (base_cap > skill_cap || skill_formula == 0)
		base_cap = skill_cap;

	//If post 50, add post 50 cap to base cap.
	if (in_level > 50 && skill_cap2 > 0)
		base_cap += skill_cap2*(in_level-50);

	//No cap should ever go above its post50cap
	if (skill_cap3 > 0 && base_cap > skill_cap3)
		base_cap = skill_cap3;

	//Base cap is now the max value at the person's level, return it!
	return base_cap;
}

uint32 Database::GetCharacterInfo(const char* iName, uint32* oAccID, uint32* oZoneID, uint32* oInstanceID, float* oX, float* oY, float* oZ) { 
	std::string query = StringFormat("SELECT `id`, `account_id`, `zone_id`, `zone_instance`, `x`, `y`, `z` FROM `character_data` WHERE `name` = '%s'", iName);
	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return 0;
	}

	if (results.RowCount() != 1)
		return 0;

	auto row = results.begin();
	uint32 charid = atoi(row[0]);
	if (oAccID){ *oAccID = atoi(row[1]); }
	if (oZoneID){ *oZoneID = atoi(row[2]); }
	if (oInstanceID){ *oInstanceID = atoi(row[3]); }
	if (oX){ *oX = atof(row[4]); }
	if (oY){ *oY = atof(row[5]); }
	if (oZ){ *oZ = atof(row[6]); }

	return charid;
}

bool Database::UpdateLiveChar(char* charname,uint32 lsaccount_id) {

	std::string query = StringFormat("UPDATE account SET charname='%s' WHERE id=%i;",charname, lsaccount_id);
	auto results = QueryDatabase(query);
	if (!results.Success()){
		return false;
	}
	return true;
}

bool Database::CharacterJoin(uint32 char_id, char* char_name) {
	std::string join_query = StringFormat(
		"INSERT INTO `webdata_character` (	"
		"id,								" // char_id
		"name,								" // char_name
		"last_login,						" // time(nullptr)
		"last_seen							" // 0
		") VALUES (							"
		"%i,								" // id	
		"'%s',								" // name
		"%i,								" // last_login
		"0									" // last_seen
		") ON DUPLICATE KEY UPDATE			"
		"name='%s',							" // char_name
		"last_login=%i,						" // time(nullptr)
		"last_seen=0						" // 0
		,
		char_id,							  // id	
		char_name,							  // name
		time(nullptr),						  // last_login
		char_name,							  // name
		time(nullptr)						  // last_login
		);
	auto join_results = QueryDatabase(join_query);
	Log.Out(Logs::Detail, Logs::Debug, "CharacterJoin should have wrote to database for %s with ID %i at %i and last_seen should be zero.", char_name, char_id, time(nullptr));

	if (!join_results.Success()){
		return false;
	}
	return true;
}

bool Database::CharacterQuit(uint32 char_id) {
	std::string query = StringFormat("UPDATE `webdata_character` SET `last_seen`='%i' WHERE `id` = '%i'", time(nullptr), char_id);
	auto results = QueryDatabase(query);
	Log.Out(Logs::Detail, Logs::Debug, "CharacterQuit should have wrote to database for %i at %i", char_id, time(nullptr));
	if (!results.Success()){
		Log.Out(Logs::Detail, Logs::Debug, "Error updating character_data table from CharacterQuit.");
		return false;
	}
	Log.Out(Logs::Detail, Logs::Debug, "CharacterQuit should have wrote to database for %i...", char_id);
	return true;
}

bool Database::ZoneConnected(uint32 id, const char* name) {
	std::string connect_query = StringFormat(
		"INSERT INTO `webdata_servers` (	"
		"id,								" // id
		"name,								" // name
		"connected							" // 1
		") VALUES (							"
		"%i,								" // id
		"'%s',								" // name
		"1									" // connected
		") ON DUPLICATE KEY UPDATE			"
		"name='%s',							" // name
		"connected=1						" // 1
		,
		id,									// id
		name,								// name
		name								// name
		);
	auto connect_results = QueryDatabase(connect_query);
	Log.Out(Logs::Detail, Logs::Debug, "ZoneConnected should have wrote id %i to webdata_servers for %s with connected status 1.", id, name);

	if (!connect_results.Success()){
		Log.Out(Logs::Detail, Logs::Error, "Error updating zone status in webdata_servers table from ZoneConnected.");
		return false;
	}
	return true;
}

bool Database::ZoneDisconnect(uint32 id) {
	std::string query = StringFormat("UPDATE `webdata_servers` SET `connected`='0' WHERE `id` = '%i'", id);
	auto results = QueryDatabase(query);
	Log.Out(Logs::Detail, Logs::Debug, "ZoneDisconnect should have wrote '0' to webdata_servers for %i.", id);
	if (!results.Success()){
		Log.Out(Logs::Detail, Logs::Error, "Error updating webdata_servers table from ZoneConnected.");
		return false;
	}
	Log.Out(Logs::Detail, Logs::Error, "Updated webdata_servers table from ZoneDisconnected.");
	return true;
}

bool Database::LSConnected(uint32 port) {
	std::string connect_query = StringFormat(
		"INSERT INTO `webdata_servers` (	"
		"id,								" // id
		"name,								" // name
		"connected							" // 1
		") VALUES (							"
		"%i,								" // id
		"'LoginServer',						" // name
		"1									" // connected
		") ON DUPLICATE KEY UPDATE			"
		"connected=1						" // 1
		,
		port								// id
		);
	auto connect_results = QueryDatabase(connect_query);
	Log.Out(Logs::Detail, Logs::Debug, "LSConnected should have wrote id %i to webdata_servers for LoginServer with connected status 1.", port);

	if (!connect_results.Success()){
		Log.Out(Logs::Detail, Logs::Error, "Error updating LoginServer status in webdata_servers table from LSConnected.");
		return false;
	}
	return true;
}

bool Database::LSDisconnect() {
	std::string query = StringFormat("UPDATE `webdata_servers` SET `connected`='0' WHERE `name` = 'LoginServer'");
	auto results = QueryDatabase(query);
	Log.Out(Logs::Detail, Logs::Debug, "LSConnected should have wrote to webdata_servers for LoginServer connected status 0.");
	if (!results.Success()){
		Log.Out(Logs::Detail, Logs::Error, "Error updating webdata_servers table from LSDisconnect.");
		return false;
	}
	Log.Out(Logs::Detail, Logs::Error, "Updated webdata_servers table from LSDisconnect.");
	return true;
}

bool Database::GetLiveChar(uint32 account_id, char* cname) {

	std::string query = StringFormat("SELECT charname FROM account WHERE id=%i", account_id);
	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		return false;
	}

	if (results.RowCount() != 1)
		return false;

	auto row = results.begin();
	strcpy(cname,row[0]);

	return true;
}

void Database::SetFirstLogon(uint32 CharID, uint8 firstlogon) { 
	std::string query = StringFormat( "UPDATE `character_data` SET `firstlogon` = %i WHERE `id` = %i",firstlogon, CharID);
	QueryDatabase(query); 
}

void Database::AddReport(std::string who, std::string against, std::string lines) { 
	char *escape_str = new char[lines.size()*2+1];
	DoEscapeString(escape_str, lines.c_str(), lines.size());

	std::string query = StringFormat("INSERT INTO reports (name, reported, reported_text) VALUES('%s', '%s', '%s')", who.c_str(), against.c_str(), escape_str);
	QueryDatabase(query);
	safe_delete_array(escape_str);
}

void Database::SetGroupID(const char* name, uint32 id, uint32 charid){
	
	std::string query;
	if (id == 0) {
		// removing from group
		query = StringFormat("delete from group_id where charid=%i and name='%s'",charid, name);
		auto results = QueryDatabase(query);

		if (!results.Success())
			Log.Out(Logs::General, Logs::Error, "Error deleting character from group id: %s", results.ErrorMessage().c_str());

		return;
	}

	/* Add to the Group */
	query = StringFormat("REPLACE INTO  `group_id` SET `charid` = %i, `groupid` = %i, `name` = '%s'", charid, id, name);
	QueryDatabase(query);
}

void Database::ClearAllGroups(void)
{
	std::string query("DELETE FROM `group_id`");
	QueryDatabase(query);
	return;
}

void Database::ClearGroup(uint32 gid) {
	ClearGroupLeader(gid);
	
	if(gid == 0)
	{
		//clear all groups
		ClearAllGroups();
		return;
	}

	//clear a specific group
	std::string query = StringFormat("delete from group_id where groupid = %lu", (unsigned long)gid);
	QueryDatabase(query);
}

uint32 Database::GetGroupID(const char* name){ 
	std::string query = StringFormat("SELECT groupid from group_id where name='%s'", name);
	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return 0;
	}

	if (results.RowCount() == 0)
	{
		// Commenting this out until logging levels can prevent this from going to console
		//Log.Out(Logs::General, Logs::None,, "Character not in a group: %s", name);
		return 0;
	}

	auto row = results.begin();

	return atoi(row[0]);
}

/* Is this really getting used properly... A half implementation ? Akkadius */
char* Database::GetGroupLeaderForLogin(const char* name, char* leaderbuf) {
	strcpy(leaderbuf, "");
	uint32 group_id = 0;

	std::string query = StringFormat("SELECT `groupid` FROM `group_id` WHERE `name` = '%s'", name);
	auto results = QueryDatabase(query);

	for (auto row = results.begin(); row != results.end(); ++row)
		if (row[0])
			group_id = atoi(row[0]);

	if (group_id == 0)
		return leaderbuf;

	query = StringFormat("SELECT `leadername` FROM `group_leaders` WHERE `gid` = '%u' LIMIT 1", group_id);
	results = QueryDatabase(query);

	for (auto row = results.begin(); row != results.end(); ++row)
		if (row[0])
			strcpy(leaderbuf, row[0]);

	return leaderbuf;
}

void Database::SetGroupLeaderName(uint32 gid, const char* name) { 
	std::string query = StringFormat("REPLACE INTO `group_leaders` SET `gid` = %lu, `leadername` = '%s', `leadershipaa` = ''",(unsigned long)gid,name);
	auto results = QueryDatabase(query);

	if (!results.Success())
		Log.Out(Logs::General, Logs::None, "Unable to set group leader:", results.ErrorMessage().c_str());
}

char *Database::GetGroupLeadershipInfo(uint32 gid, char* leaderbuf){ 
	std::string query = StringFormat("SELECT `leadername` FROM `group_leaders` WHERE `gid` = %lu",(unsigned long)gid);
	auto results = QueryDatabase(query);

	if (!results.Success() || results.RowCount() == 0) {
		if(leaderbuf)
			strcpy(leaderbuf, "UNKNOWN");

		return leaderbuf;
	}

	auto row = results.begin();

	if(leaderbuf)
		strcpy(leaderbuf, row[0]);

	return leaderbuf;
}

// Clearing all group leaders
void Database::ClearAllGroupLeaders(void) {
	std::string query("DELETE from group_leaders");
	auto results = QueryDatabase(query);

	if (!results.Success())
		std::cout << "Unable to clear group leaders: " << results.ErrorMessage() << std::endl;

	return;
}

void Database::ClearGroupLeader(uint32 gid) {
	
	if(gid == 0)
	{
		ClearAllGroupLeaders();
		return;
	}

	std::string query = StringFormat("DELETE from group_leaders where gid = %lu", (unsigned long)gid);
	auto results = QueryDatabase(query);

	if (!results.Success())
		std::cout << "Unable to clear group leader: " << results.ErrorMessage() << std::endl;
}

uint16 Database::GetExpansion(uint32 acctid)
{

	std::string query = StringFormat("SELECT expansion FROM account WHERE id=%i",acctid);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return 0;

	if (results.RowCount() != 1)
		return 0;

	auto row = results.begin();

	return atoi(row[0]);
}

uint8 Database::GetAgreementFlag(uint32 acctid)
{

	std::string query = StringFormat("SELECT rulesflag FROM account WHERE id=%i",acctid);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return 0;

	if (results.RowCount() != 1)
		return 0;

	auto row = results.begin();

	return atoi(row[0]);
}

void Database::SetAgreementFlag(uint32 acctid) {
	std::string query = StringFormat("UPDATE account SET rulesflag=1 where id=%i", acctid);
	QueryDatabase(query);
}

void Database::ClearRaid(uint32 rid) {
	if(rid == 0)
	{
		//clear all raids
		ClearAllRaids();
		return;
	}

	//clear a specific group
	std::string query = StringFormat("delete from raid_members where raidid = %lu", (unsigned long)rid);
	auto results = QueryDatabase(query);

	if (!results.Success())
		std::cout << "Unable to clear raids: " << results.ErrorMessage() << std::endl;
}

void Database::ClearAllRaids(void) {

	std::string query("delete from raid_members");
	auto results = QueryDatabase(query);

	if (!results.Success())
		std::cout << "Unable to clear raids: " << results.ErrorMessage() << std::endl;
}

void Database::ClearAllRaidDetails(void)
{

	std::string query("delete from raid_details");
	auto results = QueryDatabase(query);

	if (!results.Success())
		std::cout << "Unable to clear raid details: " << results.ErrorMessage() << std::endl;
}

void Database::ClearRaidDetails(uint32 rid) {
	
	if(rid == 0)
	{
		//clear all raids
		ClearAllRaidDetails();
		return;
	}

	//clear a specific group
	std::string query = StringFormat("delete from raid_details where raidid = %lu", (unsigned long)rid);
	auto results = QueryDatabase(query);

	if (!results.Success())
		std::cout << "Unable to clear raid details: " << results.ErrorMessage() << std::endl;
}

// returns 0 on error or no raid for that character, or
// the raid id that the character is a member of.
uint32 Database::GetRaidID(const char* name)
{ 
	std::string query = StringFormat("SELECT `raidid` FROM `raid_members` WHERE `name` = '%s'", name);
	auto results = QueryDatabase(query);

	if (!results.Success()) {
		return 0;
	}

	auto row = results.begin(); 
	if (row == results.end()) {
		return 0;
	}

	if (row[0]) // would it ever be possible to have a null here?
		return atoi(row[0]);

	return 0;
}

const char* Database::GetRaidLeaderName(uint32 raid_id)
{
	// Would be a good idea to fix this to be a passed in variable and
	// make the caller responsible. static local variables like this are
	// not guaranteed to be thread safe (nor is the internal guard
	// variable). C++0x standard states this should be thread safe
	// but may not be fully supported in some compilers.
	static char name[128];
	
	std::string query = StringFormat("SELECT `name` FROM `raid_members` WHERE `raidid` = %u AND `israidleader` = 1", raid_id);
	auto results = QueryDatabase(query);

	if (!results.Success()) {
		Log.Out(Logs::General, Logs::Debug, "Unable to get Raid Leader Name for Raid ID: %u", raid_id);
		return "UNKNOWN";
	}

	auto row = results.begin();

	if (row == results.end()) {
		return "UNKNOWN";
	}

	memset(name, 0, sizeof(name));
	strcpy(name, row[0]);

	return name;
}

bool Database::VerifyInstanceAlive(uint16 instance_id, uint32 char_id)
{
	//we are not saved to this instance so set our instance to 0
	if(!GlobalInstance(instance_id) && !CharacterInInstanceGroup(instance_id, char_id))
		return false;

	if(CheckInstanceExpired(instance_id))
	{
		DeleteInstance(instance_id);
		return false;
	}

	return true;
}

bool Database::VerifyZoneInstance(uint32 zone_id, uint16 instance_id)
{

	std::string query = StringFormat("SELECT id FROM instance_list where id=%u AND zone=%u",instance_id, zone_id);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return false;

	if (results.RowCount() == 0)
		return false;

	return true;
}

bool Database::CharacterInInstanceGroup(uint16 instance_id, uint32 char_id)
{

	std::string query = StringFormat("SELECT charid FROM instance_list_player where id=%u AND charid=%u",instance_id, char_id);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return false;

	if (results.RowCount() != 1)
		return false;

	return true;
}

void Database::DeleteInstance(uint16 instance_id)
{

	std::string query = StringFormat("DELETE FROM instance_list WHERE id=%u", instance_id);
	QueryDatabase(query);

	query = StringFormat("DELETE FROM instance_list_player WHERE id=%u", instance_id);
	QueryDatabase(query);

	query = StringFormat("DELETE FROM respawn_times WHERE instance_id=%u", instance_id);
	QueryDatabase(query);

	query = StringFormat("DELETE FROM spawn_condition_values WHERE instance_id=%u", instance_id);
	QueryDatabase(query);

	BuryCorpsesInInstance(instance_id);
}

bool Database::CheckInstanceExpired(uint16 instance_id)
{
	
	int32 start_time = 0;
	int32 duration = 0;
	uint32 never_expires = 0;

	std::string query = StringFormat("SELECT start_time, duration, never_expires FROM instance_list WHERE id=%u", instance_id);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return true;

	if (results.RowCount() == 0)
		return true;

	auto row = results.begin();

	start_time = atoi(row[0]);
	duration = atoi(row[1]);
	never_expires = atoi(row[2]);

	if(never_expires == 1)
		return false;

	timeval tv;
	gettimeofday(&tv, nullptr);

	if((start_time + duration) <= tv.tv_sec)
		return true;

	return false;
}

uint32 Database::ZoneIDFromInstanceID(uint16 instance_id)
{

	std::string query = StringFormat("SELECT zone FROM instance_list where id=%u", instance_id);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return 0;

	if (results.RowCount() == 0)
		return 0;

	auto row = results.begin();

	return atoi(row[0]);
}

uint32 Database::VersionFromInstanceID(uint16 instance_id)
{

	std::string query = StringFormat("SELECT version FROM instance_list where id=%u", instance_id);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return 0;

	if (results.RowCount() == 0)
		return 0;

	auto row = results.begin();

	return atoi(row[0]);
}

uint32 Database::GetTimeRemainingInstance(uint16 instance_id, bool &is_perma)
{
	uint32 start_time = 0;
	uint32 duration = 0;
	uint32 never_expires = 0;

	std::string query = StringFormat("SELECT start_time, duration, never_expires FROM instance_list WHERE id=%u", instance_id);
	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		is_perma = false;
		return 0;
	}

	if (results.RowCount() == 0)
	{
		is_perma = false;
		return 0;
	}

	auto row = results.begin();

	start_time = atoi(row[0]);
	duration = atoi(row[1]);
	never_expires = atoi(row[2]);

	if(never_expires == 1)
	{
		is_perma = true;
		return 0;
	}

	is_perma = false;

	timeval tv;
	gettimeofday(&tv, nullptr);
	return ((start_time + duration) - tv.tv_sec);
}

bool Database::GetUnusedInstanceID(uint16 &instance_id)
{
	uint32 count = RuleI(Zone, ReservedInstances);
	uint32 max = 65535;

	std::string query = StringFormat("SELECT IFNULL(MAX(id),%u)+1 FROM instance_list  WHERE id > %u", count, count);
	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		instance_id = 0;
		return false;
	}

	if (results.RowCount() == 0)
	{
		instance_id = 0;
		return false;
	}

	auto row = results.begin();

	if (atoi(row[0]) <= max)
	{
		instance_id = atoi(row[0]);
		return true;
	}

	query = StringFormat("SELECT id FROM instance_list where id > %u ORDER BY id", count);
	results = QueryDatabase(query);

	if (!results.Success())
	{
		instance_id = 0;
		return false;
	}

	if (results.RowCount() == 0)
	{
		instance_id = 0;
		return false;
	}

	count++;
	for (auto row = results.begin();row != results.end();++row)
	{
		if(count < atoi(row[0]))
		{
			instance_id = count;
			return true;
		}

		if(count > max)
		{
			instance_id = 0;
			return false;
		}

		count++;
	}

	instance_id = count;
	return true;
}

//perhaps purge any expireds too
bool Database::CreateInstance(uint16 instance_id, uint32 zone_id, uint32 version, uint32 duration)
{

	std::string query = StringFormat("INSERT INTO instance_list (id, zone, version, start_time, duration)"
		" values(%lu, %lu, %lu, UNIX_TIMESTAMP(), %lu)", 
		(unsigned long)instance_id, (unsigned long)zone_id, (unsigned long)version, (unsigned long)duration);
	auto results = QueryDatabase(query);

	return results.Success();
}

void Database::PurgeExpiredInstances()
{

	std::string query("SELECT id FROM instance_list where (start_time+duration) <= UNIX_TIMESTAMP() and never_expires = 0");
	auto results = QueryDatabase(query);

	if (!results.Success())
		return;

	if (results.RowCount() == 0)
		return;

	for (auto row = results.begin();row != results.end();++row)
		DeleteInstance(atoi(row[0]));
}

bool Database::AddClientToInstance(uint16 instance_id, uint32 char_id)
{
	std::string query = StringFormat("INSERT INTO instance_list_player(id, charid) values(%lu, %lu)", 
		(unsigned long)instance_id, (unsigned long)char_id);
	auto results = QueryDatabase(query);

	return results.Success();
}

bool Database::RemoveClientFromInstance(uint16 instance_id, uint32 char_id)
{
	
	std::string query = StringFormat("DELETE FROM instance_list_player WHERE id=%lu AND charid=%lu",
		(unsigned long)instance_id, (unsigned long)char_id);
	auto results = QueryDatabase(query);

	return results.Success();
}

bool Database::RemoveClientsFromInstance(uint16 instance_id)
{
	std::string query = StringFormat("DELETE FROM instance_list_player WHERE id=%lu", (unsigned long)instance_id);
	auto results = QueryDatabase(query);

	return results.Success();
}

bool Database::CheckInstanceExists(uint16 instance_id) {
	std::string query = StringFormat("SELECT * FROM instance_list where id=%u", instance_id);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return false;

	if (results.RowCount() == 0)
		return false;

	return true;
}

void Database::BuryCorpsesInInstance(uint16 instance_id) {
	std::string query = StringFormat("UPDATE `character_corpses` SET `is_buried` = 1, `instance_id`  =0 WHERE `instance_id` = %u", instance_id);
	auto results = QueryDatabase(query);
}

uint16 Database::GetInstanceVersion(uint16 instance_id) {
	if(instance_id == 0)
		return 0;

	std::string query = StringFormat("SELECT version FROM instance_list where id=%u", instance_id);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return 0;

	if (results.RowCount() == 0)
		return 0;

	auto row = results.begin();
	return atoi(row[0]);
}

uint16 Database::GetInstanceID(const char* zone, uint32 character_id, int16 version) {

	std::string query = StringFormat(
		"SELECT "
		"instance_list.id "
		"FROM "
		"instance_list, "
		"instance_list_player "
		"WHERE "
		"instance_list.zone = %u "
		"AND instance_list.version = %u "
		"AND instance_list.id = instance_list_player.id "
		"AND instance_list_player.charid = %u "
		"LIMIT 1 ", 
		GetZoneID(zone), 
		version, 
		character_id
	);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return 0;

	if (results.RowCount() == 0)
		return 0;

	auto row = results.begin();
	return atoi(row[0]);
}

uint16 Database::GetInstanceID(uint32 zone, uint32 character_id, int16 version)
{
	if(!zone)
		return 0;

	std::string query = StringFormat(
		"SELECT "
		"instance_list.id "
		"FROM "
		"instance_list, "
		"instance_list_player "
		"WHERE "
		"instance_list.zone = %u "
		"AND instance_list.version = %u "
		"AND instance_list.id = instance_list_player.id "
		"AND instance_list_player.charid = %u "
		"LIMIT 1; ", 
		zone, 
		version, 
		character_id
	);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return 0;

	if (results.RowCount() == 0)
		return 0;

	auto row = results.begin();

	return atoi(row[0]);
}

void Database::GetCharactersInInstance(uint16 instance_id, std::list<uint32> &charid_list) {

	std::string query = StringFormat("SELECT `charid` FROM `instance_list_playe`r WHERE `id` = %u", instance_id);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return;

	for (auto row = results.begin(); row != results.end(); ++row)
		charid_list.push_back(atoi(row[0]));
}

void Database::AssignGroupToInstance(uint32 group_id, uint32 instance_id)
{
	
	uint32 zone_id = ZoneIDFromInstanceID(instance_id);
	uint16 version = VersionFromInstanceID(instance_id);

	std::string query = StringFormat("SELECT `charid` FROM `group_id` WHERE `groupid` = %u", group_id);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return;

	for (auto row = results.begin(); row != results.end(); ++row)
	{
		uint32 charid = atoi(row[0]);
		if (GetInstanceID(zone_id, charid, version) == 0)
			AddClientToInstance(instance_id, charid);
	}
}

void Database::AssignRaidToInstance(uint32 raid_id, uint32 instance_id)
{
	
	uint32 zone_id = ZoneIDFromInstanceID(instance_id);
	uint16 version = VersionFromInstanceID(instance_id);

	std::string query = StringFormat("SELECT `charid` FROM `raid_members` WHERE `raidid` = %u", raid_id);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return;

	for (auto row = results.begin(); row != results.end(); ++row)
	{
		uint32 charid = atoi(row[0]);
		if (GetInstanceID(zone_id, charid, version) == 0)
			AddClientToInstance(instance_id, charid);
	}
}

void Database::FlagInstanceByGroupLeader(uint32 zone, int16 version, uint32 charid, uint32 gid)
{
	uint16 id = GetInstanceID(zone, charid, version);
	if(id != 0)
		return;

	char ln[128];
	memset(ln, 0, 128);
	strcpy(ln, GetGroupLeadershipInfo(gid, ln));
	uint32 l_charid = GetCharacterID((const char*)ln);
	uint16 l_id = GetInstanceID(zone, l_charid, version);

	if(l_id == 0)
		return;

	AddClientToInstance(l_id, charid);
}

void Database::FlagInstanceByRaidLeader(uint32 zone, int16 version, uint32 charid, uint32 rid)
{
	uint16 id = GetInstanceID(zone, charid, version);
	if(id != 0)
		return;

	uint32 l_charid = GetCharacterID(GetRaidLeaderName(rid));
	uint16 l_id = GetInstanceID(zone, l_charid, version);

	if(l_id == 0)
		return;

	AddClientToInstance(l_id, charid);
}

void Database::SetInstanceDuration(uint16 instance_id, uint32 new_duration)
{
	std::string query = StringFormat("UPDATE `instance_list` SET start_time=UNIX_TIMESTAMP(), "
		"duration=%u WHERE id=%u", new_duration, instance_id);
	auto results = QueryDatabase(query);
}

bool Database::GlobalInstance(uint16 instance_id)
{
	std::string query = StringFormat(
		"SELECT "
		"is_global "
		"FROM "
		"instance_list "
		"WHERE "
		"id = %u "
		"LIMIT 1 ", 
		instance_id
	);
	auto results = QueryDatabase(query);

	if (!results.Success())
		return false;

	if (results.RowCount() == 0)
		return false;

	auto row = results.begin();

	return (atoi(row[0]) == 1) ? true : false;
}

struct TimeOfDay_Struct Database::LoadTime(time_t &realtime)
{

	TimeOfDay_Struct eqTime;
	std::string query = StringFormat("SELECT minute,hour,day,month,year,realtime FROM eqtime limit 1");
	auto results = QueryDatabase(query);

	if (!results.Success() || results.RowCount() == 0)
	{
		Log.Out(Logs::Detail, Logs::World_Server, "Loading EQ time of day failed. Using defaults.");
		eqTime.minute = 0;
		eqTime.hour = 9;
		eqTime.day = 1;
		eqTime.month = 1;
		eqTime.year = 3100;
		realtime = time(0);
	}

	auto row = results.begin();

	uint8 hour = atoi(row[1]);
	time_t realtime_ = atoi(row[5]);
	if(RuleI(World, BootHour) > 0 && RuleI(World, BootHour) <= 24)
	{
		hour = RuleI(World, BootHour);
		realtime_ = time(0);
		Log.Out(Logs::Detail, Logs::World_Server, "EQTime: Setting hour to: %d", hour);
	}

	eqTime.minute = atoi(row[0]);
	eqTime.hour = hour;
	eqTime.day = atoi(row[2]);
	eqTime.month = atoi(row[3]);
	eqTime.year = atoi(row[4]);
	realtime = realtime_;

	return eqTime;
}

bool Database::SaveTime(int8 minute, int8 hour, int8 day, int8 month, int16 year)
{
	std::string query = StringFormat("UPDATE eqtime set minute = %d, hour = %d, day = %d, month = %d, year = %d, realtime = %d limit 1", minute, hour, day, month, year, time(0));
	auto results = QueryDatabase(query);

	return results.Success();

}

void Database::LoadLogSettings(EQEmuLogSys::LogSettings* log_settings){
	std::string query = 
		"SELECT "
		"log_category_id, "
		"log_category_description, "
		"log_to_console, "
		"log_to_file, "
		"log_to_gmsay "
		"FROM "
		"logsys_categories "
		"ORDER BY log_category_id";
	auto results = QueryDatabase(query);

	int log_category = 0;
	Log.file_logs_enabled = false;

	for (auto row = results.begin(); row != results.end(); ++row) {
		log_category = atoi(row[0]);
		log_settings[log_category].log_to_console = atoi(row[2]);
		log_settings[log_category].log_to_file = atoi(row[3]);
		log_settings[log_category].log_to_gmsay = atoi(row[4]);

		/* Determine if any output method is enabled for the category 
			and set it to 1 so it can used to check if category is enabled */
		const bool log_to_console = log_settings[log_category].log_to_console > 0;
		const bool log_to_file = log_settings[log_category].log_to_file > 0;
		const bool log_to_gmsay = log_settings[log_category].log_to_gmsay > 0;
		const bool is_category_enabled = log_to_console || log_to_file || log_to_gmsay;

		if (is_category_enabled)
			log_settings[log_category].is_category_enabled = 1;

		/* 
			This determines whether or not the process needs to actually file log anything.
			If we go through this whole loop and nothing is set to any debug level, there is no point to create a file or keep anything open
		*/
		if (log_settings[log_category].log_to_file > 0){
			Log.file_logs_enabled = true;
		}
	}
}

void Database::ClearAllActive() {
	std::string query = "UPDATE `account` set active = 0";
	auto results = QueryDatabase(query);

	return;
}

bool Database::CheckAccountActive(uint32 account_id) {
	std::string query = StringFormat("SELECT `active` FROM `account` WHERE `id` = %i", account_id);

	auto results = QueryDatabase(query); 
	if (!results.Success()) {
		return false;
	}

	if (results.RowCount() != 1)
		return false;
	
	auto row = results.begin(); 
	uint8 active = atoi(row[0]); 

	if(active == 0)
	{
		//std::string query = StringFormat("UPDATE `account` SET active = 1 WHERE id = %i", account_id);
		//QueryDatabase(query);
		return false;
	}
	else
	{
		return true;
	}
}

void Database::ClearAccountActive(uint32 account_id) {
	std::string query = StringFormat("UPDATE `account` set active = 0 WHERE id = %i", account_id);
	auto results = QueryDatabase(query);
	Log.Out(Logs::Detail, Logs::World_Server,"Setting account %d inactive", account_id);
	return;
}

void Database::SetAccountActive(uint32 account_id)
{
	std::string query = StringFormat("UPDATE `account` SET active = 1 WHERE id = %i", account_id);
    QueryDatabase(query);

	return;
}

bool Database::AdjustSpawnTimes() 
{

	std::string dquery = StringFormat("DELETE rt FROM respawn_times AS rt inner join spawn2 AS s ON rt.id = s.id WHERE s.clear_timer_onboot = 1");
	auto dresults = QueryDatabase(dquery);

	if (!dresults.Success()) 
	{
		return false;
	}
	Log.Out(Logs::General, Logs::World_Server, "World has reset %d spawn timers.", dresults.RowsAffected());

	std::string query = StringFormat("SELECT id, boot_respawntime, variance FROM spawn2 WHERE boot_respawntime > 0");
	auto results = QueryDatabase(query); 

	if (!results.Success()) 
	{
		return false;
	}

	if(results.RowCount() == 0) 
	{
		return true;
	}

	for (auto row = results.begin(); row != results.end(); ++row) 
	{
		uint32 rspawn = atoi(row[1]);
		uint32 variance = atoi(row[2]);

		if (variance != 0) {
			int var = variance / 2;
			rspawn = emudb_random.Int(rspawn - var, rspawn + var);
		}

		std::string iquery = StringFormat("REPLACE INTO respawn_times SET id = %d, start = %d, duration = %d", atoi(row[0]), time(0), rspawn);
		auto iresults = QueryDatabase(iquery);

		if (!iresults.Success()) 
		{
			return false;
		}
		Log.Out(Logs::General, Logs::World_Server, "Boot time respawn timer adjusted for id: %d duration is: %d (base: %d var: %d)", atoi(row[0]), rspawn, atoi(row[1]), atoi(row[2]));
	}
	return true;
}
