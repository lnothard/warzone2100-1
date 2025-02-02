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
 * @file visibility.cpp
 * Functions for handling object visibility
 *
 * Pumpkin Studios, EIDOS Interactive 1996
 */

#include <vector>

#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "wzmaplib/map.h"

#include "baseobject.h"
#include "displaydef.h"
#include "map.h"
#include "message.h"
#include "multiplay.h"
#include "objmem.h"
#include "projectile.h"
#include "raycast.h"
#include "visibility.h"
#include "wavecast.h"

/* forward decl */
bool bInTutorial;
void jsDebugMessageUpdate();
struct BaseObject;
typedef std::vector<BaseObject *> GridList;
typedef GridList::const_iterator GridIterator;
GridList const& gridStartIterateUnseen(int x, int y, int radius, unsigned player);
Vector2i world_coord(Vector2i);
bool triggerEventSeen(BaseObject *, BaseObject *);


/// Integer amount to change visibility this turn
static int visLevelInc, visLevelDec;

Spotter::Spotter(int x, int y, unsigned plr, int radius,
                 SENSOR_CLASS type, unsigned expiry)
  : pos{x, y, 0}, player{plr}, sensorRadius{radius},
    sensorType{type}, expiryTime{expiry}
{
  id = generateSynchronisedObjectId();
}

static void updateTileVis(Tile* psTile)
{
  for (auto i = 0; i < MAX_PLAYERS; ++i)
  {
    // The definition of whether a player can see something on a given tile or not
    if (psTile->watchers[i] > 0 || (psTile->sensors[i] > 0 &&
        !(psTile->jammerBits & ~alliancebits[i]))) {
      psTile->sensorBits |= (1 << i); // mark it as being seen
    }
    else {
      psTile->sensorBits &= ~(1 << i); // mark as hidden
    }
  }
}

Spotter::~Spotter()
{
  for (auto& tilePos : watchedTiles)
  {
    auto psTile = mapTile(tilePos.x, tilePos.y);
    auto visionType = (tilePos.type == 0)
            ? psTile->watchers
            : psTile->sensors;

    ASSERT(visionType[player] > 0,
           "Not watching watched tile (%d, %d)",
           (int)tilePos.x, (int)tilePos.y);

    --visionType[player];
    updateTileVis(psTile);
  }
}

static int* gNumWalls = nullptr;
static Vector2i* gWall = nullptr;

// forward declarations
static void setSeenBy(BaseObject* psObj, unsigned viewer, int val);
static int checkFireLine(BaseObject const* psViewer, BaseObject const* psTarget, int weapon_slot, bool wallsBlock, bool direct);

// initialise the visibility stuff
bool visInitialise()
{
	visLevelInc = 1;
	visLevelDec = 0;

	return true;
}

// update the visibility change levels
void visUpdateLevel()
{
	visLevelInc = gameTimeAdjustedAverage(VIS_LEVEL_INC);
	visLevelDec = gameTimeAdjustedAverage(VIS_LEVEL_DEC);
}

unsigned addSpotter(int x, int y, unsigned player, int radius, SENSOR_CLASS type, unsigned expiry)
{
	ASSERT_OR_RETURN(0, player >= 0 && player < MAX_PLAYERS, "invalid player: %d", player);
	auto psSpot = std::make_unique<Spotter>(x, y, player, radius, type, expiry);
	size_t size;
	auto tiles = getWavecastTable(radius, &size);
	psSpot->watchedTiles.resize(size * psSpot->watchedTiles.size());
	for (auto i = 0; i < size; ++i)
	{
		const auto mapX = x + tiles[i].dx;
		const auto mapY = y + tiles[i].dy;
		if (mapX < 0 || mapX >= mapWidth ||
        mapY < 0 || mapY >= mapHeight) {
			continue;
		}
		auto psTile = mapTile(mapX, mapY);
		psTile->tileExploredBits |= alliancebits[player];
		uint8_t* visionType = (!radar) ? psTile->watchers : psTile->sensors;

		if (visionType[player] < UBYTE_MAX) {
			TILEPOS tilePos = {uint8_t(mapX), uint8_t(mapY), uint8_t(radar)};
			visionType[player]++; // we observe this tile
			updateTileVis(psTile);
			psSpot->watchedTiles[psSpot->numWatchedTiles++] = tilePos; // record having seen it
		}
	}
	apsInvisibleViewers.push_back(psSpot);
	return psSpot->id;
}

void removeSpotter(unsigned id)
{
  std::erase_if(apsInvisibleViewers, [&id](auto const& spot) {
    return spot.id == id;
  });
}

void removeSpotters()
{
	if (!apsInvisibleViewers.empty()) {
    apsInvisibleViewers.clear();
	}
}

static void updateSpotters()
{
	static GridList gridList; // static to avoid allocations.
	for (auto& psSpot : apsInvisibleViewers)
	{
		if (psSpot->expiryTime != 0 && psSpot->expiryTime < gameTime)  {
			std::erase(apsInvisibleViewers, psSpot);
			continue;
		}
		// else, if not expired, show objects around it
		gridList = gridStartIterateUnseen(world_coord(psSpot->pos.x),
                                      world_coord(psSpot->pos.y),
                                      psSpot->sensorRadius,
		                                  psSpot->player);

		for (auto psObj : gridList)
		{
			// tell system that this side can see this object
			setSeenBy(psObj, psSpot->player, UINT8_MAX);
		}
	}
}

