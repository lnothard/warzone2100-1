//
// Created by Luna Nothard on 26/12/2021.
//

#include "astar.h"

PathCoord::PathCoord(int x, int y)
  : x{x}, y{y}
{
}

PathNode::PathNode(PathCoord coord, unsigned dist, unsigned est)
  : path_coordinate{coord}, distance_from_start{dist},
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
  : x_1(bounds.top_left_coords.x),
    x_2(bounds.top_left_coords.x + bounds.size_in_coords.x),
    y_1(bounds.top_left_coords.y),
    y_2(bounds.top_left_coords.y + bounds.size_in_coords.y)
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

  return x < 0 || y < 0 || x >= map_width ||
         y >= map_height ||
         blocking_map->map[x + y * map_width];
}

bool PathContext::is_dangerous(int x, int y) const
{
  return !blocking_map->threat_map.empty() &&
          blocking_map->threat_map[x + y * map_width];
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
  map.resize(static_cast<std::size_t>(map_width) * static_cast<std::size_t>(map_height));
}

void PathContext::init(PathBlockingMap& blocking, PathCoord start,
          PathCoord real_start, PathCoord end, NonBlockingArea non_blocking)
{
  reset(blocking, start, non_blocking);

  // add the start node to the open list
  generate_new_node(*this, end, real_start,
                    real_start, 0);
}

void path_table_reset()
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

  auto& explored = context.map[current_pos.x + current_pos.y * map_width];
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

PathCoord find_nearest_explored_tile(PathContext& context, PathCoord tile)
{
  unsigned nearest_dist = UINT32_MAX;
  auto nearest_coord = PathCoord{0, 0};
  bool target_found = false;
  while (!target_found)
  {
    auto node = get_best_node(context.nodes);
    if (context.map[node.path_coordinate.x + node.path_coordinate.y * map_width].visited) {
      // already visited
      continue;
    }

    // now mark as visited
    context.map[node.path_coordinate.x + node.path_coordinate.y * map_width].visited = true;

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

ASTAR_RESULT find_astar_route(Movement& movement, PathJob& path_job)
{
  PathCoord end;
  auto result = ASTAR_RESULT::OK;
  const auto origin_tile = PathCoord{map_coord(path_job.origin.x),
                                     map_coord(path_job.origin.y)};

  const auto destination_tile = PathCoord{map_coord(path_job.destination.x),
                                          map_coord(path_job.destination.y)};

  auto it = std::find_if(path_contexts.begin(), path_contexts.end(),
                      [&end](const auto& context)
  {
    if (context.map[origin_tile.x + origin_tile.y * map_width].iteration ==
        context.map[origin_tile.x + origin_tile.y * map_width].visited)  {
      // already know the path
      end = origin_tile;
    } else {
      // continue previous exploration
      recalculate_estimates(context, origin_tile);
      end = find_nearest_explored_tile(context, origin_tile);
    }

    if (end != origin_tile)  {
      return false;
    }
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
    new_context.init(--it, path_job.blocking_map, origin_tile, origin_tile,
                     destination_tile, );
    it->nearest_reachable_tile = end;
  }

  if (it != destination_tile)  {
    result = ASTAR_RESULT::PARTIAL;
  }


}