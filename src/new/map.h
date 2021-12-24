//
// Created by luna on 10/12/2021.
//

#ifndef WARZONE2100_MAP_H
#define WARZONE2100_MAP_H

#include <array>
#include <wzmaplib/map.h>

#include "lib/ivis_opengl/pietypes.h"
#include "basedef.h"
#include "feature.h"
#include "structure.h"

static constexpr auto AUX_MAX = 3;
static constexpr auto AUX_NON_PASSABLE = 0x01;
static constexpr auto AUX_OUR_BUILDING = 0x02;
static constexpr auto AUX_BLOCKING = 0x04;
static constexpr auto AUX_TEMPORARY = 0x08;
static constexpr auto AUX_DANGER = 0x10;
static constexpr auto AUX_THREAT = 0x20;
static constexpr auto AUX_AA_THREAT = 0x40;

static constexpr auto AIR_BLOCKED = 0x01;
static constexpr auto FEATURE_BLOCKED = 0x02;
static constexpr auto WATER_BLOCKED = 0x04;
static constexpr auto LAND_BLOCKED = 0x08;

extern const int map_width, map_height;
extern const int min_horizontal_scroll, max_horizontal_scroll,
                 min_vertical_scroll, max_vertical_scroll;

extern std::array<uint8_t[], AUX_MAX> block_map;
extern std::array<uint8_t[], MAX_PLAYERS + AUX_MAX> aux_map;

enum class TILE_SET
{
	ARIZONA,
	URBAN,
	ROCKIE
};

struct Tile
{
	Simple_Object* occupying_object;
	PIELIGHT colour;
	PlayerMask explored_bits;
	PlayerMask sensor_bits;
	PlayerMask jammer_bits;
	uint8_t info_bits;
	uint8_t illumination_level;
	std::array<uint8_t, MAX_PLAYERS> watchers;
	std::array<uint8_t, MAX_PLAYERS> watching_sensors;
	int water_level;
	int height;
	uint8_t ground_type;
	uint16_t texture;
};

extern std::unique_ptr<Tile[]> map_tiles;

[[nodiscard]] constexpr bool tile_is_occupied(const Tile& tile)
{
	return tile.occupying_object != nullptr;
}

[[nodiscard]] constexpr bool tile_is_occupied_by_structure(const Tile& tile)
{
	return tile_is_occupied(tile) && dynamic_cast<Structure*>(tile.occupying_object);
}

[[nodiscard]] constexpr bool tile_is_occupied_by_feature(const Tile& tile)
{
	return tile_is_occupied(tile) && dynamic_cast<Feature*>(tile.occupying_object);
}

[[nodiscard]] constexpr Vector2i world_coord(const Vector2i& map_coord)
{
	return {world_coord(map_coord.x), world_coord(map_coord.y)};
}

[[nodiscard]] constexpr Vector2i map_coord(const Vector2i& world_coord)
{
	return {map_coord(world_coord.x), map_coord(world_coord.y)};
}

bool map_Intersect(int* Cx, int* Cy, int* Vx, int* Vy, int* Sx, int* Sy);

int calculate_map_height(int x, int y);

static inline int calculate_map_height(const Vector2i& v)
{
	return calculate_map_height(v.x, v.y);
}

[[nodiscard]] constexpr int map_tile_height(int x, int y)
{
	if (x >= map_width || y >= map_height || x < 0 || y < 0)
	{
		return 0;
	}
	return map_tiles[x + (y * map_width)].height;
}

constexpr void set_tile_height(int x, int y, int height)
{
	assert(x < map_width && x >=0);
	assert(y < map_height && y >= 0);

	map_tiles[x + (y * map_width)].height = height;
	mark_tile_dirty();
}

/** Return a pointer to the tile structure at x,y in map coordinates */
constexpr Tile* get_map_tile(int x, int y)
{
	// Clamp x and y values to actual ones
	// Give one tile worth of leeway before asserting, for units/transporters coming in from off-map.
	ASSERT(x >= -1, "mapTile: x value is too small (%d,%d) in %dx%d", x, y, map_width, map_height);
	ASSERT(y >= -1, "mapTile: y value is too small (%d,%d) in %dx%d", x, y, map_width, map_height);
	x = MAX(x, 0);
	y = MAX(y, 0);
	ASSERT(x < map_width + 1, "mapTile: x value is too big (%d,%d) in %dx%d", x, y, map_width, map_height);
	ASSERT(y < map_height + 1, "mapTile: y value is too big (%d,%d) in %dx%d", x, y, map_width, map_height);
	x = MIN(x, map_width - 1);
	y = MIN(y, map_height - 1);

	return &map_tiles[x + (y * map_width)];
}

[[nodiscard]] constexpr Feature* get_feature_from_tile(unsigned x, unsigned y)
{
	auto* tile_object = get_map_tile(x, y)->occupying_object;
	return dynamic_cast<Feature*>(tile_object);
}

[[nodiscard]] constexpr bool is_coord_on_map(int x, int y)
{
	return (x >= 0) && (x < map_width << TILE_SHIFT) &&
		(y >= 0) && (y < map_height << TILE_SHIFT);
}

[[nodiscard]] constexpr bool is_coord_on_map(Vector2i& position)
{
  return is_coord_on_map(position.x, position.y);
}

[[nodiscard]] constexpr uint8_t aux_tile(int x, int y, int player)
{
  return aux_map[player][x + y + map_width];
}

[[nodiscard]] constexpr uint8_t block_tile(int x, int y, int slot)
{
  return block_map[slot][x + y * map_width];
}

#endif // WARZONE2100_MAP_H