/**
 * Record all tiles that some object confers visibility to. Only record each tile
 * once. Note that there is both a limit to how many objects can watch any given
 * tile. Strange but non-fatal things will happen if these limits are exceeded
 */
static void visMarkTile(BaseObject const* psObj, int mapX, int mapY,
                        Tile* psTile, std::vector<TILEPOS>& watchedTiles)
{
	const auto rayPlayer = psObj->playerManager->getPlayer();
	const auto xdiff = map_coord(psObj->getPosition().x) - mapX;
	const auto ydiff = map_coord(psObj->getPosition().y) - mapY;
	const auto distSq = xdiff * xdiff + ydiff * ydiff;
	const bool inRange = (distSq < 16);
	auto visionType = inRange ? psTile->watchers : psTile->sensors;

  if (visionType[rayPlayer] >= UINT8_MAX) return;

  auto tilePos = TILEPOS{uint8_t(mapX), uint8_t(mapY), uint8_t(inRange)};
  visionType[rayPlayer]++;// we observe this tile
  if (psObj->testFlag(static_cast<size_t>(OBJECT_FLAG::JAMMED_TILES))) {
    // we are a jammer object
    psTile->jammers[rayPlayer]++;
    // mark it as being jammed
    psTile->jammerBits |= (1 << rayPlayer);
  }
  updateTileVis(psTile);
  // record having seen it
  watchedTiles.push_back(tilePos);
}

/* The terrain revealing ray callback */
static void doWaveTerrain(BaseObject* psObj)
{
	const auto sx = psObj->getPosition().x;
	const auto sy = psObj->getPosition().y;
	const auto sz = psObj->getPosition().z +
          MAX(MIN_VIS_HEIGHT, psObj->getDisplayData()->imd_shape->max.y);
	const auto radius = objSensorRange(psObj);
	const auto rayPlayer = psObj->playerManager->getPlayer();
	std::size_t size;
	const auto tiles = getWavecastTable(radius, &size);

	int heights[2][MAX_WAVECAST_LIST_SIZE];
	size_t angles[2][MAX_WAVECAST_LIST_SIZE + 1];
	auto readListSize = 0, readListPos = 0, writeListPos = 0; // readListSize, readListPos dummy initialisations.
	auto readList = 0; // Reading from this list, writing to the other. Could also initialise to rand()%2.
	auto lastHeight = 0; // lastHeight dummy initialisation.
	auto lastAngle = std::numeric_limits<size_t>::max();

	// Start with full vision of all angles. (If someday wanting to make droids that can only look in one direction, change here, after getting the original angle values saved in the wavecast table.)
	heights[!readList][writeListPos] = -0x7FFFFFFF - 1; // Smallest integer.
	angles[!readList][writeListPos] = 0; // Smallest angle.
	++writeListPos;

	psObj->watchedTiles.clear();
	for (size_t i = 0; i < size; ++i)
	{
		const auto mapX = map_coord(sx) + tiles[i].dx;
		const auto mapY = map_coord(sy) + tiles[i].dy;
		if (mapX < 0 || mapX >= mapWidth ||
        mapY < 0 || mapY >= mapHeight) {
			continue;
		}

		auto psTile = mapTile(mapX, mapY);
		auto tileHeight = std::max(psTile->height, psTile->waterLevel);
		// If we can see the water surface, then let us see water-covered tiles too.
		auto perspectiveHeight = (tileHeight - sz) * tiles[i].invRadius;
		auto perspectiveHeightLeeway = (tileHeight - sz + MIN_VIS_HEIGHT) * tiles[i].invRadius;

		if (tiles[i].angBegin < lastAngle) {
			// Gone around the circle. (Or just started scan.)
			angles[!readList][writeListPos] = lastAngle;

			// Flip the lists.
			readList = !readList;
			readListPos = 0;
			readListSize = writeListPos;
			writeListPos = 0;
			lastHeight = 1;
			// Impossible value since tiles[i].invRadius > 1 for all i, so triggers writing first entry in list.
		}
		lastAngle = tiles[i].angEnd;

		while (angles[readList][readListPos + 1] <= tiles[i].angBegin && readListPos < readListSize)
		{
			++readListPos; // Skip, not relevant.
		}

		bool seen = false;
		while (angles[readList][readListPos] < tiles[i].angEnd && readListPos < readListSize)
		{
			auto oldHeight = heights[readList][readListPos];
			auto newHeight = MAX(oldHeight, perspectiveHeight);
			seen = seen || perspectiveHeightLeeway >= oldHeight;
			// consider point slightly above ground in case there is something on the tile
			if (newHeight != lastHeight) {
				heights[!readList][writeListPos] = newHeight;
				angles[!readList][writeListPos] = MAX(angles[readList][readListPos], tiles[i].angBegin);
				lastHeight = newHeight;
				++writeListPos;
				ASSERT_OR_RETURN(, writeListPos <= MAX_WAVECAST_LIST_SIZE,
				                   "Visibility too complicated! Need to increase MAX_WAVECAST_LIST_SIZE.");
			}
			++readListPos;
		}
		--readListPos;

		if (seen) {
			// Can see this tile.
			psTile->tileExploredBits |= alliancebits[rayPlayer]; // Share exploration with allies too
			visMarkTile(psObj, mapX, mapY, psTile, psObj->watchedTiles); // Mark this tile as seen by our sensor
		}
	}
}

