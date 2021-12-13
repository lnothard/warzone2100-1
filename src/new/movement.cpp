//
// Created by luna on 09/12/2021.
//

#include "movement.h"

bool Movement::is_inactive() const
{
  return state == INACTIVE;
}

bool Movement::is_stationary() const
{
  return state == INACTIVE || state == HOVER || state == SHUFFLE;
}

void Movement::stop_moving()
{
  // if flying: state = HOVER;

  state = INACTIVE;
}

void Movement::stop_moving_instantly()
{
  stop_moving();
  speed = 0;
}