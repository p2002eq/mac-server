/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2010 EQEMu Development Team (http://eqemulator.net)

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
#include "../common/eq_packet_structs.h"
#include "../common/extprofile.h"
#include "../common/string_util.h"
#include "../common/random.h"
#include "database.h"
#include "error_log.h"
#include "login_server.h"

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
#include "../common/unix.h"
#include <netinet/in.h>
#include <sys/time.h>
#endif


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

extern ErrorLog *server_log;
extern LoginServer server;

Database::Database() {
	DBInitVars();
}

/*
* Establish a connection to a mysql database with the supplied parameters
*/
Database::Database(const char* host, const char* user, const char* passwd, const char* database, uint32 port)
{
	DBInitVars();
	Connect(host, user, passwd, database, port);
}

bool Database::Connect(const char* host, const char* user, const char* passwd, const char* database, uint32 port)
{
	server_log->Log(log_debug, "Connect got: host=%s user=%s database=%s port=%u", host, user, database, port);
	uint32 errnum = 0;
	char errbuf[MYSQL_ERRMSG_SIZE];
	if (!Open(host, user, passwd, database, port, &errnum, errbuf))
	{
		server_log->Log(log_debug, "Failed to connect to database: Error: %s", errbuf);
		return false;
	}
	else
	{
		server_log->Log(log_debug, "Using database '%s' at %s:%u", database, host, port);
		return true;
	}
}

void Database::DBInitVars() {
	varcache_array = 0;
	varcache_max = 0;
	varcache_lastupdate = 0;
}

/*
* Close the connection to the database
*/
Database::~Database()
{
	unsigned int x;
	if (varcache_array) {
		for (x = 0; x<varcache_max; x++) {
			safe_delete_array(varcache_array[x]);
		}
		safe_delete_array(varcache_array);
	}
}

#pragma region Load Server Setup
std::string Database::LoadServerSettings(std::string category, std::string type, std::string sender, bool report)
{
	std::string query;
	bool disectmysql = false; // for debugging what gets returned.

	query = StringFormat(
		"SELECT * FROM tblloginserversettings "
		"WHERE "
			"type = '%s' "
		"AND "
			"category = '%s'",
		type.c_str(),
		category.c_str()
		);

	auto results = QueryDatabase(query);

	if (!results.Success() || results.RowCount() != 1)
	{
		server_log->Log(log_debug, "LoadServerSettings Mysql query return no results: %s", query.c_str());
		return "";
	}
	auto row = results.begin();
	std::string value = row[1];
	if (disectmysql && report)
	{
		server_log->Log(log_debug, "****************************************************");
		server_log->Log(log_debug, "** Mysql query [[ %s ]]", query.c_str());
		server_log->Log(log_debug, "** for LoadServerSettings got:");
		server_log->Log(log_debug, "** [[ '%s' ]] for [[ '%s' ]]", value.c_str(), sender.c_str());
		server_log->Log(log_debug, "****************************************************");
	}
	return value.c_str();
}
#pragma endregion

#pragma region Create Server Setup
bool Database::CheckSettings(int type)
{
	std::string query;
	server_log->Log(log_debug, "Checking for database structure with type %s", std::to_string(type).c_str());
	if (type == 1)
	{
		query = StringFormat("SHOW TABLES LIKE 'tblloginserversettings'");

		auto results = QueryDatabase(query);

		if (!results.Success())
		{
			server_log->Log(log_error, "CheckSettings Mysql query return no results: %s", query.c_str());
			return false;
		}
		server_log->Log(log_debug, "tblloginserversettings exists sending continue.");
		return true;
	}
	if (type == 2)
	{
		query = StringFormat("SELECT * FROM tblloginserversettings");

		auto results = QueryDatabase(query);

		if (!results.Success())
		{
			server_log->Log(log_error, "CheckSettings Mysql query return no results: %s", query.c_str());
			return false;
		}
		server_log->Log(log_debug, "tblloginserversettings entries exist sending continue.");
		return true;
	}
	else
	{
		server_log->Log(log_debug, "tblloginserversettings does not exist.");
		return false;
	}
}

bool Database::CreateServerSettings()
{
	std::string query;
	server_log->Log(log_debug, "Entering Server Settings database setup.");

	query = StringFormat("SHOW TABLES LIKE 'tblloginserversettings'");

	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		server_log->Log(log_error, "CreateServerSettings Mysql query returned, table does not exist: %s", query.c_str());
	}
	else
	{
		return true;
	}

	auto row = results.begin();
	if (row == nullptr)
	{
		server_log->Log(log_debug, "Server Settings table does not exist, creating.");

		query = StringFormat(
				"CREATE TABLE `tblloginserversettings` ("
				"`type` varchar(50) NOT NULL,"
				"`value` varchar(50) NOT NULL,"
				"`category` varchar(20) NOT NULL,"
				"`description` varchar(99) NOT NULL,"
				"`defaults` varchar(50) NOT NULL"
				") ENGINE=InnoDB DEFAULT CHARSET=latin1");

		auto results = QueryDatabase(query);

		if (!results.Success())
		{
			server_log->Log(log_error, "CreateServerSettings Mysql query failed to create table: %s", query.c_str());
			return false;
		}

		query = StringFormat(
			"INSERT INTO `tblloginserversettings` (type, value, category, description, defaults)"
			"VALUES"
			"('access_log_table', '', 'schema', 'location for access logs, failed logins and goodIP.', 'tblaccountaccesslog'),"
			"('account_table', '', 'schema', 'location of all client account info for login server only.', 'tblLoginServerAccounts'),"
			"('auto_account_activate', '', 'options', 'set this to TRUE to allow new accounts to log in.', 'TRUE'),"
			"('auto_account_create', '', 'options', 'set this to TRUE to auto create accounts on player first log in.', 'TRUE'),"
			"('port', '', 'Old', 'port for clients to connect to.', '6000'),"
			"('dump_packets_in', '', 'options', 'debugging', 'FALSE'),"
			"('dump_packets_out', '', 'options', 'debugging', 'FALSE'),"
			"('failed_login_log', '', 'options', 'set this to TRUE to log failed log in attempts.', 'TRUE'),"
			"('good_loginIP_log', '', 'options', 'set this to TRUE to log successful account log ins.', 'TRUE'),"
			"('listen_port', '', 'options', 'this is the port we listen on for world connection.', '5998'),"
			"('local_network', '', 'options', 'set to the network ip that world server is on.', '127.0.0.1'),"
			"('mode', '', 'security', 'encryption mode the plugin uses.', '5'),"
			"('network_ip', '', 'options', 'set to the network ip that world server is on.', '127.0.0.1'),"
			"('opcodes', '', 'Old', 'opcode file for client compatibility. (Old means classic/mac)', 'login_opcodes_oldver.conf'),"
			"('plugin', '', 'security', 'the encryption type the login server uses.', 'EQEmuAuthCrypto'),"
			"('pop_count', '', 'options', '0 to only display UP or DOWN or 1 to show population count in server select.', '0'),"
			"('reject_duplicate_servers', '', 'options', 'set this to TRUE to force unique server name connections.', 'TRUE'),"
			"('salt', '', 'options', 'for account security make this a numeric random number.', '12345678'),"
			"('ticker', '', 'options', 'Sets the welcome message in server select.', 'Welcome'),"
			"('trace', '', 'options', 'debugging', 'FALSE'),"
			"('unregistered_allowed', '', 'options', 'set this to TRUE to allow any server to connect.', 'TRUE'),"
			"('world_admin_registration_table', '', 'schema', 'location of administrator account info for this login server.', 'tblServerAdminRegistration'),"
			"('world_registration_table', '', 'schema', 'location of registered or connection records of servers connecting to this loginserver.', 'tblWorldServerRegistration'),"
			"('world_server_type_table', '', 'schema', 'location of server type descriptions.', 'tblServerListType'),"
			"('world_trace', '', 'options', 'debugging', 'FALSE');");

		if (!results.Success())
		{
			server_log->Log(log_error, "CreateServerSettings Mysql query failed: %s", query.c_str());
			return false;
		}
			
			
		// type, category, default
		bool failed = false;
		failed |= !SetServerSettings("access_log_table", "schema", "tblaccountaccesslog");
		failed |= !SetServerSettings("account_table", "schema", "tblLoginServerAccounts");
		failed |= !SetServerSettings("auto_account_activate", "options", "TRUE");
		failed |= !SetServerSettings("auto_account_create", "options", "TRUE");
		failed |= !SetServerSettings("port", "Old", "6000");
		failed |= !SetServerSettings("dump_packets_in", "options", "FALSE");
		failed |= !SetServerSettings("dump_packets_out", "options", "FALSE");
		failed |= !SetServerSettings("failed_login_log", "options", "TRUE");
		failed |= !SetServerSettings("good_loginIP_log", "options", "TRUE");
		failed |= !SetServerSettings("listen_port", "options", "5998");
		failed |= !SetServerSettings("local_network", "options", "127.0.0.1");
		failed |= !SetServerSettings("mode", "security", "5");
		failed |= !SetServerSettings("network_ip", "options", "127.0.0.1");
		failed |= !SetServerSettings("opcodes", "Old", "login_opcodes_oldver.conf");
		failed |= !SetServerSettings("plugin", "security", "EQEmuAuthCrypto");
		failed |= !SetServerSettings("pop_count", "options", "0");
		failed |= !SetServerSettings("reject_duplicate_servers", "options", "TRUE");
		failed |= !SetServerSettings("salt", "options", "12345678");
		failed |= !SetServerSettings("ticker", "options", "Welcome");
		failed |= !SetServerSettings("trace", "options", "FALSE");
		failed |= !SetServerSettings("unregistered_allowed", "options", "TRUE");
		failed |= !SetServerSettings("world_admin_registration_table", "schema", "tblServerAdminRegistration");
		failed |= !SetServerSettings("world_registration_table", "schema", "tblWorldServerRegistration");
		failed |= !SetServerSettings("world_server_type_table", "schema", "tblServerListType");
		failed |= !SetServerSettings("world_trace", "options", "FALSE");

		if (failed)
		{
			return false;
		}
		server_log->Log(log_database, "Server Settings table created, continuing.");
	}
	else
	{
		server_log->Log(log_database, "Server Settings tables exists, continuing.");
	}
	if (GetServerSettings())
	{
		return true;
	}
	return false;
}

