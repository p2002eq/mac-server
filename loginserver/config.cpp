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
#include "config.h"
#include "error_log.h"
#include <fstream>
#include <iostream>

#pragma warning( disable : 4267 )

extern ErrorLog *server_log;

std::string Config::LoadOption(std::string option, std::string filename)
{
	std::ifstream file(filename);
	std::string line;
	if(!file)
	{
		std::string message = "Config::LoadOption(), specified file '" + std::string(filename) + "' doesn't exist.";
		server_log->Log(log_error, message.c_str());
		file.close();
		return "";
	}
	if (file)
	{
		while (std::getline(file, line))
		{
			size_t found;
			found = line.find(option);
			if (line.find(option) != std::string::npos && int(found) == 0)
			{
				int stringposition = option.length() + 3;
				int stringsize = line.length() - stringposition;

				std::string substringtemp = line.substr(stringposition, stringsize);
				file.close();
				return substringtemp;
			}
		}
		std::string message = "Config::LoadOption(), specified option '" + std::string(option) + "' doesn't exist.";
		server_log->Log(log_error, message.c_str());
		file.close();
		return "";
	}
	file.close();
	return "";
}

void Config::ReWriteLSini()
{
	std::string line;
	std::ifstream readini("login.ini");
	std::ofstream writebackup("login.ini.bak");
	if (!readini)
	{
		// place holder for reading from database for values
		server_log->Log(log_error, "ini file copy error.");
		return;
	}

	std::string linebackup;

	while (std::getline(readini, line))
	{
		linebackup = line;
		linebackup += "\n";
		writebackup << linebackup;
	}
	readini.close();
	writebackup.close();

	std::string lineTemp;

	std::string lineReplace = "port = 6000";
	std::string lineNew = "clientport = 6000";
	std::ifstream readbackup("login.ini.bak");
	std::ofstream writeini("login.ini");
	while (std::getline(readbackup, line))
	{
		// If line does not need to be replaced.
		if (line != lineReplace)
		{
			lineTemp = line;
		}
		// Line replacements here.
		else if (line == lineReplace)
		{
			lineTemp = lineNew;
		}
		lineTemp += "\n";
		writeini << lineTemp;
	}
	readbackup.close();
	writeini.close();
	return;
}

void Config::WriteDBini()
{
	bool login = std::ifstream("login.ini").good();
	bool dbexist = std::ifstream("db.ini").good();
	if (dbexist)
	{
		return;
	}
	if (!dbexist && login)
	{
		std::ofstream dbini("db.ini");
		dbini << "[Login Server Database]\n";
		dbini << "lshost = " + LoadOption("host", "login.ini") + "\n";
		dbini << "lsport = " + LoadOption("port", "login.ini") + "\n";
		dbini << "lsdb = " + LoadOption("db", "login.ini") + "\n";
		dbini << "lsuser = " + LoadOption("user", "login.ini") + "\n";
		dbini << "lspassword = " + LoadOption("password", "login.ini") + "\n";
		dbini << "[Game Server Database]\n";
		dbini << "*Game Server section not used yet.*\n";
		dbini << "host = " + LoadOption("host", "login.ini") + "\n";
		dbini << "port = " + LoadOption("port", "login.ini") + "\n";
		dbini << "db = " + LoadOption("db", "login.ini") + "\n";
		dbini << "user = " + LoadOption("user", "login.ini") + "\n";
		dbini << "password = " + LoadOption("password", "login.ini");
		dbini.close();
		return;
	}
	if (!dbexist && !login)
	{
		// place holder for reading from database for values
		std::ofstream dbini("db.ini");
		dbini << "[Login Server Database]\n";
		dbini << "lshost = \n";
		dbini << "lsport = \n";
		dbini << "lsdb = \n";
		dbini << "lsuser = \n";
		dbini << "lspassword = \n";
		dbini << "[Game Server Database]\n";
		dbini << "*Game Server section not used yet.*\n";
		dbini << "host = \n";
		dbini << "port = \n";
		dbini << "db = \n";
		dbini << "user = \n";
		dbini << "password = ";
		dbini.close();
		return;
	}
}