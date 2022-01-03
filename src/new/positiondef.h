
/**
 * @file positiondef.h
 *
 * Definitions for position objects.
 */

#ifndef __INCLUDED_POSITIONDEF_H__
#define __INCLUDED_POSITIONDEF_H__

#include "lib/framework/frame.h"

///
enum class POSITION_TYPE
{
  /// Delivery points (NOT waypoints)
	DELIVERY,

  /// Proximity messages that are data generated
	PROXIMITY_DATA,

  /// Proximity messages that are generated in-game
	PROXIMITY_OBJ,

  /// SAVE ONLY delivery point for factory currently
  /// assigned to commander
	TEMP_DELIVERY
};

struct ObjectPosition
{
	using enum POSITION_TYPE;

	POSITION_TYPE type;
	unsigned frame_number;
	unsigned screen_x;
	unsigned screen_y;
	unsigned screen_radius;

  /// The ID of the player currently occupying
  /// this position
	unsigned player;

  /// Boolean flag to indicate whether the position
  /// is to be highlighted
	bool selected;
};

#endif // __INCLUDED_POSITIONDEF_H__