bool Database::GetServerSettings()
{
	std::string query;
	query = StringFormat("SELECT * FROM tblloginserversettings");

	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		server_log->Log(log_error, "GetServerSettings Mysql check_query failed: %s", query.c_str());
		return false;
	}

	bool result = true;
	auto row = results.begin();

	while (row != results.end())
	{
		std::string type = row[0];
		std::string value = row[1];
		std::string category = row[2];
		std::string description = row[3];
		std::string defaults = row[4];

		if (value.empty())
		{
			server_log->Log(log_database, "Mysql check_query returns NO value for type %s", type.c_str());
			result = false;
		}
		else if (!value.empty())
		{
			server_log->Log(log_database, "Mysql check_query returns `value`: %s for type %s", value.c_str(), type.c_str());
		}
	}
	return result;
}

bool Database::SetServerSettings(std::string type, std::string category, std::string defaults)
{
	std::string query;

	Config* newval = new Config;
	std::string loadINIvalue = newval->LoadOption(category, type, "login.ini");
	query = StringFormat(
		"UPDATE tblloginserversettings "
		"SET value = '%s' "
		"WHERE type = '%s' "
		"AND category = '%s' "
		"LIMIT 1", 
		loadINIvalue.empty() ? defaults.c_str() : loadINIvalue.c_str(), type.c_str(), category.c_str());
	safe_delete(newval);

	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		server_log->Log(log_error, "SetServerSettings Mysql check_query failed: %s", query.c_str());
		return false;
	}
	return true;
}
//// This is not working right, returns false regardless, if changed on line 611 it returns true even if they don't exist.
bool Database::CheckMissingSettings(std::string type)
{
	std::string query;

	server_log->Log(log_debug, "Entered CheckMissingSettings using type: %s.", type.c_str());

	query = StringFormat("SELECT * FROM tblloginserversettings WHERE type = '%s';", type.c_str());

	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		server_log->Log(log_error, "CheckMissingSettings Mysql check_query returned - not existing type: %s", query.c_str());
		return true;
	}
	else
	{
		server_log->Log(log_debug, "tblloginserversettings '%s' entries exist sending continue.", type.c_str());
		return false;
	}
}

