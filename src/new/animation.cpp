//
// Created by Luna Nothard on 31/12/2021.
//


#include <cstdlib>
#include "animation.h"
#include "lib/gamelib/gtime.h"

void ValueTracker::start(int value)
{
  initial_value = value;
  target_value = value;
  target_delta = value;
  current_value = value;
  start_time = graphicsTime;
  target_reached = false;
}

void ValueTracker::stop()
{
  initial_value = 0;
  current_value = 0;
  start_time = 0;
  target_reached = false;
}

void ValueTracker::update()
{
  if (target_reached) {
    return;
  }

  if (std::abs(target_value - current_value) < 1) {
    target_reached = true;
    return;
  }

  current_value = (initial_value + target_delta - current_value) *
          realTimeAdjustedIncrement(speed) + current_value;
}

bool ValueTracker::currently_tracking()
{
  return start_time != 0;
}

int32_t calculateRelativeAngle(unsigned from, unsigned to)
{
  return to + (from - to);
}