/* The los ray callback */
static bool rayLOSCallback(Vector2i pos, int dist, void* data)
{
	auto help = (VisibleObjectHelp_t*)data;
	ASSERT(pos.x >= 0 && pos.x < world_coord(mapWidth) &&
         pos.y >= 0 && pos.y < world_coord(mapHeight),
	       "rayLOSCallback: coords off map");

	if (help->rayStart) {
		help->rayStart = false;
	}
	else {
		// Calculate the current LOS gradient
		auto newGrad = (help->lastHeight - help->startHeight) *
            GRADIENT_MULTIPLIER / MAX(1, help->lastDist);
		if (newGrad >= help->currGrad) {
			help->currGrad = newGrad;
		}
	}
	help->lastDist = dist;
	help->lastHeight = map_Height(pos.x, pos.y);

  if (!help->wallsBlock) return true;

  // Store the height at this tile for next time round
  Vector2i tile = map_coord(pos.xy());
  if (!(tile != help->final)) return true;

  auto psTile = mapTile(tile);
  if (!TileHasWall(psTile) || TileHasSmallStructure(psTile)) return true;

  auto psStruct = dynamic_cast<Structure*>(psTile->psObject);
  if (psStruct->getStats()->type != STRUCTURE_TYPE::GATE ||
      psStruct->getAnimationState() != STRUCTURE_ANIMATION_STATE::OPEN) {

    help->lastHeight = 2 * TILE_MAX_HEIGHT;
    help->wall = pos.xy();
    help->numWalls++;
  }
  return true;
}


/* Remove tile visibility from object */
void visRemoveVisibility(BaseObject * psObj)
{
	if (mapWidth && mapHeight) {
		for (TILEPOS pos : psObj->watchedTiles)
		{
			// FIXME: the mapTile might have been swapped out, see swapMissionPointers()
			Tile* psTile = mapTile(pos.x, pos.y);

			ASSERT(pos.type < 2, "Invalid visibility type %d", (int)pos.type);
			uint8_t* visionType = (pos.type == 0) ? psTile->sensors : psTile->watchers;
			if (visionType[psObj->playerManager->getPlayer()] == 0 && game.type == LEVEL_TYPE::CAMPAIGN) { // hack
				continue;
			}
			ASSERT(visionType[psObj->playerManager->getPlayer()] > 0, "No %s on watched tile (%d, %d)", pos.type ? "radar" : "vision",
			       (int)pos.x, (int)pos.y);
			visionType[psObj->playerManager->getPlayer()]--;
			if (psObj->testFlag(static_cast<size_t>(OBJECT_FLAG::JAMMED_TILES))) {
			// we are a jammer object — we cannot check objJammerPower(psObj) > 0 directly here, we may be in the SimpleObject destructor).
				// No jammers in campaign, no need for special hack
				ASSERT(psTile->jammers[psObj->playerManager->getPlayer()] > 0, "Not jamming watched tile (%d, %d)", (int)pos.x, (int)pos.y);
				psTile->jammers[psObj->playerManager->getPlayer()]--;
				if (psTile->jammers[psObj->playerManager->getPlayer()] == 0) {
					psTile->jammerBits &= ~(1 << psObj->playerManager->getPlayer());
				}
			}
			updateTileVis(psTile);
		}
	}
	psObj->watchedTiles.clear();
	psObj->setFlag(static_cast<size_t>(OBJECT_FLAG::JAMMED_TILES), false);
}

void visRemoveVisibilityOffWorld(BaseObject * psObj)
{
	psObj->watchedTiles.clear();
}

/* Check which tiles can be seen by an object */
void visTilesUpdate(BaseObject* psObj)
{
	ASSERT(!dynamic_cast<Feature*>(psObj), "visTilesUpdate: visibility updates are not for features!");

	// Remove previous map visibility provided by object
	visRemoveVisibility(psObj);

  if (auto psStruct = dynamic_cast<Structure*>(psObj)) {
		if (psStruct->getState() != STRUCTURE_STATE::BUILT ||
		  	psStruct->getStats()->type == STRUCTURE_TYPE::WALL ||
        psStruct->getStats()->type == STRUCTURE_TYPE::WALL_CORNER ||
        psStruct->getStats()->type == STRUCTURE_TYPE::GATE) {
			// unbuilt structures and walls do not confer visibility.
			return;
		}
	}
	// Do the whole circle in ∞ steps. No more pretty moiré patterns.
	psObj->setFlag(static_cast<size_t>(OBJECT_FLAG::JAMMED_TILES), objJammerPower(psObj) > 0);
	doWaveTerrain(psObj);
}

/*reveals all the terrain in the map*/
void revealAll(unsigned player)
{
	if (player >= MAX_PLAYERS) return;

	//reveal all tiles
	for (auto i = 0; i < mapWidth; i++)
	{
		for (auto j = 0; j < mapHeight; j++)
		{
			auto psTile = mapTile(i, j);
			psTile->tileExploredBits |= alliancebits[player];
		}
	}
	// the objects are revealed in processVisibility()
}

