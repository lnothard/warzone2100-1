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
 * @file map.h
 * Definitions for the map structure
 */

#ifndef __INCLUDED_SRC_MAP_H__
#define __INCLUDED_SRC_MAP_H__

#include "lib/ivis_opengl/pietypes.h"
#include "wzmaplib/map_types.h"
#include "wzmaplib/map_debug.h"
#include "wzmaplib/map_io.h"

#include "ai.h"
#include "feature.h"
#include "objects.h"
#include "structure.h"

/* forward decl */
bool godMode;
void markTileDirty(int, int);


static constexpr auto TALLOBJECT_YMAX	= 200;
static constexpr auto TALLOBJECT_ADJUST	= 300;

static constexpr auto BITS_MARKED = 0x01;    ///< Is this tile marked?
static constexpr auto BITS_DECAL = 0x02;    ///< Does this tile has a decal? If so, the tile from "texture" is drawn on top of the terrain.

static constexpr auto BITS_FPATHBLOCK = 0x10;    ///< Bit set temporarily by find path to mark a blocking tile
static constexpr auto BITS_ON_FIRE = 0x20;    ///< Whether tile is burning
static constexpr auto BITS_GATEWAY = 0x40;    ///< Bit set to show a gateway on the tile

static constexpr auto AIR_BLOCKED	= 0x01;	///< Aircraft cannot pass tile
static constexpr auto FEATURE_BLOCKED	= 0x02;	///< Ground units cannot pass tile due to item in the way
static constexpr auto WATER_BLOCKED	= 0x04;	///< Units that cannot pass water are blocked by this tile
static constexpr auto LAND_BLOCKED = 0x08;	///< The inverse of the above -- for propeller driven crafts

static constexpr auto AUXBITS_NONPASSABLE = 0x01;   ///< Is there any building blocking here, other than a gate that would open for us?
static constexpr auto AUXBITS_OUR_BUILDING = 0x02;	///< Do we or our allies have a building at this tile
static constexpr auto AUXBITS_BLOCKING = 0x04;   ///< Is there any building currently blocking here?
static constexpr auto AUXBITS_TEMPORARY = 0x08; ///< Temporary bit used in calculations
static constexpr auto AUXBITS_DANGER = 0x10; ///< Does AI sense danger going there?
static constexpr auto AUXBITS_THREAT = 0x20; ///< Can hostile players shoot here?
static constexpr auto AUXBITS_AATHREAT = 0x40; ///< Can hostile players shoot at my VTOLs here?
static constexpr auto AUXBITS_ALL = 0xff;
static constexpr auto AUX_MAP = 0;
static constexpr auto AUX_ASTARMAP = 1;
static constexpr auto AUX_DANGERMAP = 2;
static constexpr auto AUX_MAX = 3;

enum class TILE_SET
{
    ARIZONA,
    URBAN,
    ROCKIES
};

struct GROUND_TYPE
{
	std::string textureName;
	float textureSize;
};

/* Information stored with each tile */
struct Tile
{
	uint8_t tileInfoBits;
	PlayerMask tileExploredBits;
	PlayerMask sensorBits; ///< bit per player, who can see tile with sensor
	uint8_t illumination; // How bright is this tile?
	std::array<uint8_t, MAX_PLAYERS> watchers; // player sees through fog of war here with this many objects
	uint16_t texture; // Which graphics texture is on this tile
	int height; ///< The height at the top left of the tile
	float level; ///< The visibility level of the top left of the tile, for this client.
  BaseObject * psObject; // Any object sitting on the location (e.g. building)
	PIELIGHT colour;
	uint16_t limitedContinent; ///< For land or sea limited propulsion types
	uint16_t hoverContinent; ///< For hover type propulsions
	uint8_t ground; ///< The ground type used for the terrain renderer
	uint16_t fireEndTime; ///< The (uint16_t)(gameTime / GAME_TICKS_PER_UPDATE) that BITS_ON_FIRE should be cleared.
	int waterLevel; ///< At what height is the water for this tile
	PlayerMask jammerBits; ///< bit per player, who is jamming tile
	std::array<uint8_t, MAX_PLAYERS> sensors; ///< player sees this tile with this many radar sensors
	std::array<uint8_t, MAX_PLAYERS> jammers; ///< player jams the tile with this many objects
};

