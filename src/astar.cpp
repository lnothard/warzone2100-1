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

#include "lib/netplay/netplay.h"
#include "wzmaplib/map.h"

#include "astar.h"
#include "fpath.h"
#include "move.h"
#include "structure.h"

int mapWidth, mapHeight;
uint8_t auxTile(int, int, unsigned);
bool isHumanPlayer(unsigned);
Vector2i map_coord(Vector2i);


/// Game time for all blocking maps in fpathBlockingMaps.
static std::size_t fpathCurrentGameTime;

static constexpr auto AUXBITS_THREAT = 0x20; ///< Can hostile players shoot here?

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

bool NonBlockingArea::isNonBlocking(int x, int y) const
{
  return x >= x_1 && x < x_2 &&
         y >= y_1 && y < y_2;
}

bool NonBlockingArea::isNonBlocking(PathCoord coord) const
{
  return isNonBlocking(coord.x, coord.y);
}

bool PathContext::isBlocked(int x, int y) const
{
  if (destination_bounds.isNonBlocking(x, y))  {
    return false;
  }
  return x < 0 || y < 0 || x >= mapWidth || y >= mapHeight ||
         blocking_map->map[x + y * mapWidth];
}

bool PathContext::isDangerous(int x, int y) const
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
  game_time = blocking_map->type.gameTime;

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

PathContext::PathContext(PathBlockingMap& blocking, PathCoord start,
                         PathCoord real_start, PathCoord end, NonBlockingArea non_blocking)
{
  reset(blocking, start, non_blocking);
  // add the start node to the open list
  generateNewNode(*this, end, real_start,
                  real_start, 0);
}

bool PathContext::matches(PathBlockingMap& blocking, PathCoord start, NonBlockingArea dest) const
{
  // Must check myGameTime == blockingMap_->type.gameTime, otherwise
  // blockingMap could be a deleted pointer which coincidentally
  // compares equal to the valid pointer blockingMap_.
  return game_time == blocking.type.gameTime &&
  blocking_map.get() == &blocking &&
  start == start_coord &&
  dest == destination_bounds;
}



void fpathHardTableReset()
{
  path_contexts.clear();
  blocking_maps.clear();
}

PathNode getBestNode(std::vector<PathNode>& nodes)
{
  // find the node with the lowest distance
  // if equal totals, give preference to node closer to target
  auto const best = nodes.front();
  // remove the node from the list
  std::pop_heap(nodes.begin(), nodes.end());
  // move the best node from the front of nodes to the back of nodes,
  // preserving the heap properties, setting the front to the next best node
  nodes.pop_back();
  return best;
}

unsigned estimateDistance(PathCoord start, PathCoord finish)
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

unsigned estimateDistancePrecise(PathCoord start, PathCoord finish)
{
  /**
   * cost of moving horizontal/vertical = 70*2,
   * cost of moving diagonal = 99*2,
   * 99/70 = 1.41428571... ≈ √2 = 1.41421356...
   */
  return iHypot((start.x - finish.x) * 140,
                (start.y - finish.y) * 140);
}

void generateNewNode(PathContext& context, PathCoord destination,
                       PathCoord current_pos, PathCoord prev_pos,
                       unsigned prev_dist)
{
  const auto cost_factor = context.isDangerous(current_pos.x, current_pos.y);
  const auto dist = prev_dist +
                    estimateDistance(prev_pos, current_pos) * cost_factor;
  auto node = PathNode{current_pos, dist,
                       dist + estimateDistancePrecise(current_pos, destination)};

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

void recalculateEstimates(PathContext& context, PathCoord tile)
{
  for (auto& node : context.nodes)
  {
    node.estimated_distance_to_end = node.distance_from_start +
                                     estimateDistancePrecise(node.path_coordinate, tile);
  }
  // Changing the estimates breaks the heap ordering. Fix the heap ordering.
  std::make_heap(context.nodes.begin(), context.nodes.end());
}

PathCoord findNearestExploredTile(PathContext& context, PathCoord tile)
{
  unsigned nearest_dist = UINT32_MAX;
  auto nearest_coord = PathCoord{0, 0};
  bool target_found = false;
  while (!target_found)
  {
    auto node = getBestNode(context.nodes);
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
      if (direction % 2 != 0 && !context.destination_bounds.isNonBlocking(node.path_coordinate.x, node.path_coordinate.y) &&
          !context.destination_bounds.isNonBlocking(x, y)) {

        // cannot cut corners
        auto x_2 = node.path_coordinate.x + offset[(direction + 1) % 8].x;
        auto y_2 = node.path_coordinate.y + offset[(direction + 1) % 8].y;
        if (context.isBlocked(x_2, y_2)) {
          continue;
        }
        x_2 = node.path_coordinate.x + offset[(direction + 7) % 8].x;
        y_2 = node.path_coordinate.y + offset[(direction + 7) % 8].y;
        if (context.isBlocked(x_2, y_2)) {
          continue;
        }
      }

      // see if node is a blocking tile
      if (context.isBlocked(x, y)) {
        // blocked -- skip
        continue;
      }

      // now insert the point into the appropriate list, if not already visited.
      generateNewNode(context, tile, PathCoord{x, y},
                      node.path_coordinate, node.distance_from_start);
    }
  }
  return nearest_coord;
}