/* Check whether psViewer can see psTarget.
 * psViewer should be an object that has some form of sensor,
 * currently droids and structures.
 * psTarget can be any type of SimpleObject (e.g. a tree).
 * struckBlock controls whether structures block LOS
 */
int visibleObject(BaseObject const* psViewer, BaseObject const* psTarget, bool wallsBlock)
{
	ASSERT_OR_RETURN(0, psViewer != nullptr, "Invalid viewer pointer!");
	ASSERT_OR_RETURN(0, psTarget != nullptr, "Invalid viewed pointer!");

	auto range = objSensorRange(psViewer);

	if (!worldOnMap(psViewer->getPosition().x, psViewer->getPosition().y) ||
      !worldOnMap(psTarget->getPosition().x, psTarget->getPosition().y)) {
		//Most likely a VTOL or transporter
		debug(LOG_WARNING, "Trying to view something off map!");
		return 0;
	}

	/* Get the sensor range */
	if (auto psDroid = dynamic_cast<Droid const*>(psViewer)) {
    if (psDroid->getOrder()->target == psTarget && psDroid->hasCbSensor()) {
      // if it is targetted by a counter battery sensor, it is seen
      return UBYTE_MAX;
    }
	}
  else if (auto psStruct = dynamic_cast<Structure const*>(psViewer)) {
    // a structure that is being built cannot see anything
    if (psStruct->getState() != STRUCTURE_STATE::BUILT) {
      return 0;
    }

    if (psStruct->getStats()->type == STRUCTURE_TYPE::WALL ||
        psStruct->getStats()->type == STRUCTURE_TYPE::GATE ||
        psStruct->getStats()->type == STRUCTURE_TYPE::WALL_CORNER) {
      return 0;
    }

    if (dynamic_cast<Droid const*>(psTarget) &&
        dynamic_cast<Droid const*>(psTarget)->isVtol() &&
        psStruct->weaponManager->weapons[0].stats->surfaceToAir == SHOOT_IN_AIR) {
      range = 3 * range / 2; // increase vision range of AA vs VTOL
    }

    if (psStruct->getTarget(0) == psTarget &&
        (structCBSensor(psStruct) || structVTOLCBSensor(psStruct))) {
      // if a unit is targetted by a counter battery sensor
      // it is automatically seen
      return UBYTE_MAX;
    }
	}
	else {
		ASSERT(false, "Visibility checking is only implemented for units and structures");
		return 0;
	}

	/* First see if the target is in sensor range */
	auto const dist = iHypot((psTarget->getPosition() - psViewer->getPosition()).xy());
	if (dist == 0) {
		return UBYTE_MAX; // Should never be on top of each other, but ...
	}

	const auto psTile = mapTile(map_coord(
                                      psTarget->getPosition().x),
                              map_coord(psTarget->getPosition().y));

	const bool jammed = psTile->jammerBits & ~alliancebits[psViewer->playerManager->getPlayer()];

	// Special rule for VTOLs, as they are not affected by ECM
  auto psTargetDroid = dynamic_cast<Droid const*>(psTarget);
  auto psViewerDroid = dynamic_cast<Droid const*>(psViewer);
	if (psTargetDroid && psTargetDroid->isVtol() ||
      (psViewerDroid && psViewerDroid->isVtol() && dist < range)) {
		return UBYTE_MAX;
	}

	// initialise the callback variables
	VisibleObjectHelp_t help = {
		true,
		wallsBlock,
		psViewer->getPosition().z + map_Height(
            psViewer->getPosition().x, psViewer->getPosition().y),
		map_coord(psTarget->getPosition().xy()),
		0,
		0,
    -UBYTE_MAX * GRADIENT_MULTIPLIER * ELEVATION_SCALE,
		0,
		Vector2i(0, 0)
	};

	// Cast a ray from the viewer to the target
	rayCast(psViewer->getPosition().xy(), psTarget->getPosition().xy(), rayLOSCallback, &help);

	if (gWall != nullptr && gNumWalls != nullptr) { // Out globals are set
		*gWall = help.wall;
		*gNumWalls = help.numWalls;
	}

	bool tileWatched = psTile->watchers[psViewer->playerManager->getPlayer()] > 0;
	bool tileWatchedSensor = psTile->sensors[psViewer->playerManager->getPlayer()] > 0;

	// Show objects hidden by ECM jamming with radar blips
	if (jammed) {
		if (!tileWatched && tileWatchedSensor) {
			return UBYTE_MAX / 2;
		}
	}
	// Show objects that are seen directly
	if (tileWatched || (!jammed && tileWatchedSensor)) {
		return UBYTE_MAX;
	}
	// Show detected sensors as radar blips
	if (objRadarDetector(psViewer) &&
      objActiveRadar(psTarget) && dist < range * 10) {
		return UBYTE_MAX / 2;
	}
	// else not seen
	return 0;
}

// Find the wall that is blocking LOS to a target (if any)
Structure* visGetBlockingWall(BaseObject const* psViewer, BaseObject const* psTarget)
{
	int numWalls = 0;
	Vector2i wall;

	// HACK Using globals to not clutter visibleObject() interface too much
	gNumWalls = &numWalls;
	gWall = &wall;

	visibleObject(psViewer, psTarget, true);

	gNumWalls = nullptr;
	gWall = nullptr;

  if (numWalls <= 0) return nullptr;

  auto tile = map_coord(wall);
  for (auto player = 0; player < MAX_PLAYERS; player++)
  {
    for (auto& psWall : playerList[player].structures)
    {
      if (map_coord(psWall.getPosition()) == tile)
        return &psWall;
    }
  }
  return nullptr;
}

