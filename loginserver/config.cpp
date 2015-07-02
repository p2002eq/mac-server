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