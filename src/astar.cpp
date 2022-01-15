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
 *  @file astar.cpp
 *  Function definitions implementing A*-based pathfinding for droids
 *
 *  How this works:
 *  * The first time (in a given tick) that some droid  wants to pathfind to a particular
 *    destination, the A* algorithm from source to destination is used.  The desired
 *    destination,  and the nearest  reachable point  to the  destination is stored in a
 *    context.
 *  * The second time (in a given tick) that some droid wants to pathfind to a particular
 *    destination, the appropriate context is found, and the A* algorithm is used to
 *    find a path from the nearest reachable point to the destination (which was saved
 *    earlier), to the source.
 *  * For subsequent iterations, the path is looked up in appropriate context. If the
 *    path is not already known, the A* weights are adjusted, and the previous A* pathfinding
 *    is continued until the new source is reached.  If the new source is not reached,
 *    the droid is  on a  different island than the previous droid, and pathfinding is
 *    restarted from the first step.
 *
 *  Up to 30 pathfinding maps from A* are cached, in an LRU list. The `PathNode` heap
 *  contains the priority-heap-sorted nodes which are to be explored.  The path back is
 *  stored in the `ExploredTile` 2D array of tiles.
 */

#include "astar.h"
#include "map.h"

bool isHumanPlayer(unsigned);

PathCoord::PathCoord(int x, int y)
  : x{x}, y{y}
{
}

PathNode::PathNode(PathCoord coord, unsigned dist, unsigned est)
  : path_coordinate{coord},
    distance_from_start{dist},
    estimated_distance_to_end{est}
{
}

bool PathNode::operator <(const PathNode& rhs) const
{
  if (estimated_distance_to_end != rhs.estimated_distance_to_end)
    return estimated_distance_to_end > rhs.estimated_distance_to_end;

  if (distance_from_start != rhs.distance_from_start)
    return distance_from_start < rhs.distance_from_start;

  if (path_coordinate.x != rhs.path_coordinate.x)
    return path_coordinate.x < rhs.path_coordinate.x;

  return path_coordinate.y < rhs.path_coordinate.y;
}

NonBlockingArea::NonBlockingArea(const StructureBounds& bounds)
  : x_1(bounds.map.x),
    x_2(bounds.map.x + bounds.size.x),
    y_1(bounds.map.y),
    y_2(bounds.map.y + bounds.size.y)
{
}

bool NonBlockingArea::is_non_blocking(int x, int y) const
{
  return x >= x_1 && x < x_2 &&
         y >= y_1 && y < y_2;
}

bool NonBlockingArea::is_non_blocking(PathCoord coord) const
{
  return is_non_blocking(coord.x, coord.y);
}

bool PathContext::is_blocked(int x, int y) const
{
  if (destination_bounds.is_non_blocking(x, y))  {
    return false;
  }
  return x < 0 || y < 0 || x >= mapWidth || y >= mapHeight ||
         blocking_map->map[x + y * mapWidth];
}

bool PathContext::is_dangerous(int x, int y) const
{
  return !blocking_map->threat_map.empty() &&
         blocking_map->threat_map[x + y * mapWidth];
}

void PathContext::reset(const PathBlockingMap& blocking,
                        PathCoord start,
                        NonBlockingArea bounds)
{
  blocking_map = std::make_unique<PathBlockingMap>(blocking);
  start_coord = start;
  destination_bounds = bounds;
  game_time = blocking_map->type.game_time;

  // reset route
  nodes.clear();

  // iteration should not match value of iteration in `map`
  if (++iteration == UINT16_MAX) {
    // there are no values of iteration guaranteed not to exist
    // in `map`, so clear it
    map.clear();
    iteration = 0;
  }
  // ensure the correct size is allocated for `map`,
  // corresponding to the total area of the game map
  map.resize(static_cast<std::size_t>(mapWidth) * static_cast<std::size_t>(mapHeight));
}

void PathContext::init(PathBlockingMap& blocking, PathCoord start,
                       PathCoord real_start, PathCoord end, NonBlockingArea non_blocking)
{
  reset(blocking, start, non_blocking);

  // add the start node to the open list
  generate_new_node(*this, end, real_start,
                    real_start, 0);
}

bool PathContext::matches(PathBlockingMap& blocking, PathCoord start, NonBlockingArea dest) const
{
  // Must check myGameTime == blockingMap_->type.gameTime, otherwise
  // blockingMap could be a deleted pointer which coincidentally
  // compares equal to the valid pointer blockingMap_.
  return game_time == blocking.type.game_time &&
  blocking_map.get() == &blocking &&
  start == start_coord &&
  dest == destination_bounds;
}

//struct PathExploredTile
//{
//	PathExploredTile() : iteration(0xFFFF), dx(0), dy(0), dist(0), visited(false)
//	{
//	}
//
//	uint16_t iteration;
//	int8_t dx, dy; // Offset from previous point in the route.
//	unsigned dist; // Shortest known distance to tile.
//	bool visited;
//};

