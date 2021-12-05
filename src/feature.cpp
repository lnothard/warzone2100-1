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
/*
 * Feature.c
 *
 * Load feature stats
 */
#include "lib/framework/frame.h"

#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "lib/netplay/netplay.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "feature.h"
#include "map.h"
#include "hci.h"
#include "power.h"
#include "objects.h"
#include "display.h"
#include "order.h"
#include "structure.h"
#include "miscimd.h"
#include "visibility.h"
#include "effects.h"
#include "scores.h"
#include "combat.h"
#include "multiplay.h"
#include "qtscript.h"

#include "mapgrid.h"
#include "display3d.h"
#include "random.h"

/* The statistics for the features */
FeatureStats *asFeatureStats;
UDWORD			numFeatureStats;

//Value is stored for easy access to this feature in destroyDroid()/destroyStruct()
FeatureStats *oilResFeature = nullptr;

void featureInitVars()
{
	asFeatureStats = nullptr;
	numFeatureStats = 0;
	oilResFeature = nullptr;
}

/* Load the feature stats */
bool loadFeatureStats(WzConfig &ini)
{
	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();
	asFeatureStats = new FeatureStats[list.size()];
	numFeatureStats = list.size();
	for (int i = 0; i < list.size(); ++i)
	{
		ini.beginGroup(list[i]);
		asFeatureStats[i] = FeatureStats(STAT_FEATURE + i);
                FeatureStats *p = &asFeatureStats[i];
		p->name = ini.string(WzString::fromUtf8("name"));
		p->textId = list[i];
		WzString subType = ini.value("type").toWzString();
		if (subType == "TANK WRECK")
		{
			p->subType = FEAT_TANK;
		}
		else if (subType == "GENERIC ARTEFACT")
		{
			p->subType = FEAT_GEN_ARTE;
		}
		else if (subType == "OIL RESOURCE")
		{
			p->subType = FEAT_OIL_RESOURCE;
		}
		else if (subType == "BOULDER")
		{
			p->subType = FEAT_BOULDER;
		}
		else if (subType == "VEHICLE")
		{
			p->subType = FEAT_VEHICLE;
		}
		else if (subType == "BUILDING")
		{
			p->subType = FEAT_BUILDING;
		}
		else if (subType == "OIL DRUM")
		{
			p->subType = FEAT_OIL_DRUM;
		}
		else if (subType == "TREE")
		{
			p->subType = FEAT_TREE;
		}
		else if (subType == "SKYSCRAPER")
		{
			p->subType = FEAT_SKYSCRAPER;
		}
		else
		{
			ASSERT(false, "Unknown feature type: %s", subType.toUtf8().c_str());
		}
		p->psImd = modelGet(ini.value("model").toWzString());
		p->baseWidth = ini.value("width", 1).toInt();
		p->baseBreadth = ini.value("breadth", 1).toInt();
		p->tileDraw = ini.value("tileDraw", 1).toInt();
		p->allowLOS = ini.value("lineOfSight", 1).toInt();
		p->visibleAtStart = ini.value("startVisible", 1).toInt();
		p->damageable = ini.value("damageable", 1).toInt();
		p->body = ini.value("hitpoints", 1).toInt();
		p->armourValue = ini.value("armour", 1).toInt();

		//and the oil resource - assumes only one!
		if (asFeatureStats[i].subType == FEAT_OIL_RESOURCE)
		{
			oilResFeature = &asFeatureStats[i];
		}
		ini.endGroup();
	}

	return true;
}

/* Release the feature stats memory */
void featureStatsShutDown()
{
	delete[] asFeatureStats;
	asFeatureStats = nullptr;
	numFeatureStats = 0;
}

/** Deals with damage to a feature
 *  \param psFeature feature to deal damage to
 *  \param damage amount of damage to deal
 *  \param weaponClass,weaponSubClass the class and subclass of the weapon that deals the damage
 *  \return < 0 never, >= 0 always
 */
int32_t featureDamage(Feature *psFeature, unsigned damage, WEAPON_CLASS weaponClass, WEAPON_SUBCLASS weaponSubClass, unsigned impactTime, bool isDamagePerSecond, int minDamage)
{
	int32_t relativeDamage;

	ASSERT_OR_RETURN(0, psFeature != nullptr, "Invalid feature pointer");

	debug(LOG_ATTACK, "feature (id %d): body %d armour %d damage: %d",
	      psFeature->id, psFeature->hitPoints, psFeature->psStats->armourValue, damage);

	relativeDamage = objDamage(psFeature, damage, psFeature->psStats->body, weaponClass, weaponSubClass, isDamagePerSecond, minDamage);

	// If the shell did sufficient damage to destroy the feature
	if (relativeDamage < 0)
	{
		debug(LOG_ATTACK, "feature (id %d) DESTROYED", psFeature->id);
		destroyFeature(psFeature, impactTime);
		return relativeDamage * -1;
	}
	else
	{
		return relativeDamage;
	}
}


