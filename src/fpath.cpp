/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

/**
 * @file fpath.cpp
 */

#include "lib/framework/wzapp.h"
#include "lib/netplay/netplay.h"

#include "fpath.h"
#include "map.h"

bool isHumanPlayer(unsigned);


// If the path finding system is shutdown or not
static volatile bool fpathQuit = false;

/* Beware: Enabling this will cause significant slow-down. */
#undef DEBUG_MAP

// threading stuff
static WZ_THREAD* fpathThread = nullptr;
static WZ_MUTEX* fpathMutex = nullptr;
static WZ_SEMAPHORE* fpathSemaphore = nullptr;
using packagedPathJob = wz::packaged_task<PathResult()>;
static std::list<packagedPathJob> pathJobs;
static std::unordered_map<uint32_t, wz::future<PathResult>> pathResults;

static bool waitingForResult = false;
static uint32_t waitingForResultId;
static WZ_SEMAPHORE* waitingForResultSemaphore = nullptr;

static PathResult fpathExecute(const PathJob& psJob);


/// This runs in a separate thread
static int fpathThreadFunc(void*)
{
	wzMutexLock(fpathMutex);

	while (!fpathQuit)
	{
		if (pathJobs.empty()) {
			ASSERT(!waitingForResult, "Waiting for a result (id %u) that doesn't exist.", waitingForResultId);
			wzMutexUnlock(fpathMutex);
			wzSemaphoreWait(fpathSemaphore); // Go to sleep until needed.
			wzMutexLock(fpathMutex);
			continue;
		}

		// Copy the first job from the queue.
		packagedPathJob job = std::move(pathJobs.front());
		pathJobs.pop_front();

		wzMutexUnlock(fpathMutex);
		job();
		wzMutexLock(fpathMutex);

		waitingForResult = false;
		objTrace(waitingForResultId, "These are the droids you are looking for.");
		wzSemaphorePost(waitingForResultSemaphore);
	}
	wzMutexUnlock(fpathMutex);
	return 0;
}

// initialise the findpath module
bool fpathInitialise()
{
	// The path system is up
	fpathQuit = false;
  if (fpathThread != nullptr) return true;

  fpathMutex = wzMutexCreate();
  fpathSemaphore = wzSemaphoreCreate(0);
  waitingForResultSemaphore = wzSemaphoreCreate(0);
  fpathThread = wzThreadCreate(fpathThreadFunc, nullptr);
  wzThreadStart(fpathThread);
  return true;
}

void fpathShutdown()
{
	if (!fpathThread) {
    fpathHardTableReset();
    return;
  }

  // Signal the path finding thread to quit
  fpathQuit = true;
  wzSemaphorePost(fpathSemaphore); // Wake up thread.

  wzThreadJoin(fpathThread);
  fpathThread = nullptr;
  wzMutexDestroy(fpathMutex);
  fpathMutex = nullptr;
  wzSemaphoreDestroy(fpathSemaphore);
  fpathSemaphore = nullptr;
  wzSemaphoreDestroy(waitingForResultSemaphore);
  waitingForResultSemaphore = nullptr;
	fpathHardTableReset();
}

bool fpathIsEquivalentBlocking(PathBlockingType first, PathBlockingType second)
{
  return first.propulsion == second.propulsion &&
         first.propulsion == PROPULSION_TYPE::LIFT ||
         first == second;
}

//
//static uint8_t prop2bits(PROPULSION_TYPE propulsion)
//{
//	uint8_t bits;
//
//	switch (propulsion)
//	{
//	case PROPULSION_TYPE::LIFT:
//		bits = AIR_BLOCKED;
//		break;
//	case PROPULSION_TYPE::HOVER:
//		bits = FEATURE_BLOCKED;
//		break;
//	case PROPULSION_TYPE::PROPELLOR:
//		bits = FEATURE_BLOCKED | LAND_BLOCKED;
//		break;
//	default:
//		bits = FEATURE_BLOCKED | WATER_BLOCKED;
//		break;
//	}
//	return bits;
//}

