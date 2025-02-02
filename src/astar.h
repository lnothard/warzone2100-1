/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

/**
 * @file astar.h
 * Data types and function declarations for the A* pathfinding component
 */

#ifndef __INCLUDED_SRC_ASTAR_H__
#define __INCLUDED_SRC_ASTAR_H__

#include "lib/framework/vector.h"
#include "stats.h"

struct Movement;
struct PathJob;
struct StructureBounds;


enum class FPATH_MOVETYPE
{
  FMT_MOVE, ///< Move around all obstacles
  FMT_ATTACK, ///< Assume that we will destroy enemy obstacles
  FMT_BLOCK, ///< Don't go through obstacles, not even gates
  COUNT
};

struct PathBlockingType
{
  bool operator==(PathBlockingType const& rhs) const = default;

  unsigned gameTime = 0;
  unsigned player;
  PROPULSION_TYPE propulsion = PROPULSION_TYPE::COUNT;
  FPATH_MOVETYPE moveType = FPATH_MOVETYPE::COUNT;
};

/**
 * Conversion table from direction to offset
 * dir 0 => x = 0, y = -1
 */
static constexpr Vector2i offset[] =
{
  {0, 1},
  {-1, 1},
  {-1, 0},
  {-1, -1},
  {0, -1},
  {1, -1},
  {1, 0},
  {1, 1}
};

/// The return value of an A* iteration
enum class ASTAR_RESULT
{
	OK,
	FAILED,
	PARTIAL
};

/// A two-dimensional coordinate
struct PathCoord
{
  PathCoord() = default;
  PathCoord(int x, int y);

  /**
   * Default element-wise comparison. Evaluates equality of two coordinates
   * according to the equality of their respective scalar values
   */
  bool operator ==(PathCoord const& rhs) const = default;
  bool operator !=(PathCoord const& rhs) const = default;

  int x = 0, y = 0;
};

struct ExploredTile
{
  ExploredTile() = default;

  /// Exploration progress
  unsigned iteration = UINT16_MAX;
  /// The shortest known distance to tile
  unsigned distance = 0;
  /// The offset from the previous point in a route
  int x_diff = 0, y_diff = 0;
  /// Set to `true` if previously traversed
  bool visited = false;
};

/// Represents a route node in the pathfinding table
struct PathNode
{
  PathNode() = default;
  PathNode(PathCoord coord, unsigned dist, unsigned est);

  bool operator<(PathNode const& rhs) const;

  /// The current position in route
  PathCoord path_coordinate;
  /// The total distance traversed so far
  unsigned distance_from_start = 0;
  /// An estimate of the remaining distance. Frequently updated
  unsigned estimated_distance_to_end = 0;
};

/// Represents a region of the map that may be non-blocking
struct NonBlockingArea
{
  NonBlockingArea() = default;

  /// Construct from existing `StructureBounds` object
  explicit NonBlockingArea(StructureBounds const& bounds);

  /// Standard element-wise comparison
  [[nodiscard]] bool operator ==(NonBlockingArea const& rhs) const = default;
  [[nodiscard]] bool operator !=(NonBlockingArea const& rhs) const = default;

  /**
   * @return `true` if the coordinate (x, y) is within the bounds
   * of this region, `false` otherwise
   */
  [[nodiscard]] bool isNonBlocking(int x, int y) const;

  /**
   * @return `true` if `coord` is within the bounds of this
   * region, `false` otherwise
   */
  [[nodiscard]] bool isNonBlocking(PathCoord coord) const;

  /* Coordinates corresponding to the outer tile edges */
  int x_1 = 0;
  int x_2 = 0;
  int y_1 = 0;
  int y_2 = 0;
};

/// Represents a blocking region
struct PathBlockingMap
{
  /// Overload testing equivalence of two distinct blocking regions
  bool operator ==(PathBlockingType const& rhs) const;

  PathBlockingType type;
  std::vector<bool> map;
  std::vector<bool> threat_map;
}; extern std::vector<PathBlockingMap> blocking_maps;

/// Main pathfinding data structure. Represents a candidate route
struct PathContext
{
  PathContext() = default;
  PathContext(PathBlockingMap& blockingMap, PathCoord start,
              PathCoord realStart, PathCoord end, NonBlockingArea nonBlocking);

  /// @return \c true if the position at \c (x, y) is currently blocked
  [[nodiscard]] bool isBlocked(int x, int y) const;

  /// @return \c true if there are potential threats in the vicinity of \c (x, y)
  [[nodiscard]] bool isDangerous(int x, int y) const;

  /// Reverts the route to a default state and sets the parameters
  void reset(PathBlockingMap const& blocking_map,
             PathCoord start_coord,
             NonBlockingArea bounds);

  /// @return `true` if two path contexts are equivalent
  [[nodiscard]] bool matches(PathBlockingMap& blocking, PathCoord start,
               NonBlockingArea dest) const;


  /// How many times have we explored?
  unsigned iteration = 0;
  /// This could be either the source or target tile
  PathCoord start_coord{0, 0};
  /// The next step towards the destination tile
  PathCoord nearest_reachable_tile {0, 0};
  /// Should be equal to the game time of `blocking_map`
  unsigned game_time = 0;
  /// The edge of the explored region
  std::vector<PathNode> nodes;
  /// Paths leading back to `start_coord`, i.e., the route history
  std::vector<ExploredTile> map;
  /// Pointer (owning) to the list of blocking tiles for this route
  std::unique_ptr<PathBlockingMap> blocking_map;
  /// Destination structure bounds that may be considered non-blocking
  NonBlockingArea destination_bounds;
}; extern std::vector<PathContext> path_contexts;

/**
 * Call from the main thread. Sets `path_job.blocking_map` for later use by
 * the pathfinding thread, generating the required map if not already generated.
 */
void fpathSetBlockingMap(PathJob& path_job);

/**
 * Clear the global path contexts and blocking maps
 *
 * @note Call this on shutdown to prevent memory from leaking,
 *   or if loading/saving, to prevent stale data from being reused
 */
void fpathHardTableReset();

/// Finds the current best node, and removes it from the node heap
PathNode getBestNode(std::vector<PathNode>& nodes);

/// @return a rough estimate of the distance to the target point
unsigned estimateDistance(PathCoord start, PathCoord finish);

/// @return a more precise estimate using hypotenuse calculation
unsigned estimateDistancePrecise(PathCoord start, PathCoord finish);

/// Explore a new node
void generateNewNode(PathContext& context, PathCoord destination,
                       PathCoord current_pos, PathCoord prev_pos,
                       unsigned prev_dist);

/// Update the estimates of the given pathfinding context
void recalculateEstimates(PathContext& context, PathCoord tile);

PathCoord findNearestExploredTile(PathContext& context, PathCoord tile);

/**
 * Use the A* algorithm to find a path
 * @return Whether we successfully found a path
 */
ASTAR_RESULT fpathAStarRoute(Movement& movement, PathJob& pathJob);

#endif // __INCLUDED_SRC_ASTAR_H__
