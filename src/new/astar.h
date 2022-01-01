//
// Created by luna on 15/12/2021.
//

#ifndef WARZONE2100_ASTAR_H
#define WARZONE2100_ASTAR_H

#include <cstdlib>
#include "pathfinding.h"

enum class ASTAR_RESULT
{
	OK,
	FAILED,
	PARTIAL
};

struct PathCoordinate
{
	constexpr PathCoordinate() = default;
	constexpr PathCoordinate(int x, int y)
    : x{x}, y{y}
  {
  }

	constexpr bool operator ==(const PathCoordinate& rhs) const
  {
    return x == rhs.x && y == rhs.y;
  }

	constexpr bool operator !=(const PathCoordinate& rhs) const
  {
    return !(*this == rhs);
  }

	int x, y;
};

struct PathNode
{
  constexpr bool operator <(const PathNode& rhs) const
  {
    if (estimated_distance_to_end != rhs.estimated_distance_to_end)
      return estimated_distance_to_end > rhs.estimated_distance_to_end;

    if (distance_from_start != rhs.distance_from_start)
      return distance_from_start < rhs.distance_from_start;

    if (path_coordinate.x != rhs.path_coordinate.x)
      return path_coordinate.x < rhs.path_coordinate.x;

    return path_coordinate.y < rhs.path_coordinate.y;
  }

	PathCoordinate path_coordinate;
	unsigned distance_from_start;
	unsigned estimated_distance_to_end;
};

struct ExploredTile
{
    unsigned iteration;
    int x_diff, y_diff; ///< offset from previous point in route
    unsigned distance; ///< shortest known distance to tile
    bool visited;
};

struct PathBlockingType
{
    std::size_t game_time;
    unsigned owner;
    MOVE_TYPE move_type;
    PROPULSION_TYPE propulsion;
};

struct PathBlockingMap
{
    PathBlockingType type;
    std::vector<bool> map;
    std::vector<bool> threat_map;
};
extern std::vector<PathBlockingMap> blocking_maps;

struct NonBlockingArea
{
    NonBlockingArea() = default;
    explicit NonBlockingArea(const Structure_Bounds& bounds);
    bool operator ==(const NonBlockingArea& rhs) const;
    bool operator !=(const NonBlockingArea& rhs) const;

    [[nodiscard]] bool is_non_blocking(int x, int y) const;

    int x_1 = 0;
    int x_2 = 0;
    int y_1 = 0;
    int y_2 = 0;
};

struct PathContext
{
    [[nodiscard]] bool is_blocked(int x, int y) const;
    [[nodiscard]] bool is_dangerous(int x, int y) const;

    PathCoordinate start_coord;
    PathCoordinate nearest_reachable_tile;
    std::size_t game_time{0};
    std::vector<PathNode> nodes;
    std::unique_ptr<PathBlockingMap> blocking_map;
    NonBlockingArea non_blocking;
};
extern std::vector<PathContext> path_contexts;

void path_table_reset();

/// Takes the current best node, and removes from the node heap.
PathNode get_best_node(std::vector<PathNode>& nodes);

/// Estimate the distance to the target point
unsigned estimate_distance(PathCoordinate start, PathCoordinate finish);

#endif // WARZONE2100_ASTAR_H