//// Data structures used for pathfinding, can contain cached results.
//struct PathfindContext
//{
//	PathfindContext() : myGameTime(0), iteration(0), blockingMap(nullptr)
//	{
//	}
//
//	bool isBlocked(int x, int y) const
//	{
//		if (dstIgnore.isNonblocking(x, y))
//		{
//			return false;
//			// The path is actually blocked here by a structure, but ignore it since it's where we want to go (or where we came from).
//		}
//		// Not sure whether the out-of-bounds check is needed, can only happen if pathfinding is started on a blocking tile (or off the map).
//		return x < 0 || y < 0 || x >= mapWidth || y >= mapHeight || blockingMap->map[x + y * mapWidth];
//	}
//
//	bool isDangerous(int x, int y) const
//	{
//		return !blockingMap->dangerMap.empty() && blockingMap->dangerMap[x + y * mapWidth];
//	}

//	bool matches(std::shared_ptr<PathBlockingMap>& blockingMap_, PathCoord tileS_, PathNonblockingArea dstIgnore_) const
//	{
//		// Must check myGameTime == blockingMap_->type.gameTime, otherwise blockingMap could be a deleted pointer which coincidentally compares equal to the valid pointer blockingMap_.
//		return myGameTime == blockingMap_->type.gameTime && blockingMap == blockingMap_ && tileS == tileS_ && dstIgnore
//			== dstIgnore_;
//	}

//	void assign(std::shared_ptr<PathBlockingMap>& blockingMap_, PathCoord tileS_, PathNonblockingArea dstIgnore_)
//	{
//		blockingMap = blockingMap_;
//		tileS = tileS_;
//		dstIgnore = dstIgnore_;
//		myGameTime = blockingMap->type.gameTime;
//		nodes.clear();
//
//		// Make the iteration not match any value of iteration in map.
//		if (++iteration == 0xFFFF)
//		{
//			map.clear(); // There are no values of iteration guaranteed not to exist in map, so clear the map.
//			iteration = 0;
//		}
//		map.resize(static_cast<size_t>(mapWidth) * static_cast<size_t>(mapHeight));
//		// Allocate space for map, if needed.
//	}
//
//	PathCoord tileS; // Start tile for pathfinding. (May be either source or target tile.)
//	uint32_t myGameTime;
//
//	PathCoord nearestCoord; // Nearest reachable tile to destination.
//
//	/** Counter to implement lazy deletion from map.
//	 *
//	 *  @see fpathTableReset
//	 */
//	uint16_t iteration;
//
//	std::vector<PathNode> nodes; ///< Edge of explored region of the map.
//	std::vector<PathExploredTile> map; ///< Map, with paths leading back to tileS.
//	std::shared_ptr<PathBlockingMap> blockingMap; ///< Map of blocking tiles for the type of object which needs a path.
//	PathNonblockingArea dstIgnore; ///< Area of structure at destination which should be considered nonblocking.
//};

/// Game time for all blocking maps in fpathBlockingMaps.
static std::size_t fpathCurrentGameTime;

void fpathHardTableReset()
{
  path_contexts.clear();
  blocking_maps.clear();
}

PathNode get_best_node(std::vector<PathNode>& nodes)
{
  // find the node with the lowest distance
  // if equal totals, give preference to node closer to target
  const auto best = nodes.front();
  // remove the node from the list
  std::pop_heap(nodes.begin(), nodes.end());
  // move the best node from the front of nodes to the back of nodes,
  // preserving the heap properties, setting the front to the next best node.
  nodes.pop_back();
  return best;
}
///// Takes the current best node, and removes from the node heap.
//static inline PathNode fpathTakeNode(std::vector<PathNode>& nodes)
//{
//	// find the node with the lowest distance
//	// if equal totals, give preference to node closer to target
//	PathNode ret = nodes.front();
//
//	// remove the node from the list
//	std::pop_heap(nodes.begin(), nodes.end());
//	// Move the best node from the front of nodes to the back of nodes, preserving the heap properties, setting the front to the next best node.
//	nodes.pop_back(); // Pop the best node (which we will be returning).
//
//	return ret;
//}

unsigned estimate_distance(PathCoord start, PathCoord finish)
{
  const auto x_delta = std::abs(start.x - finish.x);
  const auto y_delta = std::abs(start.y - finish.y);

  /**
   * cost of moving horizontal/vertical = 70*2,
   * cost of moving diagonal = 99*2,
   * 99/70 = 1.41428571... ≈ √2 = 1.41421356...
   */
  return std::min(x_delta, y_delta) * (198 - 140) +
         std::max(x_delta, y_delta) * 140;
}
/** Estimate the distance to the target point
// */
//static inline unsigned WZ_DECL_PURE fpathEstimate(PathCoord s, PathCoord f)
//{
//	// Cost of moving horizontal/vertical = 70*2, cost of moving diagonal = 99*2, 99/70 = 1.41428571... ≈ √2 = 1.41421356...
//	unsigned xDelta = abs(s.x - f.x), yDelta = abs(s.y - f.y);
//	return std::min(xDelta, yDelta) * (198 - 140) + std::max(xDelta, yDelta) * 140;
//}

