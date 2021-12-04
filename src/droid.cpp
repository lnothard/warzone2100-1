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
/**
 * @file droid.c
 *
 * Droid method functions.
 *
 */
#include "lib/framework/frame.h"
#include "lib/framework/math_ext.h"
#include "lib/framework/geometry.h"
#include "lib/framework/strres.h"

#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "lib/netplay/netplay.h"

#include "objects.h"
#include "loop.h"
#include "visibility.h"
#include "map.h"
#include "droid.h"
#include "hci.h"
#include "power.h"
#include "effects.h"
#include "feature.h"
#include "action.h"
#include "order.h"
#include "move.h"
#include "geometry.h"
#include "display.h"
#include "console.h"
#include "component.h"
#include "lighting.h"
#include "multiplay.h"
#include "warcam.h"
#include "display3d.h"
#include "group.h"
#include "text.h"
#include "cmddroid.h"
#include "fpath.h"
#include "projectile.h"
#include "mission.h"
#include "levels.h"
#include "transporter.h"
#include "selection.h"
#include "difficulty.h"
#include "edit3d.h"
#include "scores.h"
#include "research.h"
#include "combat.h"
#include "template.h"
#include "qtscript.h"

#define DEFAULT_RECOIL_TIME	(GAME_TICKS_PER_SEC/4)
#define	DROID_DAMAGE_SPREAD	(16 - rand()%32)
#define	DROID_REPAIR_SPREAD	(20 - rand()%40)

// store the experience of recently recycled droids
static std::priority_queue<int> recycled_experience[MAX_PLAYERS];

/** Height the transporter hovers at above the terrain. */
#define TRANSPORTER_HOVER_HEIGHT	10

// the structure that was last hit
Droid *psLastDroidHit;

//determines the best IMD to draw for the droid - A TEMP MEASURE!
static void groupConsoleInformOfSelection(UDWORD groupNumber);
static void groupConsoleInformOfCreation(UDWORD groupNumber);
static void groupConsoleInformOfCentering(UDWORD groupNumber);
static void groupConsoleInformOfRemoval();
static void droidUpdateDroidSelfRepair(Droid *psRepairDroid);
static UDWORD calcDroidBaseBody(Droid *psDroid);





// initialise droid module
bool droidInit()
{
	for (auto & i : recycled_experience)
	{
		i = std::priority_queue <int>(); // clear it
	}
	psLastDroidHit = nullptr;

	return true;
}

int droidReloadBar(const GameObject *psObj, const Weapon *psWeap, int weapon_slot)
{
	WEAPON_STATS *psStats;
	bool bSalvo;
	int firingStage, interval;

	if (psWeap->nStat == 0)	// no weapon
	{
		return -1;
	}
	psStats = asWeaponStats + psWeap->nStat;

	/* Justifiable only when greater than a one second reload or intra salvo time  */
	bSalvo = (psStats->upgrade[psObj->owningPlayer].numRounds > 1);
	if ((bSalvo && psStats->upgrade[psObj->owningPlayer].reloadTime > GAME_TICKS_PER_SEC)
		|| psStats->upgrade[psObj->owningPlayer].firePause > GAME_TICKS_PER_SEC
		|| (psObj->type == OBJ_DROID && isVtolDroid((const Droid *)psObj)))
	{
		if (psObj->type == OBJ_DROID && isVtolDroid((const Droid *)psObj))
		{
			//deal with VTOLs
			firingStage = getNumAttackRuns((const Droid *)psObj, weapon_slot) - ((const Droid *)psObj)->m_weaponList[weapon_slot].usedAmmo;

			//compare with max value
			interval = getNumAttackRuns((const Droid *)psObj, weapon_slot);
		}
		else
		{
			firingStage = gameTime - psWeap->lastFired;
			interval = bSalvo ? weaponReloadTime(psStats, psObj->owningPlayer) : weaponFirePause(psStats, psObj->owningPlayer);
		}
		if (firingStage < interval && interval > 0)
		{
			return PERCENT(firingStage, interval);
		}
		return 100;
	}
	return -1;
}

#define UNIT_LOST_DELAY	(5*GAME_TICKS_PER_SEC)


Droid::Droid(uint32_t id, unsigned player)
	: GameObject(OBJ_DROID, id, player)
	, droidType(DROID_ANY)
	, psGroup(nullptr)
	, psGrpNext(nullptr)
	, secondaryOrder(DSS_ARANGE_LONG | DSS_REPLEV_NEVER | DSS_ALEV_ALWAYS | DSS_HALT_GUARD)
	, secondaryOrderPending(DSS_ARANGE_LONG | DSS_REPLEV_NEVER | DSS_ALEV_ALWAYS | DSS_HALT_GUARD)
	, secondaryOrderPendingCount(0)
	, action(DACTION_NONE)
	, actionPos(0, 0)
{
	memset(name, 0, sizeof(name));
	memset(asBits, 0, sizeof(asBits));
        position = Vector3i(0, 0, 0);
        rotation = Vector3i(0, 0, 0);
	order.type = DORDER_NONE;
	order.pos = Vector2i(0, 0);
	order.pos2 = Vector2i(0, 0);
	order.direction = 0;
	order.psObj = nullptr;
	order.psStats = nullptr;
	sMove.Status = MOVEINACTIVE;
	listSize = 0;
	listPendingBegin = 0;
	iAudioID = NO_SOUND;
	group = UBYTE_MAX;
	psBaseStruct = nullptr;
        displayData.frameNumber = 0;	// it was never drawn before
	for (unsigned vPlayer = 0; vPlayer < MAX_PLAYERS; ++vPlayer)
	{
		visible[vPlayer] = hasSharedVision(vPlayer, player) ? UINT8_MAX : 0;
	}
	memset(seenThisTick, 0, sizeof(seenThisTick));
	periodicalDamageStart = 0;
	periodicalDamage = 0;
        displayData.screenX = OFF_SCREEN;
        displayData.screenY = OFF_SCREEN;
        displayData.screenR = 0;
        displayData.imd = nullptr;
	illumination = UBYTE_MAX;
	resistance = ACTION_START_TIME;	// init the resistance to indicate no EW performed on this droid
	lastFrustratedTime = 0;		// make sure we do not start the game frustrated
}

/* DROID::~DROID: release all resources associated with a droid -
 * should only be called by objmem - use vanishDroid preferably
 */
Droid::~Droid()
{
	// Make sure to get rid of some final references in the sound code to this object first
	// In BASE_OBJECT::~BASE_OBJECT() is too late for this, since some callbacks require us to still be a DROID.
	audio_RemoveObj(this);

        Droid *psDroid = this;
        Droid *psCurr, *pNextGroupDroid = nullptr;

	if (isTransporter(psDroid))
	{
		if (psDroid->psGroup)
		{
			//free all droids associated with this Transporter
			for (psCurr = psDroid->psGroup->psList; psCurr != nullptr && psCurr != psDroid; psCurr = pNextGroupDroid)
			{
				pNextGroupDroid = psCurr->psGrpNext;
				delete psCurr;
			}
		}
	}

	fpathRemoveDroidData(psDroid->id);

	// leave the current group if any
	if (psDroid->psGroup)
	{
		psDroid->psGroup->remove(psDroid);
	}
}

std::priority_queue<int> copy_experience_queue(int player)
{
	return recycled_experience[player];
}

void add_to_experience_queue(int player, int value)
{
	recycled_experience[player].push(value);
}

static void removeDroidFX(Droid *psDel, unsigned impactTime)
{
	Vector3i pos;

	CHECK_DROID(psDel);

	// only display anything if the droid is visible
	if (!psDel->visibleForLocalDisplay())
	{
		return;
	}

	if (psDel->animationEvent != ANIM_EVENT_DYING)
	{
		compPersonToBits(psDel);
	}

	/* if baba then squish */
	if (psDel->droidType == DROID_PERSON)
	{
		// The barbarian has been run over ...
		audio_PlayStaticTrack(psDel->position.x, psDel->position.y, ID_SOUND_BARB_SQUISH);
	}
	else
	{
		destroyFXDroid(psDel, impactTime);
		pos.x = psDel->position.x;
		pos.z = psDel->position.y;
		pos.y = psDel->position.z;
		if (psDel->droidType == DROID_SUPERTRANSPORTER)
		{
			addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_LARGE, false, nullptr, 0, impactTime);
		}
		else
		{
			addEffect(&pos, EFFECT_DESTRUCTION, DESTRUCTION_TYPE_DROID, false, nullptr, 0, impactTime);
		}
		audio_PlayStaticTrack(psDel->position.x, psDel->position.y, ID_SOUND_EXPLOSION);
	}
}

bool destroyDroid(Droid *psDel, unsigned impactTime)
{
	ASSERT(gameTime - deltaGameTime <= impactTime, "Expected %u <= %u, gameTime = %u, bad impactTime", gameTime - deltaGameTime, impactTime, gameTime);

	if (psDel->lastHitWeapon == WSC_LAS_SAT)		// darken tile if lassat.
	{
		UDWORD width, breadth, mapX, mapY;
		MAPTILE	*psTile;

		mapX = map_coord(psDel->position.x);
		mapY = map_coord(psDel->position.y);
		for (width = mapX - 1; width <= mapX + 1; width++)
		{
			for (breadth = mapY - 1; breadth <= mapY + 1; breadth++)
			{
				psTile = mapTile(width, breadth);
				if (TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(psTile))
				{
					psTile->illumination /= 2;
				}
			}
		}
	}

	removeDroidFX(psDel, impactTime);
	removeDroidBase(psDel);
	psDel->deathTime = impactTime;
	return true;
}

void vanishDroid(Droid *psDel)
{
	removeDroidBase(psDel);
}

/* Remove a droid from the List so doesn't update or get drawn etc
TAKE CARE with removeDroid() - usually want droidRemove since it deal with grid code*/
//returns false if the droid wasn't removed - because it died!
bool droidRemove(Droid *psDroid, Droid *pList[MAX_PLAYERS])
{
	CHECK_DROID(psDroid);

	if (isDead(psDroid))
	{
		// droid has already been killed, quit
		return false;
	}

	// leave the current group if any - not if its a Transporter droid
	if (!isTransporter(psDroid) && psDroid->psGroup)
	{
		psDroid->psGroup->remove(psDroid);
		psDroid->psGroup = nullptr;
	}

	// reset the baseStruct
	setDroidBase(psDroid, nullptr);

	removeDroid(psDroid, pList);

	if (psDroid->owningPlayer == selectedPlayer)
	{
		intRefreshScreen();
	}

	return true;
}

