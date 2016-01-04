/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2005 EQEMu Development Team (http://eqemulator.net)

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
#include "../common/eqemu_logsys.h"
#include "../common/slack.h"
#include "clientlist.h"
#include "zoneserver.h"
#include "zonelist.h"
#include "client.h"
#include "console.h"
#include "worlddb.h"
#include "../common/string_util.h"
#include "../common/guilds.h"
#include "../common/races.h"
#include "../common/classes.h"
#include "../common/packet_dump.h"
#include "wguild_mgr.h"
#include "../zone/string_ids.h"
#include <boost/algorithm/string/join.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/algorithm/string/erase.hpp>

#include <set>

extern ConsoleList		console_list;
extern ZSList			zoneserver_list;
uint32 numplayers = 0;	//this really wants to be a member variable of ClientList...

ClientList::ClientList()
: CLStale_timer(45000)
{
	NextCLEID = 1;
}

ClientList::~ClientList() {
}

void ClientList::Process() {

	if (CLStale_timer.Check())
		CLCheckStale();

	LinkedListIterator<Client*> iterator(list);

	iterator.Reset();
	while(iterator.MoreElements()) {
		if (!iterator.GetData()->Process()) {
			struct in_addr in;
			in.s_addr = iterator.GetData()->GetIP();
			Log.Out(Logs::Detail, Logs::World_Server,"Removing client from %s:%d", inet_ntoa(in), iterator.GetData()->GetPort());
			uint32 accountid = iterator.GetData()->GetAccountID();
			iterator.RemoveCurrent();

			if(!ActiveConnection(accountid))
				database.ClearAccountActive(accountid);
		}
		else
			iterator.Advance();
	}
}

bool ClientList::ActiveConnection(uint32 account_id) {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);

	iterator.Reset();
	while(iterator.MoreElements()) {
		if (iterator.GetData()->AccountID() == account_id && iterator.GetData()->Online() > CLE_Status_Offline) {
			struct in_addr in;
			in.s_addr = iterator.GetData()->GetIP();
			Log.Out(Logs::Detail, Logs::World_Server,"Client with account %d exists on %s", iterator.GetData()->AccountID(), inet_ntoa(in));
			return true;
		}
		iterator.Advance();
	}
	return false;
}

void ClientList::CLERemoveZSRef(ZoneServer* iZS) {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);

	iterator.Reset();
	while(iterator.MoreElements()) {
		if (iterator.GetData()->Server() == iZS) {
			iterator.GetData()->ClearServer(); // calling this before LeavingZone() makes CLE not update the number of players in a zone
			iterator.GetData()->LeavingZone();
		}
		iterator.Advance();
	}
}

ClientListEntry* ClientList::GetCLE(uint32 iID) {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);

	iterator.Reset();
	while(iterator.MoreElements()) {
		if (iterator.GetData()->GetID() == iID) {
			return iterator.GetData();
		}
		iterator.Advance();
	}
	return 0;
}

//Account Limiting Code to limit the number of characters allowed on from a single account at once.
bool ClientList::EnforceSessionLimit(uint32 iLSAccountID) {

	ClientListEntry* ClientEntry = 0;

	LinkedListIterator<ClientListEntry*> iterator(clientlist, BACKWARD);

	int CharacterCount = 1;

	iterator.Reset();

	while(iterator.MoreElements()) {

		ClientEntry = iterator.GetData();

		if ((ClientEntry->LSAccountID() == iLSAccountID) &&
			((ClientEntry->Admin() <= (RuleI(World, ExemptAccountLimitStatus))) || (RuleI(World, ExemptAccountLimitStatus) < 0))) 
		{

			if(strlen(ClientEntry->name()) && !ClientEntry->LD()) 
			{
				CharacterCount++;
			}

			if (CharacterCount > (RuleI(World, AccountSessionLimit)))
			{
				Log.Out(Logs::Detail, Logs::World_Server,"LSAccount %d has a CharacterCount of: %d.", iLSAccountID, CharacterCount);
				return true;
			}
		}
		iterator.Advance();
	}

	return false;
}


//Check current CLE Entry IPs against incoming connection

void ClientList::GetCLEIP(uint32 iIP) {

	ClientListEntry* countCLEIPs = 0;
	LinkedListIterator<ClientListEntry*> iterator(clientlist);

	int IPInstances = 0;
	iterator.Reset();

	while(iterator.MoreElements()) {

		countCLEIPs = iterator.GetData();
		int exemptcount = database.CheckExemption(iterator.GetData()->AccountID());

		// If the IP matches, and the connection admin status is below the exempt status,
		// or exempt status is less than 0 (no-one is exempt)
		if ((countCLEIPs->GetIP() == iIP) &&
			((countCLEIPs->Admin() < (RuleI(World, ExemptMaxClientsStatus))) ||
			(RuleI(World, ExemptMaxClientsStatus) < 0))) {

			// Increment the occurrences of this IP address
			IPInstances++;

			// If the number of connections exceeds the lower limit divided by number of exemptions allowed.
			// Set exemptions in account/ip_exemption_multiplier, default is 1.
			// 1 means 1 set of MaxClientsPerIP online allowed.
			// example MaxClientsPerIP set to 3 and ip_exemption_multiplier 1, only 3 accounts will be allowed.
			// Whereas MaxClientsPerIP set to 3 and ip_exemption_multiplier 2 = a max of 6 accounts allowed.
			if (IPInstances / exemptcount > (RuleI(World, MaxClientsPerIP))) {

				// If MaxClientsSetByStatus is set to True, override other IP Limit Rules
				if (RuleB(World, MaxClientsSetByStatus)) {

					// The IP Limit is set by the status of the account if status > MaxClientsPerIP
					if (IPInstances > countCLEIPs->Admin()) {

						if(RuleB(World, IPLimitDisconnectAll)) {
							DisconnectByIP(iIP);
							return;
						} else {
							// Remove the connection
							countCLEIPs->SetOnline(CLE_Status_Offline);
							iterator.RemoveCurrent();
							continue;
						}
					}
				}
				// Else if the Admin status of the connection is not eligible for the higher limit,
				// or there is no higher limit (AddMaxClientStatus<0)
				else if ((countCLEIPs->Admin() < (RuleI(World, AddMaxClientsStatus)) ||
						(RuleI(World, AddMaxClientsStatus) < 0))) {

					if(RuleB(World, IPLimitDisconnectAll)) {
						DisconnectByIP(iIP);
						return;
					} else {
						// Remove the connection
						countCLEIPs->SetOnline(CLE_Status_Offline);
						iterator.RemoveCurrent();
						continue;
					}
				}
				// else they are eligible for the higher limit, but if they exceed that
				else if (IPInstances > RuleI(World, AddMaxClientsPerIP)) {

					if(RuleB(World, IPLimitDisconnectAll)) {
						DisconnectByIP(iIP);
						return;
					} else {
						// Remove the connection
						countCLEIPs->SetOnline(CLE_Status_Offline);
						iterator.RemoveCurrent();
						continue;
					}
				}
			}
		}
		iterator.Advance();
	}
}