//// Check if the map tile at a location blocks a droid
//bool fpathBaseBlockingTile(SDWORD x, SDWORD y, PROPULSION_TYPE propulsion, int mapIndex, FPATH_MOVETYPE moveType)
//{
//	/* All tiles outside of the map and on map border are blocking. */
//	if (x < 1 || y < 1 || x > mapWidth - 1 || y > mapHeight - 1)
//	{
//		return true;
//	}
//
//	/* Check scroll limits (used in campaign to partition the map. */
//	if (propulsion != PROPULSION_TYPE::LIFT && (x < scrollMinX + 1 || y < scrollMinY + 1 || x >= scrollMaxX - 1 || y >=
//		scrollMaxY - 1))
//	{
//		// coords off map - auto blocking tile
//		return true;
//	}
//	unsigned aux = auxTile(x, y, mapIndex);
//
//	int auxMask = 0;
//	switch (moveType)
//	{
//	case FMT_MOVE: auxMask = AUXBITS_NONPASSABLE;
//		break;
//	// do not wish to shoot our way through enemy buildings, but want to go through friendly gates (without shooting them)
//	case FMT_ATTACK: auxMask = AUXBITS_OUR_BUILDING;
//		break; // move blocked by friendly building, assuming we do not want to shoot it up en route
//	case FMT_BLOCK: auxMask = AUXBITS_BLOCKING;
//		break; // Do not wish to tunnel through closed gates or buildings.
//	}
//
//	unsigned unitbits = prop2bits(propulsion);
//	// TODO - cache prop2bits to psDroid, and pass in instead of propulsion type
//	if ((unitbits & FEATURE_BLOCKED) != 0 && (aux & auxMask) != 0)
//	{
//		return true; // move blocked by building, and we cannot or do not want to shoot our way through anything
//	}
//
//	// the MAX hack below is because blockTile() range does not include player-specific versions...
//	return (blockTile(x, y, MAX(0, mapIndex - MAX_PLAYERS)) & unitbits) != 0;
//	// finally check if move is blocked by propulsion related factors
//}

//bool fpathDroidBlockingTile(DROID* psDroid, int x, int y, FPATH_MOVETYPE moveType)
//{
//	return fpathBaseBlockingTile(x, y, getPropulsionStats(psDroid)->propulsionType, psDroid->player, moveType);
//}

//// Check if the map tile at a location blocks a droid
//bool fpathBlockingTile(SDWORD x, SDWORD y, PROPULSION_TYPE propulsion)
//{
//	return fpathBaseBlockingTile(x, y, propulsion, 0, FMT_BLOCK);
//	// with FMT_BLOCK, it is irrelevant which player is passed in
//}

// Returns the closest non-blocking tile to pos, or returns pos if no non-blocking
// tiles are present within a 2 tile distance.
static Position findNonblockingPosition(Position pos, PROPULSION_TYPE propulsion, unsigned player = 0,
                                        FPATH_MOVETYPE moveType = FPATH_MOVETYPE::FMT_BLOCK)
{
	auto centreTile = map_coord(pos.xy());
	if (!fpathBaseBlockingTile(centreTile, propulsion, player, moveType)) {
		return pos; // Fast case, pos is not on a blocking tile.
	}

	auto bestTile = centreTile;
	auto bestDistSq = INT32_MAX;
	for (auto y = -2; y <= 2; ++y)
		for (auto x = -2; x <= 2; ++x)
		{
			auto tile = centreTile + Vector2i(x, y);
			auto diff = world_coord(tile) + Vector2i(
              TILE_UNITS / 2, TILE_UNITS / 2) - pos.xy();

			auto distSq = dot(diff, diff);
			if (distSq < bestDistSq && !fpathBaseBlockingTile(tile, propulsion, player, moveType)) {
				bestTile = tile;
				bestDistSq = distSq;
			}
		}

	// Return point on tile closest to the original pos.
	auto minCoord = world_coord(bestTile);
	auto maxCoord = minCoord + Vector2i(TILE_UNITS - 1, TILE_UNITS - 1);

	return {std::min(std::max(pos.x, minCoord.x), maxCoord.x),
          std::min(std::max(pos.y, minCoord.y), maxCoord.y), pos.z};
}

