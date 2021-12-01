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
	Foundation, Inc. , 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/**
 *
 * @file
 * Function prototypes for giving droids orders.
 *
 */

#ifndef __INCLUDED_SRC_ORDER_H__
#define __INCLUDED_SRC_ORDER_H__

#include "orderdef.h"

/** \brief Gives the droid an order. */
void orderDroidBase(Droid *psDroid, DROID_ORDER_DATA *psOrder);

/** \brief Checks targets of droid's order list. */
void orderCheckList(Droid *psDroid);

/** \brief Updates a droids order state. */
void orderUpdateDroid(Droid *psDroid);

/** \brief Sends an order to a droid. */
void orderDroid(Droid *psDroid, DROID_ORDER order, QUEUE_MODE mode);

/** \brief Compares droid's order with order. */
bool orderState(Droid *psDroid, DROID_ORDER order);

/** \brief Checks if an order is valid for a location. */
bool validOrderForLoc(DROID_ORDER order);

/** \brief Checks if an order is valid for an object. */
bool validOrderForObj(DROID_ORDER order);

/** \brief Sends an order with a location to a droid. */
void orderDroidLoc(Droid *psDroid, DROID_ORDER order, UDWORD x, UDWORD y, QUEUE_MODE mode);

/** \brief Gets the state of a droid order with a location. */
bool orderStateLoc(Droid *psDroid, DROID_ORDER order, UDWORD *pX, UDWORD *pY);

/** \brief Sends an order with an object target to a droid. */
void orderDroidObj(Droid *psDroid, DROID_ORDER order, GameObject *psObj, QUEUE_MODE mode);

/** \brief Gets the state of a droid's order with an object. */
GameObject *orderStateObj(Droid *psDroid, DROID_ORDER order);

/** \brief Sends an order with a location and a stat to a droid. */
void orderDroidStatsLocDir(Droid *psDroid, DROID_ORDER order,
                           StructureStats *psStats, UDWORD x, UDWORD y, uint16_t direction, QUEUE_MODE mode);

/** \brief Gets the state of a droid order with a location and a stat. */
bool orderStateStatsLoc(Droid *psDroid, DROID_ORDER order,
                        StructureStats **ppsStats);

/** \brief Sends an order with a location and a stat to a droid. */
void orderDroidStatsTwoLocDir(Droid *psDroid, DROID_ORDER order,
                              StructureStats *psStats, UDWORD x1, UDWORD y1, UDWORD x2, UDWORD y2, uint16_t direction, QUEUE_MODE mode);

/** \brief Sends an order with two locations and a stat to a droid. */
void orderDroidStatsTwoLocDirAdd(Droid *psDroid, DROID_ORDER order,
                                 StructureStats *psStats, UDWORD x1, UDWORD y1, UDWORD x2, UDWORD y2, uint16_t direction);

/** \brief Sends an order with a location target to all selected droids. add = true queues the order. */
void orderSelectedLoc(uint32_t player, uint32_t x, uint32_t y, bool add);

/** \brief Sends an order with an object target to all selected droids. add = true queues the order. */
void orderSelectedObj(UDWORD player, GameObject *psObj);
void orderSelectedObjAdd(UDWORD player, GameObject *psObj, bool add);

/** \brief Adds an order to a droids order list. */
void orderDroidAdd(Droid *psDroid, DROID_ORDER_DATA *psOrder);

/** \brief Adds a pending order to a droids order list. */
void orderDroidAddPending(Droid *psDroid, DROID_ORDER_DATA *psOrder);

/** \brief Sends the next order from a droids order list to the droid. */
bool orderDroidList(Droid *psDroid);

/** \brief Sends an order with a location and a stat to all selected droids. add = true queues the order. */
void orderSelectedStatsLocDir(UDWORD player, DROID_ORDER order,
                              StructureStats *psStats, UDWORD x, UDWORD y, uint16_t direction, bool add);

/** \brief Sends an order with a location and a stat to all selected droids. add = true queues the order. */
void orderDroidStatsLocDirAdd(Droid *psDroid, DROID_ORDER order,
                              StructureStats *psStats, UDWORD x, UDWORD y, uint16_t direction, bool add = true);

/** \brief Sends an order with two a locations and a stat to all selected droids. add = true queues the order. */
void orderSelectedStatsTwoLocDir(UDWORD player, DROID_ORDER order,
                                 StructureStats *psStats, UDWORD x1, UDWORD y1, UDWORD x2, UDWORD y2, uint16_t direction, bool add);

/** \brief Sees if a droid supports a given secondary order. */
bool secondarySupported(Droid *psDroid, SECONDARY_ORDER sec);

/** \brief Gets the state of a secondary order, return false if unsupported. */
SECONDARY_STATE secondaryGetState(Droid *psDroid, SECONDARY_ORDER sec, QUEUE_MODE mode = ModeImmediate);

/** \brief Sets the state of a secondary order, return false if failed. */
bool secondarySetState(Droid *psDroid, SECONDARY_ORDER sec, SECONDARY_STATE State, QUEUE_MODE mode = ModeQueue);

/** \brief Checks the damage level of a droid against it's secondary state. */
void secondaryCheckDamageLevel(Droid *psDroid);

/** \brief Makes all the members of a numeric group to have the same secondary states. */
void secondarySetAverageGroupState(UDWORD player, UDWORD group);

/** \brief Gets the name of an order. */
const char *getDroidOrderName(DROID_ORDER order);
const char *getDroidOrderKey(DROID_ORDER order);

/** \brief Gets a player's transporter. */
Droid *FindATransporter(Droid const *embarkee);

/** \brief Sets the state of a secondary order for a factory. */
bool setFactoryState(Structure *psStruct, SECONDARY_ORDER sec, SECONDARY_STATE State);

/** \brief Gets the state of a secondary order for a Factory. */
bool getFactoryState(Structure *psStruct, SECONDARY_ORDER sec, SECONDARY_STATE *pState);

/** \brief lasSat structure can select a target. */
void orderStructureObj(UDWORD player, GameObject *psObj);

/** \brief Pops orders (including pending orders) from the order list. */
void orderDroidListEraseRange(Droid *psDroid, unsigned indexBegin, unsigned indexEnd);

/** \brief Clears all orders for the given target (including pending orders) from the order list. */
void orderClearTargetFromDroidList(Droid *psDroid, GameObject *psTarget);

/** \brief Chooses an order from a location. */
DROID_ORDER chooseOrderLoc(Droid *psDroid, UDWORD x, UDWORD y, bool altOrder);

/** \brief Chooses an order from an object. */
DroidOrder chooseOrderObj(Droid *psDroid, GameObject *psObj, bool altOrder);

#endif // __INCLUDED_SRC_ORDER_H__