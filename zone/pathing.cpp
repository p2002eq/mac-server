#include "../common/global_define.h"

#include "client.h"
#include "doors.h"
#include "pathing.h"
#include "water_map.h"
#include "zone.h"

#include <fstream>
#include <list>
#include <math.h>
#include <sstream>
#include <string.h>

#ifdef _WINDOWS
#define snprintf _snprintf
#endif

//#define PATHDEBUG 

extern Zone *zone;

float VectorDistance(glm::vec3 a, glm::vec3 b)
{
	float xdist = a.x - b.x;
	float ydist = a.y - b.y;
	float zdist = a.z - b.z;
	return sqrtf(xdist * xdist + ydist * ydist + zdist * zdist);
}

float VectorDistanceNoRoot(glm::vec3 a, glm::vec3 b)
{
	float xdist = a.x - b.x;
	float ydist = a.y - b.y;
	float zdist = a.z - b.z;
	return xdist * xdist + ydist * ydist + zdist * zdist;

}

PathManager* PathManager::LoadPathFile(const char* ZoneName)
{

	FILE *PathFile = nullptr;

	char LowerCaseZoneName[64];

	char ZonePathFileName[256];

	PathManager* Ret = nullptr;

	strn0cpy(LowerCaseZoneName, ZoneName, 64);

	_strlwr(LowerCaseZoneName);

	snprintf(ZonePathFileName, 250, MAP_DIR "/%s.path", LowerCaseZoneName);

	if ((PathFile = fopen(ZonePathFileName, "rb")))
	{
		Ret = new PathManager();

		if (Ret->loadPaths(PathFile))
		{
			Log.Out(Logs::General, Logs::Status, "Path File %s loaded.", ZonePathFileName);

		}
		else
		{
			Log.Out(Logs::General, Logs::Error, "Path File %s failed to load.", ZonePathFileName);
			safe_delete(Ret);
		}
		fclose(PathFile);
	}
	else
	{
		Log.Out(Logs::General, Logs::Error, "Path File %s not found.", ZonePathFileName);
	}

	return Ret;
}

PathManager::PathManager()
{
	PathNodes = nullptr;
	ClosedListFlag = nullptr;
	Head.PathNodeCount = 0;
	Head.version = 2;
	QuickConnectTarget = -1;
}

PathManager::~PathManager()
{
	safe_delete_array(PathNodes);
	safe_delete_array(ClosedListFlag);
}

bool PathManager::loadPaths(FILE *PathFile)
{

	char Magic[10];

	fread(&Magic, 9, 1, PathFile);

	if (strncmp(Magic, "EQEMUPATH", 9))
	{
		Log.Out(Logs::General, Logs::Error, "Bad Magic String in .path file.");
		return false;
	}

	fread(&Head, sizeof(Head), 1, PathFile);

	Log.Out(Logs::General, Logs::Status, "Path File Header: Version %ld, PathNodes %ld",
		(long)Head.version, (long)Head.PathNodeCount);

	if (Head.version != 2 && Head.version != 3 && Head.version != 4)
	{
		Log.Out(Logs::General, Logs::Error, "Unsupported path file version.");
		return false;
	}

	PathNodes = new PathNode[Head.PathNodeCount];

	fread(PathNodes, sizeof(PathNode), Head.PathNodeCount, PathFile);

	ClosedListFlag = new int[Head.PathNodeCount];

#ifdef PATHDEBUG
	PrintPathing();
#endif

	int MaxNodeID = Head.PathNodeCount - 1;

	bool PathFileValid = true;
	if (Head.version != 4) {
		SortNodes();
		ResortConnections();
	}
	for (uint32 i = 0; i < Head.PathNodeCount; ++i)
	{
		if (PathNodes[i].id != i) {

			Log.Out(Logs::General, Logs::Error, "Path Node id %i Not aligned with array at %i.", PathNodes[i].id, i);
			
			PathFileValid = false;
		}
			
		for (uint32 j = 0; j < PATHNODENEIGHBOURS; ++j)
		{
			if (PathNodes[i].Neighbours[j].id > MaxNodeID)
			{
				Log.Out(Logs::General, Logs::Error, "Path Node %i, Neighbour %i (%i) out of range.", i, j, PathNodes[i].Neighbours[j].id);

				PathFileValid = false;
			}
		}
	}

	if (PathFileValid) {
		Log.Out(Logs::General, Logs::Zone_Server, "Pathfile v%d loaded.", Head.version);
		if (Head.version != 4)
			RecalcDistances();
		ResizePathingVectors();
		if (Head.version == 4)
		{
			// load path tree
			for(uint32 i = 0; i < Head.PathNodeCount; i++)
			{
				fread(&path_tree[i][0], sizeof(int16) * Head.PathNodeCount, 1, PathFile);
			}
		}
	}

	if (!PathFileValid)
	{
		safe_delete_array(PathNodes);
	}

	return PathFileValid;
}

void PathManager::PrintPathing()
{

	for (uint32 i = 0; i < Head.PathNodeCount; ++i)
	{
		printf("PathNode: %2d id %2d. (%8.3f, %8.3f, %8.3f), BestZ: %8.3f\n",
			i, PathNodes[i].id, PathNodes[i].v.x, PathNodes[i].v.y, PathNodes[i].v.z, PathNodes[i].bestz);


		if (PathNodes[i].Neighbours[0].id == -1)
		{
			printf("  NO NEIGHBOURS.\n");
			continue;
		}

		for (int j = 0; j < PATHNODENEIGHBOURS; j++)
		{
			if (PathNodes[i].Neighbours[j].id == -1)
				break;

			printf("  Neighbour: %2d, Distance %8.3f", PathNodes[i].Neighbours[j].id,
				PathNodes[i].Neighbours[j].distance);

			if (PathNodes[i].Neighbours[j].Teleport)
				printf("    ***** TELEPORT *****");

			if (PathNodes[i].Neighbours[j].DoorID >= 0)
				printf("    ***** via door %i *****", PathNodes[i].Neighbours[j].DoorID);

			printf("\n");
		}
	}
}

glm::vec3 PathManager::GetPathNodeCoordinates(int NodeNumber, bool BestZ)
{
	glm::vec3 Result;

	if (NodeNumber < Head.PathNodeCount)
	{
		Result = PathNodes[NodeNumber].v;

		if (!BestZ)
			return Result;

		Result.z = PathNodes[NodeNumber].bestz;
	}

	return Result;

}

std::deque<int> PathManager::FindRoute(int startID, int endID)
{
	Log.Out(Logs::Detail, Logs::Pathing, "FindRoute from node %i to %i", startID, endID);

	std::deque<int>Route;
	for (int i = 0; i < PATHNODENEIGHBOURS; ++i) {
		if(PathNodes[startID].Neighbours[i].id == -1)
			break;
		if(PathNodes[startID].Neighbours[i].id == endID) {
			Route.push_back(startID);
			if (PathNodes[startID].Neighbours[i].Teleport)
				Route.push_back(-1);
			Route.push_back(endID);
			return Route;
		}
	}

	memset(ClosedListFlag, 0, sizeof(int) * Head.PathNodeCount);

	std::deque<AStarNode> OpenList, ClosedList;
	AStarNode AStarEntry, CurrentNode;

	AStarEntry.PathNodeID = startID;
	AStarEntry.Parent = -1;
	AStarEntry.HCost = 0;
	AStarEntry.GCost = 0;
	AStarEntry.Teleport = false;

	OpenList.push_back(AStarEntry);

	while (OpenList.size() > 0)
	{
		// The OpenList is maintained in sorted order, lowest to highest cost.

		CurrentNode = (*OpenList.begin());

		ClosedList.push_back(CurrentNode);

		ClosedListFlag[CurrentNode.PathNodeID] = true;

		OpenList.pop_front();

		for (int i = 0; i < PATHNODENEIGHBOURS; ++i)
		{
			if (PathNodes[CurrentNode.PathNodeID].Neighbours[i].id == -1)
				break;

			if (PathNodes[CurrentNode.PathNodeID].Neighbours[i].id == CurrentNode.Parent)
				continue;

			if (PathNodes[CurrentNode.PathNodeID].Neighbours[i].id == endID)
			{
				Route.push_back(CurrentNode.PathNodeID);

				Route.push_back(endID);

				std::deque<AStarNode>::iterator RouteIterator;

				while (CurrentNode.PathNodeID != startID)
				{
					for (RouteIterator = ClosedList.begin(); RouteIterator != ClosedList.end(); ++RouteIterator)
					{
						if ((*RouteIterator).PathNodeID == CurrentNode.Parent)
						{
							if (CurrentNode.Teleport)
								Route.insert(Route.begin(), -1);

							CurrentNode = (*RouteIterator);

							Route.insert(Route.begin(), CurrentNode.PathNodeID);

							break;
						}
					}
				}

				return Route;
			}
			if (ClosedListFlag[PathNodes[CurrentNode.PathNodeID].Neighbours[i].id])
				continue;

			AStarEntry.PathNodeID = PathNodes[CurrentNode.PathNodeID].Neighbours[i].id;

			AStarEntry.Parent = CurrentNode.PathNodeID;

			AStarEntry.Teleport = PathNodes[CurrentNode.PathNodeID].Neighbours[i].Teleport;

			// HCost is the estimated cost to get from this node to the end.
			AStarEntry.HCost = VectorDistance(PathNodes[PathNodes[CurrentNode.PathNodeID].Neighbours[i].id].v, PathNodes[endID].v);

			AStarEntry.GCost = CurrentNode.GCost + PathNodes[CurrentNode.PathNodeID].Neighbours[i].distance;

			float FCost = AStarEntry.HCost + AStarEntry.GCost;
#ifdef PATHDEBUG
			printf("Node: %i, Open Neighbour %i has HCost %8.3f, GCost %8.3f (Total Cost: %8.3f)\n",
				CurrentNode.PathNodeID,
				PathNodes[CurrentNode.PathNodeID].Neighbours[i].id,
				AStarEntry.HCost,
				AStarEntry.GCost,
				AStarEntry.HCost + AStarEntry.GCost);
#endif

			bool AlreadyInOpenList = false;

			std::deque<AStarNode>::iterator OpenListIterator, InsertionPoint = OpenList.end();

			for (OpenListIterator = OpenList.begin(); OpenListIterator != OpenList.end(); ++OpenListIterator)
			{
				if ((*OpenListIterator).PathNodeID == PathNodes[CurrentNode.PathNodeID].Neighbours[i].id)
				{
					AlreadyInOpenList = true;

					float GCostToNode = CurrentNode.GCost + PathNodes[CurrentNode.PathNodeID].Neighbours[i].distance;

					if (GCostToNode < (*OpenListIterator).GCost)
					{
						(*OpenListIterator).Parent = CurrentNode.PathNodeID;

						(*OpenListIterator).GCost = GCostToNode;

						(*OpenListIterator).Teleport = PathNodes[CurrentNode.PathNodeID].Neighbours[i].Teleport;
					}
					break;
				}
				else if ((InsertionPoint == OpenList.end()) && (((*OpenListIterator).HCost + (*OpenListIterator).GCost) > FCost))
				{
					InsertionPoint = OpenListIterator;
				}
			}
			if (!AlreadyInOpenList)
				OpenList.insert(InsertionPoint, AStarEntry);
		}
	}
	Log.Out(Logs::Detail, Logs::Pathing, "Unable to find a route.");
	return Route;

}

