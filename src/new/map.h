//
// Created by luna on 10/12/2021.
//

#ifndef WARZONE2100_MAP_H
#define WARZONE2100_MAP_H

#include <wzmaplib/map.h>

#include "lib/ivis_opengl/pietypes.h"
#include "basedef.h"
#include "feature.h"
#include "structure.h"

extern int32_t map_width, map_height;

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
  int32_t water_level;
  int32_t height;
  uint8_t ground_type;
  uint16_t texture;
};
extern std::unique_ptr<Tile[]> map_tiles;

static inline bool tile_is_occupied(const Tile& tile)
{
  return tile.occupying_object != nullptr;
}

static inline bool tile_is_occupied_by_structure(const Tile& tile)
{
  return tile_is_occupied(tile) && dynamic_cast<Structure*>(tile.occupying_object);
}

static inline bool tile_is_occupied_by_feature(const Tile& tile)
{
  return tile_is_occupied(tile) && dynamic_cast<Feature*>(tile.occupying_object);
}

static inline Vector2i world_coord(const Vector2i& map_coord)
{
  return { world_coord(map_coord.x), world_coord(map_coord.y) };
}

static inline Vector2i map_coord(const Vector2i& world_coord)
{
  return { map_coord(world_coord.x), map_coord(world_coord.y) };
}

bool map_Intersect(int* Cx, int* Cy, int* Vx, int* Vy, int* Sx, int* Sy);

int32_t calculate_map_height(int x, int y);

static inline int32_t calculate_map_height(const Vector2i& v)
{
  return map_height(v.x, v.y);
}

static inline __attribute__((__pure__)) int32_t map_tile_height(int32_t x, int32_t y)
{
 if (x >= map_width || y >= map_height || x < 0 || y < 0)
 {
   return 0;
 }
 return map_tiles[x + (y * map_width)].height;
}

static inline void set_tile_height(int32_t x, int32_t y, int32_t height)
{
  assert(x < map_width && x >=0);
  assert(y < map_height && y >= 0);

  map_tiles[x + (y * map_width)].height = height;
  mark_tile_dirty();
}

/** Return a pointer to the tile structure at x,y in map coordinates */
static inline __attribute__((__pure__)) Tile* get_map_tile(int32_t x, int32_t y)
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

static inline Feature* get_feature_from_tile(const uint32_t x, const uint32_t y)
{
  auto* tile_object = get_map_tile(x, y)->occupying_object;
  return dynamic_cast<Feature*>(tile_object);
}

#endif // WARZONE2100_MAP_H