unsigned estimate_distance_precise(PathCoord start, PathCoord finish)
{
  /**
   * cost of moving horizontal/vertical = 70*2,
   * cost of moving diagonal = 99*2,
   * 99/70 = 1.41428571... ≈ √2 = 1.41421356...
   */
  return iHypot((start.x - finish.x) * 140,
                (start.y - finish.y) * 140);
}
//static inline unsigned WZ_DECL_PURE fpathGoodEstimate(PathCoord s, PathCoord f)
//{
//	// Cost of moving horizontal/vertical = 70*2, cost of moving diagonal = 99*2, 99/70 = 1.41428571... ≈ √2 = 1.41421356...
//	return iHypot((s.x - f.x) * 140, (s.y - f.y) * 140);
//}

void generate_new_node(PathContext& context, PathCoord destination,
                       PathCoord current_pos, PathCoord prev_pos,
                       unsigned prev_dist)
{
  const auto cost_factor = context.is_dangerous(current_pos.x, current_pos.y);
  const auto dist = prev_dist +
                    estimate_distance(prev_pos, current_pos) * cost_factor;
  auto node = PathNode{current_pos, dist,
                       dist + estimate_distance_precise(current_pos, destination)};

  auto delta = Vector2i{current_pos.x - prev_pos.x,
                        current_pos.y - prev_pos.y} * 64;
  const bool is_diagonal = delta.x && delta.y;

  auto& explored = context.map[current_pos.x + current_pos.y * mapWidth];
  if (explored.iteration == context.iteration) {
    if (explored.visited) {
      // already visited this tile. Do nothing.
      return;
    }
    auto delta_b = Vector2i{explored.x_diff, explored.y_diff};
    auto delta_ = delta - delta_b;
    // Vector pointing from current considered source tile leading to pos,
    // to the previously considered source tile leading to pos.
    if (abs(delta_.x) + abs(delta_.y) == 64) {
      /**
       * prevPos is tile A or B, and pos is tile P. We were previously called
       * with prevPos being tile B or A, and pos tile P.
       * We want to find the distance to tile P, taking into account that
       * the actual shortest path involves coming from somewhere between
       * tile A and tile B.
       * +---+---+
       * |   | P |
       * +---+---+
       * | A | B |
       * +---+---+
       */

      unsigned distA = node.distance_from_start -
                       (is_diagonal ? 198 : 140) * cost_factor;
      // If diagonal, node is A and explored is B.
      unsigned distB = explored.distance -
                       (is_diagonal ? 140 : 198) * cost_factor;
      if (!is_diagonal) {
        std::swap(distA, distB);
        std::swap(delta, delta_b);
      }
      int gradientX = int(distB - distA) / cost_factor;
      if (gradientX > 0 && gradientX <= 98) {
        // 98 = floor(140/√2), so gradientX <= 98 is needed so that
        // gradientX < gradientY.

        // the distance gradient is now known to be somewhere between
        // the direction from A to P and the direction from B to P.
        static const uint8_t gradYLookup[99] = {
                140, 140, 140, 140, 140, 140, 140, 140, 140, 140, 140, 140, 139, 139, 139, 139, 139, 139, 139, 139,
                139, 138, 138, 138, 138, 138, 138, 137, 137, 137, 137, 137, 136, 136, 136, 136, 135, 135, 135, 134,
                134, 134, 134, 133, 133, 133, 132, 132, 132, 131, 131, 130, 130, 130, 129, 129, 128, 128, 127, 127,
                126, 126, 126, 125, 125, 124, 123, 123, 122, 122, 121, 121, 120, 119, 119, 118, 118, 117, 116, 116,
                115, 114, 113, 113, 112, 111, 110, 110, 109, 108, 107, 106, 106, 105, 104, 103, 102, 101, 100
        };
        // = sqrt(140² -  gradientX²), rounded to nearest integer
        int gradientY = gradYLookup[gradientX];
        unsigned distP = gradientY * cost_factor + distB;
        node.estimated_distance_to_end -= node.distance_from_start - distP;
        node.distance_from_start = distP;
        delta = (delta * gradientX + delta_b * (gradientY - gradientX)) / gradientY;
      }
    }
    if (explored.distance <= node.distance_from_start) {
      // a different path to this tile is shorter.
      return;
    }
  }
  // remember where we have been, and the way back
  explored.iteration = context.iteration;
  explored.x_diff = delta.x;
  explored.y_diff = delta.y;
  explored.distance = node.distance_from_start;
  explored.visited = false;

  // add the node to the heap
  context.nodes.push_back(node);
  std::push_heap(context.nodes.begin(), context.nodes.end());
}
/** Generate a new node
 */