std::deque<int> PathManager::FindRoutev4(int startID, int endID)
{
	std::deque<int>Route;
	int curid = endID;
	int previd = -1;
	Route.push_front(endID);
	bool tele = false;
	while(curid != startID && curid != -1)
	{
		previd = path_tree[startID][curid];
		if (previd == -1) {
			Route.clear();
		} else {
			if (teleports[previd][curid])
				Route.push_front(-1);
			Route.push_front(previd);
		}
		curid = previd;
	}
	return Route;
}

bool CheckLOSBetweenPoints(glm::vec3 start, glm::vec3 end) {

	glm::vec3 hit;

	if ((zone->zonemap) && (zone->zonemap->LineIntersectsZone(start, end, 1, &hit)))
		return false;

	return true;
}

auto path_compare = [](const PathNodeSortStruct& a, const PathNodeSortStruct& b)
{
	return a.Distance < b.Distance;
};

std::deque<int> PathManager::FindRoute(glm::vec3 Start, glm::vec3 End)
{
	Log.Out(Logs::Detail, Logs::Pathing, "FindRoute(%8.3f, %8.3f, %8.3f, %8.3f, %8.3f, %8.3f)", Start.x, Start.y, Start.z, End.x, End.y, End.z);

	std::deque<int> noderoute;

	float CandidateNodeRangeXY = RuleR(Pathing, CandidateNodeRangeXY);

	float CandidateNodeRangeZ = RuleR(Pathing, CandidateNodeRangeZ);

	// Find the nearest PathNode the Start has LOS to.
	//
	//
	int ClosestPathNodeToStart = -1;

	std::deque<PathNodeSortStruct> SortedByDistance;

	PathNodeSortStruct TempNode;

	for (uint32 i = 0; i < Head.PathNodeCount; ++i)
	{
		if ((std::abs(Start.x - PathNodes[i].v.x) <= CandidateNodeRangeXY) &&
			(std::abs(Start.y - PathNodes[i].v.y) <= CandidateNodeRangeXY) &&
			(std::abs(Start.z - PathNodes[i].v.z) <= CandidateNodeRangeZ)) {
			TempNode.id = i;
			TempNode.Distance = VectorDistanceNoRoot(Start, PathNodes[i].v);
			SortedByDistance.push_back(TempNode);

		}
	}

	std::sort(SortedByDistance.begin(), SortedByDistance.end(), path_compare);

	for (auto Iterator = SortedByDistance.begin(); Iterator != SortedByDistance.end(); ++Iterator)
	{
		Log.Out(Logs::Detail, Logs::Pathing, "Checking Reachability of Node %i from Start Position.", PathNodes[(*Iterator).id].id);

		if (!zone->zonemap->LineIntersectsZone(Start, PathNodes[(*Iterator).id].v, 1.0f, nullptr))
		{
			ClosestPathNodeToStart = (*Iterator).id;
			break;
		}
	}

	if (ClosestPathNodeToStart < 0) {
		Log.Out(Logs::Detail, Logs::Pathing, "No LOS to any starting Path Node within range.");
		return noderoute;
	}

	Log.Out(Logs::Detail, Logs::Pathing, "Closest Path Node To Start: %2d", ClosestPathNodeToStart);

	// Find the nearest PathNode the end point has LOS to

	int ClosestPathNodeToEnd = -1;

	SortedByDistance.clear();

	for (uint32 i = 0; i < Head.PathNodeCount; ++i)
	{
		if ((std::abs(End.x - PathNodes[i].v.x) <= CandidateNodeRangeXY) &&
			(std::abs(End.y - PathNodes[i].v.y) <= CandidateNodeRangeXY) &&
			(std::abs(End.z - PathNodes[i].v.z) <= CandidateNodeRangeZ)) {
			TempNode.id = i;
			TempNode.Distance = VectorDistanceNoRoot(End, PathNodes[i].v);
			SortedByDistance.push_back(TempNode);
		}
	}

	std::sort(SortedByDistance.begin(), SortedByDistance.end(), path_compare);

	for (auto Iterator = SortedByDistance.begin(); Iterator != SortedByDistance.end(); ++Iterator)
	{
		Log.Out(Logs::Detail, Logs::Pathing, "Checking Reachability of Node %i from End Position.", PathNodes[(*Iterator).id].id);
		Log.Out(Logs::Detail, Logs::Pathing, " (%8.3f, %8.3f, %8.3f) to (%8.3f, %8.3f, %8.3f)",
			End.x, End.y, End.z,
			PathNodes[(*Iterator).id].v.x, PathNodes[(*Iterator).id].v.y, PathNodes[(*Iterator).id].v.z);

		if (!zone->zonemap->LineIntersectsZone(End, PathNodes[(*Iterator).id].v, 1.0f, nullptr))
		{
			ClosestPathNodeToEnd = (*Iterator).id;
			break;
		}
	}

	if (ClosestPathNodeToEnd < 0) {
		Log.Out(Logs::Detail, Logs::Pathing, "No LOS to any end Path Node within range.");
		return noderoute;
	}

	Log.Out(Logs::Detail, Logs::Pathing, "Closest Path Node To End: %2d", ClosestPathNodeToEnd);

	if (ClosestPathNodeToStart == ClosestPathNodeToEnd)
	{
		noderoute.push_back(ClosestPathNodeToStart);
		return noderoute;
	}
	if (Head.version == 4)
		noderoute = FindRoutev4(ClosestPathNodeToStart, ClosestPathNodeToEnd);
	else
		noderoute = FindRoute(ClosestPathNodeToStart, ClosestPathNodeToEnd);

	return noderoute;
}

const char* DigitToWord(int i)
{
	switch (i) {
	case 0:
		return "zero";
	case 1:
		return "one";
	case 2:
		return "two";
	case 3:
		return "three";
	case 4:
		return "four";
	case 5:
		return "five";
	case 6:
		return "six";
	case 7:
		return "seven";
	case 8:
		return "eight";
	case 9:
		return "nine";
	}
	return "";
}

void PathManager::SpawnNode(PathNode *node)
{
	if (!node)
		return;

	NPCType* npc_type = database.GetNPCTypeTemp(RuleI(NPC, NPCTemplateID));

	if(node->id < 10)
		sprintf(npc_type->name, "%s", DigitToWord(node->id));
	else if(node->id < 100)
		sprintf(npc_type->name, "%s_%s", DigitToWord(node->id/10), DigitToWord(node->id % 10));
	else if(node->id < 1000)
		sprintf(npc_type->name, "%s_%s_%s", DigitToWord(node->id/100), DigitToWord((node->id % 100)/10), DigitToWord(((node->id % 100) %10)));
	else
		sprintf(npc_type->name, "%s_%s_%s_%s", DigitToWord(node->id/1000), DigitToWord((node->id % 1000)/100), DigitToWord(((node->id % 1000) %100) /10), DigitToWord((((node->id % 1000) %100) %10)));

	npc_type->cur_hp = 4000000;
	npc_type->max_hp = 4000000;
	npc_type->race = 151;
	npc_type->gender = 2;
	npc_type->class_ = 9;
	npc_type->deity = 1;
	npc_type->level = 75;
	npc_type->npc_id = 0;
	npc_type->loottable_id = 0;
	npc_type->texture = 1;
	npc_type->light = 0;
	npc_type->runspeed = 0;
	npc_type->d_melee_texture1 = 1;
	npc_type->d_melee_texture2 = 1;
	npc_type->merchanttype = 1;
	npc_type->bodytype = 1;

	npc_type->STR = 150;
	npc_type->STA = 150;
	npc_type->DEX = 150;
	npc_type->AGI = 150;
	npc_type->INT = 150;
	npc_type->WIS = 150;
	npc_type->CHA = 150;

	npc_type->findable = 1;
	strcpy(npc_type->special_abilities, "19,1^20,1^24,1^35,1");

	auto position = glm::vec4(node->v.x, node->v.y, node->v.z, 0.0f);
	NPC* npc = new NPC(npc_type, nullptr, position, FlyMode1);

	entity_list.AddNPC(npc, true, true);
}

void PathManager::SpawnPathNodes(float x, float y, float z, int startid)
{
	glm::vec3 around(x, y, z);
	uint32 startID = startid < Head.PathNodeCount ? startid : 0;
	for(uint32 i = startID; i < Head.PathNodeCount; ++i)
	{
		if (x != 0.0f || y!= 0.0f || z != 0.0f) {
			if (VectorDistanceNoRoot(PathNodes[i].v, around) < 40000)
				SpawnNode(&PathNodes[i]);
		} else {
			SpawnNode(&PathNodes[i]);
		}
	}
}

void PathManager::MeshTest()
{
	// This will test connectivity between all path nodes
	if (Head.version != 4)
		Optimize();
	int TotalTests = 0;
	int NoConnections = 0;
	Log.Out(Logs::General, Logs::Zone_Server, "Beginning Pathmanager connectivity tests.");

	for (uint32 i = 0; i < Head.PathNodeCount; ++i)
	{
		for (uint32 j = 0; j < Head.PathNodeCount; ++j)
		{
			if (j == i)
				continue;
			std::deque <int> Route;
			if (Head.version == 4)
				Route = FindRoutev4(PathNodes[i].id, PathNodes[j].id);
			else
				Route = FindRoute(PathNodes[i].id, PathNodes[j].id);

			if (Route.size() == 0)
			{
				++NoConnections;
				Log.Out(Logs::General, Logs::Zone_Server, "FindRoute(%i, %i) **** NO ROUTE FOUND ****", PathNodes[i].id, PathNodes[j].id);
			}
			++TotalTests;
		}
	}
	Log.Out(Logs::General, Logs::Zone_Server, "Executed %i route searches.", TotalTests);
	Log.Out(Logs::General, Logs::Zone_Server, "Failed to find %i routes.", NoConnections);
}

