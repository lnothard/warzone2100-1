//
// Created by Luna Nothard on 24/12/2021.
//

#include "movement.h"

void Movement::set_path_vars(int target_x, int target_y)
{
  path.resize(1);
  destination = {target_x, target_y};
  path[0] = {target_x, target_y};
}