void ClientList::DisconnectByIP(uint32 iIP) {
	ClientListEntry* countCLEIPs = 0;
	LinkedListIterator<ClientListEntry*> iterator(clientlist);
	iterator.Reset();

	while(iterator.MoreElements()) {
		countCLEIPs = iterator.GetData();
		if ((countCLEIPs->GetIP() == iIP)) {
			if(strlen(countCLEIPs->name())) {
				auto pack = new ServerPacket(ServerOP_KickPlayer, sizeof(ServerKickPlayer_Struct));
				ServerKickPlayer_Struct* skp = (ServerKickPlayer_Struct*) pack->pBuffer;
				strcpy(skp->adminname, "SessionLimit");
				strcpy(skp->name, countCLEIPs->name());
				skp->adminrank = 255;
				zoneserver_list.SendPacket(pack);
				safe_delete(pack);
			}
			countCLEIPs->SetOnline(CLE_Status_Offline);
			iterator.RemoveCurrent();
			continue;
		}
		iterator.Advance();
	}
}

void ClientList::CheckAndNotifyLoggedinAccounts(uint32 iIP) {
	ClientListEntry* countCLEIPs = 0;
    LinkedListIterator<ClientListEntry*> iterator(clientlist);
	iterator.Reset();
	int IPInstances = 0;
    std::vector<std::string> AccountNames;
    while(iterator.MoreElements()) {
		countCLEIPs = iterator.GetData();
		if (countCLEIPs != nullptr && countCLEIPs->GetIP() == iIP) {
            IPInstances++;
            AccountNames.push_back(countCLEIPs->AccountName());
        }
		iterator.Advance();
    }
    // If we are > 3 they are in trouble
    if (IPInstances > 3) {
        boost::erase_all(AccountNames, boost::unique<boost::return_next_end>(AccountNames));
        std::string joinedString = boost::algorithm::join(AccountNames, ", ");
        std::string notification = StringFormat("Accounts: %s are logged in from the same IP more then 3 times", joinedString.c_str());
        Slack::SendMessageTo(Slack::CSR, notification.c_str());
    }

}

bool ClientList::CheckIPLimit(uint32 iAccID, uint32 iIP, uint16 admin, ClientListEntry* cle) {

	ClientListEntry* countCLEIPs = 0;
	LinkedListIterator<ClientListEntry*> iterator(clientlist);
	int exemptcount = database.CheckExemption(iAccID);
	int exemptadd = 0;
	if (RuleI(World, AddMaxClientsPerIP) > 0)
		exemptadd = RuleI(World, AddMaxClientsPerIP);
	int IPInstances = 1;
	iterator.Reset();

	while(iterator.MoreElements()) {

		countCLEIPs = iterator.GetData();
		
		// If the IP matches, and the connection admin status is below the exempt status,
		// or exempt status is less than 0 (no-one is exempt)
		if ((countCLEIPs != nullptr && countCLEIPs->GetIP() == iIP) &&
			((admin < (RuleI(World, ExemptMaxClientsStatus))) ||
			(RuleI(World, ExemptMaxClientsStatus) < 0))) {

			// Increment the occurrences of this IP address
			if (countCLEIPs->Online() >= CLE_Status_Zoning && (cle == nullptr || cle != countCLEIPs))
				IPInstances++;
		}
		iterator.Advance();
	}
	// Thie ip_exemption_multiplier modifies the World:MaxClientsPerIP
	// example MaxClientsPerIP set to 3 and ip_exemption_multiplier 1, only 3 accounts will be allowed.
	// Whereas MaxClientsPerIP set to 3 and ip_exemption_multiplier 2 = a max of 6 accounts allowed.
	if (IPInstances > (exemptcount * (RuleI(World, MaxClientsPerIP)))) {

		// If MaxClientsSetByStatus is set to True, override other IP Limit Rules
		if (RuleB(World, MaxClientsSetByStatus)) {

			// The IP Limit is set by the status of the account if status > MaxClientsPerIP
			if (IPInstances > admin) {
				return false;
			}
		}
		// Else if the Admin status of the connection is not eligible for the higher limit,
		// or there is no higher limit (AddMaxClientStatus<0)
		else if ((admin < (RuleI(World, AddMaxClientsPerIP)) ||
				(RuleI(World, AddMaxClientsStatus) < 0) || (RuleI(World, AddMaxClientsPerIP) < 0))) {
			return false;
		}
		// else they are eligible for the higher limit, but if they exceed that
		else if (IPInstances > (exemptcount * (RuleI(World, MaxClientsPerIP) + RuleI(World, AddMaxClientsPerIP)))) {

			return false;
		}
	}
	return true;
}

bool ClientList::CheckAccountActive(uint32 iAccID, ClientListEntry *cle) {

	ClientListEntry* countCLEIPs = 0;
	LinkedListIterator<ClientListEntry*> iterator(clientlist);
	iterator.Reset();

	while(iterator.MoreElements()) {
		if (iterator.GetData()->AccountID() == iAccID && iterator.GetData()->Online() >= CLE_Status_Zoning && (cle == nullptr || cle != iterator.GetData())) {
			return true;
		}
		iterator.Advance();
	}
	return false;
}

ClientListEntry* ClientList::FindCharacter(const char* name) {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);

	iterator.Reset();
	while(iterator.MoreElements())
	{
		if (strcasecmp(iterator.GetData()->name(), name) == 0) {
			return iterator.GetData();
		}
		iterator.Advance();
	}
	return 0;
}


ClientListEntry* ClientList::FindCLEByAccountID(uint32 iAccID) {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);

	iterator.Reset();
	while(iterator.MoreElements()) {
		if (iterator.GetData()->AccountID() == iAccID) {
			return iterator.GetData();
		}
		iterator.Advance();
	}
	return 0;
}

ClientListEntry* ClientList::FindCLEByCharacterID(uint32 iCharID) {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);

	iterator.Reset();
	while(iterator.MoreElements()) {
		if (iterator.GetData()->CharID() == iCharID) {
			return iterator.GetData();
		}
		iterator.Advance();
	}
	return 0;
}

