//
// Created by luna on 10/12/2021.
//

#include "map.h"

static inline bool tile_is_occupied(const Tile& tile)
{
  return tile.occupying_object != nullptr;
}

static inline Vector2i world_coord(const Vector2i& map_coord)
{
  return { world_coord(map_coord.x), world_coord(map_coord.y) };
}

static inline Vector2i map_coord(const Vector2i& world_coord)
{
  return { map_coord(world_coord.x), map_coord(world_coord.y) };
}