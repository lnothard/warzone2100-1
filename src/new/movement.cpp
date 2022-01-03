//
// Created by Luna Nothard on 24/12/2021.
//

#include "movement.h"

bool Movement::is_inactive() const noexcept
{
  return state == INACTIVE;
}

bool Movement::is_hovering() const noexcept
{
  return state == HOVER;
}

bool Movement::is_stationary() const noexcept
{
  return state == INACTIVE || state == HOVER || state == SHUFFLE;
}

void Movement::stop_moving() noexcept
{
  // if flying: state = HOVER;

  state = INACTIVE;
}

void Movement::stop_moving_instantly() noexcept
{
  stop_moving();
  speed = 0;
}

void Movement::set_path_vars(int target_x, int target_y)
{
  path.resize(1);
  destination = {target_x, target_y};
  path[0] = {target_x, target_y};
}
