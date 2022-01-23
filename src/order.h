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
 * @file order.h
 *
 * Data types and function prototypes for giving droids orders.
 *
 */

#ifndef __INCLUDED_SRC_ORDER_H__
#define __INCLUDED_SRC_ORDER_H__

enum class ACTION;
struct Droid;
struct Group;
struct Structure;
struct StructureStats;

static bool bOrderEffectDisplayed = false;

enum class ORDER_TYPE
{
    NONE,
    STOP,
    MOVE,
    ATTACK,
    BUILD,
    HELP_BUILD,
    LINE_BUILD,
    DEMOLISH,
    REPAIR,
    OBSERVE,
    FIRE_SUPPORT,
    RETURN_TO_BASE,
    RETURN_TO_REPAIR,
    EMBARK,
    DISEMBARK,
    ATTACK_TARGET,
    COMMANDER_SUPPORT,
    BUILD_MODULE,
    RECYCLE,
    TRANSPORT_OUT,
    TRANSPORT_IN,
    TRANSPORT_RETURN,
    GUARD,
    DROID_REPAIR,
    RESTORE,
    SCOUT,
    PATROL,
    REARM,
    RECOVER,
    RTR_SPECIFIED,
    CIRCLE,
    HOLD
};

enum class SECONDARY_ORDER
{
    ATTACK_RANGE,
    REPAIR_LEVEL,
    ATTACK_LEVEL,
    ASSIGN_PRODUCTION,
    ASSIGN_CYBORG_PRODUCTION,
    CLEAR_PRODUCTION,
    RECYCLE,
    PATROL,
    HALT_TYPE,
    RETURN_TO_LOCATION,
    FIRE_DESIGNATOR,
    ASSIGN_VTOL_PRODUCTION,
    CIRCLE
};

enum SECONDARY_STATE
{
    DSS_NONE = 0x000000,
    /**< no state. */
    DSS_ARANGE_SHORT = 0x000001,
    /**< state referred to secondary order DSO_ATTACK_RANGE. Droid can only attack with short range. */
    DSS_ARANGE_LONG = 0x000002,
    /**< state referred to secondary order DSO_ATTACK_RANGE. Droid can only attack with long range. */
    DSS_ARANGE_OPTIMUM = 0x000003,
    /**< state referred to secondary order DSO_ATTACK_RANGE. Droid can attacks with short or long range depending on what is the best hit chance. */
    DSS_REPLEV_LOW = 0x000004,
    /**< state referred to secondary order DSO_REPAIR_LEVEL. Droid falls back if its health decrease below 25%. */
    DSS_REPLEV_HIGH = 0x000008,
    /**< state referred to secondary order DSO_REPAIR_LEVEL. Droid falls back if its health decrease below 50%. */
    DSS_REPLEV_NEVER = 0x00000c,
    /**< state referred to secondary order DSO_REPAIR_LEVEL. Droid never falls back. */
    DSS_ALEV_ALWAYS = 0x000010,
    /**< state referred to secondary order DSO_ATTACK_LEVEL. Droid attacks by its free will everytime. */
    DSS_ALEV_ATTACKED = 0x000020,
    /**< state referred to secondary order DSO_ATTACK_LEVEL. Droid attacks if it is attacked. */
    DSS_ALEV_NEVER = 0x000030,
    /**< state referred to secondary order DSO_ATTACK_LEVEL. Droid never attacks. */
    DSS_HALT_HOLD = 0x000040,
    /**< state referred to secondary order DSO_HALTTYPE. If halted, droid never moves by its free will. */
    DSS_HALT_GUARD = 0x000080,
    /**< state referred to secondary order DSO_HALTTYPE. If halted, droid moves on a given region by its free will. */
    DSS_HALT_PURSUE = 0x0000c0,
    /**< state referred to secondary order DSO_HALTTYPE. If halted, droid pursues the target by its free will. */
    DSS_RECYCLE_SET = 0x000100,
    /**< state referred to secondary order DSO_RECYCLE. If set, the droid can be recycled. */
    DSS_ASSPROD_START = 0x000200,
    /**< @todo this state is not called on the code. Consider removing it. */
    DSS_ASSPROD_MID = 0x002000,
    /**< @todo this state is not called on the code. Consider removing it. */
    DSS_ASSPROD_END = 0x040000,
    /**< @todo this state is not called on the code. Consider removing it. */
    DSS_RTL_REPAIR = 0x080000,
    /**< state set to send order DORDER_RTR to droid. */
    DSS_RTL_BASE = 0x100000,
    /**< state set to send order DORDER_RTB to droid. */
    DSS_RTL_TRANSPORT = 0x200000,
    /**< state set to send order DORDER_EMBARK to droid. */
    DSS_PATROL_SET = 0x400000,
    /**< state referred to secondary order DSO_PATROL. If set, the droid is set to patrol. */
    DSS_CIRCLE_SET = 0x400100,
    /**< state referred to secondary order DSO_CIRCLE. If set, the droid is set to circle. */
    DSS_FIREDES_SET = 0x800000,
    /**< state referred to secondary order DSO_FIRE_DESIGNATOR. If set, the droid is set as a fire designator. */
};

