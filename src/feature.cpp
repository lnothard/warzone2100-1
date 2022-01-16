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
 * @file feature.cpp
 * Load feature stats
 */

#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "combat.h"
#include "display3d.h"
#include "displaydef.h"
#include "effects.h"
#include "feature.h"
#include "map.h"
#include "qtscript.h"
#include "scores.h"

/* The statistics for the features */
FeatureStats* asFeatureStats;
unsigned numFeatureStats;

//Value is stored for easy access to this feature in destroyDroid()/destroyStruct()
FeatureStats* oilResFeature = nullptr;

FeatureStats::FeatureStats(int idx)
  : BaseStats(idx)
{
}

Vector2i FeatureStats::size() const
{
  return {baseWidth, baseBreadth};
}

Vector2i Feature::size() const
{
  return psStats->size();
}

void featureInitVars()
{
	asFeatureStats = nullptr;
	numFeatureStats = 0;
	oilResFeature = nullptr;
}

/* Load the feature stats */
bool loadFeatureStats(WzConfig& ini)
{
	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();
	asFeatureStats = new FeatureStats[list.size()];
	numFeatureStats = list.size();
	for (int i = 0; i < list.size(); ++i)
	{
		ini.beginGroup(list[i]);
		asFeatureStats[i] = FeatureStats(STAT_FEATURE + i);
		FeatureStats* p = &asFeatureStats[i];
		p->name = ini.string(WzString::fromUtf8("name"));
		p->id = list[i];
		WzString subType = ini.value("type").toWzString();
		if (subType == "TANK WRECK")
		{
			p->subType = FEATURE_TYPE::TANK;
		}
		else if (subType == "GENERIC ARTEFACT")
		{
			p->subType = FEATURE_TYPE::GEN_ARTE;
		}
		else if (subType == "OIL RESOURCE")
		{
			p->subType = FEATURE_TYPE::OIL_RESOURCE;
		}
		else if (subType == "BOULDER")
		{
			p->subType = FEATURE_TYPE::BOULDER;
		}
		else if (subType == "VEHICLE")
		{
			p->subType = FEATURE_TYPE::VEHICLE;
		}
		else if (subType == "BUILDING")
		{
			p->subType = FEATURE_TYPE::BUILDING;
		}
		else if (subType == "OIL DRUM")
		{
			p->subType = FEATURE_TYPE::OIL_DRUM;
		}
		else if (subType == "TREE")
		{
			p->subType = FEATURE_TYPE::TREE;
		}
		else if (subType == "SKYSCRAPER")
		{
			p->subType = FEATURE_TYPE::SKYSCRAPER;
		}
		else
		{
			ASSERT(false, "Unknown feature type: %s", subType.toUtf8().c_str());
		}
		p->psImd = std::make_unique<iIMDShape>(*modelGet(ini.value("model").toWzString()));
		p->baseWidth = ini.value("width", 1).toInt();
		p->baseBreadth = ini.value("breadth", 1).toInt();
		p->tileDraw = ini.value("tileDraw", 1).toInt();
		p->allowLOS = ini.value("lineOfSight", 1).toInt();
		p->visibleAtStart = ini.value("startVisible", 1).toInt();
		p->damageable = ini.value("damageable", 1).toInt();
		p->body = ini.value("hitpoints", 1).toInt();
		p->armourValue = ini.value("armour", 1).toInt();

		//and the oil resource - assumes only one!
		if (asFeatureStats[i].subType == FEATURE_TYPE::OIL_RESOURCE) {
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

/**
 * Deals with damage to a feature
 * \param psFeature feature to deal damage to
 * \param damage amount of damage to deal
 * \param weaponClass,weaponSubClass the class and subclass of the weapon that deals the damage
 * \return < 0 never, >= 0 always
 */
int featureDamage(Feature* psFeature, unsigned damage, WEAPON_CLASS weaponClass, WEAPON_SUBCLASS weaponSubClass,
                      unsigned impactTime, bool isDamagePerSecond, int minDamage)
{
	ASSERT_OR_RETURN(0, psFeature != nullptr, "Invalid feature pointer");

	debug(LOG_ATTACK, "feature (id %d): body %d armour %d damage: %d",
	      psFeature->getId(), psFeature->getHp(), psFeature->getStats()->armourValue, damage);

	auto relativeDamage = objDamage(psFeature, damage,
                             psFeature->getStats()->body,
                              weaponClass, weaponSubClass,
	                            isDamagePerSecond, minDamage);

	// If the shell did sufficient damage to destroy the feature
	if (relativeDamage < 0) {
		debug(LOG_ATTACK, "feature (id %d) DESTROYED", psFeature->getId());
		destroyFeature(psFeature, impactTime);
		return relativeDamage * -1;
	}
	else {
		return relativeDamage;
	}
}

/* Create a feature on the map */
std::unique_ptr<Feature> buildFeature(FeatureStats const* stats, unsigned x, unsigned y, bool fromSave)
{
	// try and create the Feature
	auto psFeature = std::make_unique<Feature>(
          generateSynchronisedObjectId(), stats);

	if (psFeature == nullptr) {
		debug(LOG_WARNING, "Feature couldn't be built.");
		return nullptr;
	}

	//add the feature to the list - this enables it to be drawn whilst being built
	addFeature(psFeature.get());

	// snap the coords to a tile
	if (!fromSave) {
		x = (x & ~TILE_MASK) + stats->baseWidth % 2 * TILE_UNITS / 2;
		y = (y & ~TILE_MASK) + stats->baseBreadth % 2 * TILE_UNITS / 2;
	} else {
		if ((x & TILE_MASK) != stats->baseWidth % 2 * TILE_UNITS / 2 ||
        (y & TILE_MASK) != stats->baseBreadth % 2 * TILE_UNITS / 2)
		{
			debug(LOG_WARNING, "Feature not aligned. position (%d,%d), size (%d,%d)", x, y, stats->baseWidth,
            stats->baseBreadth);
		}
	}

	psFeature->setPosition({x, y, psFeature->getPosition().z});

	auto b = getStructureBounds(psFeature.get());

	// get the terrain average height
	auto foundationMin = INT32_MAX;
	auto foundationMax = INT32_MIN;
	for (int breadth = 0; breadth <= b.size.y; ++breadth)
	{
		for (int width = 0; width <= b.size.x; ++width)
		{
			auto h = map_TileHeight(b.map.x + width, b.map.y + breadth);
			foundationMin = std::min(foundationMin, h);
			foundationMax = std::max(foundationMax, h);
		}
	}
	//return the average of max/min height
	auto height = (foundationMin + foundationMax) / 2;

	if (stats->subType == FEATURE_TYPE::TREE) {
		psFeature->setRotation({gameRand(DEG_360),
                                        psFeature->getRotation().pitch,
                                        psFeature->getRotation().roll});
	}
	else {
		psFeature->setRotation({0, psFeature->getRotation().pitch,
                                        psFeature->getRotation().roll});
	}
	psFeature->hitPoints = stats->body;
	psFeature->periodicalDamageStart = 0;
	psFeature->periodicalDamage = 0;

	// it has never been drawn
	psFeature->display->frame_number = 0;

	memset(psFeature->seenThisTick, 0, sizeof(psFeature->seenThisTick));
	memset(psFeature->visible, 0, sizeof(psFeature->visible));

	// set up the imd for the feature
	psFeature->display->imd_shape = std::make_unique<iIMDShape>(*stats->psImd);

	ASSERT_OR_RETURN(nullptr, psFeature->getDisplayData().imd_shape.get(), "No IMD for feature"); // make sure we have an imd.

	for (int breadth = 0; breadth < b.size.y; ++breadth)
	{
		for (int width = 0; width < b.size.x; ++width)
		{
			Tile* psTile = mapTile(b.map.x + width, b.map.y + breadth);

			//check not outside of map - for load save game
			ASSERT_OR_RETURN(nullptr, b.map.x + width < mapWidth, "x coord bigger than map width - %s, id = %d",
			                 getStatsName(psFeature->getStats()), psFeature->getId());
			ASSERT_OR_RETURN(nullptr, b.map.y + breadth < mapHeight, "y coord bigger than map height - %s, id = %d",
			                 getStatsName(psFeature->getStats()), psFeature->getId());

			if (width != stats->baseWidth && breadth != stats->baseBreadth)
			{
				if (TileHasFeature(psTile))
				{
					auto psBlock = dynamic_cast<Feature*>(psTile->psObject);

					debug(LOG_ERROR,
					      "%s(%d) already placed at (%d+%d, %d+%d) when trying to place %s(%d) at (%d+%d, %d+%d) - removing it",
					      getStatsName(psBlock->getStats()), psBlock->getId(), map_coord(psBlock->getPosition().x),
					      psBlock->getStats()->baseWidth, map_coord(psBlock->getPosition().y),
					      psBlock->getStats()->baseBreadth, getStatsName(psFeature->getStats()), psFeature->getId(), b.map.x,
					      b.size.x, b.map.y, b.size.y);

					removeFeature(psBlock);
				}

				psTile->psObject = dynamic_cast<PersistentObject*>(psFeature.get());

				// if it's a tall feature then flag it in the map.
				if (psFeature->getDisplayData().imd_shape->max.y > TALLOBJECT_YMAX)
				{
					auxSetBlocking(b.map.x + width, b.map.y + breadth, AIR_BLOCKED);
				}

				if (stats->subType != FEATURE_TYPE::GEN_ARTE && stats->subType != FEATURE_TYPE::OIL_DRUM)
				{
					auxSetBlocking(b.map.x + width, b.map.y + breadth, FEATURE_BLOCKED);
				}
			}

			if ((!stats->tileDraw) && !fromSave)
			{
				psTile->height = height;
			}
		}
	}

	psFeature->setPosition({psFeature->getPosition().x,
                               psFeature->getPosition().y,
                               map_TileHeight(b.map.x, b.map.y)});

	return psFeature;
}


Feature::Feature(unsigned id, FeatureStats const* psStats)
	: PersistentObject(id, PLAYER_FEATURE) // Set the default player out of range to avoid targeting confusions
	  , psStats(std::make_shared<FeatureStats>(psStats))
{
}

/* Release the resources associated with a feature */
Feature::~Feature()
{
	// Make sure to get rid of some final references in the sound code to this object first
	audio_RemoveObj(this);
}

void _syncDebugFeature(const char* function, Feature const* psFeature, char ch)
{
	int list[] =
	{
		ch,
		(int)psFeature->getId(),
    (int)psFeature->getPlayer(),
		psFeature->getPosition().x,
    psFeature->getPosition().y,
    psFeature->getPosition().z,
		(int)psFeature->getStats()->subType,
		psFeature->getStats()->damageable,
		(int)psFeature->body,
	};
	_syncDebugIntList(function, "%c feature%d = p%d;pos(%d,%d,%d),subtype%d,damageable%d,body%d", list,
	                  ARRAY_SIZE(list));
}

/* Update routine for features */
void featureUpdate(Feature* psFeat)
{
	syncDebugFeature(psFeat, '<');

	/* Update the periodical damage data */
	if (psFeat->periodicalDamageStart != 0 && psFeat->periodicalDamageStart != gameTime - deltaGameTime)
	// -deltaGameTime, since projectiles are updated after features.
	{
		// The periodicalDamageStart has been set, but is not from the previous tick, so we must be out of the periodical damage.
		psFeat->periodicalDamage = 0; // Reset periodical damage done this tick.
		// Finished periodical damaging
		psFeat->periodicalDamageStart = 0;
	}

	syncDebugFeature(psFeat, '>');
}


// free up a feature with no visual effects
bool removeFeature(Feature* psDel)
{
	MESSAGE* psMessage;
	Vector3i pos;

	ASSERT_OR_RETURN(false, psDel != nullptr, "Invalid feature pointer");
	ASSERT_OR_RETURN(false, !psDel->died, "Feature already dead");

	//remove from the map data
	StructureBounds b = getStructureBounds(psDel);
	for (int breadth = 0; breadth < b.size.y; ++breadth)
	{
		for (int width = 0; width < b.size.x; ++width)
		{
			if (tileOnMap(b.map.x + width, b.map.y + breadth))
			{
				Tile* psTile = mapTile(b.map.x + width, b.map.y + breadth);

				if (psTile->psObject == psDel)
				{
					psTile->psObject = nullptr;
					auxClearBlocking(b.map.x + width, b.map.y + breadth, FEATURE_BLOCKED | AIR_BLOCKED);
				}
			}
		}
	}

	if (psDel->getStats()->subType == FEATURE_TYPE::GEN_ARTE || psDel->getStats()->subType == FEATURE_TYPE::OIL_DRUM)
	{
		pos.x = psDel->getPosition().x;
		pos.z = psDel->getPosition().y;
		pos.y = map_Height(pos.x, pos.z) + 30;
		addEffect(&pos, EFFECT_GROUP::EXPLOSION, EFFECT_TYPE::EXPLOSION_TYPE_DISCOVERY, false, nullptr, 0, gameTime - deltaGameTime + 1);
		if (psDel->getStats()->subType == FEATURE_TYPE::GEN_ARTE) {
			scoreUpdateVar(WD_ARTEFACTS_FOUND);
			intRefreshScreen();
		}
	}

	bool removedAMessage = false;
	if (psDel->getStats()->subType == FEATURE_TYPE::GEN_ARTE ||
      psDel->getStats()->subType == FEATURE_TYPE::OIL_RESOURCE) {
		for (unsigned player = 0; player < MAX_PLAYERS; ++player)
		{
			psMessage = findMessage(psDel, MESSAGE_TYPE::MSG_PROXIMITY, player);
			while (psMessage)
			{
				removeMessage(psMessage, player);
				removedAMessage = true;
				psMessage = findMessage(psDel, MESSAGE_TYPE::MSG_PROXIMITY, player);
			}
		}
	}
	if (removedAMessage) {
		jsDebugMessageUpdate();
	}

	debug(LOG_DEATH, "Killing off feature %s id %d (%p)", objInfo(psDel),
        psDel->getId(), static_cast<void *>(psDel));

	killFeature(psDel);
	return true;
}

/* Remove a Feature and free it's memory */
bool destroyFeature(Feature* psDel, unsigned impactTime)
{
	unsigned widthScatter, breadthScatter, heightScatter, i;
	EFFECT_TYPE explosionSize;
	Vector3i pos;

	ASSERT_OR_RETURN(false, psDel != nullptr, "Invalid feature pointer");
	ASSERT(gameTime - deltaGameTime < impactTime, "Expected %u < %u, gameTime = %u, bad impactTime",
	       gameTime - deltaGameTime, impactTime, gameTime);

	/* Only add if visible and damageable*/
	if (psDel->visibleToSelectedPlayer() && psDel->getStats()->damageable)
	{
		/* Set off a destruction effect */
		/* First Explosions */
		widthScatter = TILE_UNITS / 2;
		breadthScatter = TILE_UNITS / 2;
		heightScatter = TILE_UNITS / 4;
		//set which explosion to use based on size of feature
		if (psDel->getStats()->baseWidth < 2 && psDel->getStats()->baseBreadth < 2) {
			explosionSize = EFFECT_TYPE::EXPLOSION_TYPE_SMALL;
		}
		else if (psDel->getStats()->baseWidth < 3 && psDel->getStats()->baseBreadth < 3) {
			explosionSize = EFFECT_TYPE::EXPLOSION_TYPE_MEDIUM;
		}
		else {
			explosionSize = EFFECT_TYPE::EXPLOSION_TYPE_LARGE;
		}
		for (i = 0; i < 4; i++)
		{
			pos.x = psDel->getPosition().x + widthScatter - rand() % (2 * widthScatter);
			pos.z = psDel->getPosition().y + breadthScatter - rand() % (2 * breadthScatter);
			pos.y = psDel->getPosition().z + 32 + rand() % heightScatter;
			addEffect(&pos, EFFECT_GROUP::EXPLOSION, explosionSize, false, nullptr, 0, impactTime);
		}

		if (psDel->getStats()->subType == FEATURE_TYPE::SKYSCRAPER) {
			pos.x = psDel->getPosition().x;
			pos.z = psDel->getPosition().y;
			pos.y = psDel->getPosition().z;
			addEffect(&pos, EFFECT_GROUP::DESTRUCTION, EFFECT_TYPE::DESTRUCTION_TYPE_SKYSCRAPER,
                true, psDel->getDisplayData().imd_shape.get(), 0, impactTime);
			initPerimeterSmoke(psDel->getDisplayData().imd_shape.get(), pos);

			shakeStart(250); // small shake
		}

		/* Then a sequence of effects */
		pos.x = psDel->getPosition().x;
    pos.z = psDel->getPosition().y;
		pos.y = map_Height(pos.x, pos.z);
		addEffect(&pos, EFFECT_GROUP::DESTRUCTION,
              EFFECT_TYPE::DESTRUCTION_TYPE_FEATURE, false, nullptr, 0, impactTime);

		//play sound
		// ffs gj
		if (psDel->getStats()->subType == FEATURE_TYPE::SKYSCRAPER) {
			audio_PlayStaticTrack(psDel->getPosition().x, psDel->getPosition().y, ID_SOUND_BUILDING_FALL);
		}
		else {
			audio_PlayStaticTrack(psDel->getPosition().x, psDel->getPosition().y, ID_SOUND_EXPLOSION);
		}
	}

	if (psDel->getStats()->subType == FEATURE_TYPE::SKYSCRAPER)
	{
		// ----- Flip all the tiles under the skyscraper to a rubble tile
		// smoke effect should disguise this happening
		StructureBounds b = getStructureBounds(psDel);
		for (int breadth = 0; breadth < b.size.y; ++breadth)
		{
			for (int width = 0; width < b.size.x; ++width)
			{
				Tile* psTile = mapTile(b.map.x + width, b.map.y + breadth);
				// stops water texture changing for underwater features
				if (terrainType(psTile) != TER_WATER)
				{
					if (terrainType(psTile) != TER_CLIFFFACE)
					{
						/* Clear feature bits */
						psTile->texture = TileNumber_texture(psTile->texture) | RUBBLE_TILE;
						auxClearBlocking(b.map.x + width, b.map.y + breadth, AUXBITS_ALL);
					}
					else
					{
						/* This remains a blocking tile */
						psTile->psObject = nullptr;
						auxClearBlocking(b.map.x + width, b.map.y + breadth, AIR_BLOCKED);
						// Shouldn't remain blocking for air units, however.
						psTile->texture = TileNumber_texture(psTile->texture) | BLOCKING_RUBBLE_TILE;
					}
				}
			}
		}
	}

	removeFeature(psDel);
	psDel->died = impactTime;
	return true;
}


int getFeatureStatFromName(const WzString& name)
{
	FeatureStats* psStat;

	for (unsigned inc = 0; inc < numFeatureStats; inc++)
	{
		psStat = &asFeatureStats[inc];
		if (psStat->id.compare(name) == 0) {
			return inc;
		}
	}
	return -1;
}

StructureBounds getStructureBounds(Feature const* object)
{
	return getStructureBounds(object->getStats(), object->getPosition().xy());
}

StructureBounds getStructureBounds(FeatureStats const* stats, Vector2i pos)
{
	const Vector2i size = stats->size();
	const Vector2i map = map_coord(pos) - size / 2;
	return {map, size};
}

const FeatureStats* Feature::getStats() const
{
  return psStats.get();
}