void ClientList::SendCLEList(const int16& admin, const char* to, WorldTCPConnection* connection, const char* iName) {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);
	char* output = 0;
	uint32 outsize = 0, outlen = 0;
	int x = 0, y = 0;
	int namestrlen = iName == 0 ? 0 : strlen(iName);
	bool addnewline = false;
	char newline[3];
	if (connection->IsConsole())
		strcpy(newline, "\r\n");
	else
		strcpy(newline, "^");

	iterator.Reset();
	while(iterator.MoreElements()) {
		ClientListEntry* cle = iterator.GetData();
		if (admin >= cle->Admin() && (iName == 0 || namestrlen == 0 || strncasecmp(cle->name(), iName, namestrlen) == 0 || strncasecmp(cle->AccountName(), iName, namestrlen) == 0 || strncasecmp(cle->LSName(), iName, namestrlen) == 0)) {
			struct in_addr in;
			in.s_addr = cle->GetIP();
			if (addnewline) {
				AppendAnyLenString(&output, &outsize, &outlen, newline);
			}
			AppendAnyLenString(&output, &outsize, &outlen, "ID: %i  Acc# %i  AccName: %s  IP: %s", cle->GetID(), cle->AccountID(), cle->AccountName(), inet_ntoa(in));
			AppendAnyLenString(&output, &outsize, &outlen, "%s  Stale: %i  Online: %i  Admin: %i", newline, cle->GetStaleCounter(), cle->Online(), cle->Admin());
			if (cle->LSID())
				AppendAnyLenString(&output, &outsize, &outlen, "%s  LSID: %i  LSName: %s  WorldAdmin: %i", newline, cle->LSID(), cle->LSName(), cle->WorldAdmin());
			if (cle->CharID())
				AppendAnyLenString(&output, &outsize, &outlen, "%s  CharID: %i  CharName: %s  Zone: %s (%i)", newline, cle->CharID(), cle->name(), database.GetZoneName(cle->zone()), cle->zone());
			if (outlen >= 3072) {
				connection->SendEmoteMessageRaw(to, 0, 0, 10, output);
				safe_delete(output);
				outsize = 0;
				outlen = 0;
				addnewline = false;
			}
			else
				addnewline = true;
			y++;
		}
		iterator.Advance();
		x++;
	}
	AppendAnyLenString(&output, &outsize, &outlen, "%s%i CLEs in memory. %i CLEs listed. numplayers = %i.", newline, x, y, numplayers);
	connection->SendEmoteMessageRaw(to, 0, 0, 10, output);
	safe_delete(output);
}


void ClientList::CLEAdd(uint32 iLSID, const char* iLoginName, const char* iLoginKey, int16 iWorldAdmin, uint32 ip, uint8 local, uint8 version) {
	auto tmp = new ClientListEntry(GetNextCLEID(), iLSID, iLoginName, iLoginKey, iWorldAdmin, ip, local, version);

	clientlist.Append(tmp);
}

void ClientList::CLCheckStale() {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);

	iterator.Reset();
	while(iterator.MoreElements()) {
		if (iterator.GetData()->CheckStale()) {
			struct in_addr in;
			in.s_addr = iterator.GetData()->GetIP();
			Log.Out(Logs::Detail, Logs::World_Server,"Removing stale client on account %d from %s", iterator.GetData()->AccountID(), inet_ntoa(in));
			uint32 accountid = iterator.GetData()->AccountID();
			iterator.RemoveCurrent();
			if(!ActiveConnection(accountid))
				database.ClearAccountActive(accountid);
		}
		else
			iterator.Advance();
	}
}

void ClientList::ClientUpdate(ZoneServer* zoneserver, ServerClientList_Struct* scl) {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);
	ClientListEntry* cle;
	iterator.Reset();
	while(iterator.MoreElements()) {
		if (iterator.GetData()->GetID() == scl->wid) {
			cle = iterator.GetData();
			if (scl->remove == 2){
				cle->LeavingZone(zoneserver, CLE_Status_Offline);
			}
			else if (scl->remove == 1)
				cle->LeavingZone(zoneserver, CLE_Status_Zoning);
			else
				cle->Update(zoneserver, scl);
			return;
		}
		iterator.Advance();
	}
	if (scl->remove == 2)
		cle = new ClientListEntry(GetNextCLEID(), zoneserver, scl, CLE_Status_Online);
	else if (scl->remove == 1)
		cle = new ClientListEntry(GetNextCLEID(), zoneserver, scl, CLE_Status_Zoning);
	else
		cle = new ClientListEntry(GetNextCLEID(), zoneserver, scl, CLE_Status_InZone);
	clientlist.Insert(cle);
	zoneserver->ChangeWID(scl->charid, cle->GetID());
}

void ClientList::CLEKeepAlive(uint32 numupdates, uint32* wid) {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);
	uint32 i;

	iterator.Reset();
	while(iterator.MoreElements()) {
		for (i=0; i<numupdates; i++) {
			if (wid[i] == iterator.GetData()->GetID())
				iterator.GetData()->KeepAlive();
		}
		iterator.Advance();
	}
}


ClientListEntry* ClientList::CheckAuth(uint32 id, const char* iKey, uint32 ip ) {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);

	iterator.Reset();
	while(iterator.MoreElements()) {
		if (iterator.GetData()->CheckAuth(id, iKey, ip))
			return iterator.GetData();
		iterator.Advance();
	}
	return 0;
}
ClientListEntry* ClientList::CheckAuth(uint32 iLSID, const char* iKey) {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);

	iterator.Reset();
	while(iterator.MoreElements()) {
		if (iterator.GetData()->CheckAuth(iLSID, iKey))
			return iterator.GetData();
		iterator.Advance();
	}
	return 0;
}

ClientListEntry* ClientList::CheckAuth(const char* iName, const char* iPassword) {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);
	MD5 tmpMD5(iPassword);

	iterator.Reset();
	while(iterator.MoreElements()) {
		if (iterator.GetData()->CheckAuth(iName, tmpMD5))
			return iterator.GetData();
		iterator.Advance();
	}
	int16 tmpadmin;

	//Log.LogDebugType(Logs::Detail, Logs::World_Server,"Login with '%s' and '%s'", iName, iPassword);

	uint32 accid = database.CheckLogin(iName, iPassword, &tmpadmin);
	if (accid) {
		uint32 lsid = 0;
		database.GetAccountIDByName(iName, &tmpadmin, &lsid);
		auto tmp = new ClientListEntry(GetNextCLEID(), lsid, iName, iPassword, tmpadmin, 0, 0, 2);
		clientlist.Append(tmp);
		return tmp;
	}
	return 0;
}