/* The size and contents of the map */
extern int mapWidth, mapHeight;

extern std::vector<Tile> psMapTiles;
extern float waterLevel;
extern std::vector<GROUND_TYPE> psGroundTypes;
extern int numGroundTypes;
extern char* tilesetDir;

extern std::array<std::vector<uint8_t>, AUX_MAX> psBlockMap;
extern std::array<std::vector<uint8_t>, AUX_MAX + MAX_PLAYERS> psAuxMap;

/// Find aux bitfield for a given tile
 static inline uint8_t auxTile(int x, int y, unsigned player)
{
	ASSERT_OR_RETURN(AUXBITS_ALL, player >= 0 && player < MAX_PLAYERS + AUX_MAX, "invalid player: %d", player);
	return psAuxMap[player][x + y * mapWidth];
}

/// Find blocking bitfield for a given tile
 static inline uint8_t blockTile(int x, int y, int slot)
{
	return psBlockMap[slot][x + y * mapWidth];
}

/// Store a shadow copy of a player's aux map for use in threaded calculations
static inline void auxMapStore(unsigned player, int slot)
{
	psBlockMap[slot] = psBlockMap[0];
  psAuxMap[MAX_PLAYERS + slot] = psAuxMap[player];
}

/// Restore selected fields from the shadow copy of a player's aux map (ignoring the block map)
static inline void auxMapRestore(unsigned player, int slot, int mask)
{
	for (auto i = 0; i < mapHeight * mapWidth; i++)
	{
		auto original = psAuxMap[player][i];
		auto cached = psAuxMap[MAX_PLAYERS + slot][i];
		psAuxMap[player][i] = original ^ ((original ^ cached) & mask);
	}
}

/// Set aux bits. Always set identically for all players. States not set are retained.
 static inline void auxSet(int x, int y, unsigned player, int state)
{
	psAuxMap[player][x + y * mapWidth] |= state;
}

/// Set aux bits. Always set identically for all players. States not set are retained.
 static inline void auxSetAll(int x, int y, int state)
{
	int i;

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		psAuxMap[i][x + y * mapWidth] |= state;
	}
}

/// Set aux bits. Always set identically for all players. States not set are retained.
 static inline void auxSetAllied(int x, int y, unsigned player, int state)
{
	int i;

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		if (alliancebits[player] & (1 << i))
		{
			psAuxMap[i][x + y * mapWidth] |= state;
		}
	}
}

/// Set aux bits. Always set identically for all players. States not set are retained.
 static inline void auxSetEnemy(int x, int y, unsigned player, int state)
{
	int i;

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		if (!(alliancebits[player] & (1 << i)))
		{
			psAuxMap[i][x + y * mapWidth] |= state;
		}
	}
}

/// Clear aux bits. Always set identically for all players. States not cleared are retained.
 static inline void auxClear(int x, int y, unsigned player, int state)
{
	psAuxMap[player][x + y * mapWidth] &= ~state;
}

/// Clear all aux bits. Always set identically for all players. States not cleared are retained.
 static inline void auxClearAll(int x, int y, int state)
{
	int i;

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		psAuxMap[i][x + y * mapWidth] &= ~state;
	}
}

/// Set blocking bits. Always set identically for all players. States not set are retained.
 static inline void auxSetBlocking(int x, int y, int state)
{
	psBlockMap[0][x + y * mapWidth] |= state;
}

/// Clear blocking bits. Always set identically for all players. States not cleared are retained.
 static inline void auxClearBlocking(int x, int y, int state)
{
	psBlockMap[0][x + y * mapWidth] &= ~state;
}

/**
 * Check if tile contains a structure or feature. Function is thread-safe,
 * but do not rely on the result if you mean to alter the object pointer.
 */
static bool TileIsOccupied(Tile const* tile)
{
	return tile->psObject != nullptr;
}

static bool TileIsKnownOccupied(Tile const* tile, unsigned player)
{
	return TileIsOccupied(tile) &&
         (!dynamic_cast<Structure*>(tile->psObject) ||
          dynamic_cast<Structure*>(tile->psObject)->isVisibleToPlayer(player) ||
          aiCheckAlliances(player, dynamic_cast<Structure*>(tile->psObject)->playerManager->getPlayer()));
}