void Database::InsertMissingSettings(std::string type, std::string value, std::string category, std::string description, std::string defaults)
{
	std::string query;

	if (!CheckMissingSettings(type))
	{
		string query = StringFormat(
			"INSERT INTO `tblloginserversettings` (`type`, `value`, category, description, defaults)"
			"VALUES('%s', '%s', '%s', '%s', '%s');", 
			type.c_str(),
			value.c_str(),
			category.c_str(),
			description.c_str(),
			defaults.c_str());

		auto results = QueryDatabase(query);

		if (!results.Success())
		{
			server_log->Log(log_error, "InsertMissingSettings query failed: %s", query.c_str());
			return;
		}
		return;
	}
	return;
}
#pragma endregion

#pragma region World Server Account Info
bool Database::CreateWorldRegistration(std::string long_name, std::string short_name, unsigned int &id)
{
	char escaped_long_name[201];
	char escaped_short_name[101];
	DoEscapeString(escaped_long_name, long_name.substr(0, 100).c_str(), (int)strlen(long_name.substr(0, 100).c_str()));
	DoEscapeString(escaped_short_name, short_name.substr(0, 100).c_str(), (int)strlen(short_name.substr(0, 100).c_str()));

	std::string query;

	query = StringFormat("SELECT max(ServerID) FROM %s", LoadServerSettings("schema", "world_registration_table", "CreateWorldRegistration..Select", true).c_str());

	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
		return false;
	}

	auto row = results.begin();
	if (row[0] == NULL)
	{
		id = 0;
	}
	id++;

	query = StringFormat("INSERT INTO %s SET "
		"ServerID = '%s', "
		"ServerLongName = '%s', "
		"ServerShortName = '%s', "
		"ServerListTypeID = 3, "
		"ServerAdminID = 0, "
		"ServerTrusted = 0, "
		"ServerTagDescription = ''", 
		LoadServerSettings("schema", "world_registration_table", "CreateWorldRegistration..Insert", true).c_str(),
		std::to_string(id).c_str(),
		escaped_long_name,
		escaped_short_name
		);

	auto results2 = QueryDatabase(query);

	if (!results2.Success())
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
		return false;
	}
	return true;
}

