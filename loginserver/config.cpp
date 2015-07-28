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

std::string Config::LoadOption(std::string title, std::string parameter, std::string filename)
{
	//std::ifstream file(filename);
	//std::string line;
	//if(!file)
	//{
	//	std::string message = "Config::LoadOption(), specified file '" + std::string(filename) + "' doesn't exist.";
	//	server_log->Log(log_error, message.c_str());
	//	file.close();
	//	return "";
	//}
	//if (file)
	//{
	//	while (std::getline(file, line))
	//	{
	//		size_t found;
	//		found = line.find(option);
	//		if (line.find(option) != std::string::npos && int(found) == 0)
	//		{
	//			int stringposition = option.length() + 3;
	//			int stringsize = line.length() - stringposition;

	//			std::string substringtemp = line.substr(stringposition, stringsize);
	//			file.close();
	//			return substringtemp;
	//		}
	//	}
	//	std::string message = "Config::LoadOption(), specified option '" + std::string(option) + "' doesn't exist.";
	//	server_log->Log(log_error, message.c_str());
	//	file.close();
	//	return "";
	//}
	//file.close();
	//return "";
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

//void Config::ReWriteLSini()
//{
//	std::string line;
//	std::ifstream readini("login.ini");
//	std::ofstream writebackup("login.ini.bak");
//	if (!readini)
//	{
//		// place holder for reading from database for values
//		server_log->Log(log_error, "ini file copy error.");
//		return;
//	}
//
//	std::string linebackup;
//
//	while (std::getline(readini, line))
//	{
//		linebackup = line;
//		linebackup += "\n";
//		writebackup << linebackup;
//	}
//	readini.close();
//	writebackup.close();
//
//	std::string lineTemp;
//
//	std::string lineReplace = "port = 6000";
//	std::string lineNew = "clientport = 6000";
//	std::ifstream readbackup("login.ini.bak");
//	std::ofstream writeini("login.ini");
//	while (std::getline(readbackup, line))
//	{
//		// If line does not need to be replaced.
//		if (line != lineReplace)
//		{
//			lineTemp = line;
//		}
//		// Line replacements here.
//		else if (line == lineReplace)
//		{
//			lineTemp = lineNew;
//		}
//		lineTemp += "\n";
//		writeini << lineTemp;
//	}
//	readbackup.close();
//	writeini.close();
//	return;
//}

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
		dbini << "lshost = " + LoadOption("database", "host", "login.ini") + "\n";
		dbini << "lsport = " + LoadOption("database", "port", "login.ini") + "\n";
		dbini << "lsdb = " + LoadOption("database", "db", "login.ini") + "\n";
		dbini << "lsuser = " + LoadOption("database", "user", "login.ini") + "\n";
		dbini << "lspassword = " + LoadOption("database", "password", "login.ini") + "\n";
		dbini << "[Game Server Database]\n";
		dbini << "*Game Server section not used yet.*\n";
		dbini << "host = " + LoadOption("database", "host", "login.ini") + "\n";
		dbini << "port = " + LoadOption("database", "port", "login.ini") + "\n";
		dbini << "db = " + LoadOption("database", "db", "login.ini") + "\n";
		dbini << "user = " + LoadOption("database", "user", "login.ini") + "\n";
		dbini << "password = " + LoadOption("database", "password", "login.ini");
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

/**
* Opens a file and passes it to be tokenized
* Then it parses the tokens returned and puts them into titles and variables.
*/
void Config::Parse(const char *file_name)
{
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