//static inline void fpathNewNode(PathfindContext& context, PathCoord dest, PathCoord pos, unsigned prevDist,
//                                PathCoord prevPos)
//{
//	ASSERT_OR_RETURN(, (unsigned)pos.x < (unsigned)mapWidth && (unsigned)pos.y < (unsigned)mapHeight,
//	                   "X (%d) or Y (%d) coordinate for path finding node is out of range!", pos.x, pos.y);
//
//	// Create the node.
//	PathNode node;
//	unsigned costFactor = context.isDangerous(pos.x, pos.y) ? 5 : 1;
//	node.p = pos;
//	node.dist = prevDist + fpathEstimate(prevPos, pos) * costFactor;
//	node.est = node.dist + fpathGoodEstimate(pos, dest);
//
//	Vector2i delta = Vector2i(pos.x - prevPos.x, pos.y - prevPos.y) * 64;
//	bool isDiagonal = delta.x && delta.y;
//
//	PathExploredTile& expl = context.map[pos.x + pos.y * mapWidth];
//	if (expl.iteration == context.iteration)
//	{
//		if (expl.visited)
//		{
//			return; // Already visited this tile. Do nothing.
//		}
//		Vector2i deltaA = delta;
//		Vector2i deltaB = Vector2i(expl.dx, expl.dy);
//		Vector2i deltaDelta = deltaA - deltaB;
//		// Vector pointing from current considered source tile leading to pos, to the previously considered source tile leading to pos.
//		if (abs(deltaDelta.x) + abs(deltaDelta.y) == 64)
//		{
//			// prevPos is tile A or B, and pos is tile P. We were previously called with prevPos being tile B or A, and pos tile P.
//			// We want to find the distance to tile P, taking into account that the actual shortest path involves coming from somewhere between tile A and tile B.
//			// +---+---+
//			// |   | P |
//			// +---+---+
//			// | A | B |
//			// +---+---+
//			unsigned distA = node.dist - (isDiagonal ? 198 : 140) * costFactor;
//			// If isDiagonal, node is A and expl is B.
//			unsigned distB = expl.dist - (isDiagonal ? 140 : 198) * costFactor;
//			if (!isDiagonal)
//			{
//				std::swap(distA, distB);
//				std::swap(deltaA, deltaB);
//			}
//			int gradientX = int(distB - distA) / costFactor;
//			if (gradientX > 0 && gradientX <= 98)
//			// 98 = floor(140/√2), so gradientX <= 98 is needed so that gradientX < gradientY.
//			{
//				// The distance gradient is now known to be somewhere between the direction from A to P and the direction from B to P.
//				static const uint8_t gradYLookup[99] = {
//					140, 140, 140, 140, 140, 140, 140, 140, 140, 140, 140, 140, 139, 139, 139, 139, 139, 139, 139, 139,
//					139, 138, 138, 138, 138, 138, 138, 137, 137, 137, 137, 137, 136, 136, 136, 136, 135, 135, 135, 134,
//					134, 134, 134, 133, 133, 133, 132, 132, 132, 131, 131, 130, 130, 130, 129, 129, 128, 128, 127, 127,
//					126, 126, 126, 125, 125, 124, 123, 123, 122, 122, 121, 121, 120, 119, 119, 118, 118, 117, 116, 116,
//					115, 114, 113, 113, 112, 111, 110, 110, 109, 108, 107, 106, 106, 105, 104, 103, 102, 101, 100
//				};
//				int gradientY = gradYLookup[gradientX]; // = sqrt(140² -  gradientX²), rounded to nearest integer
//				unsigned distP = gradientY * costFactor + distB;
//				node.est -= node.dist - distP;
//				node.dist = distP;
//				delta = (deltaA * gradientX + deltaB * (gradientY - gradientX)) / gradientY;
//			}
//		}
//		if (expl.dist <= node.dist)
//		{
//			return; // A different path to this tile is shorter.
//		}
//	}
//
//	// Remember where we have been, and remember the way back.
//	expl.iteration = context.iteration;
//	expl.dx = delta.x;
//	expl.dy = delta.y;
//	expl.dist = node.dist;
//	expl.visited = false;
//
//	// Add the node to the node heap.
//	context.nodes.push_back(node); // Add the new node to nodes.
//	std::push_heap(context.nodes.begin(), context.nodes.end()); // Move the new node to the right place in the heap.
//}

void recalculate_estimates(PathContext& context, PathCoord tile)
{
  for (auto& node : context.nodes)
  {
    node.estimated_distance_to_end = node.distance_from_start +
                                     estimate_distance_precise(node.path_coordinate, tile);
  }
  // Changing the estimates breaks the heap ordering. Fix the heap ordering.
  std::make_heap(context.nodes.begin(), context.nodes.end());
}
///// Recalculates estimates to new tileF tile.
//static void fpathAStarReestimate(PathfindContext& context, PathCoord tileF)
//{
//	for (auto& node : context.nodes)
//	{
//		node.est = node.dist + fpathGoodEstimate(node.p, tileF);
//	}
//
//	// Changing the estimates breaks the heap ordering. Fix the heap ordering.
//	std::make_heap(context.nodes.begin(), context.nodes.end());
//}