/* Create a feature on the map */
Feature *buildFeature(FeatureStats *psStats, UDWORD x, UDWORD y, bool FromSave)
{
	//try and create the Feature
        auto *psFeature = new Feature(generateSynchronisedObjectId(), psStats);

	if (psFeature == nullptr)
	{
		debug(LOG_WARNING, "Feature couldn't be built.");
		return nullptr;
	}
	//add the feature to the list - this enables it to be drawn whilst being built
	addFeature(psFeature);

	// snap the coords to a tile
	if (!FromSave)
	{
		x = (x & ~TILE_MASK) + psStats->baseWidth  % 2 * TILE_UNITS / 2;
		y = (y & ~TILE_MASK) + psStats->baseBreadth % 2 * TILE_UNITS / 2;
	}
	else
	{
		if ((x & TILE_MASK) != psStats->baseWidth  % 2 * TILE_UNITS / 2 ||
		    (y & TILE_MASK) != psStats->baseBreadth % 2 * TILE_UNITS / 2)
		{
			debug(LOG_WARNING, "Feature not aligned. position (%d,%d), size (%d,%d)", x, y, psStats->baseWidth, psStats->baseBreadth);
		}
	}

	psFeature->position.x = x;
	psFeature->position.y = y;

	StructureBounds b = getStructureBounds(psFeature);

	// get the terrain average height
	int foundationMin = INT32_MAX;
	int foundationMax = INT32_MIN;
	for (int breadth = 0; breadth <= b.size.y; ++breadth)
	{
		for (int width = 0; width <= b.size.x; ++width)
		{
			int h = map_TileHeight(b.map.x + width, b.map.y + breadth);
			foundationMin = std::min(foundationMin, h);
			foundationMax = std::max(foundationMax, h);
		}
	}
	//return the average of max/min height
	int height = (foundationMin + foundationMax) / 2;

	if (psStats->subType == FEAT_TREE)
	{
		psFeature->rotation.direction = gameRand(DEG_360);
	}
	else
	{
		psFeature->rotation.direction = 0;
	}
	psFeature->hitPoints = psStats->body;
	psFeature->periodicalDamageStart = 0;
	psFeature->periodicalDamage = 0;

	// it has never been drawn
	psFeature->displayData.frameNumber = 0;

	memset(psFeature->seenThisTick, 0, sizeof(psFeature->seenThisTick));
	memset(psFeature->visible, 0, sizeof(psFeature->visible));

	// set up the imd for the feature
	psFeature->displayData.imd = psStats->psImd;

	ASSERT_OR_RETURN(nullptr, psFeature->displayData.imd, "No IMD for feature");		// make sure we have an imd.

	for (int breadth = 0; breadth < b.size.y; ++breadth)
	{
		for (int width = 0; width < b.size.x; ++width)
		{
			MAPTILE *psTile = mapTile(b.map.x + width, b.map.y + breadth);

			//check not outside of map - for load save game
			ASSERT_OR_RETURN(nullptr, b.map.x + width < mapWidth, "x coord bigger than map width - %s, id = %d", getStatsName(psFeature->psStats), psFeature->id);
			ASSERT_OR_RETURN(nullptr, b.map.y + breadth < mapHeight, "y coord bigger than map height - %s, id = %d", getStatsName(psFeature->psStats), psFeature->id);

			if (width != psStats->baseWidth && breadth != psStats->baseBreadth)
			{
				if (TileHasFeature(psTile))
				{
                                  Feature *psBlock = (Feature *)psTile->psObject;

					debug(LOG_ERROR, "%s(%d) already placed at (%d+%d, %d+%d) when trying to place %s(%d) at (%d+%d, %d+%d) - removing it",
					      getStatsName(psBlock->psStats), psBlock->id, map_coord(psBlock->position.x), psBlock->psStats->baseWidth, map_coord(psBlock->position.y),
					      psBlock->psStats->baseBreadth, getStatsName(psFeature->psStats), psFeature->id, b.map.x, b.size.x, b.map.y, b.size.y);

					removeFeature(psBlock);
				}

				psTile->psObject = (GameObject *)psFeature;

				// if it's a tall feature then flag it in the map.
				if (psFeature->displayData.imd->max.y > TALLOBJECT_YMAX)
				{
					auxSetBlocking(b.map.x + width, b.map.y + breadth, AIR_BLOCKED);
				}

				if (psStats->subType != FEAT_GEN_ARTE && psStats->subType != FEAT_OIL_DRUM)
				{
					auxSetBlocking(b.map.x + width, b.map.y + breadth, FEATURE_BLOCKED);
				}
			}

			if ((!psStats->tileDraw) && (FromSave == false))
			{
				psTile->height = height;
			}
		}
	}
	psFeature->position.z = map_TileHeight(b.map.x, b.map.y);//jps 18july97

	return psFeature;
}

