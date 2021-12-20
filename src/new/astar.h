//
// Created by luna on 15/12/2021.
//

#ifndef WARZONE2100_ASTAR_H
#define WARZONE2100_ASTAR_H

enum class ASTAR_RETVAL
{
  OK,
  FAILED,
  PARTIAL
};

class Path_Coordinate
{
public:
  constexpr Path_Coordinate() = default;
  constexpr Path_Coordinate(int x, int y);

  constexpr bool operator ==(const Path_Coordinate &rhs) const;
  constexpr bool operator !=(const Path_Coordinate &rhs) const;
private:
  int x, y;
};

struct Path_Node
{
  Path_Coordinate path_coordinate;
  unsigned distance_from_start;
  unsigned estimated_distance_to_end;
};

#endif // WARZONE2100_ASTAR_H