void _syncDebugDroid(const char *function, Droid const *psDroid, char ch)
{
	if (psDroid->type != OBJ_DROID) {
		ASSERT(false, "%c Broken psDroid->type %u!", ch, psDroid->type);
		syncDebug("Broken psDroid->type %u!", psDroid->type);
	}
	int list[] =
	{
		ch,

		(int)psDroid->id,

		psDroid->owningPlayer,
		psDroid->position.x, psDroid->position.y, psDroid->position.z,
		psDroid->rotation.direction, psDroid->rotation.pitch, psDroid->rotation.roll,
		(int)psDroid->order.type, psDroid->order.pos.x, psDroid->order.pos.y, psDroid->listSize,
		(int)psDroid->action,
		(int)psDroid->secondaryOrder,
		(int)psDroid->hitPoints,
		(int)psDroid->sMove.Status,
		psDroid->sMove.speed, psDroid->sMove.moveDir,
		psDroid->sMove.pathIndex, (int)psDroid->sMove.asPath.size(),
		psDroid->sMove.src.x, psDroid->sMove.src.y, psDroid->sMove.target.x, psDroid->sMove.target.y, psDroid->sMove.destination.x, psDroid->sMove.destination.y,
		psDroid->sMove.bumpDir, (int)psDroid->sMove.bumpTime, (int)psDroid->sMove.lastBump, (int)psDroid->sMove.pauseTime, psDroid->sMove.bumpPos.x, psDroid->sMove.bumpPos.y, (int)psDroid->sMove.shuffleStart,
		(int)psDroid->experience,
	};
	_syncDebugIntList(function, "%c droid%d = p%d;pos(%d,%d,%d),rot(%d,%d,%d),order%d(%d,%d)^%d,action%d,secondaryOrder%X,body%d,sMove(status%d,speed%d,moveDir%d,path%d/%d,src(%d,%d),target(%d,%d),destination(%d,%d),bump(%d,%d,%d,%d,(%d,%d),%d)),exp%u", list, ARRAY_SIZE(list));
}

/* See if a droid is next to a structure */
static bool droidNextToStruct(Droid *psDroid, Structure *psStruct)
{
	CHECK_DROID(psDroid);

	auto pos = map_coord(psDroid->position);
	int minX = std::max(pos.x - 1, 0);
	int minY = std::max(pos.y - 1, 0);
	int maxX = std::min(pos.x + 1, mapWidth);
	int maxY = std::min(pos.y + 1, mapHeight);
	for (int y = minY; y <= maxY; ++y)
	{
		for (int x = minX; x <= maxX; ++x)
		{
			if (TileHasStructure(mapTile(x, y)) &&
				getTileStructure(x, y) == psStruct)
			{
				return true;
			}
		}
	}

	return false;
}

static bool droidCheckBuildStillInProgress(void *psObj)
{
	if (psObj == nullptr)
	{
		return false;
	}

	auto psDroid = (Droid *)psObj;
	CHECK_DROID(psDroid);

	return !psDroid->deathTime && psDroid->action == DACTION_BUILD;
}

static bool droidBuildStartAudioCallback(void *psObj)
{
	auto psDroid = (Droid *)psObj;

	if (psDroid != nullptr && psDroid->visibleForLocalDisplay())
	{
		audio_PlayObjDynamicTrack(psDroid, ID_SOUND_CONSTRUCTION_LOOP, droidCheckBuildStillInProgress);
	}

	return true;
}



static void droidAddWeldSound(Vector3i iVecEffect)
{
	int iAudioID = ID_SOUND_CONSTRUCTION_1 + (rand() % 4);

	audio_PlayStaticTrack(iVecEffect.x, iVecEffect.z, iAudioID);
}

static void addConstructorEffect(Structure *psStruct)
{
	if ((ONEINTEN) && (psStruct->visibleForLocalDisplay()))
	{
		/* This needs fixing - it's an arse effect! */
		const Vector2i size = psStruct->size() * TILE_UNITS / 4;
		Vector3i temp;
		temp.x = psStruct->position.x + ((rand() % (2 * size.x)) - size.x);
		temp.y = map_TileHeight(map_coord(psStruct->position.x), map_coord(psStruct->position.y)) + (psStruct->displayData.imd->max.y / 6);
		temp.z = psStruct->position.y + ((rand() % (2 * size.y)) - size.y);
		if (rand() % 2)
		{
			droidAddWeldSound(temp);
		}
	}
}

bool droidUpdateDemolishing(Droid *psDroid)
{
	CHECK_DROID(psDroid);

	ASSERT_OR_RETURN(false, psDroid->action == DACTION_DEMOLISH, "unit is not demolishing");
        Structure *psStruct = (Structure *)psDroid->order.psObj;
	ASSERT_OR_RETURN(false, psStruct->type == OBJ_STRUCTURE, "target is not a structure");

	int constructRate = 5 * constructorPoints(asConstructStats + psDroid->asBits[COMP_CONSTRUCT], psDroid->owningPlayer);
	int pointsToAdd = gameTimeAdjustedAverage(constructRate);

	structureDemolish(psStruct, psDroid, pointsToAdd);

	addConstructorEffect(psStruct);

	CHECK_DROID(psDroid);

	return true;
}

void droidStartAction(Droid *psDroid)
{
	psDroid->actionStarted = gameTime;
	psDroid->actionPoints  = 0;
}

// Declared in weapondef.h.
int getRecoil(Weapon const &weapon)
{
	if (weapon.nStat != 0)
	{
		// We have a weapon.
		if (graphicsTime >= weapon.lastFired && graphicsTime < weapon.lastFired + DEFAULT_RECOIL_TIME)
		{
			int recoilTime = graphicsTime - weapon.lastFired;
			int recoilAmount = DEFAULT_RECOIL_TIME / 2 - abs(recoilTime - DEFAULT_RECOIL_TIME / 2);
			int maxRecoil = asWeaponStats[weapon.nStat].recoilValue;  // Max recoil is 1/10 of this value.
			return maxRecoil * recoilAmount / (DEFAULT_RECOIL_TIME / 2 * 10);
		}
		// Recoil effect is over.
	}
	return 0;
}

/*Updates a Repair Droid working on a damaged droid*/
static bool droidUpdateDroidRepairBase(Droid *psRepairDroid,
                                       Droid *psDroidToRepair)
{
	CHECK_DROID(psRepairDroid);

	int iRepairRateNumerator = repairPoints(asRepairStats + psRepairDroid->asBits[COMP_REPAIRUNIT], psRepairDroid->owningPlayer);
	int iRepairRateDenominator = 1;

	//if self repair then add repair points depending on the time delay for the stat
	if (psRepairDroid == psDroidToRepair)
	{
		iRepairRateNumerator *= GAME_TICKS_PER_SEC;
		iRepairRateDenominator *= (asRepairStats + psRepairDroid->asBits[COMP_REPAIRUNIT])->time;
	}

	int iPointsToAdd = gameTimeAdjustedAverage(iRepairRateNumerator, iRepairRateDenominator);

	psDroidToRepair->hitPoints = clip<UDWORD>(psDroidToRepair->hitPoints + iPointsToAdd, 0, psDroidToRepair->originalBody);

	/* add plasma repair effect whilst being repaired */
	if ((ONEINFIVE) && (psDroidToRepair->visibleForLocalDisplay()))
	{
		Vector3i iVecEffect = (psDroidToRepair->position + Vector3i(DROID_REPAIR_SPREAD, DROID_REPAIR_SPREAD, rand() % 8)).xzy();
		effectGiveAuxVar(90 + rand() % 20);
		addEffect(&iVecEffect, EFFECT_EXPLOSION, EXPLOSION_TYPE_LASER, false, nullptr, 0, gameTime - deltaGameTime + 1 + rand() % deltaGameTime);
		droidAddWeldSound(iVecEffect);
	}

	CHECK_DROID(psRepairDroid);
	/* if not finished repair return true else complete repair and return false */
	return psDroidToRepair->hitPoints < psDroidToRepair->originalBody;
}

bool droidUpdateDroidRepair(Droid *psRepairDroid)
{
	ASSERT_OR_RETURN(false, psRepairDroid->action == DACTION_DROIDREPAIR, "Unit does not have unit repair order");
	ASSERT_OR_RETURN(false, psRepairDroid->asBits[COMP_REPAIRUNIT] != 0, "Unit does not have a repair turret");

        Droid *psDroidToRepair = (Droid *)psRepairDroid->psActionTarget[0];
	ASSERT_OR_RETURN(false, psDroidToRepair->type == OBJ_DROID, "Target is not a unit");
	bool needMoreRepair = droidUpdateDroidRepairBase(psRepairDroid, psDroidToRepair);
	if (needMoreRepair && psDroidToRepair->order.type == DORDER_RTR && psDroidToRepair->order.rtrType == RTR_TYPE_DROID && psDroidToRepair->action == DACTION_NONE)
	{
		psDroidToRepair->action = DACTION_WAITDURINGREPAIR;
	}
	if (!needMoreRepair && psDroidToRepair->order.type == DORDER_RTR && psDroidToRepair->order.rtrType == RTR_TYPE_DROID)
	{
		// if psDroidToRepair has a commander, commander will call him back anyway
		// if no commanders, just DORDER_GUARD the repair turret
		orderDroidObj(psDroidToRepair, DORDER_GUARD, psRepairDroid, ModeImmediate);
		secondarySetState(psDroidToRepair, DSO_RETURN_TO_LOC, DSS_NONE);
		psDroidToRepair->order.psObj = nullptr;
	}
	return needMoreRepair;
}

static void droidUpdateDroidSelfRepair(Droid *psRepairDroid)
{
	droidUpdateDroidRepairBase(psRepairDroid, psRepairDroid);
}