void PathManager::SimpleMeshTest(Client* c)
{
	// This will test connectivity between the first path node and all other nodes
	if (Head.version != 4)
		Optimize();
	int TotalTests = 0;
	int NoConnections = 0;
	int firstbad = -1;

	Log.Out(Logs::General, Logs::Zone_Server, "Beginning Pathmanager connectivity tests.");
	fflush(stdout);
	c->Message(CC_Default,"Beginning Pathmanager connectivity tests.");
	std::deque<int> Route;
	for (uint32 j = 1; j < Head.PathNodeCount; ++j)
	{
		if (Head.version == 4)
			Route = FindRoutev4(PathNodes[0].id, PathNodes[j].id);
		else
			Route = FindRoute(PathNodes[0].id, PathNodes[j].id);

		if (Route.size() == 0)
		{
			if (firstbad == -1)
				firstbad = PathNodes[j].id;
			++NoConnections;
			Log.Out(Logs::General, Logs::Zone_Server, "FindRoute[%i][%i] **** NO ROUTE FOUND ****", (int)PathNodes[0].id, (int)PathNodes[j].id);
			c->Message(CC_Default,"FindRoute From: %i To: %i  NO ROUTE FOUND ", PathNodes[0].id, PathNodes[j].id);
		}
		Route.clear();
		++TotalTests;
	}
	Log.Out(Logs::General, Logs::Zone_Server, "Executed %i route searches.", TotalTests);
	Log.Out(Logs::General, Logs::Zone_Server, "Failed to find %i routes.", NoConnections);
	if (firstbad != -1) {
		c->Message(CC_Default,"First Bad Node at %i.", firstbad);
		char Name[64];

		if (firstbad < 10)
			sprintf(Name, "%s000", DigitToWord(firstbad));
		else if (firstbad < 100)
			sprintf(Name, "%s_%s000", DigitToWord(firstbad / 10), DigitToWord(firstbad));
		else if (firstbad < 1000)
			sprintf(Name, "%s_%s_%s000", DigitToWord(firstbad / 100), DigitToWord((firstbad % 100) / 10), DigitToWord(((firstbad % 100) % 10)));
		else
			sprintf(Name, "%s_%s_%s_%s000", DigitToWord(firstbad / 1000), DigitToWord((firstbad % 1000)/100), DigitToWord(((firstbad % 1000) %100) /10), DigitToWord((((firstbad % 1000) %100) %10)));

		Mob *m = entity_list.GetMob(Name);
		if (m) {
			if (c->GetTarget()) {
				if (c->GetTarget() != m) {
					c->GetTarget()->IsTargeted(-1);
					c->SetTarget(m);
					m->IsTargeted(1);
				}
			} else {
				c->SetTarget(m);
				m->IsTargeted(1);
			}
			c->SendTargetCommand(m->GetID());
		}
	}
			
	c->Message(CC_Default,"Executed %i route searches.", TotalTests);
	c->Message(CC_Default,"Failed to find %i routes.", NoConnections);
}

glm::vec3 Mob::UpdatePath(float ToX, float ToY, float ToZ, float Speed, bool &WaypointChanged, bool &NodeReached)
{
	WaypointChanged = false;

	NodeReached = false;

	glm::vec3 NodeLoc;

	glm::vec3 From(GetX(), GetY(), GetZ());

	glm::vec3 HeadPosition(From.x, From.y, From.z + (GetSize() < 6.0 ? 6 : GetSize()) * HEAD_POSITION);

	glm::vec3 To(ToX, ToY, ToZ);

	bool SameDestination = (To == PathingDestination);

	int NextNode;

	if (To == From)
		return To;

	Log.Out(Logs::Detail, Logs::Pathing, "UpdatePath. From(%8.3f, %8.3f, %8.3f) To(%8.3f, %8.3f, %8.3f)", From.x, From.y, From.z, To.x, To.y, To.z);

	if (From == PathingLastPosition)
	{
		++PathingLoopCount;

		if ((PathingLoopCount > 5) && !IsRooted())
		{
			Log.Out(Logs::Detail, Logs::Pathing, "appears to be stuck. Teleporting them to next position.", GetName());

			if (Route.size() == 0)
			{
				Teleport(To);

				WaypointChanged = true;

				PathingLoopCount = 0;

				return To;
			}
			NodeLoc = zone->pathing->GetPathNodeCoordinates(Route.front());

			Route.pop_front();

			++PathingTraversedNodes;

			Teleport(NodeLoc);

			WaypointChanged = true;

			PathingLoopCount = 0;

			return NodeLoc;
		}
	}
	else
	{
		PathingLoopCount = 0;

		PathingLastPosition = From;
	}

	if (Route.size() > 0)
	{
		if (!SameDestination) {
						// We are already pathing, destination changed, no LOS. Find the nearest node to our destination.
			int DestinationPathNode = zone->pathing->FindNearestPathNode(To);

			// Destination unreachable via pathing, return direct route.
			if (DestinationPathNode == -1)
			{
				Log.Out(Logs::Detail, Logs::Pathing, "  Unable to find path node for new destination. Running straight to target.");
				Route.clear();
				return To;
			}
			// If the nearest path node to our new destination is the same as for the previous
			// one, we will carry on on our path.
			if (DestinationPathNode == Route.back() || DestinationPathNode == PathingLastNodeSearched)
			{
				Log.Out(Logs::Detail, Logs::Pathing, "  Same destination Node (%i). Continue with current path.", DestinationPathNode);
				SameDestination = true;
				PathingDestination = To;
			} else {
				if (Route.size() > 5) {
					std::deque<int>::iterator Last;
					Last = Route.end();
					--Last;
					if (DestinationPathNode == (*Last))
					{
						// we are one off from end.  So chop it off and continue.
						Log.Out(Logs::Detail, Logs::Pathing, "  Destination is next to last node (%i). Chopping off end.  Continue with current path.", DestinationPathNode);
						Route.erase(Last);
						SameDestination = true;
						PathingLastNodeSearched = Route.back();
						PathingDestination = To;
					}
				}
			}
		}

		// If we are already pathing, and the destination is the same as before ...
		if (SameDestination)
		{
			if (!PathingRouteUpdateTimerLong->Check() && PathingLOSState == Direct)
				return To;
			Log.Out(Logs::Detail, Logs::Pathing, "  Still pathing to the same destination.");

			// Get the coordinates of the first path node we are going to.
			NextNode = Route.front();

			NodeLoc = zone->pathing->GetPathNodeCoordinates(NextNode);

			// May need to refine this as rounding errors may mean we never have equality
			// We have reached the path node.
			if (NodeLoc == From)
			{
				Log.Out(Logs::Detail, Logs::Pathing, "  Arrived at node %i", NextNode);

				NodeReached = true;

				PathingLastNodeVisited = Route.front();
				// We only check for LOS again after traversing more than 1 node, otherwise we can get into
				// a loop where we have a hazard and so run to a path node far enough away from the hazard, and
				// then run right back towards the same hazard again.
				//
				// An exception is when we are about to head for the last node. We always check LOS then. This
				// is because we are seeking a path to the node nearest to our target. This node may be behind the
				// target, and we may run past the target if we don't check LOS at this point.
				int RouteSize = Route.size();

				Log.Out(Logs::Detail, Logs::Pathing, "Route size is %i", RouteSize);

				if ((RouteSize == 2)
					|| ((PathingTraversedNodes >= RuleI(Pathing, MinNodesTraversedForLOSCheck))
					&& (RouteSize <= RuleI(Pathing, MinNodesLeftForLOSCheck))
					&& PathingLOSCheckTimer->Check()))
				{
					Log.Out(Logs::Detail, Logs::Pathing, "  Checking distance to target.");
					float Distance = VectorDistanceNoRoot(From, To);

					Log.Out(Logs::Detail, Logs::Pathing, "  Distance between From and To (NoRoot) is %8.3f", Distance);

					if ((Distance <= RuleR(Pathing, MinDistanceForLOSCheckShort)) &&
						(std::abs(From.z - To.z) <= RuleR(Pathing, ZDiffThreshold))) {
						if (!zone->zonemap->LineIntersectsZone(HeadPosition, To, 1.0f, nullptr))
							PathingLOSState = HaveLOS;
						else
							PathingLOSState = NoLOS;
						Log.Out(Logs::Detail, Logs::Pathing, "LOSState = %s", PathingLOSState == HaveLOS ? "HaveLOS" : "NoLOS");

						if ((PathingLOSState == HaveLOS) && zone->pathing->NoHazards(From, To))
						{
							Log.Out(Logs::Detail, Logs::Pathing, "  No hazards. Running directly to target.");
							Route.clear();
							PathingLOSState = Direct;
							return To;
						}
						else
						{
							Log.Out(Logs::Detail, Logs::Pathing, "  Continuing on node path.");
						}
					}
					else
						PathingLOSState = UnknownLOS;
					PathingTraversedNodes = 0;
				}
				// We are on the same route, no LOS (or not checking this time, so pop off the node we just reached
				//
				Route.pop_front();

				++PathingTraversedNodes;

				WaypointChanged = true;

				// If there are more nodes on the route, return the coords of the next node
				if (Route.size() > 0)
				{
					NextNode = Route.front();

					if (NextNode == -1)
					{
						// -1 indicates a teleport to the next node
						Route.pop_front();

						if (Route.size() == 0)
						{
							Log.Out(Logs::Detail, Logs::Pathing, "Missing node after teleport.");
							return To;
						}

						NextNode = Route.front();

						NodeLoc = zone->pathing->GetPathNodeCoordinates(NextNode);

						Teleport(NodeLoc);

						Log.Out(Logs::Detail, Logs::Pathing, "  TELEPORTED to %8.3f, %8.3f, %8.3f\n", NodeLoc.x, NodeLoc.y, NodeLoc.z);

						Route.pop_front();

						if (Route.size() == 0)
							return To;

						NextNode = Route.front();
					}
					zone->pathing->OpenDoors(PathingLastNodeVisited, NextNode, this);

					Log.Out(Logs::Detail, Logs::Pathing, "  Now moving to node %i", NextNode);

					return zone->pathing->GetPathNodeCoordinates(NextNode);
				}
				else
				{
					// we have run all the nodes, all that is left is the direct path from the last node
					// to the destination
					Log.Out(Logs::Detail, Logs::Pathing, "  Reached end of node path, running direct to target.");
					PathingLOSState = Direct;
					return To;
				}
			}
			// At this point, we are still on the previous path, but not reached a node yet.
			// The route shouldn't be empty, but check anyway.
			//
			int RouteSize = Route.size();

			if ((PathingTraversedNodes >= RuleI(Pathing, MinNodesTraversedForLOSCheck))
				&& (RouteSize <= RuleI(Pathing, MinNodesLeftForLOSCheck))
				&& PathingLOSCheckTimer->Check())
			{
				Log.Out(Logs::Detail, Logs::Pathing, "  Checking distance to target.");

				float Distance = VectorDistanceNoRoot(From, To);

				Log.Out(Logs::Detail, Logs::Pathing, "  Distance between From and To (NoRoot) is %8.3f", Distance);

				if ((Distance <= RuleR(Pathing, MinDistanceForLOSCheckShort)) &&
					(std::abs(From.z - To.z) <= RuleR(Pathing, ZDiffThreshold))) {
					if (!zone->zonemap->LineIntersectsZone(HeadPosition, To, 1.0f, nullptr))
						PathingLOSState = HaveLOS;
					else
						PathingLOSState = NoLOS;
					Log.Out(Logs::Detail, Logs::Pathing, "NoLOS");

					if ((PathingLOSState == HaveLOS) && zone->pathing->NoHazards(From, To))
					{
						Log.Out(Logs::Detail, Logs::Pathing, "  No hazards. Running directly to target.");
						Route.clear();
						PathingLOSState = Direct;
						return To;
					}
					else
					{
						Log.Out(Logs::Detail, Logs::Pathing, "  Continuing on node path.");
					}
				}
				else
					PathingLOSState = UnknownLOS;

				PathingTraversedNodes = 0;
			}
			return NodeLoc;
		}
		else
		{
			// We get here if we were already pathing, but our destination has now changed.
			//
			if(PathingLOSState == Direct)
				PathingLOSState = UnknownLOS;
			Log.Out(Logs::Detail, Logs::Pathing, "  Target has changed position.");
			// Update our record of where we are going to.
			PathingDestination = To;
			// Check if we now have LOS etc to the new destination.
			if (PathingLOSCheckTimer->Check())
			{
				float Distance = VectorDistanceNoRoot(From, To);

				if ((Distance <= RuleR(Pathing, MinDistanceForLOSCheckShort)) &&
					(std::abs(From.z - To.z) <= RuleR(Pathing, ZDiffThreshold))) {
					Log.Out(Logs::Detail, Logs::Pathing, "  Checking for short LOS at distance %8.3f.", Distance);
					if (!zone->zonemap->LineIntersectsZone(HeadPosition, To, 1.0f, nullptr))
						PathingLOSState = HaveLOS;
					else
						PathingLOSState = NoLOS;

					Log.Out(Logs::Detail, Logs::Pathing, "NoLOS");

					if ((PathingLOSState == HaveLOS) && zone->pathing->NoHazards(From, To))
					{
						Log.Out(Logs::Detail, Logs::Pathing, "  No hazards. Running directly to target.");
						Route.clear();
						PathingLOSState = Direct;
						return To;
					}
					else
					{
						Log.Out(Logs::Detail, Logs::Pathing, "  Continuing on node path.");
					}
				}
			}

			// If the player is moving, we don't want to recalculate our route too frequently.
			//
			if (static_cast<int>(Route.size()) <= RuleI(Pathing, RouteUpdateFrequencyNodeCount))
			{
				if (!PathingRouteUpdateTimerShort->Check())
				{
					Log.Out(Logs::Detail, Logs::Pathing, "Short route update timer not yet expired.");
					return zone->pathing->GetPathNodeCoordinates(Route.front());
				}
				Log.Out(Logs::Detail, Logs::Pathing, "Short route update timer expired.");
			}
			else
			{
				if (!PathingRouteUpdateTimerLong->Check())
				{
					Log.Out(Logs::Detail, Logs::Pathing, "Long route update timer not yet expired.");
					return zone->pathing->GetPathNodeCoordinates(Route.front());
				}
				Log.Out(Logs::Detail, Logs::Pathing, "Long route update timer expired.");
			}
		}
	}
	Log.Out(Logs::Detail, Logs::Pathing, "  Our route list is empty.");
	if (SameDestination && PathingLOSState == Direct)
		return To;
	PathingLOSState = UnknownLOS;

	PathingDestination = To;

	WaypointChanged = true;

	float Distance = VectorDistanceNoRoot(From, To);

	if ((Distance <= RuleR(Pathing, MinDistanceForLOSCheckLong)) &&
		(std::abs(From.z - To.z) <= RuleR(Pathing, ZDiffThreshold))) {
		Log.Out(Logs::Detail, Logs::Pathing, "  Checking for long LOS at distance %8.3f.", Distance);

		if (!zone->zonemap->LineIntersectsZone(HeadPosition, To, 1.0f, nullptr))
			PathingLOSState = HaveLOS;
		else
			PathingLOSState = NoLOS;

		Log.Out(Logs::Detail, Logs::Pathing, "NoLOS");

		if ((PathingLOSState == HaveLOS) && zone->pathing->NoHazards(From, To))
		{
			Log.Out(Logs::Detail, Logs::Pathing, "Target is reachable. Running directly there.");
			PathingLOSState = Direct;
			return To;
		}
	}
	Log.Out(Logs::Detail, Logs::Pathing, "  Calculating new route to target.");

	Route = zone->pathing->FindRoute(From, To);

	if (Route.size() == 0)
		PathingLastNodeSearched = -1;
	else
		PathingLastNodeSearched = Route.back();

	int NodesToAttemptToCull = RuleI(Pathing, CullNodesFromStart);

	if (NodesToAttemptToCull > 0)
	{
		int CulledNodes = 0;

		std::deque<int>::iterator First, Second;

		while ((Route.size() >= 2) && (CulledNodes < NodesToAttemptToCull))
		{
			First = Route.begin();

			Second = First;

			++Second;

			if ((*Second) < 0)
				break;

			if (!zone->zonemap->LineIntersectsZone(From, zone->pathing->GetPathNodeCoordinates(*Second), 1.0f, nullptr)
				&& zone->pathing->NoHazards(From, zone->pathing->GetPathNodeCoordinates(*Second)))
			{
				Route.erase(First);

				++CulledNodes;
			}
			else
				break;
		}
	}

	NodesToAttemptToCull = RuleI(Pathing, CullNodesFromEnd);

	if (NodesToAttemptToCull > 0)
	{
		int CulledNodes = 0;

		std::deque<int>::iterator First, Second;

		while ((Route.size() >= 2) && (CulledNodes < NodesToAttemptToCull))
		{
			First = Route.end();

			--First;

			Second = First;

			--Second;

			if ((*Second) < 0)
				break;

			if (!zone->zonemap->LineIntersectsZone(To, zone->pathing->GetPathNodeCoordinates(*Second), 1.0f, nullptr)
				&& zone->pathing->NoHazards(To, zone->pathing->GetPathNodeCoordinates(*Second)))
			{
				Route.erase(First);

				++CulledNodes;
			}
			else
				break;
		}
	}

	PathingTraversedNodes = 0;

	if (Route.size() == 0)
	{
		Log.Out(Logs::Detail, Logs::Pathing, "  No route available, running direct.");
		PathingLOSState = Direct;
		return To;
	}

	NodeLoc = zone->pathing->GetPathNodeCoordinates(Route.front());

	Log.Out(Logs::Detail, Logs::Pathing, "  New route determined, heading for node %i", Route.front());

	PathingLoopCount = 0;

	return NodeLoc;

}