/** Check if tile contains a structure. Function is NOT thread-safe. */
static bool TileHasStructure(Tile const* tile)
{
	return TileIsOccupied(tile) && dynamic_cast<Structure*>(tile->psObject);
}

/** Check if tile contains a feature. Function is NOT thread-safe. */
static bool TileHasFeature(Tile const* tile)
{
	return TileIsOccupied(tile) && dynamic_cast<Feature*>(tile->psObject);
}

/** Check if tile contains a wall structure. Function is NOT thread-safe. */
static bool TileHasWall(Tile const* tile)
{
  using enum STRUCTURE_TYPE;
	return TileHasStructure(tile) &&
          (dynamic_cast<Structure*>(tile->psObject)->getStats()->type == WALL ||
           dynamic_cast<Structure*>(tile->psObject)->getStats()->type == GATE ||
           dynamic_cast<Structure*>(tile->psObject)->getStats()->type == WALL_CORNER);
}

/** Check if tile is burning. */
static bool TileIsBurning(const Tile* tile)
{
	return tile->tileInfoBits & BITS_ON_FIRE;
}

/** Check if tile has been explored. */
static bool tileIsExplored(const Tile* psTile)
{
	if (selectedPlayer >= MAX_PLAYERS) {
    return true;
  }
	return psTile->tileExploredBits & (1LLU << selectedPlayer);
}

/**
 * Is the tile ACTUALLY, 100% visible? -- (For DISPLAY-ONLY purposes
 * - *NOT* game-state calculations!) This is not the same as, e.g.,
 * psStructure->visible[selectedPlayer], because that would only
 * mean the psStructure is in an *explored Tile*. psDroid->visible, on
 * the other hand, works correctly because its visibility fades
 * away in fog of war
 */
static bool tileIsClearlyVisible(const Tile* psTile)
{
	if (selectedPlayer >= MAX_PLAYERS || godMode) {
    return true;
  }
	return psTile->sensorBits & (1 << selectedPlayer);
}

/// Check if @c tile contains a small structure (NOT thread-safe)
static bool TileHasSmallStructure(const Tile* tile)
{
	return TileHasStructure(tile) &&
         dynamic_cast<Structure*>(tile->psObject)->getStats()->height == 1;
}

#define SET_TILE_DECAL(x)	((x)->tileInfoBits |= BITS_DECAL)
#define CLEAR_TILE_DECAL(x)	((x)->tileInfoBits &= ~BITS_DECAL)
#define TILE_HAS_DECAL(x)	((x)->tileInfoBits & BITS_DECAL)

/* Allows us to do if(TRI_FLIPPED(psTile)) */
#define TRI_FLIPPED(x)		((x)->texture & TILE_TRIFLIP)
/* Flips the triangle partition on a tile pointer */
#define TOGGLE_TRIFLIP(x)	((x)->texture ^= TILE_TRIFLIP)

/* Can player number p has explored tile t? */
#define TEST_TILE_VISIBLE(p,t)	((t)->tileExploredBits & (1<<(p)))

/* Can selectedPlayer see tile t? */
/* To be used for *DISPLAY* purposes only (*not* game-state/calculation related) */
static inline bool TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(Tile* pTile)
{
	if (godMode) {
		// always visible
		return true;
	}
	ASSERT_OR_RETURN(false, selectedPlayer < MAX_PLAYERS,
	                 "Players should always have a selectedPlayer / player "
                   "index < MAX_PLAYERS; non-players are always expected to "
                   "have godMode enabled; selectedPlayer: %" PRIu32 "",
                   selectedPlayer);
  
	return TEST_TILE_VISIBLE(selectedPlayer, pTile);
}

/* Set a tile to be visible for a player */
#define SET_TILE_VISIBLE(p,t) ((t)->tileExploredBits |= alliancebits[p])

/* Arbitrary maximum number of terrain textures - used in look up table for terrain type */
static constexpr auto MAX_TILE_TEXTURES	= 255;

extern std::array<uint8_t, MAX_TILE_TEXTURES> terrainTypes;