/* Return the type of a droid from it's template */
DROID_TYPE droidTemplateType(const DroidStats *psTemplate)
{
	DROID_TYPE type = DROID_DEFAULT;

	if (psTemplate->droidType == DROID_PERSON ||
		psTemplate->droidType == DROID_CYBORG ||
		psTemplate->droidType == DROID_CYBORG_SUPER ||
		psTemplate->droidType == DROID_CYBORG_CONSTRUCT ||
		psTemplate->droidType == DROID_CYBORG_REPAIR ||
		psTemplate->droidType == DROID_TRANSPORTER ||
		psTemplate->droidType == DROID_SUPERTRANSPORTER)
	{
		type = psTemplate->droidType;
	}
	else if (psTemplate->asParts[COMP_BRAIN] != 0)
	{
		type = DROID_COMMAND;
	}
	else if ((asSensorStats + psTemplate->asParts[COMP_SENSOR])->location == LOC_TURRET)
	{
		type = DROID_SENSOR;
	}
	else if ((asECMStats + psTemplate->asParts[COMP_ECM])->location == LOC_TURRET)
	{
		type = DROID_ECM;
	}
	else if (psTemplate->asParts[COMP_CONSTRUCT] != 0)
	{
		type = DROID_CONSTRUCT;
	}
	else if ((asRepairStats + psTemplate->asParts[COMP_REPAIRUNIT])->location == LOC_TURRET)
	{
		type = DROID_REPAIR;
	}
	else if (psTemplate->asWeaps[0] != 0)
	{
		type = DROID_WEAPON;
	}
	/* with more than weapon is still a DROID_WEAPON */
	else if (psTemplate->numWeaps > 1)
	{
		type = DROID_WEAPON;
	}

	return type;
}

template <typename F, typename G>
static unsigned calcSum(const uint8_t (&asParts)[DROID_MAXCOMP], int numWeaps, const uint32_t (&asWeaps)[MAX_WEAPONS], F func, G propulsionFunc)
{
	unsigned sum =
		func(asBrainStats    [asParts[COMP_BRAIN]]) +
		func(asSensorStats   [asParts[COMP_SENSOR]]) +
		func(asECMStats      [asParts[COMP_ECM]]) +
		func(asRepairStats   [asParts[COMP_REPAIRUNIT]]) +
		func(asConstructStats[asParts[COMP_CONSTRUCT]]) +
		propulsionFunc(asBodyStats[asParts[COMP_BODY]], asPropulsionStats[asParts[COMP_PROPULSION]]);
	for (int i = 0; i < numWeaps; ++i)
	{
		sum += func(asWeaponStats[asWeaps[i]]);
	}
	return sum;
}

#define ASSERT_PLAYER_OR_RETURN(retVal, player) \
	ASSERT_OR_RETURN(retVal, player >= 0 && player < MAX_PLAYERS, "Invalid player: %" PRIu32 "", player);

template <typename F, typename G>
static unsigned calcUpgradeSum(const uint8_t (&asParts)[DROID_MAXCOMP], int numWeaps, const uint32_t (&asWeaps)[MAX_WEAPONS], int player, F func, G propulsionFunc)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	unsigned sum =
		func(asBrainStats    [asParts[COMP_BRAIN]].upgrade[player]) +
		func(asSensorStats   [asParts[COMP_SENSOR]].upgrade[player]) +
		func(asECMStats      [asParts[COMP_ECM]].upgrade[player]) +
		func(asRepairStats   [asParts[COMP_REPAIRUNIT]].upgrade[player]) +
		func(asConstructStats[asParts[COMP_CONSTRUCT]].upgrade[player]) +
		propulsionFunc(asBodyStats[asParts[COMP_BODY]].upgrade[player], asPropulsionStats[asParts[COMP_PROPULSION]].upgrade[player]);
	for (int i = 0; i < numWeaps; ++i)
	{
		// asWeaps[i] > 0 check only needed for droids, not templates.
		if (asWeaps[i] > 0)
		{
			sum += func(asWeaponStats[asWeaps[i]].upgrade[player]);
		}
	}
	return sum;
}

struct FilterDroidWeaps
{
	FilterDroidWeaps(unsigned numWeaps, const Weapon (&asWeaps)[MAX_WEAPONS])
	{
		std::transform(asWeaps, asWeaps + numWeaps, this->asWeaps, [](const Weapon &weap) {
			return weap.nStat;
		});
		this->numWeaps = std::remove_if(this->asWeaps, this->asWeaps + numWeaps, [](uint32_t stat) {
			return stat == 0;
		}) - this->asWeaps;
	}

	unsigned numWeaps;
	uint32_t asWeaps[MAX_WEAPONS];
};

template <typename F, typename G>
static unsigned calcSum(const DroidStats *psTemplate, F func, G propulsionFunc)
{
	return calcSum(psTemplate->asParts, psTemplate->numWeaps, psTemplate->asWeaps, func, propulsionFunc);
}

template <typename F, typename G>
static unsigned calcSum(const Droid *psDroid, F func, G propulsionFunc)
{
	FilterDroidWeaps f = {psDroid->numWeapons, psDroid->m_weaponList};
	return calcSum(psDroid->asBits, f.numWeaps, f.asWeaps, func, propulsionFunc);
}

template <typename F, typename G>
static unsigned calcUpgradeSum(const DroidStats *psTemplate, int player, F func, G propulsionFunc)
{
	return calcUpgradeSum(psTemplate->asParts, psTemplate->numWeaps, psTemplate->asWeaps, player, func, propulsionFunc);
}

template <typename F, typename G>
static unsigned calcUpgradeSum(const Droid *psDroid, int player, F func, G propulsionFunc)
{
	FilterDroidWeaps f = {psDroid->numWeapons, psDroid->m_weaponList};
	return calcUpgradeSum(psDroid->asBits, f.numWeaps, f.asWeaps, player, func, propulsionFunc);
}

/* Calculate the weight of a droid from it's template */
UDWORD calcDroidWeight(const DroidStats *psTemplate)
{
	return calcSum(psTemplate, [](COMPONENT_STATS const &stat) {
		return stat.weight;
	}, [](BODY_STATS const &bodyStat, PROPULSION_STATS const &propStat) {
		// Propulsion weight is a percentage of the body weight.
		return bodyStat.weight * (100 + propStat.weight) / 100;
	});
}

template <typename T>
static uint32_t calcBody(T *obj, int player)
{
	int hitpoints = calcUpgradeSum(obj, player, [](COMPONENT_STATS::UPGRADE const &upgrade) {
		return upgrade.hitpoints;
	}, [](BODY_STATS::UPGRADE const &bodyUpgrade, PROPULSION_STATS::UPGRADE const &propUpgrade) {
		// propulsion hitpoints can be a percentage of the body's hitpoints
		return bodyUpgrade.hitpoints * (100 + propUpgrade.hitpointPctOfBody) / 100 + propUpgrade.hitpoints;
	});

	int hitpointPct = calcUpgradeSum(obj, player, [](COMPONENT_STATS::UPGRADE const &upgrade) {
		return upgrade.hitpointPct - 100;
	}, [](BODY_STATS::UPGRADE const &bodyUpgrade, PROPULSION_STATS::UPGRADE const &propUpgrade) {
		return bodyUpgrade.hitpointPct - 100 + propUpgrade.hitpointPct - 100;
	});

	// Final adjustment based on the hitpoint modifier
	return hitpoints * (100 + hitpointPct) / 100;
}

// Calculate the body points of a droid from its template
UDWORD calcTemplateBody(const DroidStats *psTemplate, UBYTE player)
{
	if (psTemplate == nullptr)
	{
		ASSERT(false, "null template");
		return 0;
	}

	return calcBody(psTemplate, player);
}

// Calculate the base body points of a droid with upgrades
static UDWORD calcDroidBaseBody(Droid *psDroid)
{
	return calcBody(psDroid, psDroid->owningPlayer);
}


/* Calculate the base speed of a droid from it's template */
UDWORD calcDroidBaseSpeed(const DroidStats *psTemplate, UDWORD weight, UBYTE player)
{
	unsigned speed = asPropulsionTypes[asPropulsionStats[psTemplate->asParts[COMP_PROPULSION]].propulsionType].powerRatioMult *
				 bodyPower(&asBodyStats[psTemplate->asParts[COMP_BODY]], player) / MAX(1, weight);

	// reduce the speed of medium/heavy VTOLs
	if (asPropulsionStats[psTemplate->asParts[COMP_PROPULSION]].propulsionType == PROPULSION_TYPE_LIFT)
	{
		if (asBodyStats[psTemplate->asParts[COMP_BODY]].size == SIZE_HEAVY)
		{
			speed /= 4;
		}
		else if (asBodyStats[psTemplate->asParts[COMP_BODY]].size == SIZE_MEDIUM)
		{
			speed = speed * 3 / 4;
		}
	}

	// applies the engine output bonus if output > weight
	if (asBodyStats[psTemplate->asParts[COMP_BODY]].base.power > weight)
	{
		speed = speed * 3 / 2;
	}

	return speed;
}


/* Calculate the speed of a droid over a terrain */
UDWORD calcDroidSpeed(UDWORD baseSpeed, UDWORD terrainType, UDWORD propIndex, UDWORD level)
{
	PROPULSION_STATS const &propulsion = asPropulsionStats[propIndex];

	// Factor in terrain
	unsigned speed = baseSpeed * getSpeedFactor(terrainType, propulsion.propulsionType) / 100;

	// Need to ensure doesn't go over the max speed possible for this propulsion
	speed = std::min(speed, propulsion.maxSpeed);

	// Factor in experience
	speed *= 100 + EXP_SPEED_BONUS * level;
	speed /= 100;

	return speed;
}

template <typename T>
static uint32_t calcBuild(T *obj)
{
	return calcSum(obj, [](COMPONENT_STATS const &stat) {
		return stat.buildPoints;
	}, [](BODY_STATS const &bodyStat, PROPULSION_STATS const &propStat) {
		// Propulsion power points are a percentage of the body's build points.
		return bodyStat.buildPoints * (100 + propStat.buildPoints) / 100;
	});
}

/* Calculate the points required to build the template - used to calculate time*/
UDWORD calcTemplateBuild(const DroidStats *psTemplate)
{
	return calcBuild(psTemplate);
}

UDWORD calcDroidPoints(Droid *psDroid)
{
	return calcBuild(psDroid);
}

template <typename T>
static uint32_t calcPower(const T *obj)
{
	ASSERT_NOT_NULLPTR_OR_RETURN(0, obj);
	return calcSum(obj, [](COMPONENT_STATS const &stat) {
		return stat.buildPower;
	}, [](BODY_STATS const &bodyStat, PROPULSION_STATS const &propStat) {
		// Propulsion power points are a percentage of the body's power points.
		return bodyStat.buildPower * (100 + propStat.buildPower) / 100;
	});
}

/* Calculate the power points required to build/maintain a template */
UDWORD calcTemplatePower(const DroidStats *psTemplate)
{
	return calcPower(psTemplate);
}