void fpathSetMove(Movement* psMoveCntl, Vector2i targetLocation)
{
	psMoveCntl->path.resize(1);
	psMoveCntl->destination = targetLocation;
	psMoveCntl->path[0] = targetLocation;
}

void fpathRemoveDroidData(unsigned id)
{
	pathResults.erase(id);
}

static FPATH_RESULT fpathRoute(Movement* psMove, unsigned id, int startX, 
                               int startY, int tX, int tY, PathBlockingType moveType,
                               DROID_TYPE droidType, bool acceptNearest, StructureBounds const& dstStructure)
{
  using enum FPATH_RESULT;
	objTrace(id, "called(*,id=%d,sx=%d,sy=%d,ex=%d,ey=%d,prop=%d,type=%d,move=%d,owner=%d)",
           id, startX, startY, tX, tY, (int)moveType.propulsion, (int)droidType,
           (int)moveType.moveType, moveType.player);

	if (!worldOnMap(startX, startY) || !worldOnMap(tX, tY)) {
		debug(LOG_ERROR, "Droid trying to find path to/from invalid location (%d %d) -> (%d %d).",
          startX, startY, tX, tY);
		objTrace(id, "Invalid start/end.");
		syncDebug("fpathRoute(..., %d, %d, %d, %d, %d, %d, %d, %d, %d) = FPR_FAILED",
              id, startX, startY, tX, tY, moveType.propulsion, droidType,
              moveType.moveType, moveType.player);
		return FAILED;
	}

	// don't have to do anything if already there
	if (startX == tX && startY == tY) {
		// return failed to stop them moving anywhere
		objTrace(id, "Tried to move nowhere");
		syncDebug("fpathRoute(..., %d, %d, %d, %d, %d, %d, %d, %d, %d) = FPR_FAILED", 
              id, startX, startY, tX, tY, propulsionType, droidType, moveType, owner);
		return FAILED;
	}

	// check if waiting for a result
	while (psMove->status == MOVE_STATUS::WAIT_FOR_ROUTE)
	{
		objTrace(id, "Checking if we have a path yet");

		auto const& I = pathResults.find(id);
		ASSERT(I != pathResults.end(), "Missing path result promise");
		PathResult result = I->second.get();
		ASSERT(result.retval != OK || !result.sMove->path.empty(),
           "Ok result but no path in list");

		// copy over select fields - preserve others
		psMove->destination = result.sMove->destination;
    bool correctDestination = tX == result.originalDest.x &&
            tY == result.originalDest.y;
		psMove->pathIndex = 0;
		psMove->status = MOVE_STATUS::NAVIGATE;
		psMove->path = result.sMove->path;
		FPATH_RESULT retval = result.retval;
    
		ASSERT(retval != OK || !psMove->path.empty(), 
           "Ok result but no path after copy");

		// remove it from the result list
		pathResults.erase(id);

		objTrace(id, "Got a path to (%d, %d)! Length=%d Retval=%d", 
             psMove->destination.x, psMove->destination.y,
             (int)psMove->path.size(), (int)retval);
    
		syncDebug("fpathRoute(..., %d, %d, %d, %d, %d, %d, %d, %d, %d) = %d, path[%d] = %08X->(%d, %d)",
              id, startX, startY, tX, tY, propulsionType, droidType, moveType, owner, retval,
              (int)psMove->path.size(), ~crcSumVector2i(
            0, psMove->path.data(), psMove->path.size()),
            psMove->destination.x, psMove->destination.y);

    if (correctDestination) return retval;

    // seems we got the result of an old pathfinding job for this
    // droid, so need to pathfind again.
    // we were not waiting for a result, and found no trivial path,
    // so create new job and start waiting
    PathJob job;
    job.origin = {startX, startY};
    job.droidID = id;
    job.destination = {tX, tY};
    job.dstStructure = dstStructure;
    job.droidType = droidType;
    job.moveType = moveType;
    job.acceptNearest = acceptNearest;
    job.deleted = false;
    fpathSetBlockingMap(job);

    debug(LOG_NEVER, "starting new job for droid %d 0x%x", id, id);
    // Clear any results or jobs waiting already. It is a vital assumption that there is only one
    // job or result for each droid in the system at any time.
    fpathRemoveDroidData(id);

    packagedPathJob task([job]() {
      return fpathExecute(job);
    });

    pathResults[id] = task.get_future();

    // add to end of list
    wzMutexLock(fpathMutex);
    bool isFirstJob = pathJobs.empty();
    pathJobs.push_back(std::move(task));
    wzMutexUnlock(fpathMutex);

    if (isFirstJob) {
      // wake up processing thread
      wzSemaphorePost(fpathSemaphore);
    }

    objTrace(id, "Queued up a path-finding request to (%d, %d), at least %d items earlier in queue",
             tX, tY, isFirstJob);
      syncDebug("fpathRoute(..., %d, %d, %d, %d, %d, %d, %d, %d, %d) = FPR_WAIT",
                id, startX, startY, tX, tY, propulsionType, droidType, moveType, owner);
    return WAIT;// wait while polling result queue
  }
}

