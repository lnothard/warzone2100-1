//
// Created by luna on 01/12/2021.
//

#ifndef WARZONE2100_PLAYERDEF_H
#define WARZONE2100_PLAYERDEF_H

#include <vector>

#include "droiddef.h"

/**
 * Represents a player (either human or CPU)
 */
class Player
{
private:
  std::vector<Droid> droidList;               /**< list of droids owned by this player */
  std::vector<Structure> structureList;       /**< list of structures owned by this player */
};

#endif // WARZONE2100_PLAYERDEF_H