/* Calculate the power points required to build/maintain a droid */
UDWORD calcDroidPower(const Droid *psDroid)
{
	return calcPower(psDroid);
}

//Builds an instance of a Droid - the x/y passed in are in world coords.
Droid *reallyBuildDroid(const DroidStats *pTemplate, Position pos, UDWORD player, bool onMission, Rotation rot)
{
	// Don't use this assertion in single player, since droids can finish building while on an away mission
	ASSERT(!bMultiPlayer || worldOnMap(pos.x, pos.y), "the build locations are not on the map");

	ASSERT_OR_RETURN(nullptr, player < MAX_PLAYERS, "Invalid player: %" PRIu32 "", player);

        Droid *psDroid = new Droid(generateSynchronisedObjectId(), player);
	droidSetName(psDroid, getStatsName(pTemplate));

	// Set the droids type
	psDroid->droidType = droidTemplateType(pTemplate);  // Is set again later to the same thing, in droidSetBits.
	psDroid->position = pos;
	psDroid->rotation = rot;

	//don't worry if not on homebase cos not being drawn yet
	if (!onMission)
	{
		//set droid height
		psDroid->position.z = map_Height(psDroid->position.x, psDroid->position.y);
	}

	if (isTransporter(psDroid) || psDroid->droidType == DROID_COMMAND)
	{
		DROID_GROUP *psGrp = grpCreate();
		psGrp->add(psDroid);
	}

	// find the highest stored experience
	// Unless game time is stopped, then we're hopefully loading a game and
	// don't want to use up recycled experience for the droids we just loaded.
	if (!gameTimeIsStopped() &&
		(psDroid->droidType != DROID_CONSTRUCT) &&
		(psDroid->droidType != DROID_CYBORG_CONSTRUCT) &&
		(psDroid->droidType != DROID_REPAIR) &&
		(psDroid->droidType != DROID_CYBORG_REPAIR) &&
		!isTransporter(psDroid) &&
		!recycled_experience[psDroid->owningPlayer].empty())
	{
		psDroid->experience = recycled_experience[psDroid->owningPlayer].top();
		recycled_experience[psDroid->owningPlayer].pop();
	}
	else
	{
		psDroid->experience = 0;
	}
	psDroid->kills = 0;

	droidSetBits(pTemplate, psDroid);

	//calculate the droids total weight
	psDroid->weight = calcDroidWeight(pTemplate);

	// Initialise the movement stuff
	psDroid->baseSpeed = calcDroidBaseSpeed(pTemplate, psDroid->weight, (UBYTE)player);

	initDroidMovement(psDroid);

	//allocate 'easy-access' data!
	psDroid->hitPoints = calcDroidBaseBody(psDroid); // includes upgrades
	ASSERT(psDroid->hitPoints > 0, "Invalid number of hitpoints");
	psDroid->originalBody = psDroid->hitPoints;

	/* Set droid's initial illumination */
	psDroid->displayData.imd = BODY_IMD(psDroid, psDroid->owningPlayer);

	//don't worry if not on homebase cos not being drawn yet
	if (!onMission)
	{
		/* People always stand upright */
		if (psDroid->droidType != DROID_PERSON)
		{
			updateDroidOrientation(psDroid);
		}
		visTilesUpdate(psDroid);
	}

	/* transporter-specific stuff */
	if (isTransporter(psDroid))
	{
		//add transporter launch button if selected player and not a reinforcable situation
		if (player == selectedPlayer && !missionCanReEnforce())
		{
			(void)intAddTransporterLaunch(psDroid);
		}

		//set droid height to be above the terrain
		psDroid->position.z += TRANSPORTER_HOVER_HEIGHT;

		/* reset halt secondary order from guard to hold */
		secondarySetState(psDroid, DSO_HALTTYPE, DSS_HALT_HOLD);
	}

	if (player == selectedPlayer)
	{
		scoreUpdateVar(WD_UNITS_BUILT);
	}

	// Avoid droid appearing to jump or turn on spawn.
	psDroid->prevSpacetime.m_position = psDroid->position;
	psDroid->prevSpacetime.m_rotation = psDroid->rotation;

	debug(LOG_LIFE, "created droid for player %d, droid = %p, id=%d (%s): position: x(%d)y(%d)z(%d)", player, static_cast<void *>(psDroid), (int)psDroid->id, psDroid->name, psDroid->position.x, psDroid->position.y, psDroid->position.z);

	return psDroid;
}

Droid *buildDroid(DroidStats *pTemplate, UDWORD x, UDWORD y, UDWORD player, bool onMission, const INITIAL_DROID_ORDERS *initialOrders, Rotation rot)
{
	ASSERT_OR_RETURN(nullptr, player < MAX_PLAYERS, "invalid player?: %" PRIu32 "", player);
	// ajl. droid will be created, so inform others
	if (bMultiMessages)
	{
		// Only sends if it's ours, otherwise the owner should send the message.
		SendDroid(pTemplate, x, y, player, generateNewObjectId(), initialOrders);
		return nullptr;
	}
	else
	{
		return reallyBuildDroid(pTemplate, Position(x, y, 0), player, onMission, rot);
	}
}

// Sets the parts array in a template given a droid.
void templateSetParts(const Droid *psDroid, DroidStats *psTemplate)
{
	psTemplate->numWeaps = 0;
	psTemplate->droidType = psDroid->droidType;
	for (int inc = 0; inc < MAX_WEAPONS; inc++)
	{
		//this should fix the NULL weapon stats for empty weaponslots
		psTemplate->asWeaps[inc] = 0;
		if (psDroid->m_weaponList[inc].nStat > 0)
		{
			psTemplate->numWeaps += 1;
			psTemplate->asWeaps[inc] = psDroid->m_weaponList[inc].nStat;
		}
	}
	memcpy(psTemplate->asParts, psDroid->asBits, sizeof(psDroid->asBits));
}