ASTAR_RESULT fpathAStarRoute(Movement& movement, PathJob& pathJob)
{
  PathCoord end {};
  auto result = ASTAR_RESULT::OK;
  auto must_reverse = true;

  const auto origin_tile = PathCoord{map_coord(pathJob.origin.x),
                                     map_coord(pathJob.origin.y)};

  const auto destination_tile = PathCoord{map_coord(pathJob.destination.x),
                                          map_coord(pathJob.destination.y)};

  const auto dstIgnore = NonBlockingArea{pathJob.dstStructure};

  auto it = std::find_if(path_contexts.begin(), path_contexts.end(),
                         [&origin_tile, &end, &must_reverse](auto& context) {
      if (context.map[origin_tile.x + origin_tile.y * mapWidth].iteration ==
          context.map[origin_tile.x + origin_tile.y * mapWidth].visited)  {
        // already know the path
        end = origin_tile;
      }
      else {
        // continue previous exploration
        recalculateEstimates(context, origin_tile);
        end = findNearestExploredTile(context, origin_tile);
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
    auto new_context = PathContext(*pathJob.blockingMap, origin_tile, origin_tile,
                                   destination_tile, pathJob.dstStructure);
    end = findNearestExploredTile(*it, destination_tile);
    it->nearest_reachable_tile = end;
  }

  // return the nearest route if no optimal one was found
  if (it->start_coord != destination_tile)  {
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

    if (it->isBlocked(map.x + x, map.y))  {
      // point too close to a blocking tile on left or right side,
      // so move the point to the middle.
      next.x = world_coord(map.x) + TILE_UNITS / 2;
    }
    if (it->isBlocked(map.x, map.y + y)) {
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
    auto coord = Vector2i{pathJob.destination.x,
                          pathJob.destination.y};

    if (must_reverse)  {
      route.front() = coord;
    }
    else  {
      route.back() = coord;
    }
  }
  // reallocate correct memory
  movement.path.resize(route.size());

  if (must_reverse) {
    std::copy(route.rbegin(), route.rend(), movement.path.data());

		// if blocked, searching from `destination_tile` to
    // `origin_tile` wouldn't find the origin tile.
    if (!it->isBlocked(origin_tile.x, origin_tile.y))  {
			// next time, search starting from the nearest reachable
      // tile to the destination.
      *it = PathContext(*pathJob.blockingMap, destination_tile,
                       it->nearest_reachable_tile, origin_tile, dstIgnore);
    }
  }
  else {
    std::copy(route.begin(), route.end(), movement.path.data());
  }

  // move context to beginning of last recently used list.
  if (it != path_contexts.begin())  {
    path_contexts.insert(path_contexts.begin(), *it);
  }

  movement.destination = movement.path[route.size() - 1];
  return result;
}

void fpathSetBlockingMap(PathJob& path_job)
{
	if (fpathCurrentGameTime != gameTime)  {
		// new tick, remove maps which are no longer needed.
		fpathCurrentGameTime = gameTime;
		blocking_maps.clear();
	}

	// figure out which map we are looking for.
	PathBlockingType type;
	type.gameTime = gameTime;
	type.propulsion = path_job.propulsion;
	type.owner = path_job.owner;
	type.moveType = path_job.moveType;

	// find the map.
	auto it = std::find_if(blocking_maps.begin(), blocking_maps.end(),
	                      [&](PathBlockingMap const& map) {
    return map == type;
  });

  if (it != blocking_maps.end()) {
    syncDebug("blockingMap(%d,%d,%d,%d) = cached", gameTime, path_job.propulsion, path_job.owner, path_job.moveType);
    path_job.blockingMap = std::make_shared<PathBlockingMap>(*it);
    return;
  }

  // didn't find the map, so i does not point to a map.
  auto blocking = PathBlockingMap();
  blocking_maps.emplace_back(blocking);

  // `blocking` now points to an empty map with no data. fill the map.
  blocking.type = type;
  std::vector<bool> &map = blocking.map;
  map.resize(static_cast<size_t>(mapWidth) * static_cast<size_t>(mapHeight));
  unsigned checksum_map = 0, checksum_threat_map = 0, factor = 0;
  for (auto y = 0; y < mapHeight; ++y) {
    for (auto x = 0; x < mapWidth; ++x) {
      map[x + y * mapWidth] = fpathBaseBlockingTile(x, y, type.propulsion,
                                                    type.owner, type.moveType);
      checksum_map ^= map[x + y * mapWidth] * (factor = 3 * factor + 1);
    }
  }
  if (!isHumanPlayer(type.owner) && type.moveType == FPATH_MOVETYPE::FMT_MOVE) {
    auto threat = blocking.threat_map;
    threat.resize(static_cast<size_t>(mapWidth) *
                  static_cast<size_t>(mapHeight));
    for (auto y = 0; y < mapHeight; ++y) {
      for (auto x = 0; x < mapWidth; ++x) {
        threat[x + y * mapWidth] = auxTile(x, y, type.owner) & AUXBITS_THREAT;
        checksum_threat_map ^= threat[x + y * mapWidth] * (factor = 3 * factor + 1);
      }
    }
  }
  syncDebug("blockingMap(%d,%d,%d,%d) = %08X %08X", gameTime,
            path_job.propulsion, path_job.owner, path_job.moveType,
            checksum_map, checksum_threat_map);

  path_job.blockingMap = std::make_shared<PathBlockingMap>(blocking_maps.back());
}

bool PathBlockingMap::operator==(PathBlockingType const& rhs) const
{
  return type.gameTime == rhs.gameTime &&
         fpathIsEquivalentBlocking(type.propulsion, type.owner, type.moveType,
                                   rhs.propulsion, rhs.owner, rhs.moveType);;
}