void ClientList::SendOnlineGuildMembers(uint32 FromID, uint32 GuildID)
{
	int PacketLength = 8;

	uint32 Count = 0;
	ClientListEntry* from = this->FindCLEByCharacterID(FromID);

	if(!from)
	{
		Log.Out(Logs::Detail, Logs::World_Server,"Invalid client. FromID=%i GuildID=%i", FromID, GuildID);
		return;
	}

	LinkedListIterator<ClientListEntry*> Iterator(clientlist);

	Iterator.Reset();

	while(Iterator.MoreElements())
	{
		ClientListEntry* CLE = Iterator.GetData();

		if(CLE && (CLE->GuildID() == GuildID))
		{
			PacketLength += (strlen(CLE->name()) + 5);
			++Count;
		}

		Iterator.Advance();

	}

	Iterator.Reset();

	auto pack = new ServerPacket(ServerOP_OnlineGuildMembersResponse, PacketLength);

	char *Buffer = (char *)pack->pBuffer;

	VARSTRUCT_ENCODE_TYPE(uint32, Buffer, FromID);
	VARSTRUCT_ENCODE_TYPE(uint32, Buffer, Count);

	while(Iterator.MoreElements())
	{
		ClientListEntry* CLE = Iterator.GetData();

		if(CLE && (CLE->GuildID() == GuildID))
		{
			VARSTRUCT_ENCODE_STRING(Buffer, CLE->name());
			VARSTRUCT_ENCODE_TYPE(uint32, Buffer, CLE->zone());
		}

		Iterator.Advance();
	}
	zoneserver_list.SendPacket(from->zone(), from->instance(), pack);
	safe_delete(pack);
}


void ClientList::SendWhoAll(uint32 fromid,const char* to, int16 admin, Who_All_Struct* whom, WorldTCPConnection* connection) {
	try{
	LinkedListIterator<ClientListEntry*> iterator(clientlist);
	LinkedListIterator<ClientListEntry*> countclients(clientlist);
	ClientListEntry* cle = 0;
	ClientListEntry* countcle = 0;
	//char tmpgm[25] = "";
	//char accinfo[150] = "";
	char line[300] = "";
	//char tmpguild[50] = "";
	//char LFG[10] = "";
	//uint32 x = 0;
	int whomlen = 0;
	if (whom) {
		whomlen = strlen(whom->whom);
		if(whom->wrace == 0x1A) // 0x001A is the old Froglok race number and is sent by the client for /who all froglok
			whom->wrace = FROGLOK; // This is what EQEmu uses for the Froglok Race number.
	}

	char* output = 0;
	uint32 outsize = 0, outlen = 0;
	uint16 totalusers=0;
	uint16 totallength=0;
	uint8 gmwholist = RuleI(GM, GMWhoList);
	AppendAnyLenString(&output, &outsize, &outlen, "Players on server:");
	if (connection->IsConsole())
		AppendAnyLenString(&output, &outsize, &outlen, "\r\n");
	else
		AppendAnyLenString(&output, &outsize, &outlen, "\n");
	countclients.Reset();
	while(countclients.MoreElements()){
		countcle = countclients.GetData();
		if(WhoAllFilter(countcle, whom, admin, whomlen))
		{
			if((countcle->Anon()>0 && admin>=countcle->Admin() && admin>0) || countcle->Anon()==0 )
			{
				totalusers++;
				if (totalusers <= 20 || admin >= gmwholist)
					totallength=totallength+strlen(countcle->name())+strlen(countcle->AccountName())+strlen(guild_mgr.GetGuildName(countcle->GuildID()))+5;
			}
			else if((countcle->Anon()>0 && admin<=countcle->Admin()) || (countcle->Anon()==0 && !countcle->GetGM())) 
			{
				totalusers++;
				if (totalusers <= 20 || admin >= gmwholist)
					totallength=totallength+strlen(countcle->name())+strlen(guild_mgr.GetGuildName(countcle->GuildID()))+5;
			}
		}
		countclients.Advance();
	}
	uint16 plid=fromid;
	uint16 playerineqstring=5001;
	const char line2[]="---------------------------";
	uint8 unknown35=0x0A;
	uint16 unknown36=0;
	uint16 playersinzonestring=5028;
	if (totalusers>20 && admin<gmwholist){
		totalusers=20;
		playersinzonestring=5033;
	}
	else if(totalusers>1)
		playersinzonestring=5036;
	uint16 unknown44[5];
	unknown44[0]=0;
	unknown44[1]=0;
	unknown44[2]=0;
	unknown44[3]=0;
	unknown44[4]=0;
	uint32 unknown52=totalusers;
	uint32 unknown56=1;
	auto pack2 = new ServerPacket(ServerOP_WhoAllReply,58+totallength+(30*totalusers));
	memset(pack2->pBuffer,0,pack2->size);
	uchar *buffer=pack2->pBuffer;
	uchar *bufptr=buffer;
	//memset(buffer,0,pack2->size);
	memcpy(bufptr,&plid, sizeof(uint32));
	bufptr+=sizeof(uint32);
	memcpy(bufptr,&playerineqstring, sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&line2, strlen(line2));
	bufptr+=strlen(line2);
	memcpy(bufptr,&unknown35, sizeof(uint8));
	bufptr+=sizeof(uint8);
	memcpy(bufptr,&unknown36, sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&playersinzonestring, sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&unknown44[0], sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&unknown44[1], sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&unknown44[2], sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&unknown44[3], sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&unknown44[4], sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&unknown52, sizeof(uint32));
	bufptr+=sizeof(uint32);
	memcpy(bufptr,&unknown56, sizeof(uint32));
	bufptr+=sizeof(uint32);
	memcpy(bufptr,&totalusers, sizeof(uint16));
	bufptr+=sizeof(uint16);

	iterator.Reset();
	int idx=-1;
	while(iterator.MoreElements()) {
		cle = iterator.GetData();
		if(WhoAllFilter(cle, whom, admin, whomlen))
		{
			line[0] = 0;
			uint16 rankstring=0xFFFF;
			if ((cle->Anon() == 1 && cle->GetGM() && cle->Admin()>admin) || (idx >= 20 && admin<gmwholist)){ //hide gms that are anon from lesser gms and normal players, cut off at 20
					rankstring=0;
					iterator.Advance();
					continue;
				} else if (cle->GetGM()) {
					if (cle->Admin() >=250)
						rankstring=5021;
					else if (cle->Admin() >= 200)
						rankstring=5020;
					else if (cle->Admin() >= 180)
						rankstring=5019;
					else if (cle->Admin() >= 170)
						rankstring=5018;
					else if (cle->Admin() >= 160)
						rankstring=5017;
					else if (cle->Admin() >= 150)
						rankstring=5016;
					else if (cle->Admin() >= 100)
						rankstring=5015;
					else if (cle->Admin() >= 95)
						rankstring=5014;
					else if (cle->Admin() >= 90)
						rankstring=5013;
					else if (cle->Admin() >= 85)
						rankstring=5012;
					else if (cle->Admin() >= 81)
						rankstring=5011;
					else if (cle->Admin() >= 80)
						rankstring=5010;
					else if (cle->Admin() >= 50)
						rankstring=5009;
					else if (cle->Admin() >= 20)
						rankstring=5008;
					else if (cle->Admin() >= 10)
						rankstring=5007;
				}
			idx++;
			char guildbuffer[67]={0};
			if (cle->GuildID() != GUILD_NONE && cle->GuildID()>0 && (cle->Anon() != 1 || admin >= cle->Admin()))
				sprintf(guildbuffer,"<%s>", guild_mgr.GetGuildName(cle->GuildID()));
			uint16 formatstring=WHOALL_ALL;
			if(cle->Anon()==1 && (admin<cle->Admin() || admin==0))
				formatstring=WHOALL_ANON;
			else if(cle->Anon()==1 && admin>=cle->Admin() && admin>0)
				formatstring=WHOALL_GM;
			else if(cle->Anon()==2 && (admin<cle->Admin() || admin==0))
				formatstring=WHOALL_ROLE;//display guild
			else if(cle->Anon()==2 && admin>=cle->Admin() && admin>0)
				formatstring=WHOALL_GM;//display everything

	//war* wars2 = (war*)pack2->pBuffer;

	uint16 plclass_=0;
	uint16 pllevel=0;
	uint16 pidstring=0xFFFF;//5003;
	uint16 plrace=0;
	uint16 zonestring=0xFFFF;
	uint32 plzone=0;
	uint16 unknown80[3];
	if(cle->Anon()==0 || (admin>=cle->Admin() && admin>0)){
		plclass_=cle->class_();
		pllevel=cle->level();
		if (admin >= gmwholist)
			pidstring=5004;
		plrace=cle->race();
		zonestring=5006;
		plzone=cle->zone();
	}


	if(admin>=cle->Admin() && admin>0)
		unknown80[0]=cle->Admin();
	else
	unknown80[0]=0xFFFF;
	unknown80[1]=0xFFFF;//1035
	unknown80[2]=0xFFFF;

	//char plstatus[20]={0};
	//sprintf(plstatus, "Status %i",cle->Admin());
	char plname[64]={0};
	strcpy(plname,cle->name());

	char placcount[30]={0};
	if(admin>=cle->Admin() && admin>0)
		strcpy(placcount,cle->AccountName());
	else if(admin>0)
		strcpy(placcount,"NA");

	memcpy(bufptr,&formatstring, sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&pidstring, sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&plname, strlen(plname)+1);
	bufptr+=strlen(plname)+1;
	memcpy(bufptr,&rankstring, sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&guildbuffer, strlen(guildbuffer)+1);
	bufptr+=strlen(guildbuffer)+1;
	memcpy(bufptr,&unknown80[0], sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&unknown80[1], sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&unknown80[2], sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&zonestring, sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&plzone, sizeof(uint32));
	bufptr+=sizeof(uint32);
	memcpy(bufptr,&plclass_, sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&pllevel, sizeof(uint16));
	bufptr+=sizeof(uint16);
	memcpy(bufptr,&plrace, sizeof(uint16));
	bufptr+=sizeof(uint16);
	uint16 ending=0;
	memcpy(bufptr,&placcount, strlen(placcount)+1);
	bufptr+=strlen(placcount)+1;
	ending=211;
	memcpy(bufptr,&ending, sizeof(uint16));
	bufptr+=sizeof(uint16);
		}
		iterator.Advance();
	}
	pack2->Deflate();
	SendPacket(to,pack2);
	safe_delete(pack2);
	safe_delete(output);
	}
	catch(...){
		Log.Out(Logs::Detail, Logs::World_Server, "Unknown error in world's SendWhoAll (probably mem error), ignoring... Player id is: %i, Name is: %s", fromid, to);
		return;
	}
}