/* Make all the droids for a certain player a member of a specific group */
void assignDroidsToGroup(UDWORD	playerNumber, UDWORD groupNumber, bool clearGroup)
{
  Droid *psDroid;
	bool	bAtLeastOne = false;
	FLAG_POSITION	*psFlagPos;

	ASSERT_OR_RETURN(, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	if (groupNumber < UBYTE_MAX)
	{
		/* Run through all the droids */
		for (psDroid = allDroidLists[playerNumber]; psDroid != nullptr; psDroid = psDroid->psNext)
		{
			/* Clear out the old ones */
			if (clearGroup && psDroid->group == groupNumber)
			{
				psDroid->group = UBYTE_MAX;
			}

			/* Only assign the currently selected ones */
			if (psDroid->selected)
			{
				/* Set them to the right group - they can only be a member of one group */
				psDroid->group = (UBYTE)groupNumber;
				bAtLeastOne = true;
			}
		}
	}
	if (bAtLeastOne)
	{
		//clear the Deliv Point if one
		ASSERT_OR_RETURN(, selectedPlayer < MAX_PLAYERS, "Unsupported selectedPlayer: %" PRIu32 "", selectedPlayer);
		for (psFlagPos = apsFlagPosLists[selectedPlayer]; psFlagPos;
			 psFlagPos = psFlagPos->psNext)
		{
			psFlagPos->selected = false;
		}
		groupConsoleInformOfCreation(groupNumber);
		secondarySetAverageGroupState(selectedPlayer, groupNumber);
	}
}


void removeDroidsFromGroup(UDWORD playerNumber)
{
  Droid *psDroid;
	unsigned removedCount = 0;

	ASSERT_OR_RETURN(, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	for (psDroid = allDroidLists[playerNumber]; psDroid != nullptr; psDroid = psDroid->psNext)
	{
		if (psDroid->selected)
		{
			psDroid->group = UBYTE_MAX;
			removedCount++;
		}
	}
	if (removedCount)
	{
		groupConsoleInformOfRemoval();
	}
}

bool activateGroupAndMove(UDWORD playerNumber, UDWORD groupNumber)
{
  Droid *psDroid, *psCentreDroid = nullptr;
	bool selected = false;
	FLAG_POSITION	*psFlagPos;

	ASSERT_OR_RETURN(false, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	if (groupNumber < UBYTE_MAX)
	{
		for (psDroid = allDroidLists[playerNumber]; psDroid != nullptr; psDroid = psDroid->psNext)
		{
			/* Wipe out the ones in the wrong group */
			if (psDroid->selected && psDroid->group != groupNumber)
			{
				DeSelectDroid(psDroid);
			}
			/* Get the right ones */
			if (psDroid->group == groupNumber)
			{
				SelectDroid(psDroid);
				psCentreDroid = psDroid;
			}
		}

		/* There was at least one in the group */
		if (psCentreDroid)
		{
			//clear the Deliv Point if one
			ASSERT(selectedPlayer < MAX_PLAYERS, "Unsupported selectedPlayer: %" PRIu32 "", selectedPlayer);
			if (selectedPlayer < MAX_PLAYERS)
			{
				for (psFlagPos = apsFlagPosLists[selectedPlayer]; psFlagPos;
					 psFlagPos = psFlagPos->psNext)
				{
					psFlagPos->selected = false;
				}
			}

			selected = true;
			if (getWarCamStatus())
			{
				camToggleStatus();			 // messy - fix this
				processWarCam(); //odd, but necessary
				camToggleStatus();				// messy - FIXME
			}
			else
			{
				/* Centre display on him if warcam isn't active */
				setViewPos(map_coord(psCentreDroid->position.x), map_coord(psCentreDroid->position.y), true);
			}
		}
	}

	if (selected)
	{
		groupConsoleInformOfCentering(groupNumber);
	}

	return selected;
}

bool activateNoGroup(UDWORD playerNumber, const SELECTIONTYPE selectionType, const SELECTION_CLASS selectionClass, const bool bOnScreen) {
  Droid *psDroid;
	bool selected = false;
	FLAG_POSITION	*psFlagPos;
	SELECTIONTYPE dselectionType = selectionType;
	SELECTION_CLASS dselectionClass = selectionClass;
	bool dbOnScreen = bOnScreen;

	ASSERT_OR_RETURN(false, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	selDroidSelection(selectedPlayer, dselectionClass, dselectionType, dbOnScreen);
	for (psDroid = allDroidLists[playerNumber]; psDroid; psDroid = psDroid->psNext)
	{
		/* Wipe out the ones in the wrong group */
		if (psDroid->selected && psDroid->group != UBYTE_MAX)
		{
			DeSelectDroid(psDroid);
		}
	}
	if (selected)
	{
		//clear the Deliv Point if one
		ASSERT_OR_RETURN(false, selectedPlayer < MAX_PLAYERS, "Unsupported selectedPlayer: %" PRIu32 "", selectedPlayer);
		for (psFlagPos = apsFlagPosLists[selectedPlayer]; psFlagPos;
			 psFlagPos = psFlagPos->psNext)
		{
			psFlagPos->selected = false;
		}
	}
	return selected;
}

bool activateGroup(UDWORD playerNumber, UDWORD groupNumber)
{
  Droid *psDroid;
	bool selected = false;
	FLAG_POSITION	*psFlagPos;

	ASSERT_OR_RETURN(false, playerNumber < MAX_PLAYERS, "Invalid player: %" PRIu32 "", playerNumber);

	if (groupNumber < UBYTE_MAX)
	{
		for (psDroid = allDroidLists[playerNumber]; psDroid; psDroid = psDroid->psNext)
		{
			/* Wipe out the ones in the wrong group */
			if (psDroid->selected && psDroid->group != groupNumber)
			{
				DeSelectDroid(psDroid);
			}
			/* Get the right ones */
			if (psDroid->group == groupNumber)
			{
				SelectDroid(psDroid);
				selected = true;
			}
		}
	}

	if (selected)
	{
		//clear the Deliv Point if one
		ASSERT_OR_RETURN(false, selectedPlayer < MAX_PLAYERS, "Unsupported selectedPlayer: %" PRIu32 "", selectedPlayer);
		for (psFlagPos = apsFlagPosLists[selectedPlayer]; psFlagPos;
			 psFlagPos = psFlagPos->psNext)
		{
			psFlagPos->selected = false;
		}
		groupConsoleInformOfSelection(groupNumber);
	}
	return selected;
}

void	groupConsoleInformOfSelection(UDWORD groupNumber)
{
	unsigned int num_selected = selNumSelected(selectedPlayer);

	CONPRINTF(ngettext("Group %u selected - %u Unit", "Group %u selected - %u Units", num_selected), groupNumber, num_selected);
}

void	groupConsoleInformOfCreation(UDWORD groupNumber)
{
	if (!getWarCamStatus())
	{
		unsigned int num_selected = selNumSelected(selectedPlayer);

		CONPRINTF(ngettext("%u unit assigned to Group %u", "%u units assigned to Group %u", num_selected), num_selected, groupNumber);
	}

}

void 	groupConsoleInformOfRemoval()
{
	if (!getWarCamStatus())
	{
		unsigned int num_selected = selNumSelected(selectedPlayer);

		CONPRINTF(ngettext("%u units removed from their Group", "%u units removed from their Group", num_selected), num_selected);
	}
}

void	groupConsoleInformOfCentering(UDWORD groupNumber)
{
	unsigned int num_selected = selNumSelected(selectedPlayer);

	if (!getWarCamStatus())
	{
		CONPRINTF(ngettext("Centered on Group %u - %u Unit", "Centered on Group %u - %u Units", num_selected), groupNumber, num_selected);
	}
	else
	{
		CONPRINTF(ngettext("Aligning with Group %u - %u Unit", "Aligning with Group %u - %u Units", num_selected), groupNumber, num_selected);
	}
}

/**
 * calculate muzzle base location in 3d world
 */
bool calcDroidMuzzleBaseLocation(const Droid *psDroid, Vector3i *muzzle, int weapon_slot)
{
	const iIMDShape *psBodyImd = BODY_IMD(psDroid, psDroid->owningPlayer);

	CHECK_DROID(psDroid);

	if (psBodyImd && psBodyImd->nconnectors)
	{
		Vector3i barrel(0, 0, 0);

		Affine3F af;

		af.Trans(psDroid->position.x, -psDroid->position.z, psDroid->position.y);

		//matrix = the center of droid
		af.RotY(psDroid->rotation.direction);
		af.RotX(psDroid->rotation.pitch);
		af.RotZ(-psDroid->rotation.roll);
		af.Trans(psBodyImd->connectors[weapon_slot].x, -psBodyImd->connectors[weapon_slot].z,
				 -psBodyImd->connectors[weapon_slot].y);//note y and z flipped

		*muzzle = (af * barrel).xzy();
		muzzle->z = -muzzle->z;
	}
	else
	{
		*muzzle = psDroid->position + Vector3i(0, 0, psDroid->displayData.imd->max.y);
	}

	CHECK_DROID(psDroid);

	return true;
}

/**
 * calculate muzzle tip location in 3d world
 */
bool calcDroidMuzzleLocation(const Droid *psDroid, Vector3i *muzzle, int weapon_slot)
{
	const iIMDShape *psBodyImd = BODY_IMD(psDroid, psDroid->owningPlayer);

	CHECK_DROID(psDroid);

	if (psBodyImd && psBodyImd->nconnectors)
	{
		char debugStr[250], debugLen = 0;  // Each "(%d,%d,%d)" uses up to 34 bytes, for very large values. So 250 isn't exaggerating.

		Vector3i barrel(0, 0, 0);
		const iIMDShape *psWeaponImd = nullptr, *psMountImd = nullptr;

		if (psDroid->m_weaponList[weapon_slot].nStat)
		{
			psMountImd = WEAPON_MOUNT_IMD(psDroid, weapon_slot);
			psWeaponImd = WEAPON_IMD(psDroid, weapon_slot);
		}

		Affine3F af;

		af.Trans(psDroid->position.x, -psDroid->position.z, psDroid->position.y);

		//matrix = the center of droid
		af.RotY(psDroid->rotation.direction);
		af.RotX(psDroid->rotation.pitch);
		af.RotZ(-psDroid->rotation.roll);
		af.Trans(psBodyImd->connectors[weapon_slot].x, -psBodyImd->connectors[weapon_slot].z,
				 -psBodyImd->connectors[weapon_slot].y);//note y and z flipped
		debugLen += sprintf(debugStr + debugLen, "connect:body[%d]=(%d,%d,%d)", weapon_slot, psBodyImd->connectors[weapon_slot].x, -psBodyImd->connectors[weapon_slot].z, -psBodyImd->connectors[weapon_slot].y);

		//matrix = the weapon[slot] mount on the body
		af.RotY(psDroid->m_weaponList[weapon_slot].rot.direction);  // +ve anticlockwise

		// process turret mount
		if (psMountImd && psMountImd->nconnectors)
		{
			af.Trans(psMountImd->connectors->x, -psMountImd->connectors->z, -psMountImd->connectors->y);
			debugLen += sprintf(debugStr + debugLen, ",turret=(%d,%d,%d)", psMountImd->connectors->x, -psMountImd->connectors->z, -psMountImd->connectors->y);
		}

		//matrix = the turret connector for the gun
		af.RotX(psDroid->m_weaponList[weapon_slot].rot.pitch);      // +ve up

		//process the gun
		if (psWeaponImd && psWeaponImd->nconnectors)
		{
			unsigned int connector_num = 0;

			// which barrel is firing if model have multiple muzzle connectors?
			if (psDroid->m_weaponList[weapon_slot].shotsFired && (psWeaponImd->nconnectors > 1))
			{
				// shoot first, draw later - substract one shot to get correct results
				connector_num = (psDroid->m_weaponList[weapon_slot].shotsFired - 1) % (psWeaponImd->nconnectors);
			}

			barrel = Vector3i(psWeaponImd->connectors[connector_num].x,
							  -psWeaponImd->connectors[connector_num].z,
							  -psWeaponImd->connectors[connector_num].y);
			debugLen += sprintf(debugStr + debugLen, ",barrel[%u]=(%d,%d,%d)", connector_num, psWeaponImd->connectors[connector_num].x, -psWeaponImd->connectors[connector_num].y, -psWeaponImd->connectors[connector_num].z);
		}

		*muzzle = (af * barrel).xzy();
		muzzle->z = -muzzle->z;
		sprintf(debugStr + debugLen, ",muzzle=(%d,%d,%d)", muzzle->x, muzzle->y, muzzle->z);

		syncDebug("%s", debugStr);
	}
	else
	{
		*muzzle = psDroid->position + Vector3i(0, 0, psDroid->displayData.imd->max.y);
	}

	CHECK_DROID(psDroid);

	return true;
}

struct rankMap
{
	unsigned int kills;          // required minimum amount of kills to reach this rank
	unsigned int commanderKills; // required minimum amount of kills for a commander (or sensor) to reach this rank
	const char  *name;           // name of this rank
};

UDWORD getDroidEffectiveLevel(const Droid *psDroid)
{
	UDWORD level = getDroidLevel(psDroid);
	UDWORD cmdLevel = 0;

	// get commander level
	if (hasCommander(psDroid))
	{
		cmdLevel = cmdGetCommanderLevel(psDroid);

		// Commanders boost units' effectiveness just by being assigned to it
		level++;
	}

	return MAX(level, cmdLevel);
}

const char *getDroidLevelName(const Droid *psDroid)
{
	const BRAIN_STATS *psStats = getBrainStats(psDroid);
	return PE_("rank", psStats->rankNames[getDroidLevel(psDroid)].c_str());
}

UDWORD	getNumDroidsForLevel(uint32_t player, UDWORD level)
{
  Droid *psDroid;
	UDWORD	count;

	if (player >= MAX_PLAYERS) { return 0; }

	for (psDroid = allDroidLists[player], count = 0;
		 psDroid; psDroid = psDroid->psNext)
	{
		if (getDroidLevel(psDroid) == level)
		{
			count++;
		}
	}

	return count;
}

//
// Set the name of a droid in it's DROID structure.
//
// - only possible on the PC where you can adjust the names,
//
void droidSetName(Droid *psDroid, const char *pName)
{
	sstrcpy(psDroid->name, pName);
}

// ////////////////////////////////////////////////////////////////////////////
// returns true when no droid on x,y square.
bool noDroid(UDWORD x, UDWORD y)
{
	unsigned int i;

	// check each droid list
	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		const Droid *psDroid;
		for (psDroid = allDroidLists[i]; psDroid; psDroid = psDroid->psNext)
		{
			if (map_coord(psDroid->position.x) == x
				&& map_coord(psDroid->position.y) == y)
			{
				return false;
			}
		}
	}
	return true;
}

// ////////////////////////////////////////////////////////////////////////////
// returns true when at most one droid on x,y square.
static bool oneDroidMax(UDWORD x, UDWORD y)
{
	UDWORD i;
	bool bFound = false;
        Droid *pD;
	// check each droid list
	for (i = 0; i < MAX_PLAYERS; i++)
	{
		for (pD = allDroidLists[i]; pD ; pD = pD->psNext)
		{
			if (map_coord(pD->position.x) == x
				&& map_coord(pD->position.y) == y)
			{
				if (bFound)
				{
					return false;
				}

				bFound = true;//first droid on this square so continue
			}
		}
	}
	return true;
}

// ////////////////////////////////////////////////////////////////////////////
// returns true if it's a sensible place to put that droid.
static bool sensiblePlace(SDWORD x, SDWORD y, PROPULSION_TYPE propulsion)
{
	// not too near the edges.
	if ((x < TOO_NEAR_EDGE) || (x > (SDWORD)(mapWidth - TOO_NEAR_EDGE)))
	{
		return false;
	}
	if ((y < TOO_NEAR_EDGE) || (y > (SDWORD)(mapHeight - TOO_NEAR_EDGE)))
	{
		return false;
	}

	// not on a blocking tile.
	if (fpathBlockingTile(x, y, propulsion))
	{
		return false;
	}

	return true;
}

// ------------------------------------------------------------------------------------
// Should stop things being placed in inaccessible areas? Assume wheeled propulsion.
bool	zonedPAT(UDWORD x, UDWORD y)
{
	return sensiblePlace(x, y, PROPULSION_TYPE_WHEELED) && noDroid(x, y);
}

static bool canFitDroid(UDWORD x, UDWORD y)
{
	return sensiblePlace(x, y, PROPULSION_TYPE_WHEELED) && oneDroidMax(x, y);
}

/// find a tile for which the function will return true
bool	pickATileGen(UDWORD *x, UDWORD *y, UBYTE numIterations,
					 bool (*function)(UDWORD x, UDWORD y))
{
	return pickATileGenThreat(x, y, numIterations, -1, -1, function);
}

bool pickATileGen(Vector2i *pos, unsigned numIterations, bool (*function)(UDWORD x, UDWORD y))
{
	UDWORD x = pos->x, y = pos->y;
	bool ret = pickATileGenThreat(&x, &y, numIterations, -1, -1, function);
	*pos = Vector2i(x, y);
	return ret;
}

static bool ThreatInRange(SDWORD player, SDWORD range, SDWORD rangeX, SDWORD rangeY, bool bVTOLs)
{
	UDWORD				i, structType;
        Structure *psStruct;
        Droid *psDroid;

	const int tx = map_coord(rangeX);
	const int ty = map_coord(rangeY);

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		if ((alliances[player][i] == ALLIANCE_FORMED) || (i == player))
		{
			continue;
		}

		//check structures
		for (psStruct = apsStructLists[i]; psStruct; psStruct = psStruct->psNext)
		{
			if (psStruct->visible[player] || psStruct->creationTime == 2)	// if can see it or started there
			{
				if (psStruct->status == SS_BUILT)
				{
					structType = psStruct->stats->type;

					switch (structType)		//dangerous to get near these structures
					{
					case REF_DEFENSE:
					case REF_CYBORG_FACTORY:
					case REF_FACTORY:
					case REF_VTOL_FACTORY:
					case REF_REARM_PAD:

						if (range < 0
							|| world_coord(static_cast<int32_t>(hypotf(tx - map_coord(psStruct->position.x), ty - map_coord(psStruct->position
                                                               .y)))) < range)	//enemy in range
						{
							return true;
						}

						break;
					}
				}
			}
		}

		//check droids
		for (psDroid = allDroidLists[i]; psDroid; psDroid = psDroid->psNext)
		{
			if (psDroid->visible[player])		//can see this droid?
			{
				if (!objHasWeapon((GameObject *)psDroid))
				{
					continue;
				}

				//if VTOLs are excluded, skip them
				if (!bVTOLs && ((asPropulsionStats[psDroid->asBits[COMP_PROPULSION]].propulsionType == PROPULSION_TYPE_LIFT) || isTransporter(psDroid)))
				{
					continue;
				}

				if (range < 0
					|| world_coord(static_cast<int32_t>(hypotf(tx - map_coord(psDroid->position.x), ty - map_coord(psDroid->position.y)))) < range)	//enemy in range
				{
					return true;
				}
			}
		}
	}

	return false;
}

/// find a tile for which the passed function will return true without any threat in the specified range
bool	pickATileGenThreat(UDWORD *x, UDWORD *y, UBYTE numIterations, SDWORD threatRange,
						   SDWORD player, bool (*function)(UDWORD x, UDWORD y))
{
	SDWORD		i, j;
	SDWORD		startX, endX, startY, endY;
	UDWORD		passes;
	Vector3i	origin(world_coord(*x), world_coord(*y), 0);

	ASSERT_OR_RETURN(false, *x < mapWidth, "x coordinate is off-map for pickATileGen");
	ASSERT_OR_RETURN(false, *y < mapHeight, "y coordinate is off-map for pickATileGen");

	if (function(*x, *y) && ((threatRange <= 0) || (!ThreatInRange(player, threatRange, *x, *y, false))))	//TODO: vtol check really not needed?
	{
		return (true);
	}

	/* Initial box dimensions and set iteration count to zero */
	startX = endX = *x;	startY = endY = *y;	passes = 0;

	/* Keep going until we get a tile or we exceed distance */
	while (passes < numIterations)
	{
		/* Process whole box */
		for (i = startX; i <= endX; i++)
		{
			for (j = startY; j <= endY; j++)
			{
				/* Test only perimeter as internal tested previous iteration */
				if (i == startX || i == endX || j == startY || j == endY)
				{
					Vector3i newPos(world_coord(i), world_coord(j), 0);

					/* Good enough? */
					if (function(i, j)
						&& fpathCheck(origin, newPos, PROPULSION_TYPE_WHEELED)
						&& ((threatRange <= 0) || (!ThreatInRange(player, threatRange, world_coord(i), world_coord(j), false))))
					{
						/* Set exit conditions and get out NOW */
						*x = i;	*y = j;
						return true;
					}
				}
			}
		}
		/* Expand the box out in all directions - off map handled by tileAcceptable */
		startX--; startY--;	endX++;	endY++;	passes++;
	}
	/* If we got this far, then we failed - passed in values will be unchanged */
	return false;

}

/// find a tile for a wheeled droid with only one other droid present
PICKTILE pickHalfATile(UDWORD *x, UDWORD *y, UBYTE numIterations)
{
	return pickATileGen(x, y, numIterations, canFitDroid) ? FREE_TILE : NO_FREE_TILE;
}

/* Looks through the players list of droids to see if any of them are
building the specified structure - returns true if finds one*/
bool checkDroidsBuilding(Structure *psStructure)
{
  Droid *psDroid;

	for (psDroid = allDroidLists[psStructure->owningPlayer]; psDroid != nullptr; psDroid =
			 psDroid->psNext)
	{
		//check DORDER_BUILD, HELP_BUILD is handled the same
                GameObject *const psStruct = orderStateObj(psDroid, DORDER_BUILD);
		if ((Structure *)psStruct == psStructure)
		{
			return true;
		}
	}
	return false;
}

/* Looks through the players list of droids to see if any of them are
demolishing the specified structure - returns true if finds one*/
bool checkDroidsDemolishing(Structure *psStructure)
{
  Droid *psDroid;

	for (psDroid = allDroidLists[psStructure->owningPlayer]; psDroid != nullptr; psDroid =
			 psDroid->psNext)
	{
		//check DORDER_DEMOLISH
                GameObject *const psStruct = orderStateObj(psDroid, DORDER_DEMOLISH);
		if ((Structure *)psStruct == psStructure)
		{
			return true;
		}
	}
	return false;
}


int nextModuleToBuild(Structure const *psStruct, int lastOrderedModule)
{
	int order = 0;
	UDWORD	i = 0;

	ASSERT_OR_RETURN(0, psStruct != nullptr && psStruct->stats != nullptr, "Invalid structure pointer");

	int next = psStruct->status == SS_BUILT ? 1 : 0; // If complete, next is one after the current number of modules, otherwise next is the one we're working on.
	int max;
	switch (psStruct->stats->type)
	{
	case REF_POWER_GEN:
		//check room for one more!
		max = std::max<int>(psStruct->capacity + next, lastOrderedModule + 1);
		if (max <= 1)
		{
			i = powerModuleStat;
			order = max;
		}
		break;
	case REF_FACTORY:
	case REF_VTOL_FACTORY:
		//check room for one more!
		max = std::max<int>(psStruct->capacity + next, lastOrderedModule + 1);
		if (max <= NUM_FACTORY_MODULES)
		{
			i = factoryModuleStat;
			order = max;
		}
		break;
	case REF_RESEARCH:
		//check room for one more!
		max = std::max<int>(psStruct->capacity + next, lastOrderedModule + 1);
		if (max <= 1)
		{
			i = researchModuleStat;
			order = max;  // Research modules are weird. Build one, get three free.
		}
		break;
	default:
		//no other structures can have modules attached
		break;
	}

	if (order)
	{
		// Check availability of Module
		if (!((i < numStructureStats) &&
			  (apStructTypeLists[psStruct->owningPlayer][i] == AVAILABLE)))
		{
			order = 0;
		}
	}

	return order;
}

/*Deals with building a module - checking if any droid is currently doing this
 - if so, helping to build the current one*/
void setUpBuildModule(Droid *psDroid)
{
	Vector2i tile = map_coord(psDroid->order.pos);

	//check not another Truck started
        Structure *psStruct = getTileStructure(tile.x, tile.y);
	if (psStruct)
	{
		// if a droid is currently building, or building is in progress of being built/upgraded the droid's order should be DORDER_HELPBUILD
		if (checkDroidsBuilding(psStruct) || !psStruct->status)
		{
			//set up the help build scenario
			psDroid->order.type = DORDER_HELPBUILD;
			setDroidTarget(psDroid, (GameObject *)psStruct);
			if (droidStartBuild(psDroid))
			{
				psDroid->action = DACTION_BUILD;
				return;
			}
		}
		else
		{
			if (nextModuleToBuild(psStruct, -1) > 0)
			{
				//no other droids building so just start it off
				if (droidStartBuild(psDroid))
				{
					psDroid->action = DACTION_BUILD;
					return;
				}
			}
		}
	}
	cancelBuild(psDroid);
}

/* Just returns true if the droid's present body points aren't as high as the original*/
bool	droidIsDamaged(const Droid *psDroid)
{
	return psDroid->hitPoints < psDroid->originalBody;
}


char const *getDroidResourceName(char const *pName)
{
	/* See if the name has a string resource associated with it by trying
	 * to get the string resource.
	 */
	return strresGetString(psStringRes, pName);
}


/*checks to see if an electronic warfare weapon is attached to the droid*/
bool electronicDroid(const Droid *psDroid)
{
	CHECK_DROID(psDroid);

	//use slot 0 for now
	if (psDroid->numWeapons > 0 && asWeaponStats[psDroid->m_weaponList[0].nStat].weaponSubClass == WSC_ELECTRONIC)
	{
		return true;
	}

	if (psDroid->droidType == DROID_COMMAND && psDroid->psGroup && psDroid->psGroup->psCommander == psDroid)
	{
		// if a commander has EW units attached it is electronic
		for (const Droid *psCurr = psDroid->psGroup->psList; psCurr; psCurr = psCurr->psGrpNext)
		{
			if (psDroid != psCurr && electronicDroid(psCurr))
			{
				return true;
			}
		}
	}

	return false;
}

/*checks to see if the droid is currently being repaired by another*/
bool droidUnderRepair(const Droid *psDroid)
{
	CHECK_DROID(psDroid);

	//droid must be damaged
	if (droidIsDamaged(psDroid))
	{
		//look thru the list of players droids to see if any are repairing this droid
		for (const Droid *psCurr = allDroidLists[psDroid->owningPlayer]; psCurr != nullptr; psCurr = psCurr->psNext)
		{
			if ((psCurr->droidType == DROID_REPAIR || psCurr->droidType ==
				 DROID_CYBORG_REPAIR) && psCurr->action ==
				DACTION_DROIDREPAIR && psCurr->order.psObj == psDroid)
			{
				return true;
			}
		}
	}
	return false;
}

//count how many Command Droids exist in the world at any one moment
UBYTE checkCommandExist(UBYTE player)
{
	UBYTE	quantity = 0;

	for (Droid *psDroid = allDroidLists[player]; psDroid != nullptr; psDroid = psDroid->psNext)
	{
		if (psDroid->droidType == DROID_COMMAND)
		{
			quantity++;
		}
	}
	return quantity;
}

static inline bool isTransporter(DROID_TYPE type)
{
	return type == DROID_TRANSPORTER || type == DROID_SUPERTRANSPORTER;
}

bool isTransporter(Droid const *psDroid)
{
	return isTransporter(psDroid->droidType);
}

bool isTransporter(DroidStats const *psTemplate)
{
	return isTransporter(psTemplate->droidType);
}

//access functions for vtols
bool isVtolDroid(const Droid *psDroid)
{
	return asPropulsionStats[psDroid->asBits[COMP_PROPULSION]].propulsionType == PROPULSION_TYPE_LIFT
		   && !isTransporter(psDroid);
}

/* returns true if the droid has lift propulsion and is moving */
bool isFlying(const Droid *psDroid)
{
	return (asPropulsionStats + psDroid->asBits[COMP_PROPULSION])->propulsionType == PROPULSION_TYPE_LIFT
		   && (psDroid->sMove.Status != MOVEINACTIVE || isTransporter(psDroid));
}

/* returns true if it's a VTOL weapon droid which has completed all runs */
bool vtolEmpty(const Droid *psDroid)
{
	CHECK_DROID(psDroid);

	if (!isVtolDroid(psDroid))
	{
		return false;
	}
	if (psDroid->droidType != DROID_WEAPON)
	{
		return false;
	}

	for (int i = 0; i < psDroid->numWeapons; i++)
	{
		if (asWeaponStats[psDroid->m_weaponList[i].nStat].vtolAttackRuns > 0 &&
			psDroid->m_weaponList[i].usedAmmo < getNumAttackRuns(psDroid, i))
		{
			return false;
		}
	}

	return true;
}

/* returns true if it's a VTOL weapon droid which still has full ammo */
bool vtolFull(const Droid *psDroid)
{
	CHECK_DROID(psDroid);

	if (!isVtolDroid(psDroid))
	{
		return false;
	}
	if (psDroid->droidType != DROID_WEAPON)
	{
		return false;
	}

	for (int i = 0; i < psDroid->numWeapons; i++)
	{
		if (asWeaponStats[psDroid->m_weaponList[i].nStat].vtolAttackRuns > 0 &&
			psDroid->m_weaponList[i].usedAmmo > 0)
		{
			return false;
		}
	}

	return true;
}

// true if a vtol is waiting to be rearmed by a particular rearm pad
bool vtolReadyToRearm(Droid *psDroid, Structure *psStruct)
{
	CHECK_DROID(psDroid);

	if (!isVtolDroid(psDroid) || psDroid->action != DACTION_WAITFORREARM)
	{
		return false;
	}

	// If a unit has been ordered to rearm make sure it goes to the correct base
        Structure *psRearmPad = castStructure(orderStateObj(psDroid, DORDER_REARM));
	if (psRearmPad && psRearmPad != psStruct && !vtolOnRearmPad(psRearmPad, psDroid))
	{
		// target rearm pad is clear - let it go there
		objTrace(psDroid->id, "rearm pad at %d,%d won't snatch us - we already have another available at %d,%d", psStruct->position.x / TILE_UNITS, psStruct->position.y / TILE_UNITS, psRearmPad->position.x / TILE_UNITS, psRearmPad->position.y / TILE_UNITS);
		return false;
	}

	if (vtolHappy(psDroid) && vtolOnRearmPad(psStruct, psDroid))
	{
		// there is a vtol on the pad and this vtol is already rearmed
		// don't bother shifting the other vtol off
		objTrace(psDroid->id, "rearm pad at %d,%d won't snatch us - we're rearmed and pad is busy", psStruct->position.x / TILE_UNITS, psStruct->position.y / TILE_UNITS);
		return false;
	}

        Structure *psTarget = castStructure(psDroid->psActionTarget[0]);
	if (psTarget && psTarget->pFunctionality && psTarget->pFunctionality->rearmPad.psObj == psDroid)
	{
		// vtol is rearming at a different base, leave it alone
		objTrace(psDroid->id, "rearm pad at %d,%d won't snatch us - we already are snatched by %d,%d", psStruct->position.x / TILE_UNITS, psStruct->position.y / TILE_UNITS, psTarget->position.x / TILE_UNITS, psTarget->position.y / TILE_UNITS);
		return false;
	}

	return true;
}

//assign rearmPad to the VTOL
void assignVTOLPad(Droid *psNewDroid, Structure *psReArmPad)
{
	ASSERT_OR_RETURN(, isVtolDroid(psNewDroid), "%s is not a VTOL droid", objInfo(psNewDroid));
	ASSERT_OR_RETURN(,  psReArmPad->type == OBJ_STRUCTURE && psReArmPad->stats->type == REF_REARM_PAD,
					 "%s cannot rearm", objInfo(psReArmPad));

	setDroidBase(psNewDroid, psReArmPad);
}

/*compares the droid sensor type with the droid weapon type to see if the
FIRE_SUPPORT order can be assigned*/
bool droidSensorDroidWeapon(const GameObject *psObj, const Droid *psDroid)
{
	const SENSOR_STATS	*psStats = nullptr;
	int compIndex;

	CHECK_DROID(psDroid);

	if (!psObj || !psDroid)
	{
		return false;
	}

	//first check if the object is a droid or a structure
	if ((psObj->type != OBJ_DROID) &&
		(psObj->type != OBJ_STRUCTURE))
	{
		return false;
	}
	//check same player
	if (psObj->owningPlayer != psDroid->owningPlayer)
	{
		return false;
	}
	//check obj is a sensor droid/structure
	switch (psObj->type)
	{
	case OBJ_DROID:
		if (((const Droid *)psObj)->droidType != DROID_SENSOR &&
			((const Droid *)psObj)->droidType != DROID_COMMAND)
		{
			return false;
		}
		compIndex = ((const Droid *)psObj)->asBits[COMP_SENSOR];
		ASSERT_OR_RETURN(false, compIndex < numSensorStats, "Invalid range referenced for numSensorStats, %d > %d", compIndex, numSensorStats);
		psStats = asSensorStats + compIndex;
		break;
	case OBJ_STRUCTURE:
		psStats = ((const Structure *)psObj)->stats->pSensor;
		if ((psStats == nullptr) ||
			(psStats->location != LOC_TURRET))
		{
			return false;
		}
		break;
	default:
		break;
	}

	//check droid is a weapon droid - or Cyborg!!
	if (!(psDroid->droidType == DROID_WEAPON || psDroid->droidType ==
		  DROID_CYBORG || psDroid->droidType == DROID_CYBORG_SUPER))
	{
		return false;
	}

	//finally check the right droid/sensor combination
	// check vtol droid with commander
	if ((isVtolDroid(psDroid) || !proj_Direct(asWeaponStats + psDroid->m_weaponList[0].nStat)) &&
		psObj->type == OBJ_DROID && ((const Droid *)psObj)->droidType == DROID_COMMAND)
	{
		return true;
	}

	//check vtol droid with vtol sensor
	if (isVtolDroid(psDroid) && psDroid->m_weaponList[0].nStat > 0)
	{
		if (psStats->type == VTOL_INTERCEPT_SENSOR || psStats->type == VTOL_CB_SENSOR || psStats->type == SUPER_SENSOR /*|| psStats->type == RADAR_DETECTOR_SENSOR*/)
		{
			return true;
		}
		return false;
	}

	// Check indirect weapon droid with standard/CB/radar detector sensor
	if (!proj_Direct(asWeaponStats + psDroid->m_weaponList[0].nStat))
	{
		if (psStats->type == STANDARD_SENSOR ||	psStats->type == INDIRECT_CB_SENSOR || psStats->type == SUPER_SENSOR /*|| psStats->type == RADAR_DETECTOR_SENSOR*/)
		{
			return true;
		}
		return false;
	}
	return false;
}

// ////////////////////////////////////////////////////////////////////////////
// Give a droid from one player to another - used in Electronic Warfare and multiplayer.
// Got to destroy the droid and build another since there are too many complications otherwise.
// Returns the droid created.
Droid *giftSingleDroid(Droid *psD, UDWORD to, bool electronic)
{
	CHECK_DROID(psD);
	ASSERT_OR_RETURN(nullptr, !isDead(psD), "Cannot gift dead unit");
	ASSERT_OR_RETURN(psD, psD->owningPlayer != to, "Cannot gift to self");
	ASSERT_OR_RETURN(nullptr, to < MAX_PLAYERS, "Cannot gift to = %" PRIu32 "", to);

	// Check unit limits (multiplayer only)
	syncDebug("Limits: %u/%d %u/%d %u/%d", getNumDroids(to), getMaxDroids(to), getNumConstructorDroids(to), getMaxConstructors(to), getNumCommandDroids(to), getMaxCommanders(to));
	if (bMultiPlayer
		&& ((int)getNumDroids(to) >= getMaxDroids(to)
			|| ((psD->droidType == DROID_CYBORG_CONSTRUCT || psD->droidType == DROID_CONSTRUCT)
				&& (int)getNumConstructorDroids(to) >= getMaxConstructors(to))
			|| (psD->droidType == DROID_COMMAND && (int)getNumCommandDroids(to) >= getMaxCommanders(to))))
	{
		if (to == selectedPlayer || psD->owningPlayer == selectedPlayer)
		{
			CONPRINTF("%s", _("Unit transfer failed -- unit limits exceeded"));
		}
		return nullptr;
	}

	// electronic or campaign will destroy and recreate the droid.
	if (electronic || !bMultiPlayer)
	{
          DroidStats sTemplate;
                Droid *psNewDroid;

		templateSetParts(psD, &sTemplate);	// create a template based on the droid
		sTemplate.name = WzString::fromUtf8(psD->name);	// copy the name across
		// update score
		if (psD->owningPlayer == selectedPlayer && to != selectedPlayer && !bMultiPlayer)
		{
			scoreUpdateVar(WD_UNITS_LOST);
		}
		// make the old droid vanish (but is not deleted until next tick)
		adjustDroidCount(psD, -1);
		vanishDroid(psD);
		// create a new droid
		psNewDroid = reallyBuildDroid(&sTemplate, Position(psD->position.x, psD->position.y, 0), to, false, psD->rotation);
		ASSERT_OR_RETURN(nullptr, psNewDroid, "Unable to build unit");

		addDroid(psNewDroid, allDroidLists);
		adjustDroidCount(psNewDroid, 1);

		psNewDroid->hitPoints = clip((psD->hitPoints *psNewDroid->originalBody + psD->originalBody/2)/std::max(psD->originalBody, 1u), 1u, psNewDroid->originalBody);
		psNewDroid->experience = psD->experience;
		psNewDroid->kills = psD->kills;

		if (!(psNewDroid->droidType == DROID_PERSON || cyborgDroid(psNewDroid) || isTransporter(psNewDroid)))
		{
			updateDroidOrientation(psNewDroid);
		}

		triggerEventObjectTransfer(psNewDroid, psD->owningPlayer);
		return psNewDroid;
	}

	int oldPlayer = psD->owningPlayer;

	// reset the assigned state of units attached to a leader
	for (Droid *psCurr = allDroidLists[oldPlayer]; psCurr != nullptr; psCurr = psCurr->psNext)
	{
          GameObject *psLeader;

		if (hasCommander(psCurr))
		{
			psLeader = (GameObject *)psCurr->psGroup->psCommander;
		}
		else
		{
			//psLeader can be either a droid or a structure
			psLeader = orderStateObj(psCurr, DORDER_FIRESUPPORT);
		}

		if (psLeader && psLeader->id == psD->id)
		{
			psCurr->selected = false;
			orderDroid(psCurr, DORDER_STOP, ModeQueue);
		}
	}

	visRemoveVisibility((GameObject *)psD);
	psD->selected = false;

	adjustDroidCount(psD, -1);
	scriptRemoveObject(psD); //Remove droid from any script groups

	if (droidRemove(psD, allDroidLists))
	{
		psD->owningPlayer = to;

		addDroid(psD, allDroidLists);
		adjustDroidCount(psD, 1);

		// the new player may have different default sensor/ecm/repair components
		if ((asSensorStats + psD->asBits[COMP_SENSOR])->location == LOC_DEFAULT)
		{
			if (psD->asBits[COMP_SENSOR] != aDefaultSensor[psD->owningPlayer])
			{
				psD->asBits[COMP_SENSOR] = aDefaultSensor[psD->owningPlayer];
			}
		}
		if ((asECMStats + psD->asBits[COMP_ECM])->location == LOC_DEFAULT)
		{
			if (psD->asBits[COMP_ECM] != aDefaultECM[psD->owningPlayer])
			{
				psD->asBits[COMP_ECM] = aDefaultECM[psD->owningPlayer];
			}
		}
		if ((asRepairStats + psD->asBits[COMP_REPAIRUNIT])->location == LOC_DEFAULT)
		{
			if (psD->asBits[COMP_REPAIRUNIT] != aDefaultRepair[psD->owningPlayer])
			{
				psD->asBits[COMP_REPAIRUNIT] = aDefaultRepair[psD->owningPlayer];
			}
		}
	}
	else
	{
		// if we couldn't remove it, then get rid of it.
		return nullptr;
	}

	// Update visibility
	visTilesUpdate((GameObject *)psD);

	// check through the players, and our allies, list of droids to see if any are targetting it
	for (unsigned int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!aiCheckAlliances(i, to))
		{
			continue;
		}

		for (Droid *psCurr = allDroidLists[i]; psCurr != nullptr; psCurr = psCurr->psNext)
		{
			if (psCurr->order.psObj == psD || psCurr->psActionTarget[0] == psD)
			{
				orderDroid(psCurr, DORDER_STOP, ModeQueue);
				break;
			}
			for (unsigned iWeap = 0; iWeap < psCurr->numWeapons; ++iWeap)
			{
				if (psCurr->psActionTarget[iWeap] == psD)
				{
					orderDroid(psCurr, DORDER_STOP, ModeImmediate);
					break;
				}
			}
			// check through order list
			orderClearTargetFromDroidList(psCurr, (GameObject *)psD);
		}
	}

	for (unsigned int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!aiCheckAlliances(i, to))
		{
			continue;
		}

		// check through the players list, and our allies, of structures to see if any are targetting it
		for (Structure *psStruct = apsStructLists[i]; psStruct != nullptr; psStruct = psStruct->psNext)
		{
			if (psStruct->psTarget[0] == psD)
			{
				setStructureTarget(psStruct, nullptr, 0, ORIGIN_UNKNOWN);
			}
		}
	}

	triggerEventObjectTransfer(psD, oldPlayer);
	return psD;
}

