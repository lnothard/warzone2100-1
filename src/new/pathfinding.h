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

enum class PATH_RESULT
{
    OK,
    FAILED,
    WAIT
};

struct PathJob
{
    Vector2i destination{0, 0};
    Vector2i origin{0, 0};
    MOVE_TYPE move_type;
    unsigned player{0};
};

struct PathResult
{
    PathResult(PATH_RESULT retval, Movement movement)
      : return_val{retval}, movement{std::move(movement)}
    {
    }

    PATH_RESULT return_val;
    Movement movement;
};

uint8_t get_path_bits_from_propulsion(PROPULSION_TYPE propulsion);
bool is_tile_blocking(int x, int y, PROPULSION_TYPE propulsion, int map_index, MOVE_TYPE move_type);
bool is_droid_blocking_tile(const Droid& droid, int x, int y, MOVE_TYPE move_type);
bool is_droid_blocked_by_tile(int x, int y, PROPULSION_TYPE propulsion);

#endif //WARZONE2100_PATHFINDING_H
