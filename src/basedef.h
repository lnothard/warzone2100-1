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
/** \file
 *  Definitions for the base object type.
 */

#ifndef __INCLUDED_BASEDEF_H__
#define __INCLUDED_BASEDEF_H__

#include <bitset>
#include <memory>
#include <utility>

#include "baseobject.h"
#include "displaydef.h"
#include "lib/framework/vector.h"
#include "statsdef.h"
#include "weapondef.h"

#define NOT_CURRENT_LIST 1   //the died flag for a droid is set to this when it gets added to the non-current list
#define MAX_WEAPONS 3

/**
 * FIXME Shouldn't be necessary
 */
enum OBJECT_TYPE
{
	OBJ_DROID,      ///< Droids
	OBJ_STRUCTURE,  ///< All Buildings
	OBJ_FEATURE,    ///< Things like roads, trees, bridges, fires
	OBJ_PROJECTILE, ///< Comes out of guns, stupid :-)
	OBJ_NUM_TYPES   ///< number of object types - MUST BE LAST
};

struct TILEPOS
{
	UBYTE x, y, type;
};

/*
 Coordinate system used for objects in Warzone 2100:
  x - "right"
  y - "forward"
  z - "up"

  For explanation of yaw/pitch/roll look for "flight dynamics" in your encyclopedia.
*/

enum OBJECT_FLAG
{
  JAMMED_TILES,
  TARGETED,
  DIRTY,
  UNSELECTABLE,
  OBJECT_FLAG_COUNT
};

class GameObject {
protected:
const OBJECT_TYPE m_type;                         ///< The type of object
  std::shared_ptr<Spacetime> m_spacetime;           ///< The object's space-time coordinate
  uint32_t id;                                    ///< ID number of the object
  uint8_t owningPlayer;                           ///< Which player the object belongs to
  uint32_t creationTime;                                  ///< Time the game object was born
  uint32_t deathTime;                                  ///< When an object was destroyed, if 0 still alive
  SCREEN_DISP_DATA displayData;                   ///< screen coordinate details
  UBYTE visible[MAX_PLAYERS];       ///< Whether object is visible to specific player
  UBYTE seenThisTick[MAX_PLAYERS];  ///< Whether object has been seen this tick by the specific player.
  UDWORD lastEmission;               ///< When did it last puff out smoke?
  WEAPON_SUBCLASS lastHitWeapon;              ///< The weapon that last hit it
  UDWORD timeLastHit;                ///< The time the object was last attacked
  UDWORD hitPoints;                       ///< Hit points
  UDWORD periodicalDamageStart;                  ///< When the object entered the fire
  UDWORD periodicalDamage;                 ///< How much damage has been done since the object entered the fire
  std::vector<TILEPOS> watchedTiles;              ///< Variable size array of watched tiles, empty for features
  std::bitset<OBJECT_FLAG_COUNT> flags;

public:
  GameObject(OBJECT_TYPE type, uint32_t id, unsigned player);
  virtual ~GameObject();

  std::shared_ptr<Spacetime> spacetime() const;
  void spacetime(std::shared_ptr<Spacetime> st);
  OBJECT_TYPE type() const;
  bool alive() const;

  // Query visibility for display purposes (i.e. for `selectedPlayer`)
  // *DO NOT USE TO QUERY VISIBILITY FOR CALCULATIONS INVOLVING GAME / SIMULATION STATE*
  UBYTE visibleForLocalDisplay() const;
};

/** Space-time coordinate, including orientation.
 */
class Spacetime
{
public:
  Spacetime() = default;
  Spacetime(Position position, Rotation rotation, uint32_t time);
  Position position() const;
private:
  Position m_position;              ///< Position of the object
  Rotation m_rotation;              ///< Rotation of the object
  uint32_t m_time;                  ///< Game time
};

static inline int objPosDiffSq(Position pos1, Position pos2);
static inline int objPosDiffSq(GameObject const *pos1, GameObject const *pos2);

#endif // __INCLUDED_BASEDEF_H__