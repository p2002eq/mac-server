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
#include "database.h"
#include "error_log.h"
#include "login_server.h"

#include "../common/string_util.h"

#pragma warning( disable : 4267 )

extern ErrorLog *server_log;
extern LoginServer server;

#pragma comment(lib, "mysqlclient.lib")

Database::Database(string user, string pass, string host, string port, string name)
{
	this->user = user;
	this->pass = pass;
	this->host = host;
	this->name = name;

	db = mysql_init(nullptr);
	if(db)
	{
		my_bool r = 1;
		mysql_options(db, MYSQL_OPT_RECONNECT, &r);
		if(!mysql_real_connect(db, host.c_str(), user.c_str(), pass.c_str(), name.c_str(), atoi(port.c_str()), nullptr, 0))
		{
			mysql_close(db);
			server_log->Log(log_database, "Failed to connect to MySQL database. Error: %s", mysql_error(db));
			exit(1);
		}
	}
	else
	{
		server_log->Log(log_database, "Failed to create db object in MySQL database.");
	}
}

Database::~Database()
{
	if(db)
	{
		mysql_close(db);
	}
}

#pragma region Player Account Info
void Database::CreateLSAccount(unsigned int id, std::string name, std::string password, std::string email, unsigned int created_by, std::string LastIPAddress, std::string creationIP)
{
	bool activate = 0;
	if (!db)
	{
		return;
	}
	if (LoadServerSettings("options", "auto_account_activate") == "TRUE")
	{
		activate = 1;
	}
	char tmpUN[1024];
	mysql_real_escape_string(db, tmpUN, name.c_str(), name.length());

	string query = "INSERT INTO " +
						LoadServerSettings("schema", "account_table") +
					" SET LoginServerID = " + std::to_string(id) + ", " +
						"AccountName = '" + tmpUN + "', " +
						"AccountPassword = sha('" +	password + "'), " +
						"AccountCreateDate = now(), " +
						"LastLoginDate = now(), " +
						"LastIPAddress = '" + LastIPAddress + "', " +
						"client_unlock = '" + std::to_string(activate) + "', " +
						"created_by = '" + std::to_string(created_by) + "', " +
						"creationIP = '" + creationIP + "'";

	if (mysql_query(db, query.c_str()) != 0)
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
	}
}

void Database::UpdateLSAccount(unsigned int id, string ip_address)
{
	if (!db)
	{
		return;
	}

	string query = "UPDATE " +
		LoadServerSettings("schema", "account_table") +
		" SET " +
		"LastIPAddress = '" + ip_address + "', " +
		"LastLoginDate = now() " +
		"WHERE " +
		"LoginServerID = " + std::to_string(id);

	if (mysql_query(db, query.c_str()) != 0)
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
	}
}

bool Database::GetAccountLockStatus(std::string name)
{
	if (!db)
	{
		return false;
	}

	string query = "SELECT "
						"client_unlock "
					"FROM " +
						LoadServerSettings("schema", "account_table") +
					" WHERE "
						"AccountName = '" + name + "'";

	if (mysql_query(db, query.c_str()) != 0)
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
		return false;
	}

	res = mysql_use_result(db);

	if (res)
	{
		while ((row = mysql_fetch_row(res)))
		{
			int unlock = atoi(row[0]);
			if (unlock == 1)
			{
				mysql_free_result(res);
				return true;
			}
			else
			{
				mysql_free_result(res);
				return false;
			}
		}
	}
	return false;
}

void Database::UpdateAccessLog(unsigned int account_id, std::string account_name, std::string IP, unsigned int accessed, std::string reason)
{
	if(!db)
	{
		return;
	}

	char tmpUN[1024];
	mysql_real_escape_string(db, tmpUN, account_name.c_str(), account_name.length());

	string query = "INSERT INTO " +
						LoadServerSettings("schema", "access_log_table") +
					" SET " +
						"account_id = " + std::to_string(account_id) + ", " +
						"account_name = '" + tmpUN +"', " +
						"IP = '" + IP + "', " +
						"accessed = '" + std::to_string(accessed) + "', " +
						"reason = '" + reason + "'";

	if(mysql_query(db, query.c_str()) != 0)
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
	}
}