PathCoord find_nearest_explored_tile(PathContext& context, PathCoord tile)
{
  unsigned nearest_dist = UINT32_MAX;
  auto nearest_coord = PathCoord{0, 0};
  bool target_found = false;
  while (!target_found)
  {
    auto node = get_best_node(context.nodes);
    if (context.map[node.path_coordinate.x + node.path_coordinate.y * mapWidth].visited) {
      // already visited
      continue;
    }

    // now mark as visited
    context.map[node.path_coordinate.x + node.path_coordinate.y * mapWidth].visited = true;

    // note the nearest node to the target so far
    if (node.estimated_distance_to_end - node.distance_from_start < nearest_dist) {
      nearest_coord = node.path_coordinate;
      nearest_dist = node.estimated_distance_to_end - node.distance_from_start;
    }

    if (node.path_coordinate == tile) {
      // target reached
      nearest_coord = node.path_coordinate;
      target_found = true;
    }

    for (auto direction = 0; direction < ARRAY_SIZE(offset); ++direction)
    {
      // try a new location
      auto x = node.path_coordinate.x + offset[direction].x;
      auto y = node.path_coordinate.y + offset[direction].y;

      /**
       *         5  6  7
       *			     \|/
       *			   4 -I- 0
       *			     /|\
       *			   3  2  1
       * odd: orthogonal-adjacent tiles even: non-orthogonal-adjacent tiles
       */
      if (direction % 2 != 0 && !context.destination_bounds.
              is_non_blocking(node.path_coordinate.x, node.path_coordinate.y) &&
          !context.destination_bounds.is_non_blocking(x, y)) {

        // cannot cut corners
        auto x_2 = node.path_coordinate.x + offset[(direction + 1) % 8].x;
        auto y_2 = node.path_coordinate.y + offset[(direction + 1) % 8].y;
        if (context.is_blocked(x_2, y_2)) {
          continue;
        }
        x_2 = node.path_coordinate.x + offset[(direction + 7) % 8].x;
        y_2 = node.path_coordinate.y + offset[(direction + 7) % 8].y;
        if (context.is_blocked(x_2, y_2)) {
          continue;
        }
      }

      // see if node is a blocking tile
      if (context.is_blocked(x, y)) {
        // blocked -- skip
        continue;
      }

      // now insert the point into the appropriate list, if not already visited.
      generate_new_node(context, tile, PathCoord{x, y},
                        node.path_coordinate, node.distance_from_start);
    }
  }
  return nearest_coord;
}
///// Returns nearest explored tile to tileF.
//static PathCoord fpathAStarExplore(PathfindContext& context, PathCoord tileF)
//{
//	PathCoord nearestCoord(0, 0);
//	unsigned nearestDist = 0xFFFFFFFF;
//
//	// search for a route
//	bool foundIt = false;
//	while (!context.nodes.empty() && !foundIt)
//	{
//		PathNode node = fpathTakeNode(context.nodes);
//		if (context.map[node.p.x + node.p.y * mapWidth].visited)
//		{
//			continue; // Already been here.
//		}
//		context.map[node.p.x + node.p.y * mapWidth].visited = true;
//
//		// note the nearest node to the target so far
//		if (node.est - node.dist < nearestDist)
//		{
//			nearestCoord = node.p;
//			nearestDist = node.est - node.dist;
//		}
//
//		if (node.p == tileF)
//		{
//			// reached the target
//			nearestCoord = node.p;
//			foundIt = true;
//			// Break out of loop, but not before inserting neighbour nodes, since the neighbours may be important if the context gets reused.
//		}
//
//		// loop through possible moves in 8 directions to find a valid move
//		for (unsigned dir = 0; dir < ARRAY_SIZE(aDirOffset); ++dir)
//		{
//			// Try a new location
//			int x = node.p.x + aDirOffset[dir].x;
//			int y = node.p.y + aDirOffset[dir].y;
//
//			/*
//			   5  6  7
//			     \|/
//			   4 -I- 0
//			     /|\
//			   3  2  1
//			   odd:orthogonal-adjacent tiles even:non-orthogonal-adjacent tiles
//			*/
//			if (dir % 2 != 0 && !context.dstIgnore.isNonblocking(node.p.x, node.p.y) && !context.dstIgnore.
//				isNonblocking(x, y))
//			{
//				int x2, y2;
//
//				// We cannot cut corners
//				x2 = node.p.x + aDirOffset[(dir + 1) % 8].x;
//				y2 = node.p.y + aDirOffset[(dir + 1) % 8].y;
//				if (context.isBlocked(x2, y2))
//				{
//					continue;
//				}
//				x2 = node.p.x + aDirOffset[(dir + 7) % 8].x;
//				y2 = node.p.y + aDirOffset[(dir + 7) % 8].y;
//				if (context.isBlocked(x2, y2))
//				{
//					continue;
//				}
//			}
//
//			// See if the node is a blocking tile
//			if (context.isBlocked(x, y))
//			{
//				// tile is blocked, skip it
//				continue;
//			}
//
//			// Now insert the point into the appropriate list, if not already visited.
//			fpathNewNode(context, tileF, PathCoord(x, y), node.dist, node.p);
//		}
//	}
//
//	return nearestCoord;
//}
//
//static void fpathInitContext(PathfindContext& context, std::shared_ptr<PathBlockingMap>& blockingMap, PathCoord tileS,
//                             PathCoord tileRealS, PathCoord tileF, PathNonblockingArea dstIgnore)
//{
//	context.assign(blockingMap, tileS, dstIgnore);
//
//	// Add the start point to the open list
//	fpathNewNode(context, tileF, tileRealS, 0, tileRealS);
//	ASSERT(!context.nodes.empty(), "fpathNewNode failed to add node.");
//}

