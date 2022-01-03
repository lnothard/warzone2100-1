//
// Created by luna on 15/12/2021.
//

#ifndef WARZONE2100_ASTAR_H
#define WARZONE2100_ASTAR_H

#include <cstdlib>
#include "pathfinding.h"

/**
 * Conversion table from direction to offset
 * dir 0 => x = 0, y = -1
 */
constexpr Vector2i offset[] =
{
  Vector2i(0, 1),
  Vector2i(-1, 1),
  Vector2i(-1, 0),
  Vector2i(-1, -1),
  Vector2i(0, -1),
  Vector2i(1, -1),
  Vector2i(1, 0),
  Vector2i(1, 1),
};

/// The return value of an A* iteration
enum class ASTAR_RESULT
{
	OK,
	FAILED,
	PARTIAL
};

/// Two-dimensional coordinate
struct PathCoord
{
	PathCoord() = default;
	PathCoord(int x, int y);

  /**
   * Default element-wise comparison.
   * Evaluates equality of two coordinates according to
   * the equality of their respective scalar values
   */
	bool operator ==(const PathCoord& rhs) const = default;
	bool operator !=(const PathCoord& rhs) const = default;

	int x, y;
};

/// Represents a route node in the pathfinding table
struct PathNode
{
  constexpr PathNode(PathCoord coord, unsigned dist, unsigned est)
    : path_coordinate{coord},
      distance_from_start{dist},
      estimated_distance_to_end{est}
  {
  }

  /// Overload for comparing two candidate nodes
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

  /// Current position in route
	PathCoord path_coordinate;

  /// Distance traversed so far
	unsigned distance_from_start;

  /// Estimate of remaining distance. Frequently updated
	unsigned estimated_distance_to_end;
};

struct ExploredTile
{
    ExploredTile() = default;

    /// Exploration progress
    unsigned iteration = UINT16_MAX;

    /// Offset from previous point in route
    int x_diff = 0, y_diff = 0;

    /// Shortest known distance to tile
    unsigned distance = 0;

    /// `true` if previously traversed
    bool visited = false;
};

/// Specifics regarding interaction with a blocking region
struct PathBlockingType
{
    /// Internal representation of game time
    std::size_t game_time;

    /// The player id for the owner of this region
    unsigned owner;

    /// How does this region interact with colliding units?
    MOVE_TYPE move_type;

    /// Which movement class are we blocking?
    PROPULSION_TYPE propulsion;
};

/// Represents a blocking region
struct PathBlockingMap
{
    /// Overload testing equivalence of two distinct blocking regions
    bool operator ==(const PathBlockingType& rhs) const;

    ///
    PathBlockingType type;

    ///
    std::vector<bool> map;

    ///
    std::vector<bool> threat_map;
};

/// Global list of blocking regions
extern std::vector<PathBlockingMap> blocking_maps;

/// Represents a region of the map that may be non-blocking
struct NonBlockingArea
{
    NonBlockingArea() = default;

    /// Construct from existing structure bounds
    explicit NonBlockingArea(const StructureBounds& bounds);

    /// Element-wise comparison
    [[nodiscard]] bool operator ==(const NonBlockingArea& rhs) const = default;
    [[nodiscard]] bool operator !=(const NonBlockingArea& rhs) const = default;

    /**
     * @return `true` if the coordinate is within the bounds
     * of this region, `false` otherwise
     */
    [[nodiscard]] bool is_non_blocking(int x, int y) const;


    [[nodiscard]] bool is_non_blocking(PathCoord coord) const;

    /* Coordinates corresponding to the outer tile edges */
    int x_1 = 0;
    int x_2 = 0;
    int y_1 = 0;
    int y_2 = 0;
};

/// Main pathfinding data structure. Represents a candidate route
struct PathContext
{
    /// Is this position currently blocked?
    [[nodiscard]] bool is_blocked(int x, int y) const;

    /// Are there potential threats in the vicinity?
    [[nodiscard]] bool is_dangerous(int x, int y) const;

    /// Reverts the route to a default state and sets the parameters
    void reset(const PathBlockingMap& blocking_map,
               PathCoord start_coord,
               NonBlockingArea bounds);

    /// How many times have we explored?
    unsigned iteration;

    /// This could be either the source or target tile
    PathCoord start_coord;

    /// Next step towards destination
    PathCoord nearest_reachable_tile;

    /// Should be equal to the game time of the blocking_map
    std::size_t game_time{0};

    /// Edge of explored region
    std::vector<PathNode> nodes;

    /// Paths leading back to start_coord, i.e., route history
    std::vector<ExploredTile> map;

    /// Owning pointer to the list of blocking tiles for this route
    std::unique_ptr<PathBlockingMap> blocking_map;

    /// Destination structure bounds that may be considered non-blocking
    NonBlockingArea destination_bounds;
};

/// Global list of available routes
extern std::vector<PathContext> path_contexts;

/// Clear the global path contexts and blocking maps
void path_table_reset();

/// Takes the current best node, and removes from the node heap.
PathNode get_best_node(std::vector<PathNode>& nodes);

/// Rough estimate of the distance to the target point
unsigned estimate_distance(PathCoord start, PathCoord finish);

/// More precise estimate using hypotenuse calculation
unsigned estimate_distance_precise(PathCoord start, PathCoord finish);

/// Explore a new node
void generate_new_node(PathContext& context, PathCoord destination,
                       PathCoord current_pos, PathCoord prev_pos,
                       unsigned prev_dist);

/// Update the estimates of the given pathfinding context
void recalculate_estimates(PathContext& context, PathCoord tile);

#endif // WARZONE2100_ASTAR_H