int PathManager::FindNearestPathNode(glm::vec3 Position)
{

	// Find the nearest PathNode we have LOS to.
	//
	//

	float CandidateNodeRangeXY = RuleR(Pathing, CandidateNodeRangeXY);

	float CandidateNodeRangeZ = RuleR(Pathing, CandidateNodeRangeZ);

	int ClosestPathNodeToStart = -1;

	std::deque<PathNodeSortStruct> SortedByDistance;

	PathNodeSortStruct TempNode;

	for (uint32 i = 0; i < Head.PathNodeCount; ++i)
	{
		if ((std::abs(Position.x - PathNodes[i].v.x) <= CandidateNodeRangeXY) &&
			(std::abs(Position.y - PathNodes[i].v.y) <= CandidateNodeRangeXY) &&
			(std::abs(Position.z - PathNodes[i].v.z) <= CandidateNodeRangeZ)) {
			TempNode.id = i;
			TempNode.Distance = VectorDistanceNoRoot(Position, PathNodes[i].v);
			SortedByDistance.push_back(TempNode);

		}
	}

	std::sort(SortedByDistance.begin(), SortedByDistance.end(), path_compare);

	for (auto Iterator = SortedByDistance.begin(); Iterator != SortedByDistance.end(); ++Iterator)
	{
		Log.Out(Logs::Detail, Logs::Pathing, "Checking Reachability of Node %i from Start Position.", PathNodes[(*Iterator).id].id);

		if (!zone->zonemap->LineIntersectsZone(Position, PathNodes[(*Iterator).id].v, 1.0f, nullptr))
		{
			ClosestPathNodeToStart = (*Iterator).id;
			break;
		}
	}

	if (ClosestPathNodeToStart < 0) {
		Log.Out(Logs::Detail, Logs::Pathing, "No LOS to any starting Path Node within range.");
		return -1;
	}
	return ClosestPathNodeToStart;
}

bool PathManager::NoHazards(glm::vec3 From, glm::vec3 To)
{
	// Test the Z coordinate at the mid point.
	//
	glm::vec3 MidPoint((From.x + To.x) / 2, (From.y + To.y) / 2, From.z);

	float NewZ = zone->zonemap->FindBestZ(MidPoint, nullptr);

	if (std::abs(NewZ - From.z) > RuleR(Pathing, ZDiffThreshold)) {
		Log.Out(Logs::Detail, Logs::Pathing, "  HAZARD DETECTED moving from %8.3f, %8.3f, %8.3f to %8.3f, %8.3f, %8.3f. Z Change is %8.3f",
			From.x, From.y, From.z, MidPoint.x, MidPoint.y, MidPoint.z, NewZ - From.z);

		return false;
	}
	else
	{
		Log.Out(Logs::Detail, Logs::Pathing, "No HAZARD DETECTED moving from %8.3f, %8.3f, %8.3f to %8.3f, %8.3f, %8.3f. Z Change is %8.3f",
			From.x, From.y, From.z, MidPoint.x, MidPoint.y, MidPoint.z, NewZ - From.z);
	}

	return true;
}

