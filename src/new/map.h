//
// Created by luna on 10/12/2021.
//

#ifndef WARZONE2100_MAP_H
#define WARZONE2100_MAP_H

#include <wzmaplib/map.h>

#include "lib/ivis_opengl/pietypes.h"
#include "basedef.h"

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
extern std::unique_ptr<Tile[]> psMapTiles;

static inline bool tile_is_occupied(const Tile& tile);
static inline bool tile_is_occupied_by_structure(const Tile& tile);
static inline Vector2i world_coord(const Vector2i& map_coord);
static inline Vector2i map_coord(const Vector2i& world_coord);

bool map_Intersect(int* Cx, int* Cy, int* Vx, int* Vy, int* Sx, int* Sy);

int32_t map_Height(int x, int y);
static inline int32_t map_Height(const Vector2i& v)
{
  return map_Height(v.x, v.y);
}

/** Return a pointer to the tile structure at x,y in map coordinates */
static inline WZ_DECL_PURE Tile *mapTile(int32_t x, int32_t y)
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

  return &psMapTiles[x + (y * map_width)];
}

#endif // WARZONE2100_MAP_H