bool Database::GetLoginDataFromAccountName(string name, string &password, unsigned int &id)
{
	if(!db)
	{
		return false;
	}

	char tmpUN[1024];
	mysql_real_escape_string(db, tmpUN, name.c_str(), name.length());(tmpUN, name.c_str(), name.length());

	string query = "SELECT "
						"LoginServerID, AccountPassword "
					"FROM " +
						LoadServerSettings("schema", "account_table") +
					" WHERE " +
						"AccountName = '" + tmpUN + "'";

	if(mysql_query(db, query.c_str()) != 0)
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
		return false;
	}

	res = mysql_use_result(db);

	if(res)
	{
		while((row = mysql_fetch_row(res)) != nullptr)
		{
			id = atoi(row[0]);
			password = row[1];
			mysql_free_result(res);
			return true;
		}
	}
	server_log->Log(log_database, "Mysql query returned no result: %s", query.c_str());
	return false;
}
#pragma endregion

#pragma region World Server Account Info
bool Database::CreateWorldRegistration(string long_name, string short_name, unsigned int &id)
{
	if (!db)
	{
		return false;
	}

	char escaped_long_name[201];
	char escaped_short_name[101];
	unsigned long length;
	length = mysql_real_escape_string(db, escaped_long_name, long_name.substr(0, 100).c_str(), long_name.substr(0, 100).length());
	escaped_long_name[length + 1] = 0;
	length = mysql_real_escape_string(db, escaped_short_name, short_name.substr(0, 100).c_str(), short_name.substr(0, 100).length());
	escaped_short_name[length + 1] = 0;

	string query = "SELECT max(ServerID) FROM " + LoadServerSettings("schema", "world_registration_table");

	if (mysql_query(db, query.c_str()) != 0)
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
		return false;
	}

	res = mysql_use_result(db);
	if (res)
	{
		if ((row = mysql_fetch_row(res)) != nullptr)
		{
			id = atoi(row[0]) + 1;
			mysql_free_result(res);

			string query = "INSERT INTO " +
					LoadServerSettings("schema", "world_registration_table") +
				" SET " +
					"ServerID = '" + std::to_string(id) + "', " +
					"ServerLongName = '" + escaped_long_name + "', " +
					"ServerShortName = '" + escaped_short_name + "', " +
					"ServerListTypeID = 3, " +
					"ServerAdminID = 0, " +
					"ServerTrusted = 0, " +
					"ServerTagDescription = ''";

			if (mysql_query(db, query.c_str()) != 0)
			{
				server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
				return false;
			}
			return true;
		}
	}
	server_log->Log(log_database, "World registration did not exist in the database for %s %s", long_name.c_str(), short_name.c_str());
	return false;
}

void Database::UpdateWorldRegistration(unsigned int id, string long_name, string ip_address)
{
	if (!db)
	{
		return;
	}

	char escaped_long_name[101];
	unsigned long length;
	length = mysql_real_escape_string(db, escaped_long_name, long_name.substr(0, 100).c_str(), long_name.substr(0, 100).length());
	escaped_long_name[length + 1] = 0;

	string query = "UPDATE " +
			LoadServerSettings("schema", "world_registration_table") +
		" SET " +
			"ServerLastLoginDate = now(), " +
			"ServerLastIPAddr = '" + ip_address + "', " +
			"ServerLongName = '" + escaped_long_name + "' " +
		"WHERE " +
			"ServerID = '" + std::to_string(id) + "'";

	if (mysql_query(db, query.c_str()) != 0)
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
	}
}