ASTAR_RESULT find_astar_route(Movement& movement, PathJob& path_job)
{
  PathCoord end {};
  auto result = ASTAR_RESULT::OK;
  auto must_reverse = true;

  const auto origin_tile = PathCoord{map_coord(path_job.origin.x),
                                     map_coord(path_job.origin.y)};

  const auto destination_tile = PathCoord{map_coord(path_job.destination.x),
                                          map_coord(path_job.destination.y)};

  auto it = std::find_if(path_contexts.begin(), path_contexts.end(),
                         [&end, &must_reverse](const auto& context)
  {
      if (context.map[origin_tile.x + origin_tile.y * mapWidth].iteration ==
          context.map[origin_tile.x + origin_tile.y * mapWidth].visited)  {
        // already know the path
        end = origin_tile;
      } else {
        // continue previous exploration
        recalculate_estimates(context, origin_tile);
        end = find_nearest_explored_tile(context, origin_tile);
      }

      if (end != origin_tile)  {
        // `origin_tile` turned out to be on a different island than what this
        // context was used for, so can't use this context data after all.
        return false;
      }
      // we have the path from the nearest reachable tile
      // to `destination_tile`, to `origin_tile`.
      must_reverse = false;

      // found the path -- stop searching
      return true;
  });

  if (it == path_contexts.end())  {
    // did not find an appropriate route so make one
    if (path_contexts.size() < 30)  {
      path_contexts.emplace_back(PathContext());
    }

    /**
     * init a new context, overwriting the oldest one if we are caching too many.
     * we will be searching from orig to dest, since we don't know where the
     * nearest reachable tile to dest is.
     */
    auto new_context = PathContext();
    new_context.init(--it, path_job.blockingMap, origin_tile, origin_tile,
                     destination_tile, path_job.dstStructure);
    end = find_nearest_explored_tile(it, destination_tile);
    it->nearest_reachable_tile = end;
  }

  // return the nearest route if no optimal one was found
  if (it != destination_tile)  {
    result = ASTAR_RESULT::PARTIAL;
  }
  static std::vector<Vector2i> route;
  route.clear();

  auto start = Vector2i{world_coord(end.x) + TILE_UNITS / 2,
                        world_coord(end.y) + TILE_UNITS / 2};

  for(;;)
  {
    route.push_back(start);
    auto& tile = it->map[map_coord(start.x) +
                         map_coord(start.y) * mapWidth];
    auto next = start - Vector2i{tile.x_diff, tile.y_diff} *
                        (TILE_UNITS / 64);
    auto map = map_coord(next);
    // 1 if `next` is on the bottom edge of the tile, -1 if on the left
    auto x = next.x - world_coord(map.x) > TILE_UNITS / 2 ? 1 : -1;
    // 1 if `next` is on the bottom edge of the tile, -1 if on the top
    auto y = next.y - world_coord(map.y) > TILE_UNITS / 2 ? 1 : -1;

    if (it->is_blocked(map.x + x, map.y))  {
      // point too close to a blocking tile on left or right side,
      // so move the point to the middle.
      next.x = world_coord(map.x) + TILE_UNITS / 2;
    }
    if (it->is_blocked(map.x, map.y + y)) {
      // point too close to a blocking tile on rop or bottom side,
      // so move the point to the middle.
      next.y = world_coord(map.y) + TILE_UNITS / 2;
    }
    if (map_coord(start) == Vector2i{it->start_coord.x, it->start_coord.y} ||
        start == next)  {
      // we stopped moving, because we reached the destination or
      // the closest reachable tile to it->start. give up now.
      break;
    }
  }
  if (result == ASTAR_RESULT::OK)  {
    // found exact path, so use the exact coordinates for
    // last point. no reason to lose precision
    auto coord = Vector2i{path_job.destination.x,
                          path_job.destination.y};

    if (must_reverse)  {
      route.front() = coord;
    } else  {
      route.back() = coord;
    }
  }
  // reallocate correct memory
  movement.path.resize(route.size());

  if (must_reverse) {
    std::copy(route.rbegin(), route.rend(), movement.path.data());

		// if blocked, searching from `destination_tile` to
    // `origin_tile` wouldn't find the origin tile.
    if (!it->is_blocked(origin_tile.x, origin_tile.y))  {

			// next time, search starting from the nearest reachable
      // tile to the destination.
			it->init(path_job.blockingMap, destination_tile,
               it->nearest_reachable_tile, origin_tile, dstIgnore);
    }
  } else  {
    std::copy(route.begin(), route.end(), movement.path.data());
  }

  // move context to beginning of last recently used list.
  if (it != path_contexts.begin())  {
    path_contexts.splice(path_contexts.begin(), path_contexts, it);
  }

  movement.destination = movement.path[route.size() - 1];
  return result;
}