bool hasSharedVision(unsigned viewer, unsigned ally)
{
  ASSERT_OR_RETURN(false, viewer < MAX_PLAYERS, "");
	ASSERT_OR_RETURN(false, ally < MAX_PLAYERS, "Bad ally %u (viewer: %u)", ally, viewer);

	return viewer == ally || (bMultiPlayer &&
          alliancesSharedVision(game.alliance) &&
          aiCheckAlliances(viewer, ally));
}

static void setSeenBy(BaseObject* psObj, unsigned viewer, int val)
{
  ASSERT_OR_RETURN(, viewer < MAX_PLAYERS, "");

	//forward out vision to our allies
	for (auto ally = 0; ally < MAX_PLAYERS; ++ally)
	{
		if (hasSharedVision(viewer, ally)) {
			psObj->seenThisTick[ally] = MAX(psObj->seenThisTick(ally), val);
		}
	}
}

static void setSeenByInstantly(BaseObject * psObj, unsigned viewer, int val /*= UBYTE_MAX*/)
{
  ASSERT_OR_RETURN(, viewer < MAX_PLAYERS, "");

	//forward out vision to our allies
	for (auto ally = 0; ally < MAX_PLAYERS; ++ally)
	{
		if (hasSharedVision(viewer, ally)) {
			psObj->seenThisTick[ally] = MAX(psObj->seenThisTick(ally), val);
			psObj->setVisibleToPlayer(ally, MAX(psObj->isVisibleToPlayer(ally), val));
		}
	}
}

// Calculate which objects we should know about based on alliances and satellite view.
static void processVisibilitySelf(BaseObject* psObj)
{
	if (!dynamic_cast<Feature*>(psObj) && objSensorRange(psObj) > 0) {
		// one can trivially see oneself
		setSeenBy(psObj, psObj->playerManager->getPlayer(), UBYTE_MAX);
	}

	// if a player has a SAT_UPLINK structure, or has godMode enabled,
	// they can see everything!
	for (auto viewer = 0; viewer < MAX_PLAYERS; viewer++)
	{
		if (getSatUplinkExists(viewer) || viewer == selectedPlayer && godMode) {
			setSeenBy(psObj, viewer, UBYTE_MAX);
		}
	}

  // remove any targeting locks from last update
	psObj->setFlag(static_cast<size_t>(OBJECT_FLAG::TARGETED), false);

	// if we're a CB sensor, make our target visible instantly. Although this is
  // actually checking visibility of our target, we do it here anyway
	auto psStruct = dynamic_cast<Structure*>(psObj);
	// you can always see anything that a CB sensor is targeting
	if (psStruct != nullptr &&
      psStruct->getState() == STRUCTURE_STATE::BUILT &&
      (structCBSensor(psStruct) || structVTOLCBSensor(psStruct)) &&
      psStruct->psTarget[0] != nullptr) {
		setSeenByInstantly(psStruct->psTarget[0],
                       psObj->playerManager->getPlayer(), UBYTE_MAX);
	}
	auto psDroid = dynamic_cast<Droid*>(psObj);
	if (psDroid != nullptr && psDroid->getAction() == ACTION::OBSERVE && psDroid->hasCbSensor()) {
		// Anyone commenting this out will get a knee capping from John.
		// You have been warned!!
		setSeenByInstantly(psDroid->getTarget(0),
                       psObj->playerManager->getPlayer(), UBYTE_MAX);
	}
}

// Calculate which objects we can see. Better to call after processVisibilitySelf, since that check is cheaper.
static void processVisibilityVision(BaseObject* psViewer)
{
	if (dynamic_cast<Feature*>(psViewer)) return;

	// get all the objects from the grid the droid is in
	// Will give inconsistent results if hasSharedVision is not an equivalence relation.
	static GridList gridList; // static to avoid allocations.
	gridList = gridStartIterateUnseen(
          psViewer->getPosition().x, psViewer->getPosition().y,
          objSensorRange(psViewer), psViewer->playerManager->getPlayer());

	for (auto psObj : gridList)
	{
  	auto val = visibleObject(psViewer, psObj, false);

		// If we've got ranged line of sight...
		if (val > 0) {
			// Tell system that this side can see this object
			setSeenBy(psObj, psViewer->playerManager->getPlayer(), val);

			// Check if scripting system wants to trigger an event for this
			triggerEventSeen(psViewer, psObj);
		}
	}
}