// Find a route for an DROID to a location in world coordinates
FPATH_RESULT fpathDroidRoute(Droid* psDroid, Vector2i targetLocation, FPATH_MOVETYPE moveType)
{
  using enum FPATH_MOVETYPE;
	bool acceptNearest;
	auto psPropStats = dynamic_cast<PropulsionStats const*>(
          psDroid->getComponent(COMPONENT_TYPE::PROPULSION));

	// override for AI to blast our way through stuff
	if (!isHumanPlayer(psDroid->playerManager->getPlayer()) && moveType == FMT_MOVE) {
		moveType = numWeapons(*psDroid) == 0 ? FMT_MOVE : FMT_ATTACK;
	}

	// check whether the start and end points of the route are 
  // blocking tiles and find an alternative if they are
	auto endPos = Position{targetLocation.x, targetLocation.y, 0};
  auto dstStructure = getStructureBounds(
          dynamic_cast<Structure*>(worldTile(endPos.xy())->psObject));

  auto propulsionType = psPropStats->propulsionType;
  auto startPos = findNonblockingPosition(psDroid->getPosition(), propulsionType,
                                          psDroid->playerManager->getPlayer(), moveType);
  
	if (!dstStructure.isValid()) {
    // if there's a structure over the destination, ignore it, otherwise
    // pathfind from somewhere around the obstruction.
		endPos = findNonblockingPosition(endPos, propulsionType, psDroid->playerManager->getPlayer(), moveType);
	}
	objTrace(psDroid->getId(),
           "Want to go to (%d, %d) -> (%d, %d), going (%d, %d) -> (%d, %d)",
           map_coord(psDroid->getPosition().x),
	         map_coord(psDroid->getPosition().y),
           map_coord(targetLocation.x), map_coord(targetLocation.y),
           map_coord(startPos.x), map_coord(startPos.y),
	         map_coord(endPos.x), map_coord(endPos.y));
  
	switch (psDroid->getOrder()->type) {
    using enum ORDER_TYPE;
  	case BUILD:

    // build a number of structures in a row (walls + bridges)
  	case LINE_BUILD:
  		dstStructure = getStructureBounds(
              psDroid->getOrder()->structure_stats.get(),
              psDroid->getOrder()->pos,
              psDroid->getOrder()->direction);
  	// just need to get close enough to build (can be diagonally),
    // do not need to reach the destination tile

    // help to build a structure
  	case HELP_BUILD:

    // demolish a structure
  	case DEMOLISH:
  	case REPAIR:
  		acceptNearest = false;
  		break;
  	default:
  		acceptNearest = true;
  		break;
	}
	return fpathRoute(psDroid->getMovementData(), psDroid->getId(), startPos.x,
                    startPos.y, endPos.x, endPos.y, psPropStats->propulsionType,
                    psDroid->getType(), moveType, psDroid->playerManager->getPlayer(),
                    acceptNearest, dstStructure);
}