bool Database::GetWorldRegistration(string long_name, string short_name, unsigned int &id, string &desc, unsigned int &list_id,
		unsigned int &trusted, string &list_desc, string &account, string &password)
{
	if(!db)
	{
		return false;
	}

	char escaped_short_name[101];
	unsigned long length;
	length = mysql_real_escape_string(db, escaped_short_name, short_name.substr(0, 100).c_str(), short_name.substr(0, 100).length());
	escaped_short_name[length+1] = 0;

	string query = "SELECT "
						"WSR.ServerID, WSR.ServerTagDescription, WSR.ServerTrusted, SLT.ServerListTypeID, SLT.ServerListTypeDescription, WSR.ServerAdminID "
					"FROM " +
						LoadServerSettings("schema", "world_registration_table") +
					" AS WSR JOIN " +
						LoadServerSettings("schema", "world_server_type_table") +
					" AS SLT ON "
						"WSR.ServerListTypeID = SLT.ServerListTypeID"
					" WHERE "
						"WSR.ServerShortName = '" + escaped_short_name + " '";

	if(mysql_query(db, query.c_str()) != 0)
	{
		server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
		return false;
	}

	res = mysql_use_result(db);
	if(res)
	{
		if((row = mysql_fetch_row(res)) != nullptr)
		{
			id = atoi(row[0]);
			desc = row[1];
			trusted = atoi(row[2]);
			list_id = atoi(row[3]);
			list_desc = row[4];
			int db_account_id = atoi(row[5]);
			mysql_free_result(res);

			if(db_account_id > 0)
			{
				string query = "SELECT "
									"AccountName, AccountPassword "
								"FROM " +
									LoadServerSettings("schema", "world_admin_registration_table") +
								" WHERE "
									"ServerAdminID = " + std::to_string(db_account_id);

				if(mysql_query(db, query.c_str()) != 0)
				{
					server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
					return false;
				}

				res = mysql_use_result(db);
				if(res)
				{
					if((row = mysql_fetch_row(res)) != nullptr)
					{
						account = row[0];
						password = row[1];
						mysql_free_result(res);
						return true;
					}
				}
				server_log->Log(log_database, "Mysql query returned no result: %s", query.c_str());
				return false;
			}
			return true;
		}
	}
	server_log->Log(log_database, "Mysql query returned no result: %s", query.c_str());
	return false;
}
#pragma endregion

