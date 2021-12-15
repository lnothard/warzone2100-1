//
// Created by luna on 15/12/2021.
//

#ifndef WARZONE2100_ASTAR_H
#define WARZONE2100_ASTAR_H

#include <cstdint>

enum class ASTAR_RETVAL
{
  OK,
  FAILED,
  PARTIAL
};

class Path_Coordinate
{
public:
  Path_Coordinate() = default;
  Path_Coordinate(int16_t x, int16_t y);

  bool operator ==(const Path_Coordinate &rhs) const;
  bool operator !=(const Path_Coordinate &rhs) const;
private:
  int16_t x, y;
};

struct Path_Node
{
  Path_Coordinate path_coordinate;
  uint32_t distance_from_start;
  uint32_t estimated_distance_to_end;
};

#endif // WARZONE2100_ASTAR_H