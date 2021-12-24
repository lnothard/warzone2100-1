//
// Created by Luna Nothard on 24/12/2021.
//

#include "map.h"
#include "pathfinding.h"

uint8_t get_path_bits_from_propulsion(PROPULSION_TYPE propulsion)
{
  switch (propulsion)
  {
    case PROPULSION_TYPE::LIFT:
      return AIR_BLOCKED;
    case PROPULSION_TYPE::HOVER:
      return FEATURE_BLOCKED;
    case PROPULSION_TYPE::PROPELLER:
      return FEATURE_BLOCKED | LAND_BLOCKED;
    default:
      return FEATURE_BLOCKED | WATER_BLOCKED;
  }
}

bool is_tile_blocking(int x, int y, PROPULSION_TYPE propulsion, int map_index, MOVE_TYPE move_type)
{
  if (x < 1 || y < 1 || x > map_width - 1 || y > map_height - 1)
    return true;

  if (propulsion != PROPULSION_TYPE::LIFT &&
      (x < min_horizontal_scroll + 1 || y < min_vertical_scroll + 1 ||
       x >= max_horizontal_scroll - 1 || y >= max_vertical_scroll - 1))
  {
    return true;
  }

  auto aux = aux_tile(x, y, map_index);

  auto aux_mask = int{0};
  switch (move_type)
  {
    case MOVE_TYPE::MOVE:
      aux_mask = AUX_NON_PASSABLE;
      break;
    case MOVE_TYPE::ATTACK:
      aux_mask = AUX_OUR_BUILDING;
      break;
    case MOVE_TYPE::BLOCK:
      aux_mask = AUX_BLOCKING;
      break;
  }

  auto path_bits = get_path_bits_from_propulsion(propulsion);
  if ((path_bits & FEATURE_BLOCKED) != 0 && (aux & aux_mask) != 0)
    return true;

  return (block_tile(x, y, MAX(0, map_index - MAX_PLAYERS)) & path_bits) != 0;
}

bool is_droid_blocking_tile(const Droid& droid, int x, int y, MOVE_TYPE move_type)
{
  assert(*droid.get_propulsion());
  return is_tile_blocking(x, y, *droid.get_propulsion().type, droid.get_player(), move_type);
}