/*this is called to check the weapon is 'allowed'. Check if VTOL, the weapon is
direct fire. Also check numVTOLattackRuns for the weapon is not zero - return
true if valid weapon*/
/* this will be buggy if the droid being checked has both AA weapon and non-AA weapon
Cannot think of a solution without adding additional return value atm.
*/
bool checkValidWeaponForProp(DroidStats *psTemplate)
{
	PROPULSION_STATS	*psPropStats;

	//check propulsion stat for vtol
	psPropStats = asPropulsionStats + psTemplate->asParts[COMP_PROPULSION];

	ASSERT_OR_RETURN(false, psPropStats != nullptr, "invalid propulsion stats pointer");

	// if there are no weapons, then don't even bother continuing
	if (psTemplate->numWeaps == 0)
	{
		return false;
	}

	if (asPropulsionTypes[psPropStats->propulsionType].travel == AIR)
	{
		//check weapon stat for indirect
		if (!proj_Direct(asWeaponStats + psTemplate->asWeaps[0])
			|| !asWeaponStats[psTemplate->asWeaps[0]].vtolAttackRuns)
		{
			return false;
		}
	}
	else
	{
		// VTOL weapons do not go on non-AIR units.
		if (asWeaponStats[psTemplate->asWeaps[0]].vtolAttackRuns)
		{
			return false;
		}
	}

	//also checks that there is no other system component
	if (psTemplate->asParts[COMP_BRAIN] != 0
		&& asWeaponStats[psTemplate->asWeaps[0]].weaponSubClass != WSC_COMMAND)
	{
		assert(false);
		return false;
	}

	return true;
}

