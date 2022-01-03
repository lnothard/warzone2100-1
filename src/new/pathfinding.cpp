//
// Created by Luna Nothard on 24/12/2021.
//

#include "droid.h"
#include "map.h"
#include "pathfinding.h"

PathResult::PathResult(PATH_RESULT ret, Movement movement)
  : return_value{ret}, movement{std::move(movement)}
{
}

uint8_t get_path_bits_from_propulsion(PROPULSION_TYPE propulsion)
{
  using enum PROPULSION_TYPE;
  switch (propulsion)
  {
    case LIFT:
      return AIR_BLOCKED;
    case HOVER:
      return FEATURE_BLOCKED;
    case PROPELLER:
      return FEATURE_BLOCKED | LAND_BLOCKED;
    default:
      return FEATURE_BLOCKED | WATER_BLOCKED;
  }
}

bool is_tile_blocking(int x, int y,
                      PROPULSION_TYPE propulsion,
                      int map_index,
                      MOVE_TYPE move_type)
{
  if (x < 1 || y < 1 || x > map_width - 1 || y > map_height - 1) {
    // we are blocked -- return
    return true;
  }

  if (propulsion != PROPULSION_TYPE::LIFT &&
      (x < min_horizontal_scroll + 1 ||
       y < min_vertical_scroll + 1 ||
       x >= max_horizontal_scroll - 1 ||
       y >= max_vertical_scroll - 1)) {
    // we are blocked -- return
    return true;
  }

  // auxiliary map tile corresponding to this tile coordinate
  const auto aux = aux_tile(x, y, map_index);

  // conversion from unit's `move_type` to the corresponding
  // bitmask for auxiliary map
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

  // convert the `propulsion-type`
  const auto path_bits =
          get_path_bits_from_propulsion(propulsion);

  if ((path_bits & FEATURE_BLOCKED) != 0 && (aux & aux_mask) != 0) {
    // we are blocked -- return
    return true;
  }

  return (block_tile(x, y, MAX(0, map_index - MAX_PLAYERS)) & path_bits) != 0;
}

bool is_tile_blocked_by_droid(const Droid& droid,
                              int x, int y,
                              MOVE_TYPE move_type)
{
  // ensure propulsion data exists for this droid,
  // otherwise, call to `is_tile_blocking` will fail
  assert(droid.get_propulsion());

  return is_tile_blocking(x, y,
                          droid.get_propulsion()->propulsion_type,
                          droid.get_player(), move_type);
}

bool is_droid_blocked_by_tile(int x, int y,
                              PROPULSION_TYPE propulsion)
{
  // test whether an actively blocking droid with
  // this `propulsion_type` blocks a particular tile coord
  return is_tile_blocking(x, y, propulsion, 0, MOVE_TYPE::BLOCK);
}