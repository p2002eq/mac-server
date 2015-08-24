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
#include "login_server.h"
#include "error_log.h"

#include <fstream>
#include <iostream>

#pragma warning( disable : 4267 )

extern ErrorLog *server_log;
extern LoginServer server;

std::string Config::LoadOption(std::string title, std::string parameter, std::string filename)
{
	Parse(filename.c_str());
	std::map<std::string, std::map<std::string, std::string> >::iterator iter = vars.find(title);
	if (iter != vars.end())
	{
		std::map<std::string, std::string>::iterator arg_iter = iter->second.find(parameter);
		if (arg_iter != iter->second.end())
		{
			return arg_iter->second;
		}
	}
	return std::string("");
}

/**
* Runs all configuration routines and sets loginserver settings.
*/
bool Config::ConfigSetup()
{
	//server_log->Log(log_debug, "Debugging parse error: %s", LoadOption("LoginServerDatabase", "user", "db.ini").c_str());
	//return false;

	std::string configFile;

	bool login = std::ifstream("login.ini").good();
	bool dbini = std::ifstream("db.ini").good();

	//create db.ini and if all goes well copy login.ini to db table and continue starting the server.
	//if (login && !dbini)
	//{
	//	server_log->Log(log_debug, "login.ini found but db.ini missing.");

	//	if (server.db) { delete server.db; }
	//	server.db = (Database*)new Database(
	//		server.config->LoadOption("database", "user", "login.ini"),
	//		server.config->LoadOption("database", "password", "login.ini"),
	//		server.config->LoadOption("database", "host", "login.ini"),
	//		server.config->LoadOption("database", "port", "login.ini"),
	//		server.config->LoadOption("database", "db", "login.ini")
	//		);

	//	//write db.ini based on login.ini entries
	//	server.config->WriteDBini();
	//	//check if table exists and set the field values based on the login.ini
	//	if (!server.db->CreateServerSettings())
	//	{
	//		//send shutdown to main.cpp critical failure.
	//		return false;
	//	}
	//}
	//else if (!login && dbini)
	//{
	//	server_log->Log(log_debug, "db.ini found but login.ini missing. We are able to continue.");

	//	//check if settings exist in database.
	//	if (server.db) { delete server.db; }
	//	server.db = (Database*)new Database(
	//		server.config->LoadOption("LoginServerDatabase", "user", "db.ini"),
	//		server.config->LoadOption("LoginServerDatabase", "password", "db.ini"),
	//		server.config->LoadOption("LoginServerDatabase", "host", "db.ini"),
	//		server.config->LoadOption("LoginServerDatabase", "port", "db.ini"),
	//		server.config->LoadOption("LoginServerDatabase", "db", "db.ini")
	//		);

	//	if (!server.db->CheckSettings(2))
	//	{
	//		//send shutdown to main.cpp critical failure.
	//		server_log->Log(log_error, "Missing settings in tblloginserversettings.");
	//		return false;
	//	}
	//	//db.ini was already set up. Settings are fine in database.
	//}
	//else if (login && dbini)
	//{
	//	server_log->Log(log_debug, "db.ini and login.ini found. We are able to continue.");

	//	if (server.db) { delete server.db; }
	//	server.db = (Database*)new Database(
	//		server.config->LoadOption("LoginServerDatabase", "user", "db.ini"),
	//		server.config->LoadOption("LoginServerDatabase", "password", "db.ini"),
	//		server.config->LoadOption("LoginServerDatabase", "host", "db.ini"),
	//		server.config->LoadOption("LoginServerDatabase", "port", "db.ini"),
	//		server.config->LoadOption("LoginServerDatabase", "db", "db.ini")
	//		);

	//	//check if settings exist in database.
	//	if (!server.db->CheckSettings(2))
	//	{
	//		server_log->Log(log_debug, "Settings entries not found in database.");
	//		if (server.db->CheckSettings(1))
	//		{
	//			server_log->Log(log_debug, "Settings entries not found in database but table was.");
	//			//server.db->ResetDBSettings();
	//		}
	//		if (!server.db->CreateServerSettings())
	//		{
	//			server_log->Log(log_debug, "Settings entries not found in database creating base settings from ini files.");
	//			//send shutdown to main.cpp critical failure.
	//			server_log->Log(log_error, "Missing settings in tblloginserversettings.");
	//			return false;
	//		}
	//	}
	//	//db.ini was already set up. Settings are fine in database.
	//	server_log->Log(log_debug, "login.ini and db.ini found.");
	//}
	////no ini found, can't start the server this way.
	////write the default ini and prompt user to edit it.
	//else
	//{
	//	server.config->WriteDBini();
	//	//send shutdown to main.cpp critical failure.
	//	return false;
	//}

	//ini processing succeeded, continue on.

	//Create our DB from options.
	server_log->Log(log_debug, "MySQL Database Init.");

	bool config = std::ifstream("db.ini").good();

	if (config)
	{
		//if (server.db) { delete server.db; }
		server_log->Log(log_debug, "Connecting to database...");
		if (!server.db->Connect(
			server.config->LoadOption("LoginServerDatabase", "host", "db.ini").c_str(),
			server.config->LoadOption("LoginServerDatabase", "user", "db.ini").c_str(),
			server.config->LoadOption("LoginServerDatabase", "password", "db.ini").c_str(),
			server.config->LoadOption("LoginServerDatabase", "db", "db.ini").c_str(),
			stoi(server.config->LoadOption("LoginServerDatabase", "port", "db.ini").c_str())))
		{
			server_log->Log(log_debug, "Unable to connect to the database, cannot continue without a database connection");
			return false;
		}
		server_log->Log(log_debug, "Database connected.");

		//server.db = (Database*)new Database(
		//	server.config->LoadOption("LoginServerDatabase", "user", "db.ini"),
		//	server.config->LoadOption("LoginServerDatabase", "password", "db.ini"),
		//	server.config->LoadOption("LoginServerDatabase", "host", "db.ini"),
		//	server.config->LoadOption("LoginServerDatabase", "port", "db.ini"),
		//	server.config->LoadOption("LoginServerDatabase", "db", "db.ini")
		//	);
	}
	else
	{
		//send shutdown to main.cpp critical failure.
		server_log->Log(log_error, "db.ini not processed, unknown error allowed us to get this far.");
		return false;
	}

	//Make sure our database got created okay, otherwise cleanup and exit.
	//if (!server.db)
	//{
	//	//send shutdown to main.cpp critical failure.
	//	server_log->Log(log_error, "database access not set.");
	//	return false;
	//}
	UpdateSettings();
	//return true;
	return false;
}