/** Callback function for stopped audio tracks
 *  Sets the droid's current track id to NO_SOUND
 *  \return true on success, false on failure
 */
bool droidAudioTrackStopped(void *psObj)
{
  Droid *psDroid;

	psDroid = (Droid *)psObj;
	if (psDroid == nullptr)
	{
		debug(LOG_ERROR, "droid pointer invalid");
		return false;
	}

	if (psDroid->type != OBJ_DROID || psDroid->deathTime)
	{
		return false;
	}

	psDroid->iAudioID = NO_SOUND;
	return true;
}



bool isConstructionDroid(GameObject const *psObject)
{
	return isDroid(psObject) && isConstructionDroid(castDroid(psObject));
}

/** Check validity of a droid. Crash hard if it fails. */
void checkDroid(const Droid *droid, const char *const location, const char *function, const int recurse)
{
	if (recurse < 0)
	{
		return;
	}

	ASSERT_HELPER(droid != nullptr, location, function, "CHECK_DROID: NULL pointer");
	ASSERT_HELPER(droid->type == OBJ_DROID, location, function, "CHECK_DROID: Not droid (type %d)", (int)droid->type);
	ASSERT_HELPER(droid->numWeapons <= MAX_WEAPONS, location, function, "CHECK_DROID: Bad number of droid weapons %d", (int)droid->numWeapons);
	ASSERT_HELPER((unsigned)droid->listSize <= droid->asOrderList.size() && (unsigned)droid->listPendingBegin <= droid->asOrderList.size(), location, function, "CHECK_DROID: Bad number of droid orders %d %d %d", (int)droid->listSize, (int)droid->listPendingBegin, (int)droid->asOrderList.size());
	ASSERT_HELPER(droid->owningPlayer < MAX_PLAYERS, location, function, "CHECK_DROID: Bad droid owner %d", (int)droid->owningPlayer);
	ASSERT_HELPER(droidOnMap(droid), location, function, "CHECK_DROID: Droid off map");
	ASSERT_HELPER(droid->hitPoints <= droid->originalBody, location, function, "CHECK_DROID: More body points (%u) than original body points (%u).", (unsigned)droid->hitPoints, (unsigned)droid->originalBody);

	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		ASSERT_HELPER(droid->m_weaponList[i].lastFired <= gameTime, location, function, "CHECK_DROID: Bad last fired time for turret %u", i);
	}
}

int droidSqDist(Droid *psDroid, GameObject *psObj)
{
	PROPULSION_STATS *psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION];

	if (!fpathCheck(psDroid->position, psObj->position, psPropStats->propulsionType))
	{
		return -1;
	}
	return objPosDiffSq(psDroid->position, psObj->position);
}