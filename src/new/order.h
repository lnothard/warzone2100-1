//
// Created by luna on 09/12/2021.
//

#ifndef WARZONE2100_ORDER_H
#define WARZONE2100_ORDER_H

#include "lib/framework/vector.h"
#include "basedef.h"

enum class ORDER_TYPE
{
	NONE,
	/**< no order set. */
	STOP,
	/**< stop the current order. */
	MOVE,
	/**< 2 - move to a location. */
	ATTACK,
	/**< attack an enemy. */
	BUILD,
	/**< 4 - build a structure. */
	HELP_BUILD,
	/**< help to build a structure. */
	LINE_BUILD,
	/**< 6 - build a number of structures in a row (walls + bridges). */
	DEMOLISH,
	/**< demolish a structure. */
	REPAIR,
	/**< 8 - repair a structure. */
	OBSERVE,
	/**< keep a target in sensor view. */
	FIRE_SUPPORT,
	/**< 10 - attack whatever the linked sensor droid attacks. */
	RTB,
	/**< return to base. */
	RTR,
	/**< 14 - return to repair at any repair facility*/
	EMBARK,
	/**< 16 - board a transporter. */
	DISEMBARK,
	/**< get off a transporter. */
	ATTACK_TARGET,
	/**< 18 - a suggestion to attack something i.e. the target was chosen because the droid could see it. */
	COMMANDER_SUPPORT,
	/**< Assigns droid to the target commander. */
	BUILD_MODULE,
	/**< 20 - build a module (power, research or factory). */
	RECYCLE,
	/**< return to factory to be recycled. */
	TRANSPORT_OUT,
	/**< 22 - off-world transporter order. */
	TRANSPORT_IN,
	/**< on-world transporter order. */
	TRANSPORT_RETURN,
	/**< 24 - transporter return after unloading. */
	GUARD,
	/**< guard a structure. */
	DROID_REPAIR,
	/**< 26 - repair a droid. */
	RESTORE,
	/**< restore resistance points for a structure. */
	SCOUT,
	/**< 28 - same as move, but stop if an enemy is seen. */
	PATROL,
	/**< move between two way points. */
	REARM,
	/**< 32 - order a vtol to rearming pad. */
	RECOVER,
	/**< pick up an artifact. */
	RTR_SPECIFIED,
	/**< return to repair at a specified repair center. */
	CIRCLE,
	/**< circles target location and engage. */
	HOLD /**< hold position until given next order. */
};

enum class SECONDARY_ORDER
{
	ATTACK_RANGE,
	/**< The attack range a given droid is allowed to fire: can be short, long or optimum (best chance to hit). Used with DSS_ARANGE_SHORT, DSS_ARANGE_LONG, DSS_ARANGE_OPTIMUM. */
	REPAIR_LEVEL,
	/**< The repair level at which the droid falls back to repair: can be low, high or never. Used with DSS_REPLEV_LOW, DSS_REPLEV_HIGH, DSS_REPLEV_NEVER. */
	ATTACK_LEVEL,
	/**< The attack level at which a droid can attack: can be always, attacked or never. Used with DSS_ALEV_ALWAYS, DSS_ALEV_ATTACKED, DSS_ALEV_NEVER. */
	ASSIGN_PRODUCTION,
	/**< Assigns a factory to a command droid - the state is given by the factory number. */
	ASSIGN_CYBORG_PRODUCTION,
	/**< Assigns a cyborg factory to a command droid - the state is given by the factory number. */
	CLEAR_PRODUCTION,
	/**< Removes the production from a command droid. */
	RECYCLE,
	/**< If can be recycled or not. */
	PATROL,
	/**< If it is assigned to patrol between current pos and next move target. */
	HALT_TYPE,
	/**< The type of halt. It can be hold, guard or pursue. Used with DSS_HALT_HOLD, DSS_HALT_GUARD,  DSS_HALT_PURSUE. */
	RETURN_TO_LOC,
	/**< Generic secondary order to return to a location. Will depend on the secondary state DSS_RTL* to be specific. */
	FIRE_DESIGNATOR,
	/**< Assigns a droid to be a target designator. */
	ASSIGN_VTOL_PRODUCTION,
	/**< Assigns a vtol factory to a command droid - the state is given by the factory number. */
	CIRCLE /**< circling target position and engage. */
};

struct Order
{
	using enum ORDER_TYPE;

	ORDER_TYPE type;
	Vector2i position;
	Simple_Object* target_object;
};

#endif // WARZONE2100_ORDER_H