bool PathManager::NoHazardsAccurate(glm::vec3 From, glm::vec3 To)
{
	float stepx, stepy, stepz, curx, cury, curz;
	glm::vec3 cur = From;
	float last_z = From.z;
	float step_size = 1.0;

	curx = From.x;
	cury = From.y;
	curz = From.z;

	do
	{
		stepx = (float)To.x - curx;
		stepy = (float)To.y - cury;
		stepz = (float)To.z - curz;
		float factor = sqrt(stepx*stepx + stepy*stepy + stepz*stepz);
		stepx = (stepx / factor)*step_size;
		stepy = (stepy / factor)*step_size;
		stepz = (stepz / factor)*step_size;

		glm::vec3 TestPoint(curx, cury, curz);
		float NewZ = zone->zonemap->FindBestZ(TestPoint, nullptr);
		if (std::abs(NewZ - last_z) > 5.0f) {
			Log.Out(Logs::Detail, Logs::Pathing, "  HAZARD DETECTED moving from %8.3f, %8.3f, %8.3f to %8.3f, %8.3f, %8.3f. Best Z %8.3f, Z Change is %8.3f",
				From.x, From.y, From.z, TestPoint.x, TestPoint.y, TestPoint.z, NewZ, NewZ - From.z);
			return false;
		}
		last_z = NewZ;

		if (zone->watermap)
		{
			auto from = glm::vec3(From.x, From.y, From.z);
			auto to = glm::vec3(To.x, To.y, To.z);
			if (zone->watermap->InLiquid(from) || zone->watermap->InLiquid(to))
			{
				break;
			}
			auto testPointNewZ = glm::vec3(TestPoint.x, TestPoint.y, NewZ);
			if (zone->watermap->InLiquid(testPointNewZ))
			{
				glm::vec3 TestPointWater(TestPoint.x, TestPoint.y, NewZ - 0.5f);
				glm::vec3 TestPointWaterDest = TestPointWater;
				glm::vec3 hit;
				TestPointWaterDest.z -= 500;
				float best_z2 = BEST_Z_INVALID;
				if (zone->zonemap->LineIntersectsZone(TestPointWater, TestPointWaterDest, 1.0f, &hit))
				{
					best_z2 = hit.z;
				}
				if (best_z2 == BEST_Z_INVALID)
				{
					Log.Out(Logs::Detail, Logs::Pathing, "  HAZARD DETECTED, really deep water/lava!");
					return false;
				}
				else
				{
					if (std::abs(NewZ - best_z2) > RuleR(Pathing, ZDiffThreshold)) {
						Log.Out(Logs::Detail, Logs::Pathing,
							"  HAZARD DETECTED, water is fairly deep at %8.3f units deep",
							std::abs(NewZ - best_z2));
						return false;
					}
					else
					{
						Log.Out(Logs::Detail, Logs::Pathing,
							"  HAZARD NOT DETECTED, water is shallow at %8.3f units deep",
							std::abs(NewZ - best_z2));
					}
				}
			}
			else
			{
				Log.Out(Logs::Detail, Logs::Pathing, "Hazard point not in water or lava!");
			}
		}
		else
		{
			Log.Out(Logs::Detail, Logs::Pathing, "No water map loaded for hazards!");
		}

		curx += stepx;
		cury += stepy;
		curz += stepz;

		cur.x = curx;
		cur.y = cury;
		cur.z = curz;

		if (std::abs(curx - To.x) < step_size)
			cur.x = To.x;
		if (std::abs(cury - To.y) < step_size)
			cur.y = To.y;
		if (std::abs(curz - To.z) < step_size)
			cur.z = To.z;

	} while (cur.x != To.x || cur.y != To.y || cur.z != To.z);
	return true;
}

void Mob::PrintRoute()
{

	printf("Route is : ");

	for (auto Iterator = Route.begin(); Iterator != Route.end(); ++Iterator)
	{
		printf("%i, ", (*Iterator));
	}

	printf("\n");

}

void PathManager::OpenDoors(int Node1, int Node2, Mob *ForWho)
{
	if (!ForWho || (Node1 >= Head.PathNodeCount) || (Node2 >= Head.PathNodeCount) || (Node1 < 0) || (Node2 < 0))
		return;

	for (int i = 0; i < PATHNODENEIGHBOURS; ++i)
	{
		if (PathNodes[Node1].Neighbours[i].id == -1)
			return;

		if (PathNodes[Node1].Neighbours[i].id != Node2)
			continue;

		if (PathNodes[Node1].Neighbours[i].DoorID >= 0)
		{
			Doors *d = entity_list.FindDoor(PathNodes[Node1].Neighbours[i].DoorID);

			if (d && !d->IsDoorOpen())
			{
				Log.Out(Logs::Detail, Logs::Pathing, "Opening door %i for %s", PathNodes[Node1].Neighbours[i].DoorID, ForWho->GetName());

				d->ForceOpen(ForWho);
			}
			return;
		}
	}
}

PathNode* PathManager::FindPathNodeByCoordinates(float x, float y, float z)
{
	for (uint32 i = 0; i < Head.PathNodeCount; ++i)
		if ((PathNodes[i].v.x == x) && (PathNodes[i].v.y == y) && (PathNodes[i].v.z == z))
			return &PathNodes[i];

	return nullptr;
}

PathNode* PathManager::FindPathNodeById(uint16 nodeid)
{
	for(uint32 i = 0; i < Head.PathNodeCount; ++i)
		if(PathNodes[i].id == nodeid)
			return &PathNodes[i];

	return NULL;
}

int PathManager::GetRandomPathNode()
{
	return zone->random.Int(0, Head.PathNodeCount - 1);

}

void PathManager::ShowPathNodeNeighbours(Client *c)
{
	if (!c || !c->GetTarget())
		return;


	PathNode *Node = zone->pathing->FindPathNodeByCoordinates(c->GetTarget()->GetX(), c->GetTarget()->GetY(), c->GetTarget()->GetZ());

	if (!Node)
	{
		c->Message(CC_Default, "Unable to find path node.");
		return;
	}
	c->Message(CC_Default, "Path node %4i", Node->id);

	for (uint32 i = 0; i < Head.PathNodeCount; ++i)
	{
		char Name[64];

		if (PathNodes[i].id < 10)
			sprintf(Name, "%s000", DigitToWord(PathNodes[i].id));
		else if (PathNodes[i].id < 100)
			sprintf(Name, "%s_%s000", DigitToWord(PathNodes[i].id / 10), DigitToWord(PathNodes[i].id % 10));
		else if (PathNodes[i].id < 1000)
			sprintf(Name, "%s_%s_%s000", DigitToWord(PathNodes[i].id / 100), DigitToWord((PathNodes[i].id % 100) / 10), DigitToWord(((PathNodes[i].id % 100) % 10)));
		else
			sprintf(Name, "%s_%s_%s_%s000", DigitToWord(PathNodes[i].id / 1000), DigitToWord((PathNodes[i].id % 1000)/100), DigitToWord(((PathNodes[i].id % 1000) %100) /10), DigitToWord((((PathNodes[i].id % 1000) %100) %10)));

		Mob *m = entity_list.GetMob(Name);

		if (m)
			m->SendIllusionPacket(151);
	}

	std::stringstream Neighbours;

	for (int i = 0; i < PATHNODENEIGHBOURS; ++i)
	{
		if (Node->Neighbours[i].id == -1)
			break;
		Neighbours << Node->Neighbours[i].id << ", ";

		char Name[64];

		if (Node->Neighbours[i].id < 10)
			sprintf(Name, "%s000", DigitToWord(Node->Neighbours[i].id));
		else if (Node->Neighbours[i].id < 100)
			sprintf(Name, "%s_%s000", DigitToWord(Node->Neighbours[i].id / 10), DigitToWord(Node->Neighbours[i].id % 10));
		else if (Node->Neighbours[i].id < 1000)
			sprintf(Name, "%s_%s_%s000", DigitToWord(Node->Neighbours[i].id / 100), DigitToWord((Node->Neighbours[i].id % 100) / 10), DigitToWord(((Node->Neighbours[i].id % 100) % 10)));
		else
			sprintf(Name, "%s_%s_%s_%s000", DigitToWord(Node->Neighbours[i].id / 1000), DigitToWord((Node->Neighbours[i].id % 1000)/100), DigitToWord(((Node->Neighbours[i].id % 1000) %100) /10), DigitToWord((((Node->Neighbours[i].id % 1000) %100) %10)));
		
		Mob *m = entity_list.GetMob(Name);

		if (m)
			m->SendIllusionPacket(46);
	}
	c->Message(CC_Default, "Neighbours: %s", Neighbours.str().c_str());
}

void PathManager::NodeInfo(Client *c)
{
	if (!c)
	{
		return;
	}

	if (!c->GetTarget())
	{
		c->Message(CC_Default, "Pathing v%d.", Head.version);
		c->Message(CC_Default, "Total Nodes %d.", Head.PathNodeCount);
		return;
	}

	PathNode *Node = zone->pathing->FindPathNodeByCoordinates(c->GetTarget()->GetX(), c->GetTarget()->GetY(), c->GetTarget()->GetZ());
	if (!Node)
	{
		return;
	}

	c->Message(CC_Default, "Pathing node: %i at (%.2f, %.2f, %.2f) with bestz %.2f",
		Node->id, Node->v.x, Node->v.y, Node->v.z, Node->bestz);

	bool neighbour = false;
	for (int x = 0; x < 50; ++x)
	{
		if (Node->Neighbours[x].id != -1)
		{
			if (!neighbour)
			{
				c->Message(CC_Default, "Neighbours found:");
				neighbour = true;
			}
			c->Message(CC_Default, "id: %i, distance: %.2f, door id: %i, is teleport: %i",
				Node->Neighbours[x].id, Node->Neighbours[x].distance,
				Node->Neighbours[x].DoorID, Node->Neighbours[x].Teleport);
		}
	}

	if (!neighbour)
	{
		c->Message(CC_Default, "No neighbours found!");
	}
	return;
}

void PathManager::DumpPath(std::string filename)
{
	if (Head.version != 4)
		Optimize();
	std::ofstream o_file;
	o_file.open(filename.c_str(), std::ios_base::binary | std::ios_base::trunc | std::ios_base::out);
	o_file.write("EQEMUPATH", 9);
	o_file.write((const char*)&Head, sizeof(Head));
	o_file.write((const char*)PathNodes, (sizeof(PathNode)*Head.PathNodeCount));
	if (Head.version == 4 && Head.PathNodeCount > 0) {
		for(uint32 i = 0; i < Head.PathNodeCount; i++)
		{
			o_file.write((const char*)&path_tree[i][0], sizeof(int16) * Head.PathNodeCount);
		}
	}
	o_file.close();
}