void Config::WriteDBini()
{
	bool login = std::ifstream("login.ini").good();
	bool dbexist = std::ifstream("db.ini").good();
	if (dbexist && !login)
	{
		return;
	}
	else if (!dbexist && login)
	{
		std::ofstream dbini("db.ini");
		dbini << "[LoginServerDatabase]\n";
		dbini << "host = " + LoadOption("database", "host", "login.ini") + "\n";
		dbini << "port = " + LoadOption("database", "port", "login.ini") + "\n";
		dbini << "db = " + LoadOption("database", "db", "login.ini") + "\n";
		dbini << "user = " + LoadOption("database", "user", "login.ini") + "\n";
		dbini << "password = " + LoadOption("database", "password", "login.ini") + "\n";
		dbini << "\n";
		dbini << "[GameServerDatabase]\n";
		dbini << "#Game Server section not used yet.\n";
		dbini << "host = " + LoadOption("database", "host", "login.ini") + "\n";
		dbini << "port = " + LoadOption("database", "port", "login.ini") + "\n";
		dbini << "db = " + LoadOption("database", "db", "login.ini") + "\n";
		dbini << "user = " + LoadOption("database", "user", "login.ini") + "\n";
		dbini << "password = " + LoadOption("database", "password", "login.ini");
		dbini.close();
		return;
	}
	else if (!dbexist && !login)
	{
		// place holder for reading from database for values
		std::ofstream dbini("db.ini");
		dbini << "[LoginServerDatabase]\n";
		dbini << "host = \n";
		dbini << "port = \n";
		dbini << "db = \n";
		dbini << "user = \n";
		dbini << "password = \n";
		dbini << "\n";
		dbini << "[GameServerDatabase]\n";
		dbini << "#Game Server section not used yet.\n";
		dbini << "host = \n";
		dbini << "port = \n";
		dbini << "db = \n";
		dbini << "user = \n";
		dbini << "password = ";
		dbini.close();
		return;
	}
	return;
}

