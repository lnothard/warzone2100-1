//
// Created by luna on 08/12/2021.
//

#include "structure.h"

Structure_Bounds::Structure_Bounds()
    : top_left_coords(0, 0), size_in_coords(0, 0)
{
}

Structure_Bounds::Structure_Bounds(const Vector2i& top_left_coords, const Vector2i& size_in_coords)
{
  this->top_left_coords = top_left_coords;
  this->size_in_coords = size_in_coords;
}

namespace Impl
{
  bool Structure::is_blueprint() const
  {
    return (state == BLUEPRINT_VALID ||
            state == BLUEPRINT_INVALID ||
            state == BLUEPRINT_PLANNED ||
            state == BLUEPRINT_PLANNED_BY_ALLY);
  }

  bool Structure::is_wall() const
  {
    if (type == WALL || type == WALL_CORNER)
      return true;

    return false;
  }

  bool Structure::is_probably_doomed() const
  {
      const auto hit_points = get_hp();
      return expected_damage > hit_points && expected_damage - hit_points > hit_points / 15;
  }
}