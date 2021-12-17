//
// Created by Luna Nothard on 17/12/2021.
//

#ifndef WARZONE2100_DISPLAYDEF_H
#define WARZONE2100_DISPLAYDEF_H

#include <cstdint>
#include <memory>

#include "lib/ivis_opengl/ivisdef.h"

struct Display_Data
{
  std::unique_ptr<iIMDShape> imd_shape;
  uint32_t frame_number;
  uint32_t screen_x, screen_y, screen_r;
};

#endif //WARZONE2100_DISPLAYDEF_H
