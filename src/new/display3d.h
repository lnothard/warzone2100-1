// Created by Luna Nothard on 27/12/2021.

/**
 * @file display3d.h
 *
 * 3D rendering interface
 *
 */

#ifndef WARZONE2100_DISPLAY3D_H
#define WARZONE2100_DISPLAY3D_H

#include "lib/framework/vector.h"

/**
 * Standard map tile dimensions
 */
static constexpr auto TILE_WIDTH = 128;
static constexpr auto TILE_HEIGHT = 128;
static constexpr auto TILE_AREA = TILE_HEIGHT * TILE_WIDTH;

/// Global list of displayed tiles
extern const Vector2i visible_tiles;

/// The physical type of a tile
enum class TILE_TYPE
{
    RIVER_BED,
    WATER,
    RUBBLE,
    BLOCKING_RUBBLE
};

/// A rendering perspective
struct ViewPosition
{
    ViewPosition() = default;
    ViewPosition(Vector3i position, Vector3i rotation);

    Vector3i position{0, 0, 0};
    Vector3i rotation{0, 0, 0};
};

/// The current rendering perspective to be used for `selectedPlayer`
extern ViewPosition player_pos;

#endif //WARZONE2100_DISPLAY3D_H