/* Find out what can see this object */
// Fade in/out of view. Must be called after calculation of which objects are seen.
static void processVisibilityLevel(BaseObject* psObj, bool& addedMessage)
{
	// update the visibility levels
	for (auto player = 0; player < MAX_PLAYERS; player++)
	{
		bool justBecameVisible = false;
		auto visLevel = psObj->seenThisTick(player);

		if (player == psObj->playerManager->getPlayer()) {
			// owner can always see it fully
			psObj->setVisibleToPlayer(player, UBYTE_MAX);
			continue;
		}

		// Droids can vanish from view, other objects will stay
		if (!dynamic_cast<Droid*>(psObj)) {
			visLevel = MAX(visLevel, psObj->isVisibleToPlayer(player));
		}

		if (visLevel > psObj->isVisibleToPlayer(player)) {
			justBecameVisible = psObj->isVisibleToPlayer(player) <= 0;
			psObj->setVisibleToPlayer(player, MIN(psObj->isVisibleToPlayer(player) + visLevelInc, visLevel));
		}
		else if (visLevel < psObj->isVisibleToPlayer(player)) {
			psObj->setVisibleToPlayer(player, MAX(psObj->isVisibleToPlayer(player) - visLevelDec, visLevel));
		}

    if (!justBecameVisible) continue;

    /* Make sure all tiles under a feature/structure become visible when you see it */
    if (getObjectType(psObj) == OBJECT_TYPE::STRUCTURE ||
        getObjectType(psObj) == OBJECT_TYPE::FEATURE) {
      setUnderTilesVis(psObj, player);
    }

    // if a feature has just become visible set the message blips
      if (auto psFeat = dynamic_cast<Feature*>(psObj)) {
        INGAME_AUDIO type = NO_SOUND;

        /* If this is an oil resource we want to add a proximity message for
         * the selected Player - if there isn't an Resource Extractor on it. */
        if (psFeat->getStats()->subType == FEATURE_TYPE::OIL_RESOURCE &&
                !TileHasStructure(
                        mapTile(map_coord(psObj->getPosition().x),
                                map_coord(psObj->getPosition().y)))) {
          type = ID_SOUND_RESOURCE_HERE;
        }
        else if (psFeat->getStats()->subType == FEATURE_TYPE::GEN_ARTE) {
          type = ID_SOUND_ARTEFACT_DISC;
        }

        if (type != NO_SOUND) {
          auto psMessage = addMessage(MESSAGE_TYPE::MSG_PROXIMITY, true, player);
          if (psMessage) {
            psMessage->psObj = psObj;
            debug(LOG_MSG, "Added message for oil well or artefact, pViewData=%p",
                  static_cast<void *>(psMessage->pViewData));
            addedMessage = true;
          }
          if (!bInTutorial && player == selectedPlayer) {
            // play message to indicate been seen
            audio_QueueTrackPos(type, psObj->getPosition().x, psObj->getPosition().y, psObj->getPosition().z);
          }
        }
      }
  }
}

void processVisibility()
{
	updateSpotters();
	for (auto player = 0; player < MAX_PLAYERS; ++player)
	{
    for (auto& droid : playerList[player].droids)
    {
      processVisibilitySelf(&droid);
      processVisibilityVision(&droid);
    }
    for (auto& structure : playerList[player].droids)
    {
      processVisibilitySelf(&structure);
      processVisibilityVision(&structure);
    }
    for (auto feature : apsFeatureLists[player])
    {
      processVisibilitySelf(feature);
    }
	}

	for (auto psObj : apsSensorList)
	{
    if (!objRadarDetector(psObj)) continue;
    for (auto psTarget : apsSensorList)
    {
      if (psObj != psTarget &&
          psTarget->isVisibleToPlayer(psObj->playerManager->getPlayer()) < UBYTE_MAX / 2 &&
          objActiveRadar(psTarget) &&
          iHypot((psTarget->getPosition() - psObj->getPosition()).xy()) < objSensorRange(psObj) * 10) {
        psTarget->setVisibleToPlayer(psObj->playerManager->getPlayer(), UBYTE_MAX / 2);
      }
    }
  }
	bool addedMessage = false;
	for (auto player = 0; player < MAX_PLAYERS; ++player)
	{
    for (auto& psDroid : playerList[player].droids)
      processVisibilityLevel(&psDroid, addedMessage);

    for (auto& psStruct : playerList[player].structures)
      processVisibilityLevel(&psStruct, addedMessage);

    for (auto psFeature : apsFeatureLists[player])
      processVisibilityLevel(psFeature, addedMessage);
	}
	if (addedMessage) {
		jsDebugMessageUpdate();
	}
}

void setUnderTilesVis(BaseObject* psObj, unsigned player)
{
  ASSERT_OR_RETURN(, player < MAX_PLAYERS, "");

  unsigned mapX, mapY, width, breadth;
	if (auto psFeature = dynamic_cast<Feature*>(psObj)) {
		auto psStats = psFeature->getStats();
		width = psStats->baseWidth;
		breadth = psStats->baseBreadth;
		mapX = map_coord(psFeature->getPosition().x - width * TILE_UNITS / 2);
		mapY = map_coord(psFeature->getPosition().y - breadth * TILE_UNITS / 2);
	}
	else if (auto psStructure = dynamic_cast<Structure*>(psObj)) {
		width = psStructure->getStats()->base_width;
		breadth = psStructure->getStats()->base_breadth;
		mapX = map_coord(psStructure->getPosition().x - width * TILE_UNITS / 2);
		mapY = map_coord(psStructure->getPosition().y - breadth * TILE_UNITS / 2);
	}

	for (auto i = 0; i < width + 1; i++) // + 1 because visibility is for top left of tile.
	{
		for (auto j = 0; j < breadth + 1; j++) // + 1 because visibility is for top left of tile.
		{
			auto psTile = mapTile(mapX + i, mapY + j);
			if (psTile) {
				psTile->tileExploredBits |= alliancebits[player];
			}
		}
	}
}

