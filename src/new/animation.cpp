//
// Created by Luna Nothard on 31/12/2021.
//


#include <cstdlib>
#include "animation.h"
#include "lib/gamelib/gtime.h"

//void ValueTracker::start(int value)
//{
//  initial_value = value;
//  target_value = value;
//  target_delta = value;
//  current_value = value;
//  start_time = graphicsTime;
//  target_reached = false;
//}
//
//void ValueTracker::stop()
//{
//  initial_value = 0;
//  current_value = 0;
//  start_time = 0;
//  target_reached = false;
//}

//void ValueTracker::update()
//{
//  if (target_reached) {
//    return;
//  }
//
//  if (std::abs(target_value - current_value) < 1) {
//    target_reached = true;
//    return;
//  }
//
//  current_value = (initial_value + target_delta - current_value) *
//          static_cast<int>( realTimeAdjustedIncrement(
//                  static_cast<float>( speed )) )
//          + current_value;
//}

//bool ValueTracker::currently_tracking() const
//{
//  return start_time != 0;
//}

//void ValueTracker::set_target(int value)
//{
//  target_delta = value - initial_value;
//  target_value = value;
//  target_reached = false;
//}

int calculateRelativeAngle(unsigned from, unsigned to)
{
  return static_cast<int>( to + (from - to) );
}

unsigned calculate_easing(EASING_FUNCTION easing_func, unsigned progress)
{
  using enum EASING_FUNCTION;
  switch (easing_func) {
    case LINEAR:
      return progress;
    case EASE_IN_OUT:
      return MAX(0, MIN(UINT16_MAX, iCos(
              UINT16_MAX / 2 + progress / 2)
                / 2 + (1 << 15)));
    case EASE_IN:
      return progress * progress / UINT16_MAX;
    case EASE_OUT:
      return 2 * progress - progress * progress / (UINT16_MAX - 1);
  }
}