int32 PathManager::AddNode(float x, float y, float z, float best_z, int32 requested_id)
{
	int32 new_id = -1;
	if (requested_id != 0)
	{
		new_id = requested_id;
		for (uint32 i = 0; i < Head.PathNodeCount; ++i)
		{
			if (PathNodes[i].id == requested_id)
			{
				new_id = -1;
				break;
			}
		}
	}

	if (new_id == -1)
	{
		for (uint32 i = 0; i < Head.PathNodeCount; ++i)
		{
			if (PathNodes[i].id > new_id)
				new_id = PathNodes[i].id;
		}
		new_id++;
	}

	PathNode new_node;
	new_node.v.x = x;
	new_node.v.y = y;
	new_node.v.z = z;
	new_node.bestz = best_z;
	new_node.id = (uint16)new_id;
	for (int x = 0; x < PATHNODENEIGHBOURS; ++x)
	{
		new_node.Neighbours[x].id = -1;
		new_node.Neighbours[x].distance = 0.0;
		new_node.Neighbours[x].DoorID = -1;
		new_node.Neighbours[x].Teleport = 0;
	}

	Head.PathNodeCount++;
	if (Head.PathNodeCount > 1)
	{
		PathNode *t_PathNodes = new PathNode[Head.PathNodeCount];
		for (uint32 x = 0; x < (Head.PathNodeCount - 1); ++x)
		{
			t_PathNodes[x].v.x = PathNodes[x].v.x;
			t_PathNodes[x].v.y = PathNodes[x].v.y;
			t_PathNodes[x].v.z = PathNodes[x].v.z;
			t_PathNodes[x].bestz = PathNodes[x].bestz;
			t_PathNodes[x].id = PathNodes[x].id;
			for (int n = 0; n < PATHNODENEIGHBOURS; ++n)
			{
				t_PathNodes[x].Neighbours[n].distance = PathNodes[x].Neighbours[n].distance;
				t_PathNodes[x].Neighbours[n].DoorID = PathNodes[x].Neighbours[n].DoorID;
				t_PathNodes[x].Neighbours[n].id = PathNodes[x].Neighbours[n].id;
				t_PathNodes[x].Neighbours[n].Teleport = PathNodes[x].Neighbours[n].Teleport;
			}

		}

		int32 index = (Head.PathNodeCount - 1);
		t_PathNodes[index].v.x = new_node.v.x;
		t_PathNodes[index].v.y = new_node.v.y;
		t_PathNodes[index].v.z = new_node.v.z;
		t_PathNodes[index].bestz = new_node.bestz;
		t_PathNodes[index].id = new_node.id;
		for (int n = 0; n < PATHNODENEIGHBOURS; ++n)
		{
			t_PathNodes[index].Neighbours[n].distance = new_node.Neighbours[n].distance;
			t_PathNodes[index].Neighbours[n].DoorID = new_node.Neighbours[n].DoorID;
			t_PathNodes[index].Neighbours[n].id = new_node.Neighbours[n].id;
			t_PathNodes[index].Neighbours[n].Teleport = new_node.Neighbours[n].Teleport;
		}

		delete[] PathNodes;
		PathNodes = t_PathNodes;

		NPCType* npc_type = database.GetNPCTypeTemp(RuleI(NPC, NPCTemplateID));

		if (new_id < 10)
			sprintf(npc_type->name, "%s", DigitToWord(new_id));
		else if (new_id < 100)
			sprintf(npc_type->name, "%s_%s", DigitToWord(new_id / 10), DigitToWord(new_id % 10));
		else if (new_id < 1000)
			sprintf(npc_type->name, "%s_%s_%s", DigitToWord(new_id / 100), DigitToWord((new_id % 100) / 10), DigitToWord(((new_id % 100) % 10)));
		else
			sprintf(npc_type->name, "%s_%s_%s_%s", DigitToWord(new_id / 1000), DigitToWord((new_id % 1000)/100), DigitToWord(((new_id % 1000) %100) /10), DigitToWord((((new_id % 1000) %100) %10)));
		
		sprintf(npc_type->lastname, "%i", new_id);
		npc_type->cur_hp = 4000000;
		npc_type->max_hp = 4000000;
		npc_type->race = 151;
		npc_type->gender = 2;
		npc_type->class_ = 9;
		npc_type->deity = 1;
		npc_type->level = 75;
		npc_type->npc_id = 0;
		npc_type->loottable_id = 0;
		npc_type->texture = 1;
		npc_type->light = 0;
		npc_type->runspeed = 0;
		npc_type->d_melee_texture1 = 1;
		npc_type->d_melee_texture2 = 1;
		npc_type->merchanttype = 1;
		npc_type->bodytype = 1;
		npc_type->STR = 150;
		npc_type->STA = 150;
		npc_type->DEX = 150;
		npc_type->AGI = 150;
		npc_type->INT = 150;
		npc_type->WIS = 150;
		npc_type->CHA = 150;
		npc_type->findable = 1;
		strcpy(npc_type->special_abilities, "19,1^20,1^24,1^35,1");

		auto position = glm::vec4(new_node.v.x, new_node.v.y, new_node.v.z, 0.0f);
		NPC* npc = new NPC(npc_type, nullptr, position, FlyMode1);
		entity_list.AddNPC(npc, true, true);

		ResizePathingVectors();
		safe_delete_array(ClosedListFlag);
		ClosedListFlag = new int[Head.PathNodeCount];
		Head.version = 2;
		return new_id;
	}
	else
	{
		PathNodes = new PathNode[Head.PathNodeCount];
		PathNodes[0].v.x = new_node.v.x;
		PathNodes[0].v.y = new_node.v.y;
		PathNodes[0].v.z = new_node.v.z;
		PathNodes[0].bestz = new_node.bestz;
		PathNodes[0].id = new_node.id;
		for (int n = 0; n < PATHNODENEIGHBOURS; ++n)
		{
			PathNodes[0].Neighbours[n].distance = new_node.Neighbours[n].distance;
			PathNodes[0].Neighbours[n].DoorID = new_node.Neighbours[n].DoorID;
			PathNodes[0].Neighbours[n].id = new_node.Neighbours[n].id;
			PathNodes[0].Neighbours[n].Teleport = new_node.Neighbours[n].Teleport;
		}

		NPCType* npc_type = database.GetNPCTypeTemp(RuleI(NPC, NPCTemplateID));

		if (new_id < 10)
			sprintf(npc_type->name, "%s", DigitToWord(new_id));
		else if (new_id < 100)
			sprintf(npc_type->name, "%s_%s", DigitToWord(new_id / 10), DigitToWord(new_id % 10));
		else if (new_id < 1000)
			sprintf(npc_type->name, "%s_%s_%s", DigitToWord(new_id / 100), DigitToWord((new_id % 100) / 10), DigitToWord(((new_id % 100) % 10)));
		else
			sprintf(npc_type->name, "%s_%s_%s_%s", DigitToWord(new_id / 1000), DigitToWord((new_id % 1000)/100), DigitToWord(((new_id % 1000) %100) /10), DigitToWord((((new_id % 1000) %100) %10)));

		sprintf(npc_type->lastname, "%i", new_id);
		npc_type->cur_hp = 4000000;
		npc_type->max_hp = 4000000;
		npc_type->race = 151;
		npc_type->gender = 2;
		npc_type->class_ = 9;
		npc_type->deity = 1;
		npc_type->level = 75;
		npc_type->npc_id = 0;
		npc_type->loottable_id = 0;
		npc_type->texture = 1;
		npc_type->light = 0;
		npc_type->runspeed = 0;
		npc_type->d_melee_texture1 = 1;
		npc_type->d_melee_texture2 = 1;
		npc_type->merchanttype = 1;
		npc_type->bodytype = 1;
		npc_type->STR = 150;
		npc_type->STA = 150;
		npc_type->DEX = 150;
		npc_type->AGI = 150;
		npc_type->INT = 150;
		npc_type->WIS = 150;
		npc_type->CHA = 150;
		npc_type->findable = 1;
		strcpy(npc_type->special_abilities, "19,1^20,1^24,1^35,1");

		auto position = glm::vec4(new_node.v.x, new_node.v.y, new_node.v.z, 0.0f);
		NPC* npc = new NPC(npc_type, nullptr, position, FlyMode1);
		entity_list.AddNPC(npc, true, true);

		ResizePathingVectors();

		ClosedListFlag = new int[Head.PathNodeCount];
		Head.version = 2;

		return new_id;
	}
}

void PathManager::CheckNodeErrors(Client *c)
{
	if(!c)
	{
		return;
	}
	bool badnodes = false;
	if (Head.PathNodeCount > 0 && PathNodes[Head.PathNodeCount-1].id != (Head.PathNodeCount-1)) {
		c->Message(15, "The path (Head.NodeCount - 1) %d, does not match the highest node number %d.", Head.PathNodeCount - 1, PathNodes[Head.PathNodeCount-1].id);
	}
	for (uint32 j = 0; j < Head.PathNodeCount; j++) {
		bool badid = false;
		bool badv = false;
		if (PathNodes[j].id != j) {
			c->Message(15, "Suspect Node: Pathnode[%d].id (%d) != %d",j,PathNodes[j].id, j);
			badnodes = true;
		}
		if (PathNodes[j].v.x > 10000 || PathNodes[j].v.x < -10000 || PathNodes[j].v.y > 10000 || PathNodes[j].v.y < -10000 || PathNodes[j].v.z > 5000 || PathNodes[j].v.z < -2000) {
			c->Message(15, "Suspect Node Coordinates: nodeid (%d) (x=%.2f) (y=%.2f) (z=%.2f)", PathNodes[j].id, PathNodes[j].v.x, PathNodes[j].v.y, PathNodes[j].v.z);
			badnodes = true;
		}
	}
	if (badnodes)
		c->Message(13, "You must run \"#path resort nodes\" before using the path or the zone may crash.");

	return;
}

bool PathManager::DeleteNode(Client *c)
{
	if (!c)
	{
		return false;
	}

	if (!c->GetTarget())
	{
		c->Message(CC_Default, "You must target a node.");
		return false;
	}

	PathNode *Node = zone->pathing->FindPathNodeByCoordinates(c->GetTarget()->GetX(), c->GetTarget()->GetY(), c->GetTarget()->GetZ());
	if (!Node)
	{
		return false;
	}
	c->GetTarget()->Depop(false);
	return DeleteNode(Node->id);
}

bool PathManager::DeleteNode(int32 id)
{
	//if the current list is > 1 in size create a new list of size current size - 1
	//transfer all but the current node to this new list and delete our current list
	//set this new list to be our current list
	//else if the size is 1 just delete our current list and set it to zero.
	//go through and delete all ref in neighbors...

	if (Head.PathNodeCount > 1)
	{
		PathNode *t_PathNodes = new PathNode[Head.PathNodeCount - 1];
		uint32 index = 0;
		for (uint32 x = 0; x < Head.PathNodeCount; x++)
		{
			if (PathNodes[x].id != id)
			{
				t_PathNodes[index].id = PathNodes[x].id;
				t_PathNodes[index].v.x = PathNodes[x].v.x;
				t_PathNodes[index].v.y = PathNodes[x].v.y;
				t_PathNodes[index].v.z = PathNodes[x].v.z;
				t_PathNodes[index].bestz = PathNodes[x].bestz;
				for (int n = 0; n < PATHNODENEIGHBOURS; ++n)
				{
					t_PathNodes[index].Neighbours[n].distance = PathNodes[x].Neighbours[n].distance;
					t_PathNodes[index].Neighbours[n].DoorID = PathNodes[x].Neighbours[n].DoorID;
					t_PathNodes[index].Neighbours[n].id = PathNodes[x].Neighbours[n].id;
					t_PathNodes[index].Neighbours[n].Teleport = PathNodes[x].Neighbours[n].Teleport;
				}
				index++;
			}
		}
		Head.PathNodeCount--;
		delete[] PathNodes;
		PathNodes = t_PathNodes;

		for (uint32 y = 0; y < Head.PathNodeCount; ++y)
		{
			for (int n = 0; n < PATHNODENEIGHBOURS; ++n)
			{
				if (PathNodes[y].Neighbours[n].id == id)
				{
					PathNodes[y].Neighbours[n].Teleport = 0;
					PathNodes[y].Neighbours[n].DoorID = -1;
					PathNodes[y].Neighbours[n].distance = 0.0;
					PathNodes[y].Neighbours[n].id = -1;
				}
			}
		}
		ResizePathingVectors();
		safe_delete_array(ClosedListFlag);
		ClosedListFlag = new int[Head.PathNodeCount];
	}
	else
	{
		delete[] PathNodes;
		PathNodes = nullptr;
	}
	Head.version = 2;
	return true;
}