/* Masks for the secondary order state */

#define DSS_ARANGE_MASK             0x000003
#define DSS_REPLEV_MASK             0x00000c
#define DSS_ALEV_MASK               0x000030
#define DSS_HALT_MASK               0x0000c0
#define DSS_RECYCLE_MASK            0x000100
#define DSS_ASSPROD_MASK            0x1f07fe00
#define DSS_ASSPROD_FACT_MASK       0x003e00
#define DSS_ASSPROD_CYB_MASK        0x07c000
#define DSS_ASSPROD_VTOL_MASK       0x1f000000
#define DSS_ASSPROD_SHIFT           9
#define DSS_ASSPROD_CYBORG_SHIFT    (DSS_ASSPROD_SHIFT + 5)
#define DSS_ASSPROD_VTOL_SHIFT      24
#define DSS_RTL_MASK                0x380000
#define DSS_PATROL_MASK             0x400000
#define DSS_FIREDES_MASK            0x800000
#define DSS_CIRCLE_MASK             0x400100

enum class RTR_DATA_TYPE
{
    NO_RESULT,
    REPAIR_FACILITY,
    DROID,
    HQ,
};

/**
 * Struct that stores the data relevant to an order. This
 * struct is needed to send orders that comes with information,
 * such as position, target, etc.
 */
struct Order
{
    explicit Order(ORDER_TYPE type = NONE);
    Order(ORDER_TYPE type, Vector2i pos);
    Order(ORDER_TYPE type, Vector2i pos, RTR_DATA_TYPE rtrType);
    Order(ORDER_TYPE type, StructureStats& stats, Vector2i pos, unsigned direction);
    Order(ORDER_TYPE type, StructureStats& stats, Vector2i pos, Vector2i pos2, unsigned direction);
    Order(ORDER_TYPE type, BaseObject& target);
    Order(ORDER_TYPE type, BaseObject& target, RTR_DATA_TYPE rtrType);
    Order(ORDER_TYPE type, BaseObject& target, unsigned index);

    using enum ORDER_TYPE;
    using enum RTR_DATA_TYPE;
    ORDER_TYPE type;
    Vector2i pos;

    /// The order's secondary position, in case those exist.
    Vector2i pos2;

    unsigned direction;

    /// Module index, with ORDER_TYPE::BUILD_MODULE.
    unsigned index;

    /// Specifies where to repair.
    RTR_DATA_TYPE rtrType;

    /// The order's target, in case it exist.
    BaseObject* target;

    std::shared_ptr<StructureStats> structure_stats;
};

struct RtrBestResult
{

    RtrBestResult();
    RtrBestResult(RTR_DATA_TYPE type, BaseObject* obj);
    explicit RtrBestResult(Order& order);

    using enum RTR_DATA_TYPE;
    RTR_DATA_TYPE type;
    BaseObject* target;
};

/** \brief Gives the droid an order. */
void orderDroidBase(Droid* psDroid, Order* psOrder);

/** \brief Checks targets of droid's order list. */
void orderCheckList(Droid* psDroid);

/** \brief Updates a droids order state. */
void orderUpdateDroid(Droid* psDroid);

/** \brief Sends an order to a droid. */
void orderDroid(Droid* psDroid, ORDER_TYPE order, QUEUE_MODE mode);

/** \brief Compares droid's order with order. */
bool orderState(Droid* psDroid, ORDER_TYPE order);

/** \brief Checks if an order is valid for a location. */
bool validOrderForLoc(ORDER_TYPE order);

/** \brief Checks if an order is valid for an object. */
bool validOrderForObj(ORDER_TYPE order);

/** \brief Sends an order with a location to a droid. */
void orderDroidLoc(Droid* psDroid, ORDER_TYPE order, UDWORD x, UDWORD y, QUEUE_MODE mode);

/** \brief Gets the state of a droid order with a location. */
bool orderStateLoc(Droid* psDroid, ORDER_TYPE order, UDWORD* pX, UDWORD* pY);

/** \brief Sends an order with an object target to a droid. */
void orderDroidObj(Droid* psDroid, ORDER_TYPE order, BaseObject* psObj, QUEUE_MODE mode);

/** \brief Gets the state of a droid's order with an object. */
BaseObject* orderStateObj(Droid* psDroid, ORDER_TYPE order);

/** \brief Sends an order with a location and a stat to a droid. */
void orderDroidStatsLocDir(Droid* psDroid, ORDER_TYPE order, StructureStats* psStats, UDWORD x, UDWORD y,
                           uint16_t direction, QUEUE_MODE mode);

/** \brief Gets the state of a droid order with a location and a stat. */
bool orderStateStatsLoc(Droid* psDroid, ORDER_TYPE order, StructureStats** ppsStats);

