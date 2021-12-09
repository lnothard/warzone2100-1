//
// Created by luna on 09/12/2021.
//

#include "movement.h"

bool Movement::is_stationary() const
{
  return state == INACTIVE || state == HOVER || state == SHUFFLE;
}