void PathManager::RecalcDistances()
{
	// update distances between nodes
		
	for(uint32 i = 0; i < Head.PathNodeCount; ++i)
	{
		for(int j=0; j<PATHNODENEIGHBOURS; j++) {
			if(PathNodes[i].Neighbours[j].id == -1)
				continue;
			PathNode* Neighbour = FindPathNodeById(PathNodes[i].Neighbours[j].id);
			if (Neighbour != NULL) {
				if (PathNodes[i].Neighbours[j].Teleport)
					PathNodes[i].Neighbours[j].distance = 0.0f;
				else
					PathNodes[i].Neighbours[j].distance = VectorDistance(Neighbour->v, PathNodes[i].v);
			} else {
				PathNodes[i].Neighbours[j].distance = 0.0f;
			}
		}
	}
	if (Head.version == 3)
		Head.version = 2;
}

void PathManager::ResizePathingVectors()
{
	// this resizes pathing vectors.
	if (Head.PathNodeCount > 0) {
		//adjust distance vector size
		path_tree.resize(Head.PathNodeCount);
		teleports.resize(Head.PathNodeCount);
		for(uint32 i = 0 ; i < Head.PathNodeCount ; ++i)
		{
	    	path_tree[i].resize(Head.PathNodeCount);
			teleports[i].resize(Head.PathNodeCount);
		}

		for (uint32 i = 0; i < Head.PathNodeCount ; ++i)
			for (uint32 j = 0; j < Head.PathNodeCount ; ++j)
				teleports[i][j] = false;


	}
}
void PathManager::Optimize()
{
	// this converts a v2 pathfile to v4
	SortNodes();
	ResortConnections();
	ResizePathingVectors();
	if (Head.PathNodeCount > 0) {
		std::vector<float> distances;
		distances.resize(Head.PathNodeCount);
		// initialize pathing tree
		for (uint32 i = 0; i < Head.PathNodeCount; i++)
		{
			for (uint32 j = 0 ; j < Head.PathNodeCount ; j++) {
				path_tree[i][j] = -1;
			}
		}
		// update teleports matrix
		for (uint32 i = 0; i < Head.PathNodeCount ; ++i) {
			for (int j = 0; j < PATHNODENEIGHBOURS; ++j) {
				if (PathNodes[i].Neighbours[j].id == -1)
					break;
				if (PathNodes[i].Neighbours[j].Teleport)
					teleports[i][PathNodes[i].Neighbours[j].id] = true;
			}
		}
		int16 closestnode = -1;
		// calculate distances and paths between nodes
		for (uint32 i = 0; i < Head.PathNodeCount ; i++) {
			memset(ClosedListFlag, 0, sizeof(int) * Head.PathNodeCount);
			for (uint32 j = 0; j < Head.PathNodeCount ; j++)
				distances[j] = 999999.0f;
			distances[PathNodes[i].id] = 0.0f;
			int count = 0;
			while (count < Head.PathNodeCount)
			{
				int mindist = 999999.0f;
				// find closest node
				for (int t = 0; t < Head.PathNodeCount; t++ ) {
					if (!ClosedListFlag[PathNodes[t].id] && mindist >= distances[PathNodes[t].id]) {
						closestnode = PathNodes[t].id;
						mindist = distances[closestnode];
					}
				}
				ClosedListFlag[closestnode] = true;
				for (int k = 0; k < PATHNODENEIGHBOURS; ++k) {
					if (PathNodes[closestnode].Neighbours[k].id == -1)
						break;
					if (distances[PathNodes[closestnode].Neighbours[k].id] > (distances[closestnode] + PathNodes[closestnode].Neighbours[k].distance)) {
						distances[PathNodes[closestnode].Neighbours[k].id] = distances[closestnode] + PathNodes[closestnode].Neighbours[k].distance;
						path_tree[i][PathNodes[closestnode].Neighbours[k].id] = closestnode;
					}
				}
				count++;
			}
		}
		Head.version = 4;
	}
}

void PathManager::ConnectNodeToNode(Client *c, int32 Node2, int32 teleport, int32 doorid)
{
	if (!c)
	{
		return;
	}

	if (!c->GetTarget())
	{
		c->Message(CC_Default, "You must target a node.");
		return;
	}

	PathNode *Node = zone->pathing->FindPathNodeByCoordinates(c->GetTarget()->GetX(), c->GetTarget()->GetY(), c->GetTarget()->GetZ());
	if (!Node)
	{
		return;
	}

	c->Message(CC_Default, "Connecting %i to %i", Node->id, Node2);

	if (doorid >= 0)
		ConnectNodeToNode(Node->id, Node2, teleport);
	else
		ConnectNodeToNode(Node->id, Node2, teleport, doorid);
}

void PathManager::ConnectNodeToNode(int32 Node1, int32 Node2, int32 teleport, int32 doorid)
{
	PathNode *a = nullptr;
	PathNode *b = nullptr;
	for (uint32 x = 0; x < Head.PathNodeCount; ++x)
	{
		if (PathNodes[x].id == Node1)
		{
			a = &PathNodes[x];
			if (b)
				break;
		}
		else if (PathNodes[x].id == Node2)
		{
			b = &PathNodes[x];
			if (a)
				break;
		}
	}

	if (a == nullptr || b == nullptr)
		return;

	bool connect_a_to_b = true;
	if (NodesConnected(a, b))
		connect_a_to_b = false;

	bool connect_b_to_a = true;
	if (NodesConnected(b, a))
		connect_b_to_a = false;


	if (connect_a_to_b)
	{
		for (int a_i = 0; a_i < PATHNODENEIGHBOURS; ++a_i)
		{
			if (a->Neighbours[a_i].id == -1)
			{
				a->Neighbours[a_i].id = b->id;
				a->Neighbours[a_i].DoorID = doorid;
				a->Neighbours[a_i].Teleport = teleport;
				if (teleport)
					a->Neighbours[a_i].distance = 0.0f;
				else
					a->Neighbours[a_i].distance = VectorDistance(a->v, b->v);
				break;
			}
		}
		Head.version = 2;
	}

	if (connect_b_to_a)
	{
		for (int b_i = 0; b_i < PATHNODENEIGHBOURS; ++b_i)
		{
			if (b->Neighbours[b_i].id == -1)
			{
				b->Neighbours[b_i].id = a->id;
				b->Neighbours[b_i].DoorID = doorid;
				b->Neighbours[b_i].Teleport = teleport;
				if (teleport)
					b->Neighbours[b_i].distance = 0.0f;
				else
					b->Neighbours[b_i].distance = VectorDistance(a->v, b->v);
				break;
			}
		}
		Head.version = 2;
	}
}

void PathManager::ConnectNode(Client *c, int32 Node2, int32 teleport, int32 doorid)
{
	if (!c)
	{
		return;
	}

	if (!c->GetTarget())
	{
		c->Message(CC_Default, "You must target a node.");
		return;
	}

	PathNode *Node = zone->pathing->FindPathNodeByCoordinates(c->GetTarget()->GetX(), c->GetTarget()->GetY(), c->GetTarget()->GetZ());
	if (!Node)
	{
		return;
	}

	c->Message(CC_Default, "Connecting %i to %i", Node->id, Node2);

	if (doorid >= 0)
		ConnectNode(Node->id, Node2, teleport);
	else
		ConnectNode(Node->id, Node2, teleport, doorid);
}

void PathManager::ConnectNode(int32 Node1, int32 Node2, int32 teleport, int32 doorid)
{
	PathNode *a = nullptr;
	PathNode *b = nullptr;
	for (uint32 x = 0; x < Head.PathNodeCount; ++x)
	{
		if (PathNodes[x].id == Node1)
		{
			a = &PathNodes[x];
			if (b)
				break;
		}
		else if (PathNodes[x].id == Node2)
		{
			b = &PathNodes[x];
			if (a)
				break;
		}
	}

	if (a == nullptr || b == nullptr)
		return;

	bool connect_a_to_b = true;
	if (NodesConnected(a, b))
		connect_a_to_b = false;

	if (connect_a_to_b)
	{
		for (int a_i = 0; a_i < PATHNODENEIGHBOURS; ++a_i)
		{
			if (a->Neighbours[a_i].id == -1)
			{
				a->Neighbours[a_i].id = b->id;
				a->Neighbours[a_i].DoorID = doorid;
				a->Neighbours[a_i].Teleport = teleport;
				if (teleport)
					a->Neighbours[a_i].distance = 0.0f;
				else
					a->Neighbours[a_i].distance = VectorDistance(a->v, b->v);
				break;
			}
		}
		Head.version = 2;
	}
}

void PathManager::DisconnectNodeToNode(Client *c, int32 Node2)
{
	if (!c)
	{
		return;
	}

	if (!c->GetTarget())
	{
		c->Message(CC_Default, "You must target a node.");
		return;
	}

	PathNode *Node = zone->pathing->FindPathNodeByCoordinates(c->GetTarget()->GetX(), c->GetTarget()->GetY(), c->GetTarget()->GetZ());
	if (!Node)
	{
		return;
	}

	DisconnectNodeToNode(Node->id, Node2);
}

void PathManager::DisconnectNodeToNode(int32 Node1, int32 Node2)
{
	PathNode *a = nullptr;
	PathNode *b = nullptr;
	for (uint32 x = 0; x < Head.PathNodeCount; ++x)
	{
		if (PathNodes[x].id == Node1)
		{
			a = &PathNodes[x];
			if (b)
				break;
		}
		else if (PathNodes[x].id == Node2)
		{
			b = &PathNodes[x];
			if (a)
				break;
		}
	}

	if (a == nullptr || b == nullptr)
		return;

	bool disconnect_a_from_b = false;
	if (NodesConnected(a, b))
		disconnect_a_from_b = true;

	bool disconnect_b_from_a = false;
	if (NodesConnected(b, a))
		disconnect_b_from_a = true;

	if (disconnect_a_from_b)
	{
		for (int a_i = 0; a_i < PATHNODENEIGHBOURS; ++a_i)
		{
			if (a->Neighbours[a_i].id == b->id)
			{
				a->Neighbours[a_i].distance = 0.0;
				a->Neighbours[a_i].DoorID = -1;
				a->Neighbours[a_i].id = -1;
				a->Neighbours[a_i].Teleport = 0;
				break;
			}
		}
		Head.version = 2;
	}

	if (disconnect_b_from_a)
	{
		for (int b_i = 0; b_i < PATHNODENEIGHBOURS; ++b_i)
		{
			if (b->Neighbours[b_i].id == a->id)
			{
				b->Neighbours[b_i].distance = 0.0;
				b->Neighbours[b_i].DoorID = -1;
				b->Neighbours[b_i].id = -1;
				b->Neighbours[b_i].Teleport = 0;
				break;
			}
		}
		Head.version = 2;
	}
}