/** \brief Sends an order with a location and a stat to a droid. */
void orderDroidStatsTwoLocDir(Droid* psDroid, ORDER_TYPE order, StructureStats* psStats, UDWORD x1, UDWORD y1,
                              UDWORD x2, UDWORD y2, uint16_t direction, QUEUE_MODE mode);

/** \brief Sends an order with two locations and a stat to a droid. */
void orderDroidStatsTwoLocDirAdd(Droid* psDroid, ORDER_TYPE order, StructureStats* psStats, UDWORD x1, UDWORD y1,
                                 UDWORD x2, UDWORD y2, uint16_t direction);

/** \brief Sends an order with a location target to all selected droids. add = true queues the order. */
void orderSelectedLoc(uint32_t player, uint32_t x, uint32_t y, bool add);

/** \brief Sends an order with an object target to all selected droids. add = true queues the order. */
void orderSelectedObj(UDWORD player, BaseObject* psObj);
void orderSelectedObjAdd(UDWORD player, BaseObject* psObj, bool add);

/** \brief Adds an order to a droids order list. */
void orderDroidAdd(Droid* psDroid, Order* psOrder);

/** \brief Adds a pending order to a droids order list. */
void orderDroidAddPending(Droid* psDroid, Order* psOrder);

/** \brief Sends the next order from a droids order list to the droid. */
bool orderDroidList(Droid* psDroid);

/** \brief Sends an order with a location and a stat to all selected droids. add = true queues the order. */
void orderSelectedStatsLocDir(UDWORD player, ORDER_TYPE order, StructureStats* psStats, UDWORD x, UDWORD y,
                              uint16_t direction, bool add);

/** \brief Sends an order with a location and a stat to all selected droids. add = true queues the order. */
void orderDroidStatsLocDirAdd(Droid* psDroid, ORDER_TYPE order, StructureStats* psStats, UDWORD x, UDWORD y,
                              uint16_t direction, bool add = true);

/** \brief Sends an order with two a locations and a stat to all selected droids. add = true queues the order. */
void orderSelectedStatsTwoLocDir(UDWORD player, ORDER_TYPE order, StructureStats* psStats, UDWORD x1, UDWORD y1,
                                 UDWORD x2, UDWORD y2, uint16_t direction, bool add);

/** \brief Sees if a droid supports a given secondary order. */
bool secondarySupported(Droid* psDroid, SECONDARY_ORDER sec);

/** \brief Gets the state of a secondary order, return false if unsupported. */
SECONDARY_STATE secondaryGetState(Droid* psDroid, SECONDARY_ORDER sec, QUEUE_MODE mode = ModeImmediate);

/** \brief Sets the state of a secondary order, return false if failed. */
bool secondarySetState(Droid* psDroid, SECONDARY_ORDER sec, SECONDARY_STATE State, QUEUE_MODE mode = ModeQueue);

/** \brief Checks the damage level of a droid against it's secondary state. */
void secondaryCheckDamageLevel(Droid* psDroid);

/** \brief Makes all the members of a numeric group to have the same secondary states. */
void secondarySetAverageGroupState(UDWORD player, UDWORD group);

/** \brief Gets the name of an order. */
std::string getDroidOrderName(ORDER_TYPE order);
std::string getDroidOrderKey(ORDER_TYPE order);

/** \brief Gets a player's transporter. */
Droid* FindATransporter(Droid const* embarkee);

/** \brief Sets the state of a secondary order for a factory. */
bool setFactoryState(Structure* psStruct, SECONDARY_ORDER sec, SECONDARY_STATE State);

/** \brief Gets the state of a secondary order for a Factory. */
bool getFactoryState(Structure* psStruct, SECONDARY_ORDER sec, SECONDARY_STATE* pState);

/** \brief lasSat structure can select a target. */
void orderStructureObj(UDWORD player, BaseObject* psObj);

/** \brief Pops orders (including pending orders) from the order list. */
void orderDroidListEraseRange(Droid* psDroid, unsigned indexBegin, unsigned indexEnd);

/** \brief Clears all orders for the given target (including pending orders) from the order list. */
void orderClearTargetFromDroidList(Droid* psDroid, BaseObject* psTarget);

/** \brief Chooses an order from a location. */
ORDER_TYPE chooseOrderLoc(Droid* psDroid, UDWORD x, UDWORD y, bool altOrder);

/** \brief Chooses an order from an object. */
Order chooseOrderObj(Droid* psDroid, BaseObject* psObj, bool altOrder);

static bool secondaryCheckDamageLevelDeselect(Droid* psDroid, SECONDARY_STATE repairState);
static void orderCmdGroupBase(Group* psGroup, Order* psData);
static void orderPlayFireSupportAudio(BaseObject* psObj);
Droid* checkForRepairRange(Droid* psDroid);
static std::pair<Structure*, ACTION> checkForDamagedStruct(Droid* psDroid);
static bool isRepairLikeAction(ACTION action);

#endif // __INCLUDED_SRC_ORDER_H__
