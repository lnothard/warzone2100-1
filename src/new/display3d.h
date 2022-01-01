//
// Created by Luna Nothard on 27/12/2021.
//

#ifndef WARZONE2100_DISPLAY3D_H
#define WARZONE2100_DISPLAY3D_H

#include "lib/framework/vector.h"

static constexpr auto TILE_WIDTH = 128;
static constexpr auto TILE_HEIGHT = 128;
static constexpr auto TILE_AREA = TILE_HEIGHT * TILE_WIDTH;

extern const Vector2i visible_tiles;

enum class TILE_TYPE
{
    RIVER_BED,
    WATER,
    RUBBLE,
    BLOCKING_RUBBLE
};

struct ViewPosition
{
    Vector3i position{0, 0, 0};
    Vector3i rotation{0, 0, 0};
};
extern ViewPosition player_pos;

#endif //WARZONE2100_DISPLAY3D_H
