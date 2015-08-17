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
#ifndef EQEMU_DATABASE_H
#define EQEMU_DATABASE_H

#include <string>
#include <stdlib.h>
#include <mysql.h>

/**
* Base database class, intended to be extended.
*/
class Database
{
public:
	/**
	* Constructor, tries to set our database to connect to the supplied options.
	*/
	Database(std::string user, std::string pass, std::string host, std::string port, std::string name);

	virtual ~Database();

	/**
	* @return Returns true if the database successfully connected.
	*/
	virtual bool IsConnected() { return (db != nullptr); }

#pragma region Player Account Info
	/**
	* Updates or creates the login server account with info from world server
	*/
	virtual void CreateLSAccount(unsigned int id, std::string name, std::string password, std::string email, unsigned int created_by, std::string LastIPAddress, std::string creationIP);

	/**
	* Updates the ip address of the client with account id = id
	*/
	virtual void UpdateLSAccount(unsigned int id, std::string ip_address);

	/**
	* Retrieves account status to check if account is activated for login.
	* Returns true if the record was found, false otherwise.
	*/
	virtual bool GetAccountLockStatus(std::string name);

	/**
	* Updates or creates the access log
	*/
	virtual void UpdateAccessLog(unsigned int account_id, std::string account_name, std::string IP, unsigned int accessed, std::string reason);

	/**
	* Retrieves the login data (password hash and account id) from the account name provided
	* Needed for client login procedure.
	* Returns true if the record was found, false otherwise.
	*/
	virtual bool GetLoginDataFromAccountName(std::string name, std::string &password, unsigned int &id);
#pragma endregion

#pragma region World Server Account Info
	/**
	* Creates new world registration for unregistered servers and returns new id
	*/
	virtual bool CreateWorldRegistration(std::string long_name, std::string short_name, unsigned int &id);

	/**
	* Updates the ip address of the world with account id = id
	*/
	virtual void UpdateWorldRegistration(unsigned int id, std::string long_name, std::string ip_address);

	/**
	* Retrieves the world registration from the long and short names provided.
	* Needed for world login procedure.
	* Returns true if the record was found, false otherwise.
	*/
	virtual bool GetWorldRegistration(std::string long_name, std::string short_name, unsigned int &id, std::string &desc, unsigned int &list_id,
		unsigned int &trusted, std::string &list_desc, std::string &account, std::string &password);
#pragma endregion

#pragma region Create Server Setup
	/**
	* Creates Server Settings table.
	*/
	virtual bool CreateServerSettings();
	/**
	* Gets values for server settings, tries ini first, if not grabs default.
	*/
	virtual bool GetServerSettings();
	/**
	* Writes values for server settings.
	*/
	virtual bool SetServerSettings(std::string type, std::string category, std::string defaults);
#pragma endregion

#pragma region Load Server Setup
	/**
	* Loads values for server settings from db.
	*/
	std::string LoadServerSettings(std::string category, std::string type);
#pragma endregion

protected:
	std::string user, pass, host, port, name;
	MYSQL *db;
	MYSQL_ROW row;
	MYSQL_RES *res;
};

#endif