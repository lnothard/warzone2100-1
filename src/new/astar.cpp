//
// Created by Luna Nothard on 26/12/2021.
//

#include "astar.h"

PathCoord::PathCoord(int x, int y)
  : x{x}, y{y}
{
}

NonBlockingArea::NonBlockingArea(const Structure_Bounds& bounds)
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
  if (destination_bounds.is_non_blocking(x, y)) {
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

  // iteration should not match value of iteration in -map-
  if (++iteration == UINT16_MAX) {
    // there are no values of iteration guaranteed not to exist
    // in -map-, so clear it
    map.clear();
    iteration = 0;
  }
  // ensure the correct size is allocated for -map-,
  // corresponding to the total area of the game map
  map.resize(static_cast<std::size_t>(map_width) * static_cast<std::size_t>(map_height));
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

  // cost of moving horizontal/vertical = 70*2,
  // cost of moving diagonal = 99*2,
  // 99/70 = 1.41428571... ≈ √2 = 1.41421356...
  return std::min(x_delta, y_delta) * (198 - 140) +
         std::max(x_delta, y_delta) * 140;
}

unsigned estimate_distance_precise(PathCoord start, PathCoord finish)
{
  // cost of moving horizontal/vertical = 70*2,
  // cost of moving diagonal = 99*2,
  // 99/70 = 1.41428571... ≈ √2 = 1.41421356...
  return iHypot((start.x - finish.x) * 140, (start.y - finish.y) * 140);
}

void generate_new_node(PathContext& context, PathCoord destination,
                       PathCoord current_pos, PathCoord prev_pos,
                       unsigned prev_dist)
{
  const auto cost_factor = context.is_dangerous(current_pos.x, current_pos.y);
  const auto dist = prev_dist +
          estimate_distance(prev_pos, current_pos) * cost_factor;
  const auto node = PathNode{current_pos, dist,
                       dist + estimate_distance_precise(current_pos, destination)};

  const auto delta = Vector2i{current_pos.x - prev_pos.x,
                        current_pos.y - prev_pos.y} * 64;
  const bool is_diagonal = delta.x && delta.y;

  auto& explored = context.map[current_pos.x + current_pos.y * map_width];
  if (explored.visited) {
    // Already visited this tile. Do nothing.
    return;
  }
  auto delta_b = Vector2i{explored.x_diff, explored.y_diff};
  auto delta_ = delta - delta_b;
  // Vector pointing from current considered source tile leading to pos,
  // to the previously considered source tile leading to pos.
  if (abs(delta_.x) + abs(delta_.y) == 64) {
    // prevPos is tile A or B, and pos is tile P. We were previously called
    // with prevPos being tile B or A, and pos tile P.
    // We want to find the distance to tile P, taking into account that
    // the actual shortest path involves coming from somewhere between
    // tile A and tile B.
    // +---+---+
    // |   | P |
    // +---+---+
    // | A | B |
    // +---+---+

  }
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

      //			   5  6  7
      //			     \|/
      //			   4 -I- 0
      //			     /|\
      //			   3  2  1
      // odd:orthogonal-adjacent tiles even:non-orthogonal-adjacent tiles
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