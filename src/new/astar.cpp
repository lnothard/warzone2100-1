//
// Created by Luna Nothard on 26/12/2021.
//

#include "astar.h"

NonBlockingArea::NonBlockingArea(const Structure_Bounds& bounds)
  : x_1(bounds.top_left_coords.x),
    x_2(bounds.top_left_coords.x + bounds.size_in_coords.x),
    y_1(bounds.top_left_coords.y),
    y_2(bounds.top_left_coords.y + bounds.size_in_coords.y)
{
}

bool NonBlockingArea::operator ==(const NonBlockingArea& rhs) const
{
  return x_1 == rhs.x_1 &&
         x_2 == rhs.x_2 &&
         y_1 == rhs.y_1 &&
         y_2 == rhs.y_2;
}

bool NonBlockingArea::operator !=(const NonBlockingArea& rhs) const
{
  return !(*this == rhs);
}

bool NonBlockingArea::is_non_blocking(int x, int y) const
{
  return x >= x_1 && x < x_2 && y >= y_1 && y < y_2;
}

bool PathContext::is_blocked(int x, int y) const
{
  if (non_blocking.is_non_blocking(x, y)) {
    return false;
  }
  return x < 0 || y < 0 || x >= map_width ||
         y >= map_height || blocking_map->map[x + y * map_width];
}

bool PathContext::is_dangerous(int x, int y) const
{
  return !blocking_map->threat_map.empty() &&
          blocking_map->threat_map[x + y * map_width];
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
  auto best = nodes.front();
  // remove the node from the list
  std::pop_heap(nodes.begin(), nodes.end());
  // move the best node from the front of nodes to the back of nodes,
  // preserving the heap properties, setting the front to the next best node.
  nodes.pop_back();
  return best;
}

unsigned estimate_distance(PathCoordinate start, PathCoordinate finish)
{
  const auto x_delta = std::abs(start.x - finish.x);
  const auto y_delta = std::abs(start.y - finish.y);

  // cost of moving horizontal/vertical = 70*2,
  // cost of moving diagonal = 99*2,
  // 99/70 = 1.41428571... ≈ √2 = 1.41421356...
  return std::min(x_delta, y_delta) * (198 - 140) +
         std::max(x_delta, y_delta) * 140;
}

unsigned estimate_distance_precise(PathCoordinate start, PathCoordinate finish)
{
  // cost of moving horizontal/vertical = 70*2,
  // cost of moving diagonal = 99*2,
  // 99/70 = 1.41428571... ≈ √2 = 1.41421356...
  return iHypot((start.x - finish.x) * 140, (start.y - finish.y) * 140);
}

void generate_new_node(PathContext& context, PathCoordinate destination,
                       PathCoordinate current_pos, PathCoordinate prev_pos,
                       unsigned prev_dist)
{
  auto cost_factor = context.is_dangerous(current_pos.x, current_pos.y);
  auto dist = prev_dist + estimate_distance(prev_pos, current_pos) * cost_factor;
  auto node = PathNode{current_pos, dist,
                       dist + estimate_distance_precise(current_pos, destination)};

  auto delta = Vector2i{current_pos.x - prev_pos.x,
                        current_pos.y - prev_pos.y} * 64;
  bool is_diagonal = delta.x && delta.y;

  auto& explored = context.map[current_pos.x + current_pos.y * map_width];
  if (explored.visited) {
    // Already visited this tile. Do nothing.
    return;
  }

}