void ClientList::SendFriendsWho(ServerFriendsWho_Struct *FriendsWho, WorldTCPConnection* connection) {

	std::vector<ClientListEntry*> FriendsCLEs;
	FriendsCLEs.reserve(100);

	char Friend_[65];

	char *FriendsPointer = FriendsWho->FriendsString;

	// FriendsString is a comma delimited list of names.

	char *Seperator = nullptr;

	Seperator = strchr(FriendsPointer, ',');
	if(!Seperator) Seperator = strchr(FriendsPointer, '\0');

	uint32 TotalLength=0;

	while(Seperator != nullptr) {

		if((Seperator - FriendsPointer) > 64) return;

		strncpy(Friend_, FriendsPointer, Seperator - FriendsPointer);
		Friend_[Seperator - FriendsPointer] = 0;

		ClientListEntry* CLE = FindCharacter(Friend_);
		if(CLE && CLE->name() && (CLE->Online() >= CLE_Status_Zoning) && !(CLE->GetGM() && CLE->Anon())) {
			FriendsCLEs.push_back(CLE);
			TotalLength += strlen(CLE->name());
			int GuildNameLength = strlen(guild_mgr.GetGuildName(CLE->GuildID()));
			if(GuildNameLength>0)
				TotalLength += (GuildNameLength + 2);
		}

		if(Seperator[0] == '\0') break;

		FriendsPointer = Seperator + 1;
		Seperator = strchr(FriendsPointer, ',');
		if(!Seperator) Seperator = strchr(FriendsPointer, '\0');
	}


	try{
		ClientListEntry* cle;
		int FriendsOnline = FriendsCLEs.size();
		int PacketLength = sizeof(WhoAllReturnStruct) + (47 * FriendsOnline) + TotalLength;
		auto pack2 = new ServerPacket(ServerOP_WhoAllReply, PacketLength);
		memset(pack2->pBuffer,0,pack2->size);
		uchar *buffer=pack2->pBuffer;
		uchar *bufptr=buffer;

		WhoAllReturnStruct *WARS = (WhoAllReturnStruct *)bufptr;

		WARS->id = FriendsWho->FromID;
		WARS->playerineqstring = 0xffff;
		strcpy(WARS->line, "");
		WARS->unknown35 = 0x0a;
		WARS->unknown36 = 0x00;

		if(FriendsCLEs.size() == 1)
			WARS->playersinzonestring = 5028; // 5028 There is %1 player in EverQuest.
		else
			WARS->playersinzonestring = 5036; // 5036 There are %1 players in EverQuest.

		WARS->unknown44[0] = 0;
		WARS->unknown44[1] = 0;
		WARS->unknown44[2] = 0;
		WARS->unknown44[3] = 0;
		WARS->unknown44[4] = 0;
		WARS->unknown52 = FriendsOnline;
		WARS->unknown56 = 1;
		WARS->playercount = FriendsOnline;

		bufptr+=sizeof(WhoAllReturnStruct);

		for(int CLEEntry = 0; CLEEntry < FriendsOnline; CLEEntry++) {

			cle = FriendsCLEs[CLEEntry];

			char GuildName[67]={0};
			if (cle->GuildID() != GUILD_NONE && cle->GuildID()>0)
				sprintf(GuildName,"<%s>", guild_mgr.GetGuildName(cle->GuildID()));
			uint16 FormatMSGID=5025; // 5025 %T1[%2 %3] %4 (%5) %6 %7 %8 %9
			if(cle->Anon()==1)
				FormatMSGID=5024; // 5024 %T1[ANONYMOUS] %2 %3
			else if(cle->Anon()==2)
				FormatMSGID=5023; // 5023 %T1[ANONYMOUS] %2 %3 %4

			uint16 PlayerClass=0;
			uint16 PlayerLevel=0;
			uint16 PlayerRace=0;
			uint16 ZoneMSGID=0xffff;
			uint16 PlayerZone=0;

			if(cle->Anon()==0) {
				PlayerClass=cle->class_();
				PlayerLevel=cle->level();
				PlayerRace=cle->race();
				ZoneMSGID=5006; // 5006 ZONE: %1
				PlayerZone=cle->zone();
			}

			char PlayerName[64]={0};
			strcpy(PlayerName,cle->name());

			WhoAllPlayerPart1* WAPP1 = (WhoAllPlayerPart1*)bufptr;

			WAPP1->FormatMSGID = FormatMSGID;
			WAPP1->PIDMSGID = 0xffff;
			strcpy(WAPP1->Name, PlayerName);

			bufptr += sizeof(WhoAllPlayerPart1) + strlen(PlayerName);
			WhoAllPlayerPart2* WAPP2 = (WhoAllPlayerPart2*)bufptr;

			WAPP2->RankMSGID = 0xffff;
			strcpy(WAPP2->Guild, GuildName);

			bufptr += sizeof(WhoAllPlayerPart2) + strlen(GuildName);
			WhoAllPlayerPart3* WAPP3 = (WhoAllPlayerPart3*)bufptr;

			WAPP3->Unknown80[0] = 0xffff;
			WAPP3->Unknown80[1] = 0xffff;
			WAPP3->Unknown80[2] = 0xffff;
			WAPP3->ZoneMSGID = ZoneMSGID;
			WAPP3->Zone = PlayerZone;
			WAPP3->Class_ = PlayerClass;
			WAPP3->Level = PlayerLevel;
			WAPP3->Race = PlayerRace;
			WAPP3->Account[0] = 0;

			bufptr += sizeof(WhoAllPlayerPart3);

			WhoAllPlayerPart4* WAPP4 = (WhoAllPlayerPart4*)bufptr;
			WAPP4->Unknown100 = 211;

			bufptr += sizeof(WhoAllPlayerPart4);

		}
		pack2->Deflate();
		SendPacket(FriendsWho->FromName,pack2);
		safe_delete(pack2);
	}
	catch(...){
		Log.Out(Logs::Detail, Logs::World_Server,"Unknown error in world's SendFriendsWho (probably mem error), ignoring...");
		return;
	}
}