void Database::UpdateWorldRegistration(unsigned int id, std::string long_name, std::string ip_address)
{
	char escaped_long_name[101];
	DoEscapeString(escaped_long_name, long_name.substr(0, 100).c_str(), (int)strlen(long_name.substr(0, 100).c_str()));

	std::string query;

	query = StringFormat("UPDATE %s "		
		"SET "
		"ServerLastLoginDate = now(), "
		"ServerLastIPAddr = '%s', "
		"ServerLongName = '%s' "
		"WHERE "
		"ServerID = '%s'",
		LoadServerSettings("schema", "world_registration_table", "UpdateWorldRegistration", true).c_str(),
		ip_address.c_str(),
		escaped_long_name,
		std::to_string(id).c_str()
		);
	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
	}
}

bool Database::GetWorldRegistration(std::string long_name, std::string short_name, unsigned int &id, std::string &desc, unsigned int &list_id,
	unsigned int &trusted, std::string &list_desc, std::string &account, std::string &password)
{
	char escaped_short_name[101];
	DoEscapeString(escaped_short_name, short_name.substr(0, 100).c_str(), (int)strlen(short_name.substr(0, 100).c_str()));

	std::string query;

	query = StringFormat("SELECT "
		"WSR.ServerID, WSR.ServerTagDescription, WSR.ServerTrusted, SLT.ServerListTypeID, SLT.ServerListTypeDescription, WSR.ServerAdminID "
		"FROM %s AS WSR JOIN %s AS SLT ON "
		"WSR.ServerListTypeID = SLT.ServerListTypeID "
		"WHERE "
		"WSR.ServerShortName = '%s'",
		LoadServerSettings("schema", "world_registration_table", "GetWorldRegistration..", true).c_str(),
		LoadServerSettings("schema", "world_server_type_table", "GetWorldRegistration..", true).c_str(),
		escaped_short_name
		);
	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
		return false;
	}

	auto row = results.begin();
	if (row != nullptr)
	{
		id = atoi(row[0]);
		desc = row[1];
		trusted = atoi(row[2]);
		list_id = atoi(row[3]);
		list_desc = row[4];
		int db_account_id = atoi(row[5]);

		if (db_account_id > 0)
		{
			std::string query;
			query = StringFormat("SELECT AccountName, AccountPassword "
				"FROM %s WHERE "
				"ServerAdminID = %s",
				LoadServerSettings("schema", "world_admin_registration_table", "GetWorldRegistration..", true).c_str(),
				std::to_string(db_account_id).c_str()
				);
			auto results = QueryDatabase(query);

			if (!results.Success())
			{
				server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
				return false;
			}

			auto row = results.begin();
			if (row != nullptr)
			{
				account = row[0];
				password = row[1];
				return true;
			}
			server_log->Log(log_database, "Mysql query returned no result: %s", query.c_str());
			return false;
		}
		return true;
	}
	server_log->Log(log_database, "Mysql query returned no result: %s", query.c_str());
	return false;
}
#pragma endregion