void PathManager::MoveNode(Client *c)
{
	if (!c)
	{
		return;
	}

	if (!c->GetTarget())
	{
		c->Message(CC_Default, "You must target a node.");
		return;
	}

	PathNode *Node = zone->pathing->FindPathNodeByCoordinates(c->GetTarget()->GetX(), c->GetTarget()->GetY(), c->GetTarget()->GetZ());
	if (!Node)
	{
		return;
	}

	Node->v.x = c->GetX();
	Node->v.y = c->GetY();
	Node->v.z = c->GetZ();

	if (zone->zonemap)
	{
		glm::vec3 loc(c->GetX(), c->GetY(), c->GetZ());
		Node->bestz = zone->zonemap->FindBestZ(loc, nullptr);
	}
	else
	{
		Node->bestz = Node->v.z;
	}
	c->Message(CC_Default,"Node Moved.");
	c->GetTarget()->Teleport(Node->v);
	c->GetTarget()->SendPosition();

	// update distances to neighbours
	for (int j = 0;j<PATHNODENEIGHBOURS; j++) {
		if (Node->Neighbours[j].id != -1) {
			PathNode *Neighbour = FindPathNodeById(Node->Neighbours[j].id);
			if (Neighbour != NULL) {
				if (Node->Neighbours[j].Teleport)
					Node->Neighbours[j].distance = 0.0f;
				else
					Node->Neighbours[j].distance = VectorDistance(Node->v, Neighbour->v);
			}
		}
	}

	for(uint32 i = 0; i < Head.PathNodeCount; ++i)
	{
		if(PathNodes[i].id == Node->id)
			continue;

		for(int j=0; j<PATHNODENEIGHBOURS; j++) {
			if(PathNodes[i].Neighbours[j].id == Node->id) {
				if (PathNodes[i].Neighbours[j].Teleport)
					PathNodes[i].Neighbours[j].distance = 0.0f;
				else
					PathNodes[i].Neighbours[j].distance = VectorDistance(Node->v, PathNodes[i].v);
			}
		}

	}
	ResizePathingVectors();
	Head.version = 2;
}

void PathManager::DisconnectAll(Client *c)
{
	if (!c)
	{
		return;
	}

	if (!c->GetTarget())
	{
		c->Message(CC_Default, "You must target a node.");
		return;
	}

	PathNode *Node = zone->pathing->FindPathNodeByCoordinates(c->GetTarget()->GetX(), c->GetTarget()->GetY(), c->GetTarget()->GetZ());
	if (!Node)
	{
		return;
	}

	for (int x = 0; x < PATHNODENEIGHBOURS; ++x)
	{
		Node->Neighbours[x].distance = 0;
		Node->Neighbours[x].Teleport = 0;
		Node->Neighbours[x].DoorID = -1;
		Node->Neighbours[x].id = -1;
	}

	for (uint32 i = 0; i < Head.PathNodeCount; ++i)
	{
		if (PathNodes[i].id == Node->id)
			continue;

		for (int ix = 0; ix < PATHNODENEIGHBOURS; ++ix)
		{
			if (PathNodes[i].Neighbours[ix].id == Node->id)
			{
				PathNodes[i].Neighbours[ix].distance = 0;
				PathNodes[i].Neighbours[ix].Teleport = 0;
				PathNodes[i].Neighbours[ix].id = -1;
				PathNodes[i].Neighbours[ix].DoorID = -1;
			}
		}
	}
	ResizePathingVectors();
	Head.version = 2;
}

//checks if anything in a points to b
bool PathManager::NodesConnected(PathNode *a, PathNode *b)
{
	if (!a)
		return false;

	if (!b)
		return false;

	for (int x = 0; x < PATHNODENEIGHBOURS; ++x)
	{
		if (a->Neighbours[x].id == b->id)
			return true;
	}
	return false;
}

bool PathManager::CheckLosFN(glm::vec3 a, glm::vec3 b)
{
	if (zone->zonemap)
	{
		glm::vec3 hit;

		glm::vec3 myloc;
		glm::vec3 oloc;

		myloc.x = a.x;
		myloc.y = a.y;
		myloc.z = a.z;

		oloc.x = b.x;
		oloc.y = b.y;
		oloc.z = b.z;


		if (zone->zonemap->LineIntersectsZone(myloc, oloc, 1.0f, nullptr))
		{
			return false;
		}
	}
	return true;
}

void PathManager::ConnectNearbyNodes(PathNode *center)
{
	if (!center)
		return;

	std::deque<PathNodeSortStruct> SortedByDistance;

	PathNodeSortStruct TempNode;

	for (uint32 i = 0; i < Head.PathNodeCount; ++i)
	{
		if (center->id == PathNodes[i].id) //can't connect to ourselves.
			continue;

		for (uint32 i = 0; i < Head.PathNodeCount; ++i)
		{
			if (!NodesConnected(center, &PathNodes[i]))
			{
				TempNode.id = i;
				TempNode.Distance = VectorDistanceNoRoot(center->v, PathNodes[i].v);
				if (TempNode.Distance < 40000)
					SortedByDistance.push_back(TempNode);
			}
		}

		std::sort(SortedByDistance.begin(), SortedByDistance.end(), path_compare);

		for (auto Iterator = SortedByDistance.begin(); Iterator != SortedByDistance.end(); ++Iterator)
		{
			if (CheckLosFN(center->v, PathNodes[(*Iterator).id].v))
			{
				if (NoHazardsAccurate(center->v, PathNodes[(*Iterator).id].v))
				{
					ConnectNodeToNode(center->id, PathNodes[(*Iterator).id].id);
				}
			}
		}
		SortedByDistance.clear();
	}
}

void PathManager::ProcessNodesAndSave(std::string filename)
{
	if (zone->zonemap)
	{
		for (uint32 i = 0; i < Head.PathNodeCount; ++i)
		{
			for (int in = 0; in < PATHNODENEIGHBOURS; ++in)
			{
				PathNodes[i].Neighbours[in].distance = 0.0;
				PathNodes[i].Neighbours[in].DoorID = -1;
				PathNodes[i].Neighbours[in].id = -1;
				PathNodes[i].Neighbours[in].Teleport = 0;
			}
		}

		for (uint32 x = 0; x < Head.PathNodeCount; ++x)
		{
			ConnectNearbyNodes(&PathNodes[x]);
		}
		
		if (Head.version != 4) {
			SortNodes();
			ResortConnections();
			ResizePathingVectors();
		}
	}
	DumpPath(filename);
}

void PathManager::ResortConnections()
{
	NeighbourNode Neigh[PATHNODENEIGHBOURS];
	for (uint32 x = 0; x < Head.PathNodeCount; ++x)
	{
		int index = 0;
		for (int y = 0; y < PATHNODENEIGHBOURS; ++y)
		{
			Neigh[y].distance = 0;
			Neigh[y].DoorID = -1;
			Neigh[y].id = -1;
			Neigh[y].Teleport = 0;
		}

		for (int z = 0; z < PATHNODENEIGHBOURS; ++z)
		{
			if (PathNodes[x].Neighbours[z].id != -1)
			{
				Neigh[index].id = PathNodes[x].Neighbours[z].id;
				Neigh[index].distance = PathNodes[x].Neighbours[z].distance;
				Neigh[index].DoorID = PathNodes[x].Neighbours[z].DoorID;
				Neigh[index].Teleport = PathNodes[x].Neighbours[z].Teleport;
				index++;
			}
		}

		for (int i = 0; i < PATHNODENEIGHBOURS; ++i)
		{
			PathNodes[x].Neighbours[i].distance = 0;
			PathNodes[x].Neighbours[i].DoorID = -1;
			PathNodes[x].Neighbours[i].id = -1;
			PathNodes[x].Neighbours[i].Teleport = 0;
		}

		for (int z = 0; z < PATHNODENEIGHBOURS; ++z)
		{
			PathNodes[x].Neighbours[z].distance = Neigh[z].distance;
			PathNodes[x].Neighbours[z].DoorID = Neigh[z].DoorID;
			PathNodes[x].Neighbours[z].id = Neigh[z].id;
			PathNodes[x].Neighbours[z].Teleport = Neigh[z].Teleport;
		}
	}
	Head.version = 2;
}

void PathManager::QuickConnect(Client *c, bool set)
{
	if (!c)
	{
		return;
	}

	if (!c->GetTarget())
	{
		c->Message(CC_Default, "You must target a node.");
		return;
	}

	PathNode *Node = zone->pathing->FindPathNodeByCoordinates(c->GetTarget()->GetX(), c->GetTarget()->GetY(), c->GetTarget()->GetZ());
	if (!Node)
	{
		return;
	}

	if (set)
	{
		c->Message(CC_Default, "Setting %i to the quick connect target", Node->id);
		QuickConnectTarget = Node->id;
	}
	else
	{
		if (QuickConnectTarget >= 0)
		{
			c->Message(CC_Default, "Connecting %i to %i", Node->id, QuickConnectTarget);
			ConnectNodeToNode(QuickConnectTarget, Node->id);
		}
	}	
}

struct InternalPathSort
{
	int16 old_id;
	int16 new_id;
};

void PathManager::SortNodes()
{
	std::vector<InternalPathSort> sorted_vals;
	for (uint32 x = 0; x < Head.PathNodeCount; ++x)
	{
		InternalPathSort tmp;
		tmp.old_id = PathNodes[x].id;
		sorted_vals.push_back(tmp);
	}

	PathNode *t_PathNodes = new PathNode[Head.PathNodeCount];
	memcpy(t_PathNodes, PathNodes, sizeof(PathNode)*Head.PathNodeCount);
	for (uint32 i = 0; i < Head.PathNodeCount; ++i)
	{
		for (size_t j = 0; j < sorted_vals.size(); ++j)
		{
			if (sorted_vals[j].old_id == PathNodes[i].id)
			{
				if (i != PathNodes[i].id)
				{
					printf("Assigning new id of index %i differs from old id %i\n", i, PathNodes[i].id);
				}
				sorted_vals[j].new_id = i;
			}
		}
		t_PathNodes[i].id = i;
	}

	for (uint32 y = 0; y < Head.PathNodeCount; ++y)
	{
		for (int z = 0; z < PATHNODENEIGHBOURS; ++z)
		{
			if (PathNodes[y].Neighbours[z].id != -1)
			{
				int new_val = -1;
				for (size_t c = 0; c < sorted_vals.size(); ++c)
				{
					if (PathNodes[y].Neighbours[z].id == sorted_vals[c].old_id)
					{
						new_val = sorted_vals[c].new_id;
						break;
					}
				}
				if (new_val != -1)
				{
					if (t_PathNodes[y].Neighbours[z].id != new_val)
					{
						printf("changing neighbor value to %i from %i\n", new_val, t_PathNodes[y].Neighbours[z].id);
					}
					t_PathNodes[y].Neighbours[z].id = new_val;
				}
			}
		}
	}
	safe_delete_array(PathNodes);
	PathNodes = t_PathNodes;
	Head.version = 2;
}

