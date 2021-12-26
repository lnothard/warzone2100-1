//
// Created by luna on 15/12/2021.
//

#ifndef WARZONE2100_ASTAR_H
#define WARZONE2100_ASTAR_H

#include <cstdlib>
#include "pathfinding.h"

enum class ASTAR_RETVAL
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
    unsigned distance;
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

struct NonBlockingArea
{

};

struct PathContext
{
    std::size_t game_time;
    std::vector<PathNode> nodes;
    std::vector<PathBlockingMap> blocking_map;
    NonBlockingArea non_blocking;
};

#endif // WARZONE2100_ASTAR_H