/**
 * Check whether psViewer can fire directly at psTarget.
 * psTarget can be any type of SimpleObject (e.g. a tree).
 */
bool lineOfFire(BaseObject const* psViewer, BaseObject const* psTarget, int weapon_slot, bool wallsBlock)
{
	WeaponStats const* psStats = nullptr;

	ASSERT_OR_RETURN(false, psViewer != nullptr, "Invalid shooter pointer!");
	ASSERT_OR_RETURN(false, psTarget != nullptr, "Invalid target pointer!");
	ASSERT_OR_RETURN(false, getObjectType(psViewer) == OBJECT_TYPE::DROID ||
                          getObjectType(psViewer) == OBJECT_TYPE::STRUCTURE,
                   "Bad viewer type");

	if (auto psDroid = dynamic_cast<Droid const*>(psViewer)) {
		psStats = psDroid->weaponManager->weapons[weapon_slot].stats.get();
	}
	else if (auto psStruct = dynamic_cast<Structure const*>(psViewer)) {
		psStats = psStruct->weaponManager->weapons[weapon_slot].stats.get();
	}
	// 2d distance
	auto distance = iHypot((psTarget->getPosition() - psViewer->getPosition()).xy());
	auto range = proj_GetLongRange(psStats, psViewer->playerManager->getPlayer());
	if (proj_Direct(psStats)) {
		/** direct shots could collide with ground **/
		return range >= distance && LINE_OF_FIRE_MINIMUM <= checkFireLine(
			psViewer, psTarget, weapon_slot, wallsBlock, true);
	}
	else {
		/**
		 * indirect shots always have a line of fire, IF the forced
		 * minimum angle doesn't move it out of range
		 **/
		auto min_angle = checkFireLine(psViewer, psTarget, weapon_slot, wallsBlock, false);
		// NOTE This code seems similar to the code in combFire in combat.cpp.
		if (min_angle > DEG(PROJ_MAX_PITCH)) {
			if (iSin(2 * min_angle) < iSin(2 * DEG(PROJ_MAX_PITCH))) {
				range = (range * iSin(2 * min_angle)) / iSin(2 * DEG(PROJ_MAX_PITCH));
			}
		}
		return range >= distance;
	}
}

/* Check how much of psTarget is hittable from psViewer's gun position */
int areaOfFire(BaseObject const* psViewer, BaseObject const* psTarget, int weapon_slot, bool wallsBlock)
{
	if (psViewer == nullptr) return 0; // Lassat special case, avoid assertion.
	return checkFireLine(psViewer, psTarget, weapon_slot, wallsBlock, true);
}

/* Check the minimum angle to hitpsTarget from psViewer via indirect shots */
int arcOfFire(const BaseObject * psViewer, const BaseObject * psTarget, int weapon_slot, bool wallsBlock)
{
	return checkFireLine(psViewer, psTarget, weapon_slot, wallsBlock, false);
}

/* helper function for checkFireLine */
static void angle_check(int64_t* angletan, int positionSq, int height, int distanceSq, int targetHeight, bool direct)
{
	int64_t current;
	if (direct) {
		current = (65536 * height) / iSqrt(positionSq);
	}
	else {
		auto dist = iSqrt(distanceSq);
		auto pos = iSqrt(positionSq);
		current = (pos * targetHeight) / dist;
		if (current < height && pos > TILE_UNITS / 2 && pos < dist - TILE_UNITS / 2) {
			// solve the following trajectory parabolic equation
			// ( targetHeight ) = a * distance^2 + factor * distance
			// ( height ) = a * position^2 + factor * position
			//  "a" depends on angle, gravity and shooting speed.
			//  luckily we don't need it for this at all, since
			// factor = tan(firing_angle)
			current = ((int64_t)65536 * ((int64_t)distanceSq * (int64_t)height - (int64_t)positionSq * (int64_t)
					targetHeight))
				/ ((int64_t)distanceSq * (int64_t)pos - (int64_t)dist * (int64_t)positionSq);
		}
		else {
			current = 0;
		}
	}
	*angletan = std::max(*angletan, current);
}

/**
 * Check fire line from psViewer to psTarget
 * psTarget can be any type of SimpleObject (e.g. a tree).
 */