#pragma region Player Account Info
void Database::CreateLSAccount(unsigned int id, std::string name, std::string password, std::string email, unsigned int created_by, std::string LastIPAddress, std::string creationIP)
{
	bool activate = 0;
	if (LoadServerSettings("options", "auto_account_activate", "CreateLSAccount..auto activate", true) == "TRUE")
	{
		activate = 1;
	}
	std::string query;

	char tmpUN[1024];
	DoEscapeString(tmpUN, name.c_str(), (int)name.length());

	query = StringFormat("INSERT INTO %s "
		"SET LoginServerID = %s, "
		"AccountName = '%s', "
		"AccountPassword = sha('%s'), "
		"AccountCreateDate = now(), "
		"LastLoginDate = now(), "
		"LastIPAddress = '%s', "
		"client_unlock = '%s', "
		"created_by = '%s', "
		"creationIP = '%s'",
		LoadServerSettings("schema", "account_table", "CreateLSAccount..Insert", true).c_str(),
		std::to_string(id).c_str(),
		tmpUN,
		password.c_str(),
		LastIPAddress.c_str(),
		std::to_string(activate).c_str(),
		std::to_string(created_by).c_str(),
		creationIP.c_str()
		);

	auto results = QueryDatabase(query);
	if (!results.Success())
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
	}
}

void Database::UpdateLSAccount(unsigned int id, std::string ip_address)
{
	std::string query;

	query = StringFormat("UPDATE %s SET "
		"LastIPAddress = '%s', "
		"LastLoginDate = now() "
		"WHERE "
		"LoginServerID = %s",
		LoadServerSettings("schema", "account_table", "UpdateLSAccount..", true).c_str(),
		ip_address.c_str(),
		std::to_string(id).c_str()
		);

	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
	}
}

bool Database::GetAccountLockStatus(std::string name)
{
	std::string query;

	query = StringFormat("SELECT client_unlock FROM %s WHERE "
						"AccountName = '%s'",
						LoadServerSettings("schema", "account_table", "GetAccountLockStatus..", true).c_str(),
						name.c_str()
						);

	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
		return false;
	}

	auto row = results.begin();
	while (row != nullptr)
	{
		int unlock = atoi(row[0]);
		if (unlock == 1)
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	return false;
}

void Database::UpdateAccessLog(unsigned int account_id, std::string account_name, std::string IP, unsigned int accessed, std::string reason)
{
	std::string query;

	char tmpUN[1024];
	DoEscapeString(tmpUN, account_name.c_str(), (int)account_name.length());

	query = StringFormat("INSERT INTO %s SET "
		"account_id = %s, "
		"account_name = '%s', "
		"IP = '%s', "
		"accessed = '%s', "
		"reason = '%s'",
		LoadServerSettings("schema", "access_log_table", "UpdateAccessLog..", true).c_str(),
		std::to_string(account_id).c_str(),
		tmpUN,
		IP.c_str(),
		std::to_string(accessed).c_str(),
		reason.c_str()
		);

	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
	}
}

bool Database::GetLoginDataFromAccountName(std::string name, std::string &password, unsigned int &id)
{
	std::string query;

	char tmpUN[1024];
	DoEscapeString(tmpUN, name.c_str(), (int)name.length()); (tmpUN, name.c_str(), (int)name.length());

	query = StringFormat("SELECT LoginServerID, AccountPassword "
		"FROM %s WHERE "
		"AccountName = '%s'",
		LoadServerSettings("schema", "account_table", "GetLoginDataFromAccountName..", true).c_str(),
		tmpUN
		);

	auto results = QueryDatabase(query);

	if (!results.Success())
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
		return false;
	}

	auto row = results.begin();
	while (row != nullptr)
	{
		id = atoi(row[0]);
		password = row[1];
		return true;
	}
	server_log->Log(log_database, "Mysql query returned no result: %s", query.c_str());
	return false;
}
#pragma endregion
