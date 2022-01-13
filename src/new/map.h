
/**
 * @file map.h
 */

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

static constexpr auto BLOCKING = 0x10;
static constexpr auto TILE_NUM_MASK = 0x01ff;

static constexpr auto MAX_TILE_TEXTURES = 255;

extern const int map_width, map_height;

extern const int min_horizontal_scroll, max_horizontal_scroll,
                 min_vertical_scroll, max_vertical_scroll;

extern std::array<uint8_t[], AUX_MAX> block_map;
extern std::array<uint8_t[], MAX_PLAYERS + AUX_MAX> aux_map;
extern terrain_types[MAX_TILE_TEXTURES];

enum class TILE_SET
{
	ARIZONA,
	URBAN,
	ROCKIE
};

struct Tile
{
	SimpleObject* occupying_object;
	PIELIGHT colour;
	PlayerMask explored_bits;
	PlayerMask sensor_bits;
	PlayerMask jammer_bits;
	uint8_t info_bits;
	uint8_t illumination_level;
  float visibility_level;
	std::array<uint8_t, MAX_PLAYERS> watchers;
	std::array<uint8_t, MAX_PLAYERS> watching_sensors;
	int water_level;
	int height;
	uint8_t ground_type;
	uint16_t texture;
};

/// The global tile array
extern std::array<Tile, map_width * map_height> map_tiles;

void aux_clear(int x, int y, int state);

void aux_set_all(int x, int y, int state);

void aux_set_enemy(int x, int y, unsigned player, int state);

void aux_set_allied(int x, int y, unsigned player, int state);

[[nodiscard]] uin8_t get_terrain_type(const Tile& tile);

[[nodiscard]] bool tile_is_occupied(const Tile& tile);

[[nodiscard]] bool tile_is_occupied_by_structure(const Tile& tile);

[[nodiscard]] bool tile_is_occupied_by_feature(const Tile& tile);

[[nodiscard]] bool tile_visible_to_player(const Tile& tile, unsigned player);

[[nodiscard]] bool tile_visible_to_selected_player(const Tile& tile);

[[nodiscard]] Vector2i world_coord(const Vector2i& map_coord);

[[nodiscard]] Vector2i map_coord(const Vector2i& world_coord);

bool map_Intersect(int* Cx, int* Cy, int* Vx, int* Vy, int* Sx, int* Sy);

int calculate_map_height(int x, int y);

static inline int calculate_map_height(const Vector2i& v);

[[nodiscard]] int map_tile_height(int x, int y);

void set_tile_height(int x, int y, int height);

/// @return a pointer to the tile at (x, y)
[[nodiscard]] Tile* get_map_tile(int x, int y);

[[nodiscard]] Tile* get_map_tile(const Vector2i& position);

[[nodiscard]] Feature* get_feature_from_tile(int x, int y);

[[nodiscard]] bool is_coord_on_map(int x, int y);

[[nodiscard]] bool is_coord_on_map(const Vector2i& position);

/// @return `true` if the tile at (x, y) exists on the map
[[nodiscard]] bool tile_on_map(int x, int y);

/// @return `true` if the tile at `position` exists on the map
[[nodiscard]] bool tile_on_map(const Vector2i& position);

/**
 * Clip world coordinates to ensure they are within the map boundaries
 *
 * @param pos a pointer to a coordinate inside the map
 * @post 1 <= pos.x <= world_coord(map_width) - 1 and
 *       1 <= pos.y <= world_coord(map_height) - 1
 */
void clip_coords(Vector2i& pos);

[[nodiscard]] uint8_t aux_tile(int x, int y, unsigned player);

[[nodiscard]] uint8_t block_tile(int x, int y, int slot);

#endif // WARZONE2100_MAP_H