Feature::Feature(uint32_t id, FeatureStats const *psStats)
	: GameObject(OBJ_FEATURE, id, PLAYER_FEATURE)  // Set the default player out of range to avoid targeting confusions
	, psStats(psStats)
{}

/* Release the resources associated with a feature */
Feature::~Feature()
{
	// Make sure to get rid of some final references in the sound code to this object first
	audio_RemoveObj(this);
}

void _syncDebugFeature(const char *function, Feature const *psFeature, char ch)
{
	if (psFeature->type != OBJ_FEATURE) {
		ASSERT(false, "%c Broken psFeature->type %u!", ch, psFeature->type);
		syncDebug("Broken psFeature->type %u!", psFeature->type);
	}
	int list[] =
	{
		ch,

		(int)psFeature->id,

		psFeature->owningPlayer,
		psFeature->position.x, psFeature->position.y, psFeature->position.z,
		(int)psFeature->psStats->subType,
		psFeature->psStats->damageable,
		(int)psFeature->hitPoints,
	};
	_syncDebugIntList(function, "%c feature%d = p%d;pos(%d,%d,%d),subtype%d,damageable%d,body%d", list, ARRAY_SIZE(list));
}

/* Update routine for features */
void featureUpdate(Feature *psFeat)
{
	syncDebugFeature(psFeat, '<');

	/* Update the periodical damage data */
	if (psFeat->periodicalDamageStart != 0 && psFeat->periodicalDamageStart != gameTime - deltaGameTime)  // -deltaGameTime, since projectiles are updated after features.
	{
		// The periodicalDamageStart has been set, but is not from the previous tick, so we must be out of the periodical damage.
		psFeat->periodicalDamage = 0;  // Reset periodical damage done this tick.
		// Finished periodical damaging
		psFeat->periodicalDamageStart = 0;
	}

	syncDebugFeature(psFeat, '>');
}


// free up a feature with no visual effects
bool removeFeature(Feature *psDel)
{
	MESSAGE		*psMessage;
	Vector3i	pos;

	ASSERT_OR_RETURN(false, psDel != nullptr, "Invalid feature pointer");
	ASSERT_OR_RETURN(false, !psDel->deathTime, "Feature already dead");

	//remove from the map data
	StructureBounds b = getStructureBounds(psDel);
	for (int breadth = 0; breadth < b.size.y; ++breadth)
	{
		for (int width = 0; width < b.size.x; ++width)
		{
			if (tileOnMap(b.map.x + width, b.map.y + breadth))
			{
				MAPTILE *psTile = mapTile(b.map.x + width, b.map.y + breadth);

				if (psTile->psObject == psDel)
				{
					psTile->psObject = nullptr;
					auxClearBlocking(b.map.x + width, b.map.y + breadth, FEATURE_BLOCKED | AIR_BLOCKED);
				}
			}
		}
	}

	if (psDel->psStats->subType == FEAT_GEN_ARTE || psDel->psStats->subType == FEAT_OIL_DRUM)
	{
		pos.x = psDel->position.x;
		pos.z = psDel->position.y;
		pos.y = map_Height(pos.x, pos.z) + 30;
		addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_DISCOVERY, false, nullptr, 0, gameTime - deltaGameTime + 1);
		if (psDel->psStats->subType == FEAT_GEN_ARTE)
		{
			scoreUpdateVar(WD_ARTEFACTS_FOUND);
			intRefreshScreen();
		}
	}

	bool removedAMessage = false;
	if (psDel->psStats->subType == FEAT_GEN_ARTE || psDel->psStats->subType == FEAT_OIL_RESOURCE)
	{
		for (unsigned player = 0; player < MAX_PLAYERS; ++player)
		{
			psMessage = findMessage(psDel, MSG_PROXIMITY, player);
			while (psMessage)
			{
				removeMessage(psMessage, player);
				removedAMessage = true;
				psMessage = findMessage(psDel, MSG_PROXIMITY, player);
			}
		}
	}
	if (removedAMessage)
	{
		jsDebugMessageUpdate();
	}

	debug(LOG_DEATH, "Killing off feature %s id %d (%p)", objInfo(psDel), psDel->id, static_cast<void *>(psDel));
	killFeature(psDel);

	return true;
}


SDWORD getFeatureStatFromName(const WzString &name)
{
  FeatureStats *psStat;

  for (unsigned inc = 0; inc < numFeatureStats; inc++)
  {
    psStat = &asFeatureStats[inc];
    if (psStat->textId.compare(name) == 0)
    {
            return inc;
    }
  }
  return -1;
}

StructureBounds getStructureBounds(Feature const *object)
{
	return getStructureBounds(object->psStats, object->position.xy());
}

StructureBounds getStructureBounds(FeatureStats const *stats, Vector2i pos)
{
	const Vector2i size = stats->size();
	const Vector2i map = map_coord(pos) - size / 2;
	return StructureBounds(map, size);
}