#pragma region Create Server Setup
bool Database::CheckSettings(int type)
{
	server_log->Log(log_debug, "Checking for database structure with type %s", std::to_string(type).c_str());
	if (!db)
	{
		server_log->Log(log_error, "MySQL Not connected.");
		return false;
	}
	if (type == 1)
	{
		string check_table_query = "SHOW TABLES LIKE 'tblloginserversettings'";

		if (mysql_query(db, check_table_query.c_str()) == NULL)//!= 0)
		{
			server_log->Log(log_error, "CheckSettings Mysql query failed: %s", check_table_query.c_str());
			return false;
		}
		server_log->Log(log_debug, "tblloginserversettings exists sending continue.");
		return true;
	}
	if (type == 2)
	{
		string check_query = "SELECT * FROM tblloginserversettings";

		if (mysql_query(db, check_query.c_str()) != 0)
		{
			server_log->Log(log_error, "CheckSettings Mysql check_query failed: %s", check_query.c_str());
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
	server_log->Log(log_debug, "Entering Server Settings database setup.");

	string check_table_query = "SHOW TABLES LIKE 'tblloginserversettings'";

	if (mysql_query(db, check_table_query.c_str()) != 0)
	{
		server_log->Log(log_error, "CreateServerSettings Mysql query returned, table does not exist: %s", check_table_query.c_str());
	}
	else
	{
		return true;
	}

	res = mysql_use_result(db);
	if (res)
	{
		if ((row = mysql_fetch_row(res)) == nullptr)
		{
			server_log->Log(log_debug, "Server Settings table does not exist, creating.");

			string create_table_query =
				"CREATE TABLE `tblloginserversettings` ("
				"`type` varchar(50) NOT NULL,"
				"`value` varchar(50) NOT NULL,"
				"`category` varchar(20) NOT NULL,"
				"`description` varchar(99) NOT NULL,"
				"`defaults` varchar(50) NOT NULL"
				") ENGINE=InnoDB DEFAULT CHARSET=latin1";

			if (mysql_query(db, create_table_query.c_str()) != 0)
			{
				server_log->Log(log_error, "CreateServerSettings Mysql query failed to create table: %s", create_table_query.c_str());
				return false;
			}

			string query =
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
				"('world_trace', '', 'options', 'debugging', 'FALSE');";

			if (mysql_query(db, query.c_str()) != 0)
			{
				server_log->Log(log_error, "Mysql query failed: %s", query.c_str());
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
	}
	mysql_free_result(res);
	if (GetServerSettings())
	{
		return true;
	}
	return false;
}

bool Database::GetServerSettings()
{
	string check_query = "SELECT * FROM tblloginserversettings";

	if (mysql_query(db, check_query.c_str()) != 0)
	{
		server_log->Log(log_error, "Mysql check_query failed: %s", check_query.c_str());
		return false;
	}

	bool result = true;
	res = mysql_use_result(db);
	if (res)
	{
		while (row = mysql_fetch_row(res))
		{
			std::string type = row[0];
			std::string value = row[1];
			std::string category = row[2];
			std::string description = row[3];
			std::string defaults = row[4];

			if (value.empty())
			{
				server_log->Log(log_database, "Mysql check_query returns NO value for type %s", type.c_str());
				//mysql_free_result(res);
				result = false;
			}
			else if (!value.empty())
			{
				server_log->Log(log_database, "Mysql check_query returns `value`: %s for type %s", value.c_str(), type.c_str());
			}
		}
	}
	//mysql_free_result(res);
	return result;
}

bool Database::SetServerSettings(std::string type, std::string category, std::string defaults)
{
	Config* newval = new Config;
	std::string loadINIvalue = newval->LoadOption(category, type, "login.ini");
	string update_value = StringFormat("UPDATE tblloginserversettings "
		"SET value = '%s' "
		"WHERE type = '%s' "
		"AND category = '%s' "
		"LIMIT 1", loadINIvalue.empty() ? defaults.c_str() : loadINIvalue.c_str(), type.c_str(), category.c_str());
	safe_delete(newval);
	if (mysql_query(db, update_value.c_str()) != 0)
	{
		server_log->Log(log_error, "Mysql check_query failed: %s", update_value.c_str());
		server_log->Log(log_error, "Mysql Error: %s", mysql_error(db));
		return false;
	}
	return true;
}
// This is not working right, returns false regardless, if changed on line 609 if returns true even if they don't exist.
bool Database::CheckMissingSettings(std::string type)
{
	string check_query = "SELECT * FROM tblloginserversettings WHERE type = '" + type + "';";

	if (mysql_query(db, check_query.c_str()) != 0)
	{
		server_log->Log(log_error, "CheckMissingSettings Mysql check_query returned - not existing type: %s", check_query.c_str());
		res = mysql_use_result(db);
		mysql_free_result(res);
		return false;
	}
	server_log->Log(log_debug, "tblloginserversettings '%s' entries exist sending continue.", type.c_str());
	res = mysql_use_result(db);
	mysql_free_result(res);
	return true;
}

void Database::InsertMissingSettings(std::string type, std::string value, std::string category, std::string description, std::string defaults)
{
		string query = StringFormat("INSERT INTO `tblloginserversettings` (`type`, `value`, category, description, defaults)"
			"VALUES('%s', '%s', '%s', '%s', '%s');", type.c_str(), value.c_str(), category.c_str(), description.c_str(), defaults.c_str());

		if (mysql_query(db, query.c_str()) != 0)
		{
			server_log->Log(log_error, "InsertMissingSettings query failed: %s", query.c_str());
			res = mysql_use_result(db);
			mysql_free_result(res);
			return;
		}
		res = mysql_use_result(db);
		mysql_free_result(res);
	return;
}
#pragma endregion

#pragma region Load Server Setup
std::string Database::LoadServerSettings(std::string category, std::string type)
{
	bool disectmysql = false; // for debugging what gets returned.
	if (!db)
	{
		server_log->Log(log_error, "MySQL Not connected.");
		return false;
	}

	if (disectmysql) { server_log->Log(log_debug, "Mysql LoadServerSettings got: %s : %s", category.c_str(), type.c_str()); }
	string query = "SELECT "
						"* "
					"FROM "
						"tblloginserversettings "
					"WHERE "
						"type = '" + type + "' "
					"AND "
						"category = '" + category + "'";

	if (disectmysql) { server_log->Log(log_debug, "Mysql LoadServerSettings query is: %s", query.c_str()); }

	if (mysql_query(db, query.c_str()) != 0)
	{
		server_log->Log(log_error, "Mysql LoadServerSettings query failed: %s", query.c_str());
		return "";
	}

	res = mysql_use_result(db);

	if (res)
	{
		while (row = mysql_fetch_row(res))
		{
			std::string value = row[1];
			if (disectmysql) { server_log->Log(log_debug, "Mysql LoadServerSettings result: %s", value.c_str()); }
			mysql_free_result(res);
			return value.c_str();
		}
	}
	return "";
}
#pragma endregion