void ClientList::ConsoleSendWhoAll(const char* to, int16 admin, Who_All_Struct* whom, WorldTCPConnection* connection) {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);
	ClientListEntry* cle = 0;
	char tmpgm[25] = "";
	char accinfo[150] = "";
	char line[300] = "";
	char tmpguild[50] = "";
	char LFG[10] = "";
	uint32 x = 0;
	int whomlen = 0;
	if (whom)
		whomlen = strlen(whom->whom);

	char* output = 0;
	uint32 outsize = 0, outlen = 0;
	AppendAnyLenString(&output, &outsize, &outlen, "Players on server:");
	if (connection->IsConsole())
		AppendAnyLenString(&output, &outsize, &outlen, "\r\n");
	else
		AppendAnyLenString(&output, &outsize, &outlen, "\n");
	iterator.Reset();
	while(iterator.MoreElements()) {
		cle = iterator.GetData();
		const char* tmpZone = database.GetZoneName(cle->zone());
		if (
	(cle->Online() >= CLE_Status_Zoning)
	&& (whom == 0 || (
		((cle->Admin() >= 80 && cle->GetGM()) || whom->gmlookup == 0xFFFF) &&
		(whom->lvllow == 0xFFFF || (cle->level() >= whom->lvllow && cle->level() <= whom->lvlhigh)) &&
		(whom->wclass == 0xFFFF || cle->class_() == whom->wclass) &&
		(whom->wrace == 0xFFFF || cle->race() == whom->wrace) &&
		(whom->guildid == 0xFFFF || cle->GuildID() == whom->guildid) &&
		(whomlen == 0 || (
			(tmpZone != 0 && strncasecmp(tmpZone, whom->whom, whomlen) == 0) ||
			strncasecmp(cle->name(),whom->whom, whomlen) == 0 ||
			(strncasecmp(guild_mgr.GetGuildName(cle->GuildID()), whom->whom, whomlen) == 0) ||
			(admin >= 100 && strncasecmp(cle->AccountName(), whom->whom, whomlen) == 0)
		))
	))
) {
			line[0] = 0;
// MYRA - use new (5.x) Status labels in who for telnet connection
			if (cle->Admin() >=250)
				strcpy(tmpgm, "* GM-Impossible * ");
			else if (cle->Admin() >= 200)
				strcpy(tmpgm, "* GM-Mgmt * ");
			else if (cle->Admin() >= 180)
				strcpy(tmpgm, "* GM-Coder * ");
			else if (cle->Admin() >= 170)
				strcpy(tmpgm, "* GM-Areas * ");
			else if (cle->Admin() >= 160)
				strcpy(tmpgm, "* QuestMaster * ");
			else if (cle->Admin() >= 150)
				strcpy(tmpgm, "* GM-Lead Admin * ");
			else if (cle->Admin() >= 100)
				strcpy(tmpgm, "* GM-Admin * ");
			else if (cle->Admin() >= 95)
				strcpy(tmpgm, "* GM-Staff * ");
			else if (cle->Admin() >= 90)
				strcpy(tmpgm, "* EQ Support * ");
			else if (cle->Admin() >= 85)
				strcpy(tmpgm, "* GM-Tester * ");
			else if (cle->Admin() >= 81)
				strcpy(tmpgm, "* Senior Guide * ");
			else if (cle->Admin() >= 80)
				strcpy(tmpgm, "* Guide * ");
			else if (cle->Admin() >= 50)
				strcpy(tmpgm, "* Novice Guide * ");
			else if (cle->Admin() >= 20)
				strcpy(tmpgm, "* Apprentice Guide * ");
			else if (cle->Admin() >= 10)
				strcpy(tmpgm, "* Steward * ");
			else
				tmpgm[0] = 0;
// end Myra

			if (guild_mgr.GuildExists(cle->GuildID())) {
				snprintf(tmpguild, 36, " <%s>", guild_mgr.GetGuildName(cle->GuildID()));
			} else
				tmpguild[0] = 0;

			if (cle->LFG())
				strcpy(LFG, " LFG");
			else
				LFG[0] = 0;

			if (admin >= 150 && admin >= cle->Admin()) {
				sprintf(accinfo, " AccID: %i AccName: %s LSID: %i Status: %i", cle->AccountID(), cle->AccountName(), cle->LSAccountID(), cle->Admin());
			}
			else
				accinfo[0] = 0;

			if (cle->Anon() == 2) { // Roleplay
				if (admin >= 100 && admin >= cle->Admin())
					sprintf(line, "  %s[RolePlay %i %s] %s (%s)%s zone: %s%s%s", tmpgm, cle->level(), GetEQClassName(cle->class_(),cle->level()), cle->name(), GetRaceName(cle->race()), tmpguild, tmpZone, LFG, accinfo);
				else if (cle->Admin() >= 80 && admin < 80 && cle->GetGM()) {
					iterator.Advance();
					continue;
				}
				else
					sprintf(line, "  %s[ANONYMOUS] %s%s%s%s", tmpgm, cle->name(), tmpguild, LFG, accinfo);
			}
			else if (cle->Anon() == 1) { // Anon
				if (admin >= 100 && admin >= cle->Admin())
					sprintf(line, "  %s[ANON %i %s] %s (%s)%s zone: %s%s%s", tmpgm, cle->level(), GetEQClassName(cle->class_(),cle->level()), cle->name(), GetRaceName(cle->race()), tmpguild, tmpZone, LFG, accinfo);
				else if (cle->Admin() >= 80 && cle->GetGM()) {
					iterator.Advance();
					continue;
				}
				else
					sprintf(line, "  %s[ANONYMOUS] %s%s%s", tmpgm, cle->name(), LFG, accinfo);
			}
			else
				sprintf(line, "  %s[%i %s] %s (%s)%s zone: %s%s%s", tmpgm, cle->level(), GetEQClassName(cle->class_(),cle->level()), cle->name(), GetRaceName(cle->race()), tmpguild, tmpZone, LFG, accinfo);

			AppendAnyLenString(&output, &outsize, &outlen, line);
			if (outlen >= 3584) {
				connection->SendEmoteMessageRaw(to, 0, 0, 10, output);
				safe_delete(output);
				outsize = 0;
				outlen = 0;
			}
			else {
				if (connection->IsConsole())
					AppendAnyLenString(&output, &outsize, &outlen, "\r\n");
				else
					AppendAnyLenString(&output, &outsize, &outlen, "\n");
			}
			x++;
			if (x >= 20 && admin < 80)
				break;
		}
		iterator.Advance();
	}

	if (x >= 20 && admin < 80)
		AppendAnyLenString(&output, &outsize, &outlen, "too many results...20 players shown");
	else
		AppendAnyLenString(&output, &outsize, &outlen, "%i players online", x);
	if (admin >= 150 && (whom == 0 || whom->gmlookup != 0xFFFF)) {
		if (connection->IsConsole())
			AppendAnyLenString(&output, &outsize, &outlen, "\r\n");
		else
			AppendAnyLenString(&output, &outsize, &outlen, "\n");
		console_list.SendConsoleWho(connection, to, admin, &output, &outsize, &outlen);
	}
	if (output)
		connection->SendEmoteMessageRaw(to, 0, 0, 10, output);
	safe_delete(output);
}