static int checkFireLine(BaseObject const* psViewer, BaseObject const* psTarget, int weapon_slot, bool wallsBlock, bool direct)
{
  ASSERT_OR_RETURN(-1, psViewer != nullptr, "Invalid shooter pointer!");
  ASSERT_OR_RETURN(-1, psTarget != nullptr, "Invalid target pointer!");

	Vector3i pos(0, 0, 0), dest(0, 0, 0);
	Vector2i start(0, 0), diff(0, 0), current(0, 0), halfway(0, 0), next(0, 0), part(0, 0);
	Vector3i muzzle(0, 0, 0);
	int distSq, partSq, oldPartSq;
	int64_t angletan;


	/* CorvusCorax: get muzzle offset (code from projectile.c)*/
	if (getObjectType(psViewer) == OBJECT_TYPE::DROID && weapon_slot >= 0) {
		calcDroidMuzzleBaseLocation(dynamic_cast<Droid const*>(psViewer), &muzzle, weapon_slot);
	}
	else if (getObjectType(psViewer) == OBJECT_TYPE::STRUCTURE && weapon_slot >= 0) {
		calcStructureMuzzleBaseLocation((const Structure*)psViewer, &muzzle, weapon_slot);
	}
	else { // incase anything wants a projectile
		muzzle = psViewer->getPosition();
	}

	pos = muzzle;
	dest = psTarget->getPosition();
	diff = (dest - pos).xy();

	distSq = dot(diff, diff);
	if (distSq == 0)
	{
		// Should never be on top of each other, but ...
		return 1000;
	}

	current = pos.xy();
	start = current;
	angletan = -1000 * 65536;
	partSq = 0;
	// run a manual trace along the line of fire until target is reached
	while (partSq < distSq)
	{
		bool hasSplitIntersection;

		oldPartSq = partSq;

		if (partSq > 0) {
			angle_check(&angletan, partSq, map_Height(current) - pos.z, distSq, dest.z - pos.z, direct);
		}

		// intersect current tile with line of fire
		next = diff;
		hasSplitIntersection = map_Intersect(&current.x, &current.y, &next.x, &next.y, &halfway.x, &halfway.y);

		if (hasSplitIntersection) {
			// check whether target was reached before tile split line:
			part = halfway - start;
			partSq = dot(part, part);

			if (partSq >= distSq) {
				break;
			}

			if (partSq > 0) {
				angle_check(&angletan, partSq, map_Height(halfway) - pos.z, distSq, dest.z - pos.z, direct);
			}
		}

		// check for walls and other structures
		// TODO: if there is a structure on the same tile as the shooter (and the shooter is not that structure) check if LOF is blocked by it.
		if (wallsBlock && oldPartSq > 0) {
			halfway = current + (next - current) / 2;
			auto const psTile = mapTile(map_coord(halfway.x), map_coord(halfway.y));
			if (TileHasStructure(psTile) && psTile->psObject != psTarget) {
				// check whether target was reached before tile's "half way" line
				part = halfway - start;
				partSq = dot(part, part);

				if (partSq >= distSq) {
					break;
				}

				// allowed to shoot over enemy structures if they are NOT the target
				if (partSq > 0) {
					angle_check(&angletan, oldPartSq,
					            psTile->psObject->getPosition().z + establishTargetHeight(psTile->psObject) - pos.z,
					            distSq, dest.z - pos.z, direct);
				}
			}
		}

		// next
		current = next;
		part = current - start;
		partSq = dot(part, part);
		ASSERT(partSq > oldPartSq, "areaOfFire(): no progress in tile-walk! From: %i,%i to %i,%i stuck in %i,%i",
		       map_coord(pos.x), map_coord(pos.y), map_coord(dest.x), map_coord(dest.y), map_coord(current.x),
		       map_coord(current.y));
	}
	if (direct) {
		return establishTargetHeight(psTarget) - (pos.z + (angletan * iSqrt(distSq)) / 65536 - dest.z);
	}
	else {
		angletan = iAtan2(angletan, 65536);
		angletan = angleDelta(angletan);
		return DEG(1) + angletan;
	}
}

static bool visObjInRange(BaseObject const* psObj1, BaseObject const* psObj2, int range)
{
  auto xdiff = psObj1->getPosition().x - psObj2->getPosition().x;
  auto ydiff = psObj1->getPosition().y - psObj2->getPosition().y;

  return abs(xdiff) <= range && abs(ydiff) <= range &&
         xdiff * xdiff + ydiff * ydiff <= range;
}

static unsigned objSensorRange(BaseObject const* psObj)
{
  if (auto psDroid = dynamic_cast<const Droid*>(psObj)) {
    auto ecm = dynamic_cast<EcmStats const*>(psDroid->getComponent(COMPONENT_TYPE::ECM));
    const auto ecmrange = ecm->upgraded[psObj->playerManager->getPlayer()].range;
    if (ecmrange > 0) {
      return ecmrange;
    }
    auto sensor = dynamic_cast<SensorStats const*>(psDroid->getComponent(COMPONENT_TYPE::SENSOR));
    return sensor->upgraded[psObj->playerManager->getPlayer()].range;
  }
  else if (auto psStruct = dynamic_cast<const Structure*>(psObj)) {
    const auto ecmrange = psStruct->getStats()->ecm_stats->upgraded[psObj->playerManager->getPlayer()].range;
    if (ecmrange) {
      return ecmrange;
    }
    return psStruct->getStats()->sensor_stats->
            upgraded[psObj->playerManager->getPlayer()].range;
  }
  return 0;
}

static unsigned objJammerPower(BaseObject const* psObj)
{
  if (auto psDroid = dynamic_cast<const Droid*>(psObj))  {
    auto ecm = dynamic_cast<EcmStats const*>(psDroid->getComponent(COMPONENT_TYPE::ECM));
    return ecm->upgraded[psObj->playerManager->getPlayer()].range;
  }
  else if (auto psStruct = dynamic_cast<const Structure*>(psObj))  {
    return psStruct->getStats()->ecm_stats->
            upgraded[psObj->playerManager->getPlayer()].range;
  }
  return 0;
}