void Config::UpdateSettings()
{
	//formatting for adding loginserver settings.
	//server.db->InsertMissingSettings("type", "value", "category", "description", "defaults");

	//server.db->InsertMissingSettings("pop_count", "0", "options", "0 to only display UP or DOWN or 1 to show population count in server select.", "0");
	//server.db->InsertMissingSettings("ticker", "Welcome", "options", "Sets the welcome message in server select.", "Welcome");
}

/**
* Opens a file and passes it to be tokenized
* Then it parses the tokens returned and puts them into titles and variables.
*/
void Config::Parse(const char *file_name)
{
	server_log->Log(log_debug, "Parsing file: %s.", file_name);
	if (file_name == nullptr)
	{
		server_log->Log(log_error, "Config::Parse(), file_name passed was null.");
		return;
	}
	vars.clear();
	FILE *input = fopen(file_name, "r");
	if (input)
	{
		std::list<std::string> tokens;
		Tokenize(input, tokens);

		char mode = 0;
		std::string title, param, arg;
		std::list<std::string>::iterator iter = tokens.begin();
		while (iter != tokens.end())
		{
			if ((*iter).compare("[") == 0)
			{
				title.clear();
				bool first = true;
				++iter;
				if (iter == tokens.end())
				{
					server_log->Log(log_error, "Config::Parse(), EOF before title done parsing.");
					fclose(input);
					vars.clear();
					return;
				}

				while ((*iter).compare("]") != 0 && iter != tokens.end())
				{
					if (!first)
					{
						title += " ";
					}
					else
					{
						first = false;
					}
					title += (*iter);
					++iter;
				}
				++iter;
			}

			if (mode == 0)
			{
				param = (*iter);
				mode++;
			}
			else if (mode == 1)
			{
				mode++;
				if ((*iter).compare("=") != 0)
				{
					server_log->Log(log_error, "Config::Parse(), invalid parse token where = should be.");
					fclose(input);
					vars.clear();
					return;
				}
			}
			else
			{
				arg = (*iter);
				mode = 0;
				std::map<std::string, std::map<std::string, std::string> >::iterator map_iter = vars.find(title);
				if (map_iter != vars.end())
				{
					map_iter->second[param] = arg;
					vars[title] = map_iter->second;
				}
				else
				{
					std::map<std::string, std::string> var_map;
					var_map[param] = arg;
					vars[title] = var_map;
				}
			}
			++iter;
		}
		fclose(input);
	}
	else
	{
		server_log->Log(log_error, "Config::Parse(), file was unable to be opened for parsing.");
		server_log->Log(log_error, "%s", file_name);
	}
}

/**
* Pretty basic lexical analyzer
* Breaks up the input character stream into tokens and puts them into the list provided.
* Ignores # as a line comment
*/
void Config::Tokenize(FILE *input, std::list<std::string> &tokens)
{
	char c = fgetc(input);
	std::string lexeme;

	while (c != EOF)
	{
		if (isspace(c))
		{
			if (lexeme.size() > 0)
			{
				tokens.push_back(lexeme);
				lexeme.clear();
			}
			c = fgetc(input);
			continue;
		}

		if (isalnum(c))
		{
			lexeme.append((const char *)&c, 1);
			c = fgetc(input);
			continue;
		}

		switch (c)
		{
		case '#':
		{
			if (lexeme.size() > 0)
			{
				tokens.push_back(lexeme);
				lexeme.clear();
			}

			while (c != '\n' && c != EOF)
			{
				c = fgetc(input);
			}
			break;
		}
		case '[':
		case ']':
		case '=':
		{
			if (lexeme.size() > 0)
			{
				tokens.push_back(lexeme);
				lexeme.clear();
			}
			lexeme.append((const char *)&c, 1);
			tokens.push_back(lexeme);
			lexeme.clear();
			break;
		}
		default:
		{
			lexeme.append((const char *)&c, 1);
		}
		}
		c = fgetc(input);
	}

	if (lexeme.size() > 0)
	{
		tokens.push_back(lexeme);
	}
}