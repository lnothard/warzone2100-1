//
// Created by Luna Nothard on 23/12/2021.
//

#ifndef WARZONE2100_PATHFINDING_H
#define WARZONE2100_PATHFINDING_H

#include "lib/framework/vector.h"

#include "astar.h"
#include "droid.h"
#include "stats.h"

/// Which movement class is the droid currently assigned?
enum class MOVE_TYPE
{
    MOVE,
    ATTACK,
    BLOCK
};

/// Return value for a path traversal procedure
enum class PATH_RESULT
{
    OK,
    FAILED,
    WAIT
};

/// A pathfinding task
struct PathJob
{
    /// Target coordinate
    PathCoord destination {0, 0};

    /// Start coordinate
    PathCoord origin {0, 0};

    /// Movement class
    MOVE_TYPE move_type = MOVE;

    /// ID of this unit's controller
    unsigned player = 0;
};

/// The result obtained from a pathfinding job
struct PathResult
{
    PathResult(PATH_RESULT ret, Movement movement);

    /// Were we successful?
    PATH_RESULT return_value;

    /// The unit's resolved movement data for the next tick
    Movement movement;
};

/// Conversion function from -propulsion_type-
/// into the bits governing which tile types are blocked
uint8_t get_path_bits_from_propulsion(PROPULSION_TYPE propulsion);

/**
 * Does this coordinate block units with movement parameters `move_type` ?
 *
 * @param x
 * @param y
 * @param propulsion
 * @param map_index
 * @param move_type
 * @return
 */
bool is_tile_blocking(int x, int y,
                      PROPULSION_TYPE propulsion,
                      unsigned map_index,
                      MOVE_TYPE move_type);

/// Are we blocked by the tile at coordinate x, y?
bool is_tile_blocked_by_droid(const Droid& droid,
                              int x, int y,
                              MOVE_TYPE move_type);

/// Do droids of this propulsion class block the specified coord?
bool is_droid_blocked_by_tile(int x, int y,
                              PROPULSION_TYPE propulsion);

#endif //WARZONE2100_PATHFINDING_H