//ASTAR_RESULT fpathAStarRoute(MOVE_CONTROL* psMove, PATHJOB* psJob)
//{
//	ASTAR_RESULT retval = ASR_OK;
//
//	bool mustReverse = true;
//
//	const PathCoord tileOrig(map_coord(psJob->origX), map_coord(psJob->origY));
//	const PathCoord tileDest(map_coord(psJob->destX), map_coord(psJob->destY));
//	const PathNonblockingArea dstIgnore(psJob->dstStructure);
//
//	PathCoord endCoord; // Either nearest coord (mustReverse = true) or orig (mustReverse = false).
//
//	std::list<PathfindContext>::iterator contextIterator = fpathContexts.begin();
//	for (contextIterator = fpathContexts.begin(); contextIterator != fpathContexts.end(); ++contextIterator)
//	{
//		if (!contextIterator->matches(psJob->blockingMap, tileDest, dstIgnore))
//		{
//			// This context is not for the same droid type and same destination.
//			continue;
//		}
//
//		// We have tried going to tileDest before.
//
//		if (contextIterator->map[tileOrig.x + tileOrig.y * mapWidth].iteration == contextIterator->iteration
//			&& contextIterator->map[tileOrig.x + tileOrig.y * mapWidth].visited)
//		{
//			// Already know the path from orig to dest.
//			endCoord = tileOrig;
//		}
//		else
//		{
//			// Need to find the path from orig to dest, continue previous exploration.
//			fpathAStarReestimate(*contextIterator, tileOrig);
//			endCoord = fpathAStarExplore(*contextIterator, tileOrig);
//		}
//
//		if (endCoord != tileOrig)
//		{
//			// orig turned out to be on a different island than what this context was used for, so can't use this context data after all.
//			continue;
//		}
//
//		mustReverse = false; // We have the path from the nearest reachable tile to dest, to orig.
//		break; // Found the path! Don't search more contexts.
//	}
//
//	if (contextIterator == fpathContexts.end())
//	{
//		// We did not find an appropriate context. Make one.
//
//		if (fpathContexts.size() < 30)
//		{
//			fpathContexts.push_back(PathfindContext());
//		}
//		--contextIterator;
//
//		// Init a new context, overwriting the oldest one if we are caching too many.
//		// We will be searching from orig to dest, since we don't know where the nearest reachable tile to dest is.
//		fpathInitContext(*contextIterator, psJob->blockingMap, tileOrig, tileOrig, tileDest, dstIgnore);
//		endCoord = fpathAStarExplore(*contextIterator, tileDest);
//		contextIterator->nearestCoord = endCoord;
//	}
//
//	PathfindContext& context = *contextIterator;
//
//	// return the nearest route if no actual route was found
//	if (context.nearestCoord != tileDest)
//	{
//		retval = ASR_NEAREST;
//	}
//
//	// Get route, in reverse order.
//	static std::vector<Vector2i> path; // Declared static to save allocations.
//	path.clear();
//
//	Vector2i newP(0, 0);
//	for (Vector2i p(world_coord(endCoord.x) + TILE_UNITS / 2, world_coord(endCoord.y) + TILE_UNITS / 2); true; p = newP)
//	{
//		ASSERT_OR_RETURN(ASR_FAILED, worldOnMap(p.x, p.y), "Assigned XY coordinates (%d, %d) not on map!", (int)p.x,
//		                 (int)p.y);
//		ASSERT_OR_RETURN(ASR_FAILED, path.size() < (static_cast<size_t>(mapWidth) * static_cast<size_t>(mapHeight)),
//		                 "Pathfinding got in a loop.");
//
//		path.push_back(p);
//
//		PathExploredTile& tile = context.map[map_coord(p.x) + map_coord(p.y) * mapWidth];
//		newP = p - Vector2i(tile.dx, tile.dy) * (TILE_UNITS / 64);
//		Vector2i mapP = map_coord(newP);
//		int xSide = newP.x - world_coord(mapP.x) > TILE_UNITS / 2 ? 1 : -1;
//		// 1 if newP is on right-hand side of the tile, or -1 if newP is on the left-hand side of the tile.
//		int ySide = newP.y - world_coord(mapP.y) > TILE_UNITS / 2 ? 1 : -1;
//		// 1 if newP is on bottom side of the tile, or -1 if newP is on the top side of the tile.
//		if (context.isBlocked(mapP.x + xSide, mapP.y))
//		{
//			newP.x = world_coord(mapP.x) + TILE_UNITS / 2;
//			// Point too close to a blocking tile on left or right side, so move the point to the middle.
//		}
//		if (context.isBlocked(mapP.x, mapP.y + ySide))
//		{
//			newP.y = world_coord(mapP.y) + TILE_UNITS / 2;
//			// Point too close to a blocking tile on rop or bottom side, so move the point to the middle.
//		}
//		if (map_coord(p) == Vector2i(context.tileS.x, context.tileS.y) || p == newP)
//		{
//			break;
//			// We stopped moving, because we reached the destination or the closest reachable tile to context.tileS. Give up now.
//		}
//	}
//	if (retval == ASR_OK)
//	{
//		// found exact path, so use exact coordinates for last point, no reason to lose precision
//		Vector2i v(psJob->destX, psJob->destY);
//		if (mustReverse)
//		{
//			path.front() = v;
//		}
//		else
//		{
//			path.back() = v;
//		}
//	}
//
//	// Allocate memory
//	psMove->asPath.resize(path.size());
//
//	// get the route in the correct order
//	// If as I suspect this is to reverse the list, then it's my suspicion that
//	// we could route from destination to source as opposed to source to
//	// destination. We could then save the reversal. to risky to try now...Alex M
//	//
//	// The idea is impractical, because you can't guarentee that the target is
//	// reachable. As I see it, this is the reason why psNearest got introduced.
//	// -- Dennis L.
//	//
//	// If many droids are heading towards the same destination, then destination
//	// to source would be faster if reusing the information in nodeArray. --Cyp
//	if (mustReverse)
//	{
//		// Copy the list, in reverse.
//		std::copy(path.rbegin(), path.rend(), psMove->asPath.data());
//
//		if (!context.isBlocked(tileOrig.x, tileOrig.y))
//		// If blocked, searching from tileDest to tileOrig wouldn't find the tileOrig tile.
//		{
//			// Next time, search starting from nearest reachable tile to the destination.
//			fpathInitContext(context, psJob->blockingMap, tileDest, context.nearestCoord, tileOrig, dstIgnore);
//		}
//	}
//	else
//	{
//		// Copy the list.
//		std::copy(path.begin(), path.end(), psMove->asPath.data());
//	}
//
//	// Move context to beginning of last recently used list.
//	if (contextIterator != fpathContexts.begin()) // Not sure whether or not the splice is a safe noop, if equal.
//	{
//		fpathContexts.splice(fpathContexts.begin(), fpathContexts, contextIterator);
//	}
//
//	psMove->destination = psMove->asPath[path.size() - 1];
//
//	return retval;
//}