static inline uint8_t terrainType(const Tile* tile)
{
	return terrainTypes[TileNumber_tile(tile->texture)];
}

static inline Vector2i world_coord(Vector2i const& mapCoord)
{
	return {world_coord(mapCoord.x), world_coord(mapCoord.y)};
}

static inline Vector2i map_coord(Vector2i const& worldCoord)
{
	return {map_coord(worldCoord.x), map_coord(worldCoord.y)};
}

static inline Vector2i round_to_nearest_tile(Vector2i const& worldCoord)
{
	return {round_to_nearest_tile(worldCoord.x),
          round_to_nearest_tile(worldCoord.y)};
}

/** 
 * Clip world coordinates to ensure they're within the map boundaries
 * @param worldX a pointer to an X coordinate inside the map
 * @param worldY a pointer to a Y coordinate inside the map
 * @post 1 <= *worldX <= world_coord(mapWidth)-1 and
 *   1 <= *worldy <= world_coord(mapHeight)-1
 */
static inline void clip_world_offmap(int* worldX, int* worldY)
{
	// x,y must be > 0
	*worldX = MAX(1, *worldX);
	*worldY = MAX(1, *worldY);
	*worldX = MIN(world_coord(mapWidth) - 1, *worldX);
	*worldY = MIN(world_coord(mapHeight) - 1, *worldY);
}

/* Shutdown the map module */
bool mapShutdown();

/* Load the map data */
bool mapLoad(char const* filename);
struct ScriptMapData;
bool mapLoadFromWzMapData(const std::shared_ptr<WzMap::MapData>& mapData);

class WzMapPhysFSIO : public WzMap::IOProvider
{
public:
	std::unique_ptr<WzMap::BinaryIOStream> openBinaryStream(
          const std::string& filename, 
          WzMap::BinaryIOStream::OpenMode mode) override;
  
	bool loadFullFile(const std::string& filename,
                    std::vector<char>& fileData) override;
  
	bool writeFullFile(const std::string& filename,
                     const char* ppFileData, 
                     size_t fileSize) override;
};

class WzMapDebugLogger : public WzMap::LoggingProtocol
{
public:
	~WzMapDebugLogger() override;
  
	void printLog(WzMap::LoggingProtocol::LogLevel level, 
                const char* function, int line, const char* str) override;
};

/* Save the map data */
bool mapSaveToWzMapData(WzMap::MapData& output);

/** Return a pointer to the tile structure at x,y in map coordinates */
static inline WZ_DECL_PURE Tile* mapTile(int x, int y)
{
	// Clamp x and y values to actual ones
	// Give one tile worth of leeway before asserting, for units/transporters coming in from off-map.
	ASSERT(x >= -1, "mapTile: x value is too small (%d,%d) in %dx%d", x, y, mapWidth, mapHeight);
	ASSERT(y >= -1, "mapTile: y value is too small (%d,%d) in %dx%d", x, y, mapWidth, mapHeight);
	x = MAX(x, 0);
	y = MAX(y, 0);
	ASSERT(x < mapWidth + 1, "mapTile: x value is too big (%d,%d) in %dx%d", x, y, mapWidth, mapHeight);
	ASSERT(y < mapHeight + 1, "mapTile: y value is too big (%d,%d) in %dx%d", x, y, mapWidth, mapHeight);
	x = MIN(x, mapWidth - 1);
	y = MIN(y, mapHeight - 1);

	return &psMapTiles[x + (y * mapWidth)];
}

static inline WZ_DECL_PURE Tile* mapTile(Vector2i const& v)
{
	return mapTile(v.x, v.y);
}

/** Return a pointer to the tile structure at x,y in world coordinates */
static inline WZ_DECL_PURE Tile* worldTile(int x, int y)
{
	return mapTile(map_coord(x), map_coord(y));
}

static inline WZ_DECL_PURE Tile* worldTile(Vector2i const& v)
{
	return mapTile(map_coord(v));
}

/// Return ground height of top-left corner of tile at x,y
static inline WZ_DECL_PURE int map_TileHeight(int x, int y)
{
	if (x >= mapWidth || y >= mapHeight ||
      x < 0 || y < 0)	{
		return 0;
	}
	return psMapTiles[x + (y * mapWidth)].height;
}