void ClientList::Add(Client* client) {
	list.Insert(client);
}

Client* ClientList::FindByAccountID(uint32 account_id) {
	LinkedListIterator<Client*> iterator(list);

	iterator.Reset();
	while(iterator.MoreElements()) {
		Log.Out(Logs::Detail, Logs::World_Server, "ClientList[0x%08x]::FindByAccountID(%p) iterator.GetData()[%p]", this, account_id, iterator.GetData());
		if (iterator.GetData()->GetAccountID() == account_id) {
			Client* tmp = iterator.GetData();
			return tmp;
		}
		iterator.Advance();
	}
	return 0;
}

Client* ClientList::FindByName(char* charname) {
	LinkedListIterator<Client*> iterator(list);

	iterator.Reset();
	while(iterator.MoreElements()) {
		Log.Out(Logs::Detail, Logs::World_Server, "ClientList[0x%08x]::FindByName(\"%s\") iterator.GetData()[%p]", this, charname, iterator.GetData());
		if (iterator.GetData()->GetCharName() == charname) {
			Client* tmp = iterator.GetData();
			return tmp;
		}
		iterator.Advance();
	}
	return 0;
}

Client* ClientList::Get(uint32 ip, uint16 port) {
	LinkedListIterator<Client*> iterator(list);

	iterator.Reset();
	while(iterator.MoreElements())
	{
		if (iterator.GetData()->GetIP() == ip && iterator.GetData()->GetPort() == port)
		{
			Client* tmp = iterator.GetData();
			return tmp;
		}
		iterator.Advance();
	}
	return 0;
}

void ClientList::ZoneBootup(ZoneServer* zs) {
	LinkedListIterator<Client*> iterator(list);

	iterator.Reset();
	while(iterator.MoreElements())
	{
		if (iterator.GetData()->WaitingForBootup()) {
			if (iterator.GetData()->GetZoneID() == zs->GetZoneID()
				&& iterator.GetData()->GetInstanceID() == zs->GetInstanceID()) {
				iterator.GetData()->EnterWorld(false);
			}
			else if (iterator.GetData()->WaitingForBootup() == zs->GetID()) {
				iterator.GetData()->ZoneUnavail();
			}
		}
		iterator.Advance();
	}
}