void fpathSetBlockingMap(PathJob& path_job)
{
	if (fpathCurrentGameTime != gameTime)  {
		// new tick, remove maps which are no longer needed.
		fpathCurrentGameTime = gameTime;
		blocking_maps.clear();
	}

	// figure out which map we are looking for.
	PathBlockingType type;
	type.game_time = gameTime;
	type.propulsion = path_job.propulsion;
	type.owner = path_job.owner;
	type.move_type = path_job.moveType;

	// find the map.
	auto it = std::find_if(blocking_maps.begin(), blocking_maps.end(),
	                      [&](PathBlockingMap const& map)
  {
    return map == type;
  });

	if (it == blocking_maps.end())  {
		// didn't find the map, so i does not point to a map.
		auto blocking = PathBlockingMap();
		blocking_maps.emplace_back(blocking);

		// `blocking` now points to an empty map with no data. fill the map.
		blocking.type = type;
		std::vector<bool>& map = blocking.map;
		map.resize(static_cast<size_t>(mapWidth) * static_cast<size_t>(mapHeight));
		unsigned checksum_map = 0, checksum_threat_map = 0, factor = 0;
		for (int y = 0; y < mapHeight; ++y)
    {
      for (int x = 0; x < mapWidth; ++x)
      {
        map[x + y * mapWidth] = fpathBaseBlockingTile(x, y, type.propulsion,
                                                      type.owner, type.move_type);
        checksum_map ^= map[x + y * mapWidth] * (factor = 3 * factor + 1);
      }
    }
		if (!isHumanPlayer(type.owner) && type.move_type == FMT_MOVE)  {
			auto threat = blocking.threat_map;
			threat.resize(static_cast<size_t>(mapWidth) *
                    static_cast<size_t>(mapHeight));
			for (int y = 0; y < mapHeight; ++y)
      {
				for (int x = 0; x < mapWidth; ++x)
				{
					threat[x + y * mapWidth] = auxTile(x, y, type.owner) & AUXBITS_THREAT;
          checksum_threat_map ^= threat[x + y * mapWidth] * (factor = 3 * factor + 1);
				}
      }
		}
		syncDebug("blockingMap(%d,%d,%d,%d) = %08X %08X", gameTime, path_job.propulsion, path_job.owner, path_job.moveType,
              checksum_map, checksum_threat_map);

    path_job.blockingMap = std::make_shared<PathBlockingMap>(blocking_maps.back());
	} else  {
		syncDebug("blockingMap(%d,%d,%d,%d) = cached", gameTime, path_job.propulsion, path_job.owner, path_job.moveType);
    path_job.blockingMap = std::make_shared<PathBlockingMap>(*it);
	}
}
