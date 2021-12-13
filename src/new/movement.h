//
// Created by luna on 09/12/2021.
//

#ifndef WARZONE2100_MOVEMENT_H
#define WARZONE2100_MOVEMENT_H

#include "lib/framework/vector.h"

enum class MOVEMENT_STATE
{
  INACTIVE,
  NAVIGATE,
  TURN,
  PAUSE,
  POINT_TO_POINT,
  TURN_TO_TARGET,
  HOVER,
  WAIT_ROUTE,
  SHUFFLE
};

class Movement
{
public:
  bool is_inactive() const;
  bool is_stationary() const;
  void stop_moving();
  void stop_moving_instantly();
private:
  using enum MOVEMENT_STATE;

  MOVEMENT_STATE state;
  Vector2i destination;
  Vector2i origin;
  int speed;
};

#endif // WARZONE2100_MOVEMENT_H