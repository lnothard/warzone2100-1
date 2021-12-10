//
// Created by luna on 10/12/2021.
//

#ifndef WARZONE2100_MAP_H
#define WARZONE2100_MAP_H

#include <wzmaplib/map.h>
#include "lib/ivis_opengl/pietypes.h"
#include "basedef.h"

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

static inline bool tile_is_occupied(const Tile& tile);
static inline Vector2i world_coord(const Vector2i& map_coord);
static inline Vector2i map_coord(const Vector2i& world_coord);

#endif // WARZONE2100_MAP_H