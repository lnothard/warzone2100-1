//
// Created by Luna Nothard on 23/12/2021.
//

#ifndef WARZONE2100_PATHFINDING_H
#define WARZONE2100_PATHFINDING_H

#include "lib/framework/vector.h"

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

#endif //WARZONE2100_PATHFINDING_H
