//
// Created by Luna Nothard on 23/12/2021.
//

#ifndef WARZONE2100_PATHFINDING_H
#define WARZONE2100_PATHFINDING_H

#include "lib/framework/vector.h"

#include "droid.h"
#include "stats.h"

enum class MOVE_TYPE
{
    MOVE,
    ATTACK,
    BLOCK
};

enum class PATH_RETVAL
{
    OK,
    FAILED,
    WAIT
};

struct Path_Job
{
    Vector2i destination;
    Vector2i origin;
    MOVE_TYPE move_type;
    unsigned player;
};

struct Path_Result
{
    PATH_RETVAL retval;
};

uint8_t get_path_bits_from_propulsion(PROPULSION_TYPE propulsion);
bool is_tile_blocking(int x, int y, PROPULSION_TYPE propulsion, int map_index, MOVE_TYPE move_type);
bool is_droid_blocking_tile(const Droid& droid, int x, int y, MOVE_TYPE move_type);
bool is_droid_blocked_by_tile(int x, int y, PROPULSION_TYPE propulsion);

#endif //WARZONE2100_PATHFINDING_H