PathResult::PathResult(unsigned id, FPATH_RESULT result, Vector2i originalDest)
  : droidID{id}, retval{result}, originalDest{originalDest}
{
}

/// Run only from path thread
PathResult fpathExecute(PathJob& job)
{
  using enum ASTAR_RESULT;
	auto result = PathResult{job.droidID, FPATH_RESULT::FAILED,
                           Vector2i(job.destination.x, job.destination.y)};

	auto retval = fpathAStarRoute(*result.sMove, job);
	ASSERT(retval != OK || !result.sMove->path.empty(),
         "Ok result but no path in result");
  
	switch (retval) {
	  case PARTIAL:
	  	if (job.acceptNearest) {
	  		objTrace(job.droidID, "** Nearest route -- accepted **");
	  		result.retval = FPATH_RESULT::OK;
	  	}
	  	else {
	  		objTrace(job.droidID, "** Nearest route -- rejected **");
	  		result.retval = FPATH_RESULT::FAILED;
	  	}
	  	break;
	  case FAILED:
	  	objTrace(job.droidID, "** Failed route **");
	    // is this really a good idea? Was in original code
	  	if (job.moveType.propulsion == PROPULSION_TYPE::LIFT &&
          job.droidType != DROID_TYPE::TRANSPORTER &&
          job.droidType != DROID_TYPE::SUPER_TRANSPORTER) {
	  		objTrace(job.droidID, "Doing fallback for non-transport VTOL");
	  		fpathSetMove(result.sMove, {job.destination.x, job.destination.y});
	  		result.retval = FPATH_RESULT::OK;
	  	}
	  	else {
	  		result.retval = FPATH_RESULT::FAILED;
	  	}
	  	break;
	  case OK:
	  	objTrace(job.droidID, "Got route of length %d", (int)result.sMove->path.size());
	  	result.retval = FPATH_RESULT::OK;
	  	break;
	}
	return result;
}

/// Find the length of the job queue. Function is thread-safe
static size_t fpathJobQueueLength()
{
	size_t count = 0;
	wzMutexLock(fpathMutex);
	count = pathJobs.size();
	// O(N) function call for std::list. .empty() is faster, but this function isn't used except in tests.
	wzMutexUnlock(fpathMutex);
	return count;
}

/// Find the length of the result queue, excepting future results. Function is thread-safe
static size_t fpathResultQueueLength()
{
	size_t count = 0;
	wzMutexLock(fpathMutex);
	count = pathResults.size();
	// O(N) function call for std::list. .empty() is faster, but this function isn't used except in tests.
	wzMutexUnlock(fpathMutex);
	return count;
}

// Only used by fpathTest.
static FPATH_RESULT fpathSimpleRoute(Movement* psMove, int id, int startX,
                                     int startY, int tX, int tY)
{
	return fpathRoute(psMove, id, startX, startY, tX, tY, 
                    PROPULSION_TYPE::WHEELED,
                    DROID_TYPE::WEAPON,
                    FPATH_MOVETYPE::FMT_BLOCK, 0, true,
                    getStructureBounds(nullptr));
}