/// @return the water height of the top-left corner of tile at (x,y)
static inline WZ_DECL_PURE int map_WaterHeight(int x, int y)
{
	if (x >= mapWidth || y >= mapHeight || x < 0 || y < 0) {
		return 0;
	}
	return psMapTiles[x + (y * mapWidth)].waterLevel;
}

/// Return max(ground, water) height of top-left corner of tile at x,y
static inline WZ_DECL_PURE int map_TileHeightSurface(int x, int y)
{
	if (x >= mapWidth || y >= mapHeight || 
      x < 0 || y < 0) {
		return 0;
	}
	return MAX(psMapTiles[x + (y * mapWidth)].height, 
             psMapTiles[x + (y * mapWidth)].waterLevel);
}

/*sets the tile height */
static inline void setTileHeight(int x, int y, int height)
{
	ASSERT_OR_RETURN(, x < mapWidth && x >= 0, 
                   "x coordinate %d bigger than map width %u",
                   x, mapWidth);
  
	ASSERT_OR_RETURN(, y < mapHeight && x >= 0,
                   "y coordinate %d bigger than map height %u",
                   y, mapHeight);

	psMapTiles[x + (y * mapWidth)].height = height;
	markTileDirty(x, y);
}

/* Return whether a tile coordinate is on the map */
static inline bool tileOnMap(int x, int y)
{
	return x >= 0 && x < (int)mapWidth &&
         y >= 0 && y < (int)mapHeight;
}

static inline bool tileOnMap(Vector2i pos)
{
	return tileOnMap(pos.x, pos.y);
}

/* Return whether a world coordinate is on the map */
static inline bool worldOnMap(int x, int y)
{
	return x >= 0 && x < (int)mapWidth << TILE_SHIFT &&
         y >= 0 && y < (int)mapHeight << TILE_SHIFT;
}


/* Return whether a world coordinate is on the map */
 static inline bool worldOnMap(Vector2i pos)
{
	return worldOnMap(pos.x, pos.y);
}


/* Intersect a line with the map and report tile intersection points */
bool map_Intersect(int* Cx, int* Cy, int* Vx, int* Vy, int* Sx, int* Sy);

/// Finds the smallest 0 ≤ t ≤ 1 such that the line segment given by src + t * (dst - src) intersects the terrain.
/// An intersection is defined to be the part of the line segment which is strictly inside the terrain (so if the
/// line segment is exactly parallel with the terrain and leaves it again, it does not count as an intersection).
/// Returns UINT32_MAX if no such 0 ≤ t ≤ 1 exists, otherwise returns t*tMax, rounded down to the nearest integer.
/// If src is strictly inside the terrain, the line segment is only considered to intersect if it exits and reenters
/// the terrain.
unsigned map_LineIntersect(Vector3i src, Vector3i dst, unsigned tMax);

/// The max height of the terrain and water at the specified world coordinates
int map_Height(int x, int y);

static inline int map_Height(Vector2i const& v)
{
	return map_Height(v.x, v.y);
}

/* returns true if object is above ground */
bool mapObjIsAboveGround(const BaseObject * psObj);

/* returns the max and min height of a tile by looking at the four corners
   in tile coords */
void getTileMaxMin(int x, int y, int* pMax, int* pMin);

bool readVisibilityData(const char* fileName);
bool writeVisibilityData(const char* fileName);

//scroll min and max values
extern int scrollMinX, scrollMaxX, scrollMinY, scrollMaxY;

void mapFloodFillContinents();

void tileSetFire(int x, int y, uint duration);
bool fireOnLocation(unsigned int x, unsigned int y);

/**
 * Transitive sensor check for tile. Has to be here rather than
 * visibility.h due to header include order issues.
 */
 static inline bool hasSensorOnTile(Tile* psTile, unsigned player)
{
	return ((player == selectedPlayer && godMode) ||
		((player < MAX_PLAYER_SLOTS) && (alliancebits[selectedPlayer] & (satuplinkbits | psTile->sensorBits)))
	);
}

void mapInit();
void mapUpdate();

//For saves to determine if loading the terrain type override should occur
extern bool builtInMap;

#endif // __INCLUDED_SRC_MAP_H__