void ClientList::RemoveCLEReferances(ClientListEntry* cle) {
	LinkedListIterator<Client*> iterator(list);

	iterator.Reset();
	while(iterator.MoreElements()) {
		if (iterator.GetData()->GetCLE() == cle) {
			iterator.GetData()->SetCLE(0);
		}
		iterator.Advance();
	}
}


bool ClientList::SendPacket(const char* to, ServerPacket* pack) {
	if (to == 0 || to[0] == 0) {
		zoneserver_list.SendPacket(pack);
		return true;
	}
	else if (to[0] == '*') {
		// Cant send a packet to a console....
		return false;
	}
	else {
		ClientListEntry* cle = FindCharacter(to);
		if (cle != nullptr) {
			if (cle->Server() != nullptr) {
				cle->Server()->SendPacket(pack);
				return true;
			}
			return false;
		} else {
			ZoneServer* zs = zoneserver_list.FindByName(to);
			if (zs != nullptr) {
				zs->SendPacket(pack);
				return true;
			}
			return false;
		}
	}
	return false;
}

void ClientList::SendGuildPacket(uint32 guild_id, ServerPacket* pack) {
	std::set<uint32> zone_ids;

	LinkedListIterator<ClientListEntry*> iterator(clientlist);

	iterator.Reset();
	while(iterator.MoreElements()) {
		if (iterator.GetData()->GuildID() == guild_id) {
			zone_ids.insert(iterator.GetData()->zone());
		}
		iterator.Advance();
	}

	//now we know all the zones, send it to each one... this is kinda a shitty way to do this
	//since its basically O(n^2)
	std::set<uint32>::iterator cur, end;
	cur = zone_ids.begin();
	end = zone_ids.end();
	for(; cur != end; cur++) {
		zoneserver_list.SendPacket(*cur, pack);
	}
}

void ClientList::UpdateClientGuild(uint32 char_id, uint32 guild_id) {
	LinkedListIterator<ClientListEntry*> iterator(clientlist);

	iterator.Reset();
	while(iterator.MoreElements()) {
		ClientListEntry *cle = iterator.GetData();
		if (cle->CharID() == char_id) {
			cle->SetGuild(guild_id);
		}
		iterator.Advance();
	}
}



int ClientList::GetClientCount() {
	return(numplayers);
}

void ClientList::GetClients(const char *zone_name, std::vector<ClientListEntry *> &res) {
	LinkedListIterator<ClientListEntry *> iterator(clientlist);
	iterator.Reset();

	if(zone_name[0] == '\0') {
		while(iterator.MoreElements()) {
			ClientListEntry* tmp = iterator.GetData();
			res.push_back(tmp);
			iterator.Advance();
		}
	} else {
		uint32 zoneid = database.GetZoneID(zone_name);
		while(iterator.MoreElements()) {
			ClientListEntry* tmp = iterator.GetData();
			if(tmp->zone() == zoneid)
				res.push_back(tmp);
			iterator.Advance();
		}
	}
}

void ClientList::SendClientVersionSummary(const char *Name)
{
	uint32 ClientUnusedCount = 0;
	uint32 ClientPCCount = 0;
	uint32 ClientIntelCount = 0;
	uint32 ClientPPCCount = 0;
	uint32 ClientEvolutionCount = 0;

	LinkedListIterator<ClientListEntry*> Iterator(clientlist);

	Iterator.Reset();

	while(Iterator.MoreElements())
	{
		ClientListEntry* CLE = Iterator.GetData();

		if(CLE && CLE->zone())
		{
			switch(CLE->GetClientVersion())
			{
				case 1:
				{
					++ClientUnusedCount;
					break;
				}
				case 2:
				{
					switch(CLE->GetMacClientVersion())
					{
						case 2:
						{
							++ClientPCCount;
							break;
						}
						case 4:
						{
							++ClientIntelCount;
							break;
						}
						case 8:
						{
							++ClientPPCCount;
							break;
						}
					}
					break;
				}
				case 3:
				{
					++ClientEvolutionCount;
					break;
				}
				default:
					break;
			}
		}

		Iterator.Advance();

	}

	zoneserver_list.SendEmoteMessage(Name, 0, 0, 13, "There are %i Unused, %i PC, %i Intel, %i PPC, %i Evolution clients currently connected.",
					ClientUnusedCount, ClientPCCount, ClientIntelCount, ClientPPCCount, ClientEvolutionCount);
}


bool ClientList::WhoAllFilter(ClientListEntry* client, Who_All_Struct* whom, int16 admin, int whomlen)
{
	uint8 gmwholist = RuleI(GM, GMWhoList);
	const char* tmpZone = database.GetZoneName(client->zone());
	if (
		(client->Online() >= CLE_Status_Zoning) && // Client is zoning or in a zone
		(!client->GetGM() || client->Anon() != 1 || (admin >= client->Admin() && client->Admin() >= gmwholist)) && // Client is a GM and does not have hideme on
		(whom == 0 || (admin >= client->Admin() && client->Admin() >= gmwholist) || // Whom is 0 or we are a GM with greater or equal status
		((whom->gmlookup == -1 || client->Admin() >= gmwholist) && // Filters. Anon should return false.
		(whom->lvllow == -1 || (client->level() >= whom->lvllow && client->level() <= whom->lvlhigh && client->Anon() == 0)) && 
		(whom->wclass == -1 || (client->class_() == whom->wclass && client->Anon() == 0)) && 
		(whom->wrace == -1 || (client->race() == whom->wrace && client->Anon() == 0)) && 
		(whom->guildid == -1 || (whom->guildid >= 0 && client->Anon() != 1)))) && // This is used by who all guild#
		(whomlen == 0 || 
		((tmpZone != 0 && strncasecmp(tmpZone, whom->whom, whomlen) == 0 && client->Anon() == 0) || 
		strncasecmp(client->name(),whom->whom, whomlen) == 0 ||
		(strncasecmp(guild_mgr.GetGuildName(client->GuildID()), whom->whom, whomlen) == 0 && client->Anon() != 1) || // This is used by who all guild
		(admin >= gmwholist && strncasecmp(client->AccountName(), whom->whom, whomlen) == 0)))) // Only GMs can filter by account.
	{
		return true;
	}
		
	else
	{
		return false;
	}

}