void fpathTest(int x, int y, int x2, int y2)
{
  using enum FPATH_RESULT;

	// on non-debug builds prevent warnings about defining
  // but not using fpathJobQueueLength
	(void)fpathJobQueueLength();

	/* Check initial state */
	assert(fpathThread != nullptr);
	assert(fpathMutex != nullptr);
	assert(fpathSemaphore != nullptr);
	assert(pathJobs.empty());
	assert(pathResults.empty());
	fpathRemoveDroidData(0); // should not crash

	/* This should not leak memory */
  Movement sMove;
	sMove.path.clear();
	for (auto i = 0; i < 100; i++)
	{
		fpathSetMove(&sMove, {1, 1});
	}

	/* Test one path */
	sMove.status = MOVE_STATUS::INACTIVE;
	auto r = fpathSimpleRoute(&sMove, 1, x,
                       y, x2, y2);
	assert(r == WAIT);
	sMove.status = MOVE_STATUS::WAIT_FOR_ROUTE;
	assert(fpathJobQueueLength() == 1 || fpathResultQueueLength() == 1);
	fpathRemoveDroidData(2); // should not crash, nor remove our path
	assert(fpathJobQueueLength() == 1 || fpathResultQueueLength() == 1);

	while (fpathResultQueueLength() == 0)
	{
		wzYieldCurrentThread();
	}

	assert(fpathJobQueueLength() == 0);
	assert(fpathResultQueueLength() == 1);
	r = fpathSimpleRoute(&sMove, 1, x,
                       y, x2, y2);
	assert(r == OK);
	assert(!sMove.path.empty());
	assert(sMove.path[sMove.path.size() - 1].x == x2);
	assert(sMove.path[sMove.path.size() - 1].y == y2);
	assert(fpathResultQueueLength() == 0);

	/* Let one hundred paths flower! */
	sMove.status = MOVE_STATUS::INACTIVE;
	for (auto i = 1; i <= 100; i++)
	{
		r = fpathSimpleRoute(&sMove, i, x,
                         y, x2, y2);
		assert(r == WAIT);
	}

	while (fpathResultQueueLength() != 100)
	{
		wzYieldCurrentThread();
	}

	assert(fpathJobQueueLength() == 0);
	for (auto i = 1; i <= 100; i++)
	{
		sMove.status = MOVE_STATUS::WAIT_FOR_ROUTE;
		r = fpathSimpleRoute(&sMove, i, x,
                         y, x2, y2);
		assert(r == OK);
		assert(!sMove.path.empty());
		assert(sMove.path[sMove.path.size() - 1].x == x2);
		assert(sMove.path[sMove.path.size() - 1].y == y2);
	}
	assert(fpathResultQueueLength() == 0);

	/* Kill a hundred flowers */
	sMove.status = MOVE_STATUS::INACTIVE;
	for (auto i = 1; i <= 100; i++)
	{
		r = fpathSimpleRoute(&sMove, i, x,
                         y, x2, y2);
		assert(r == WAIT);
	}

	for (auto i = 1; i <= 100; i++)
	{
		fpathRemoveDroidData(i);
	}
	assert(pathResults.empty());
	(void)r; // squelch unused-but-set warning.
}

bool fpathCheck(Position orig, Position dest, PROPULSION_TYPE propulsion)
{
	// We have to be careful with this check because it is called on
	// load when playing campaign on droids that are on the other
	// map during missions, and those maps are usually larger.
	if (!worldOnMap(orig.xy()) ||
      !worldOnMap(dest.xy())) {
		return false;
	}

	auto origTile = worldTile(findNonblockingPosition(orig, propulsion).xy());
	auto destTile = worldTile(findNonblockingPosition(dest, propulsion).xy());

	ASSERT_OR_RETURN(false, propulsion != PROPULSION_TYPE::COUNT, "Bad propulsion type");
	ASSERT_OR_RETURN(false, origTile != nullptr && destTile != nullptr, "Bad tile parameter");

	switch (propulsion) {
    using enum PROPULSION_TYPE;
	  case PROPELLOR:
	  case WHEELED:
	  case TRACKED:
	  case LEGGED:
	  case HALF_TRACKED:
	  	return origTile->limitedContinent == destTile->limitedContinent;
	  case HOVER:
	  	return origTile->hoverContinent == destTile->hoverContinent;
	  case LIFT:
	  	return true; // assume no map uses skyscrapers to isolate areas
	  default:
	  	break;
	}

	ASSERT(false, "Should never get here, unknown propulsion !");
	return false; // should never get here
}
