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
 * @file stats.cpp
 * Store common stats for weapons, components, brains, etc.
 */

#include "lib/framework/fixedpoint.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/sound/audio_id.h"

#include "stats.h"
#include "main.h"
#include "map.h"

static constexpr auto WEAPON_TIME = 100;
static constexpr auto DEFAULT_DROID_RESISTANCE = 150;

BaseStats::BaseStats(unsigned ref)
  : ref{ref}
{
}

bool BaseStats::hasType(StatType type) const
{
  return (ref & STAT_MASK) == type;
}

static int* asTerrainTable;

//used to hold the modifiers cross refd by weapon effect and propulsion type
WEAPON_MODIFIER asWeaponModifier[(size_t)WEAPON_EFFECT::COUNT][(size_t)PROPULSION_TYPE::COUNT];
WEAPON_MODIFIER asWeaponModifierBody[(size_t)WEAPON_EFFECT::COUNT][(size_t)BODY_SIZE::COUNT];

static std::unordered_map<WzString, BaseStats*> lookupStatPtr;
static std::unordered_map<WzString, ComponentStats*> lookupCompStatPtr;

static bool getMovementModel(const WzString& movementModel, MOVEMENT_MODEL* model);
static bool statsGetAudioIDFromString(const WzString& szStatName, const WzString& szWavName, int* piWavID);

//Deallocate the storage assigned for the Propulsion Types table
static void deallocPropulsionTypes()
{
	asPropulsionTypes.clear();
}

//dealloc the storage assigned for the terrain table
static void deallocTerrainTable()
{
	free(asTerrainTable);
	asTerrainTable = nullptr;
}

/*Deallocate all the stats assigned from input data*/
bool statsShutDown()
{
	lookupStatPtr.clear();
	lookupCompStatPtr.clear();
	deallocPropulsionTypes();
	deallocTerrainTable();
	return true;
}

static iIMDShape* statsGetIMD(WzConfig& json, BaseStats* psStats, WzString const& key,
                              WzString const& key2 = WzString())
{
  if (!json.contains(key)) return nullptr;

  auto value = json.json(key);
  if (value.is_object()) {
    ASSERT(!key2.isEmpty(), "Cannot look up a JSON object with an empty key!");
    auto obj = value;
    if (obj.find(key2.toUtf8()) == obj.end()) {
      return nullptr;
    }
    value = obj[key2.toUtf8()];
  }

  auto filename = json_variant(value).toWzString();
  auto retval = modelGet(filename);
  ASSERT(retval != nullptr, "Cannot find the PIE model %s for stat %s in %s",
         filename.toUtf8().c_str(), getStatsName(psStats), json.fileName().toUtf8().c_str());

  return retval;
}

static void loadStats(WzConfig& json, BaseStats* psStats, size_t index)
{
	psStats->id = json.group();
	psStats->name = json.string("name");
	psStats->index = index;
	ASSERT(lookupStatPtr.find(psStats->id) == lookupStatPtr.end(),
         "Duplicate ID found! (%s)", psStats->id.toUtf8().c_str());

	lookupStatPtr.insert(std::make_pair(psStats->id, psStats));
}

void unloadStructureStats_BaseStats(const StructureStats& psStats)
{
	lookupStatPtr.erase(psStats.id);
}

static void loadCompStats(WzConfig& json, ComponentStats* psStats, size_t index)
{
	loadStats(json, psStats, index);
	lookupCompStatPtr.insert(std::make_pair(psStats->id, psStats));
	psStats->buildPower = json.value("buildPower", 0).toUInt();
	psStats->buildPoints = json.value("buildPoints", 0).toUInt();
	psStats->designable = json.value("designable", false).toBool();
	psStats->weight = json.value("weight", 0).toUInt();
	psStats->base.hitPoints = json.value("hitpoints", 0).toUInt();
	psStats->base.hitpointPct = json.value("hitpointPct", 100).toUInt();

	WzString dtype = json.value("droidType", "DROID").toWzString();
	psStats->droidTypeOverride = DROID_TYPE::ANY;
	if (dtype.compare("PERSON") == 0)
	{
		psStats->droidTypeOverride = DROID_TYPE::PERSON;
	}
	else if (dtype.compare("TRANSPORTER") == 0)
	{
		psStats->droidTypeOverride = DROID_TYPE::TRANSPORTER;
	}
	else if (dtype.compare("SUPERTRANSPORTER") == 0)
	{
		psStats->droidTypeOverride = DROID_TYPE::SUPER_TRANSPORTER;
	}
	else if (dtype.compare("CYBORG") == 0)
	{
		psStats->droidTypeOverride = DROID_TYPE::CYBORG;
	}
	else if (dtype.compare("CYBORG_SUPER") == 0)
	{
		psStats->droidTypeOverride = DROID_TYPE::CYBORG_SUPER;
	}
	else if (dtype.compare("CYBORG_CONSTRUCT") == 0)
	{
		psStats->droidTypeOverride = DROID_TYPE::CYBORG_CONSTRUCT;
	}
	else if (dtype.compare("CYBORG_REPAIR") == 0)
	{
		psStats->droidTypeOverride = DROID_TYPE::CYBORG_REPAIR;
	}
	else if (dtype.compare("DROID_TYPE::CONSTRUCT") == 0)
	{
		psStats->droidTypeOverride = DROID_TYPE::CONSTRUCT;
	}
	else if (dtype.compare("DROID_TYPE::ECM") == 0)
	{
		psStats->droidTypeOverride = DROID_TYPE::ECM;
	}
	else if (dtype.compare("DROID_TYPE::COMMAND") == 0)
	{
		psStats->droidTypeOverride = DROID_TYPE::COMMAND;
	}
	else if (dtype.compare("DROID_TYPE::SENSOR") == 0)
	{
		psStats->droidTypeOverride = DROID_TYPE::SENSOR;
	}
	else if (dtype.compare("DROID_TYPE::REPAIR") == 0)
	{
		psStats->droidTypeOverride = DROID_TYPE::REPAIRER;
	}
	else if (dtype.compare("DROID") != 0)
	{
		debug(LOG_ERROR, "Unrecognized droidType %s", dtype.toUtf8().c_str());
	}
}

/*Load the weapon stats from the file exported from Access*/
bool loadWeaponStats(WzConfig& ini)
{
	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();

	// Hack to make sure ZNULLWEAPON is always first in list
	auto nullweapon = std::find(list.begin(), list.end(), WzString::fromUtf8("ZNULLWEAPON"));
	ASSERT_OR_RETURN(false, nullweapon != list.end(), "ZNULLWEAPON is mandatory");

	std::iter_swap(nullweapon, list.begin());
  weaponStatsList.resize(list.size());
	for (auto i = 0; i < list.size(); ++i)
	{
    weaponStatsList[i] = WeaponStats();
    auto psStats = &weaponStatsList[i];
		std::vector<WzString> flags;
		ini.beginGroup(list[i]);
		loadCompStats(ini, psStats, i);
		psStats->compType = COMPONENT_TYPE::WEAPON;

		psStats->radiusLife = ini.value("radiusLife", 0).toUInt();
		psStats->base.shortRange = ini.value("shortRange").toUInt();
		psStats->base.maxRange = ini.value("longRange").toUInt();
		psStats->base.minRange = ini.value("minRange", 0).toUInt();
		psStats->base.hitChance = ini.value("longHit", 100).toUInt();
		psStats->base.shortHitChance = ini.value("shortHit", 100).toUInt();
		psStats->base.firePause = ini.value("firePause").toUInt();
		psStats->base.numRounds = ini.value("numRounds").toUInt();
		psStats->base.reloadTime = ini.value("reloadTime").toUInt();
		psStats->base.damage = ini.value("damage").toUInt();
		psStats->base.minimumDamage = ini.value("minimumDamage", 0).toInt();
		psStats->base.radius = ini.value("radius", 0).toUInt();
		psStats->base.radiusDamage = ini.value("radiusDamage", 0).toUInt();
		psStats->base.periodicalDamageTime = ini.value("periodicalDamageTime", 0).toUInt();
		psStats->base.periodicalDamage = ini.value("periodicalDamage", 0).toUInt();
		psStats->base.periodicalDamageRadius = ini.value("periodicalDamageRadius", 0).toUInt();

		// multiply time stats
		psStats->base.firePause *= WEAPON_TIME;
		psStats->base.periodicalDamageTime *= WEAPON_TIME;
		psStats->radiusLife *= WEAPON_TIME;
		psStats->base.reloadTime *= WEAPON_TIME;

		// copy for upgrades
		for (auto& j : psStats->upgraded)
		{
			j = psStats->base;
		}

		psStats->numExplosions = ini.value("numExplosions").toUInt();
		psStats->flightSpeed = ini.value("flightSpeed", 1).toUInt();
		psStats->rotate = ini.value("rotate").toUInt();
		psStats->minElevation = ini.value("minElevation").toInt();
		psStats->maxElevation = ini.value("maxElevation").toInt();
		psStats->recoilValue = ini.value("recoilValue").toUInt();
		psStats->effectSize = ini.value("effectSize").toUInt();
		std::vector<WzString> flags_raw = ini.value("flags", 0).toWzStringList();

		// convert flags entries to lowercase
		std::transform(
			flags_raw.begin(),
			flags_raw.end(),
			std::back_inserter(flags),
			[](const WzString& in) { return in.toLower(); }
		);

		psStats->vtolAttackRuns = ini.value("numAttackRuns", 0).toUInt();
		psStats->penetrate = ini.value("penetrate", false).toBool();
		// weapon size limitation
		auto weaponSize = ini.value("weaponSize", (int)WEAPON_SIZE::ANY).toInt();
		ASSERT(weaponSize <= (int)WEAPON_SIZE::ANY, "Bad weapon size for %s", list[i].toUtf8().c_str());
		psStats->weaponSize = (WEAPON_SIZE)weaponSize;

		ASSERT(psStats->flightSpeed > 0, "Invalid flight speed for %s", list[i].toUtf8().c_str());

		psStats->ref = STAT_WEAPON + i;

		//get the IMD for the component
		psStats->pIMD = statsGetIMD(ini, psStats, "model");
		psStats->pMountGraphic = statsGetIMD(ini, psStats, "mountModel");
		if (GetGameMode() == GS_NORMAL) {
			psStats->pMuzzleGraphic = statsGetIMD(ini, psStats, "muzzleGfx");
			psStats->pInFlightGraphic = statsGetIMD(ini, psStats, "flightGfx");
			psStats->pTargetHitGraphic = statsGetIMD(ini, psStats, "hitGfx");
			psStats->pTargetMissGraphic = statsGetIMD(ini, psStats, "missGfx");
			psStats->pWaterHitGraphic = statsGetIMD(ini, psStats, "waterGfx");
			psStats->pTrailGraphic = statsGetIMD(ini, psStats, "trailGfx");
		}
		psStats->fireOnMove = ini.value("fireOnMove", true).toBool();

		//set the weapon class
		if (!getWeaponClass(ini.value("weaponClass").toWzString(), &psStats->weaponClass))
		{
			debug(LOG_ERROR, "Invalid weapon class for weapon %s - assuming KINETIC", getStatsName(psStats));
			psStats->weaponClass = WEAPON_CLASS::KINETIC;
		}

		//set the subClass
		if (!getWeaponSubClass(ini.value("weaponSubClass").toWzString().toUtf8().c_str(), &psStats->weaponSubClass))
		{
			return false;
		}

		// set max extra weapon range on misses, make this modifiable one day by mod makers
		if (psStats->weaponSubClass == WEAPON_SUBCLASS::MACHINE_GUN ||
        psStats->weaponSubClass == WEAPON_SUBCLASS::COMMAND) {
			psStats->distanceExtensionFactor = 120;
		}
		else if (psStats->weaponSubClass == WEAPON_SUBCLASS::AA_GUN) {
			psStats->distanceExtensionFactor = 100;
		}
		else // default
		{
			psStats->distanceExtensionFactor = 150;
		}

		//set the weapon effect
		if (!getWeaponEffect(ini.value("weaponEffect").toWzString(), &psStats->weaponEffect))
		{
			debug(LOG_FATAL, "loadWepaonStats: Invalid weapon effect for weapon %s", getStatsName(psStats));
			return false;
		}

		//set periodical damage weapon class
		WzString periodicalDamageWeaponClass = ini.value("periodicalDamageWeaponClass", "").toWzString();
		if (periodicalDamageWeaponClass.compare("") == 0)
		{
			//was not setted in ini - use default value
			psStats->periodicalDamageWeaponClass = psStats->weaponClass;
		}
		else if (!getWeaponClass(periodicalDamageWeaponClass, &psStats->periodicalDamageWeaponClass))
		{
			debug(LOG_ERROR, "Invalid periodicalDamageWeaponClass for weapon %s - assuming same class as weapon",
			      getStatsName(psStats));
			psStats->periodicalDamageWeaponClass = psStats->weaponClass;
		}

		//set periodical damage weapon subclass
		WzString periodicalDamageWeaponSubClass = ini.value("periodicalDamageWeaponSubClass", "").toWzString();
		if (periodicalDamageWeaponSubClass.compare("") == 0) {
			//was not setted in ini - use default value
			psStats->periodicalDamageWeaponSubClass = psStats->weaponSubClass;
		}
		else if (!getWeaponSubClass(periodicalDamageWeaponSubClass.toUtf8().c_str(),
		                            &psStats->periodicalDamageWeaponSubClass))
		{
			debug(LOG_ERROR, "Invalid periodicalDamageWeaponSubClass for weapon %s - assuming same subclass as weapon",
			      getStatsName(psStats));
			psStats->periodicalDamageWeaponSubClass = psStats->weaponSubClass;
		}

		//set periodical damage weapon effect
		WzString periodicalDamageWeaponEffect = ini.value("periodicalDamageWeaponEffect", "").toWzString();
		if (periodicalDamageWeaponEffect.compare("") == 0)
		{
			//was not setted in ini - use default value
			psStats->periodicalDamageWeaponEffect = psStats->weaponEffect;
		}
		else if (!getWeaponEffect(periodicalDamageWeaponEffect.toUtf8().c_str(),
		                          &psStats->periodicalDamageWeaponEffect))
		{
			debug(LOG_ERROR, "Invalid periodicalDamageWeaponEffect for weapon %s - assuming same effect as weapon",
			      getStatsName(psStats));
			psStats->periodicalDamageWeaponEffect = psStats->weaponEffect;
		}

		//set the movement model
		if (!getMovementModel(ini.value("movement").toWzString(),
                          &psStats->movementModel)) {
			return false;
		}

		unsigned int shortRange = psStats->upgraded[0].shortRange;
		unsigned int longRange = psStats->upgraded[0].maxRange;
		unsigned int shortHit = psStats->upgraded[0].shortHitChance;
		unsigned int longHit = psStats->upgraded[0].hitChance;
		if (shortRange > longRange)
		{
			debug(LOG_ERROR, "%s, Short range (%d) is greater than long range (%d)", getStatsName(psStats), shortRange,
			      longRange);
		}
		if (shortRange == longRange && shortHit != longHit)
		{
			debug(LOG_ERROR, "%s, shortHit and longHit should be equal if the ranges are the same",
			      getStatsName(psStats));
		}

		// set the face Player value
		psStats->facePlayer = ini.value("facePlayer", false).toBool();

		// set the In flight face Player value
		psStats->faceInFlight = ini.value("faceInFlight", false).toBool();

		//set the light world value
		psStats->lightWorld = ini.value("lightWorld", false).toBool();

		// interpret flags
		psStats->surfaceToAir = SHOOT_ON_GROUND; // default
		if (std::find(flags.begin(), flags.end(), "aironly") != flags.end()) // "AirOnly"
		{
			psStats->surfaceToAir = SHOOT_IN_AIR;
		}
		else if (std::find(flags.begin(), flags.end(), "shootair") != flags.end()) // "ShootAir"
		{
			psStats->surfaceToAir |= SHOOT_IN_AIR;
		}
		if (std::find(flags.begin(), flags.end(), "nofriendlyfire") != flags.end()) // "NoFriendlyFire"
		{
			psStats->flags.set(static_cast<size_t>(WEAPON_FLAGS::NO_FRIENDLY_FIRE), true);
		}

		//set the weapon sounds to default value
		psStats->iAudioFireID = NO_SOUND;
		psStats->iAudioImpactID = NO_SOUND;

		// load sounds
		int weaponSoundID, explosionSoundID;
		WzString szWeaponWav = ini.value("weaponWav", "-1").toWzString();
		WzString szExplosionWav = ini.value("explosionWav", "-1").toWzString();
		bool result = statsGetAudioIDFromString(list[i], szWeaponWav, &weaponSoundID);
		ASSERT_OR_RETURN(false, result, "Weapon sound %s not found for %s", szWeaponWav.toUtf8().c_str(),
		                 getStatsName(psStats));
		result = statsGetAudioIDFromString(list[i], szExplosionWav, &explosionSoundID);
		ASSERT_OR_RETURN(false, result, "Explosion sound %s not found for %s", szExplosionWav.toUtf8().c_str(),
		                 getStatsName(psStats));
		psStats->iAudioFireID = weaponSoundID;
		psStats->iAudioImpactID = explosionSoundID;

		ini.endGroup();
	}

	return true;
}

bool loadBodyStats(WzConfig& ini)
{
	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();

	// Hack to make sure ZNULLBODY is always first in list
	auto nullbody = std::find(list.begin(), list.end(), WzString::fromUtf8("ZNULLBODY"));
	ASSERT_OR_RETURN(false, nullbody != list.end(), "ZNULLBODY is mandatory");

	std::iter_swap(nullbody, list.begin());
  bodyStatsList.resize(list.size());
	for (auto i = 0; i < list.size(); ++i)
	{
    bodyStatsList[i] = BodyStats();
		auto psStats = &bodyStatsList[i];

		ini.beginGroup(list[i]);
		loadCompStats(ini, psStats, i);
		psStats->compType = COMPONENT_TYPE::BODY;

		psStats->weaponSlots = ini.value("weaponSlots").toInt();
		psStats->bodyClass = ini.value("class").toWzString();
		psStats->base.thermal = ini.value("armourHeat").toInt();
		psStats->base.armour = ini.value("armourKinetic").toInt();
		psStats->base.power = ini.value("powerOutput").toInt();
		psStats->base.resistance = ini.value("resistance", DEFAULT_DROID_RESISTANCE).toInt();
		for (auto& j : psStats->upgraded)
		{
			j = psStats->base;
		}
		psStats->ref = STAT_BODY + i;
		if (!getBodySize(ini.value("size").toWzString(), &psStats->size))
		{
			ASSERT(false, "Unknown body size for %s", getStatsName(psStats));
			return false;
		}
		psStats->pIMD = statsGetIMD(ini, psStats, "model");

		ini.endGroup();

    psStats->ppIMDList.resize(propulsionStatsList.size() * (int)PROP_SIDE::COUNT);
    psStats->ppMoveIMDList.resize(propulsionStatsList.size() * (int)PROP_SIDE::COUNT);
    psStats->ppStillIMDList.resize(propulsionStatsList.size() * (int)PROP_SIDE::COUNT);
	}

	for (auto& i : list)
	{
		WzString propulsionName, leftIMD, rightIMD;
		BodyStats* psBodyStat = nullptr;
		int numStats;

		ini.beginGroup(i);
		if (!ini.contains("propulsionExtraModels")) {
			ini.endGroup();
			continue;
		}
		ini.beginGroup("propulsionExtraModels");

		//get the body stats
		for (numStats = 0; numStats < bodyStatsList.size(); ++numStats)
		{
			psBodyStat = &bodyStatsList[numStats];
			if (i.compare(psBodyStat->id) == 0) {
				break;
			}
		}

		if (numStats == bodyStatsList.size()) // not found
		{
			debug(LOG_FATAL, "Invalid body name %s", i.toUtf8().c_str());
			return false;
		}

		std::vector<WzString> keys = ini.childKeys();
		for (auto& key : keys)
		{
			for (numStats = 0; numStats < propulsionStatsList.size(); numStats++)
			{
				auto psPropulsionStat = &propulsionStatsList[numStats];
				if (key.compare(psPropulsionStat->id) == 0) {
					break;
				}
			}

			if (numStats == propulsionStatsList.size()) {
				debug(LOG_FATAL, "Invalid propulsion name %s", key.toUtf8().c_str());
				return false;
			}

			//allocate the left and right propulsion IMDs + movement and standing still animations
			psBodyStat->ppIMDList[numStats * (int)PROP_SIDE::COUNT + (int)PROP_SIDE::LEFT] = statsGetIMD(
				ini, psBodyStat, key, WzString::fromUtf8("left"));
			psBodyStat->ppIMDList[numStats * (int)PROP_SIDE::COUNT + (int)PROP_SIDE::RIGHT] = statsGetIMD(
				ini, psBodyStat, key, WzString::fromUtf8("right"));
			psBodyStat->ppMoveIMDList[numStats] = statsGetIMD(ini, psBodyStat, key, WzString::fromUtf8("moving"));
			psBodyStat->ppStillIMDList[numStats] = statsGetIMD(ini, psBodyStat, key, WzString::fromUtf8("still"));
		}
		ini.endGroup();
		ini.endGroup();
	}

	return true;
}

/*Load the Brain stats from the file exported from Access*/
bool loadBrainStats(WzConfig& ini)
{
	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();
	// Hack to make sure ZNULLBRAIN is always first in list
	auto nullbrain = std::find(list.begin(), list.end(),
                             WzString::fromUtf8("ZNULLBRAIN"));

	ASSERT_OR_RETURN(false, nullbrain != list.end(), "ZNULLBRAIN is mandatory");

	std::iter_swap(nullbrain, list.begin());
  brainStatsList.resize(list.size());
	for (auto i = 0; i < list.size(); ++i)
	{
    brainStatsList[i] = CommanderStats();
		auto psStats = &brainStatsList[i];

		ini.beginGroup(list[i]);
		loadCompStats(ini, psStats, i);
		psStats->compType = COMPONENT_TYPE::BRAIN;

		psStats->weight = ini.value("weight", 0).toInt();
		psStats->base.maxDroids = ini.value("maxDroids").toInt();
		psStats->base.maxDroidsMult = ini.value("maxDroidsMult").toInt();
		auto rankNames = ini.json("ranks");
		ASSERT(rankNames.is_array(), "ranks is not an array");
		for (const auto& v : rankNames)
		{
			psStats->rankNames.push_back(v.get<std::string>());
		}
		auto rankThresholds = ini.json("thresholds");
		for (const auto& v : rankThresholds)
		{
			psStats->base.rankThresholds.push_back(v.get<int>());
		}
		psStats->ref = STAT_BRAIN + i;

		for (int j = 0; j < MAX_PLAYERS; j++)
		{
			psStats->upgraded[j] = psStats->base;
		}

		// check weapon attached
		psStats->psWeaponStat = nullptr;
		if (ini.contains("turret")) {
			int weapon = getCompFromName(COMPONENT_TYPE::WEAPON, ini.value("turret").toWzString());
			ASSERT_OR_RETURN(false, weapon >= 0, "Unable to find weapon for brain %s", getStatsName(psStats));
			psStats->psWeaponStat = &weaponStatsList[weapon];
		}
		psStats->designable = ini.value("designable", false).toBool();
		ini.endGroup();
	}

	return true;
}

/*returns the propulsion type based on the string name passed in */
bool getPropulsionType(const char* typeName, PROPULSION_TYPE* type)
{
  using enum PROPULSION_TYPE;
	if (strcmp(typeName, "Wheeled") == 0) {
		*type = WHEELED;
	}
	else if (strcmp(typeName, "Tracked") == 0) {
		*type = TRACKED;
	}
	else if (strcmp(typeName, "Legged") == 0) {
		*type = LEGGED;
	}
	else if (strcmp(typeName, "Hover") == 0) {
		*type = HOVER;
	}
	else if (strcmp(typeName, "Lift") == 0) {
		*type = LIFT;
	}
	else if (strcmp(typeName, "Propellor") == 0) {
		*type = PROPELLOR;
	}
	else if (strcmp(typeName, "Half-Tracked") == 0) {
		*type = HALF_TRACKED;
	}
	else {
		debug(LOG_ERROR, "getPropulsionType: Invalid Propulsion type %s - assuming Hover", typeName);
		*type = HOVER;

		return false;
	}

	return true;
}

/*Load the Propulsion stats from the file exported from Access*/
bool loadPropulsionStats(WzConfig& ini)
{
	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();

	// Hack to make sure ZNULLPROP is always first in list
	auto nullprop = std::find(list.begin(), list.end(), WzString::fromUtf8("ZNULLPROP"));
	ASSERT_OR_RETURN(false, nullprop != list.end(), "ZNULLPROP is mandatory");

	std::iter_swap(nullprop, list.begin());
  propulsionStatsList.resize(list.size());
	for (size_t i = 0; i < list.size(); ++i)
	{
    propulsionStatsList[i] = PropulsionStats();
		auto psStats = &propulsionStatsList[i];

		ini.beginGroup(list[i]);
		loadCompStats(ini, psStats, i);
		psStats->compType = COMPONENT_TYPE::PROPULSION;

		psStats->base.hitpointPctOfBody = ini.value("hitpointPctOfBody", 0).toInt();
		psStats->maxSpeed = ini.value("speed").toInt();
		psStats->ref = STAT_PROPULSION + i;
		psStats->turnSpeed = ini.value("turnSpeed", DEG(1) / 3).toInt();
		psStats->spinSpeed = ini.value("spinSpeed", DEG(3) / 4).toInt();
		psStats->spinAngle = ini.value("spinAngle", 180).toInt();
		psStats->acceleration = ini.value("acceleration", 250).toInt();
		psStats->deceleration = ini.value("deceleration", 800).toInt();
		psStats->skidDeceleration = ini.value("skidDeceleration", 600).toInt();
		psStats->pIMD = nullptr;
		psStats->pIMD = statsGetIMD(ini, psStats, "model");
		for (int j = 0; j < MAX_PLAYERS; j++)
		{
			psStats->upgraded[j] = psStats->base;
		}
		if (!getPropulsionType(ini.value("type").toWzString().toUtf8().c_str(), &psStats->propulsionType))
		{
			debug(LOG_FATAL, "loadPropulsionStats: Invalid Propulsion type for %s", getStatsName(psStats));
			return false;
		}
		ini.endGroup();
	}

	return true;
}

/*Load the Sensor stats from the file exported from Access*/
bool loadSensorStats(WzConfig& ini)
{
	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();

	// Hack to make sure ZNULLSENSOR is always first in list
	auto nullsensor = std::find(list.begin(), list.end(), WzString::fromUtf8("ZNULLSENSOR"));
	ASSERT_OR_RETURN(false, nullsensor != list.end(), "ZNULLSENSOR is mandatory");

	std::iter_swap(nullsensor, list.begin());
  sensorStatsList.resize(list.size());
	for (size_t i = 0; i < list.size(); ++i)
	{
		auto psStats = &sensorStatsList[i];

		ini.beginGroup(list[i]);
		loadCompStats(ini, psStats, i);
		psStats->compType = COMPONENT_TYPE::SENSOR;

		psStats->base.range = ini.value("range").toInt();
		for (int j = 0; j < MAX_PLAYERS; j++)
		{
			psStats->upgraded[j] = psStats->base;
		}

		psStats->ref = STAT_SENSOR + i;

		WzString location = ini.value("location").toWzString();
		if (location.compare("DEFAULT") == 0) {
			psStats->location = LOC::DEFAULT;
		}
		else if (location.compare("TURRET") == 0) {
			psStats->location = LOC::TURRET;
		}
		else {
			ASSERT(false, "Invalid Sensor location: %s", location.toUtf8().c_str());
		}

		WzString type = ini.value("type").toWzString();
    using enum SENSOR_TYPE;
		if (type.compare("STANDARD") == 0)
		{
			psStats->type = STANDARD;
		}
		else if (type.compare("INDIRECT CB") == 0)
		{
			psStats->type = INDIRECT_CB;
		}
		else if (type.compare("VTOL CB") == 0)
		{
			psStats->type = VTOL_CB;
		}
		else if (type.compare("VTOL INTERCEPT") == 0)
		{
			psStats->type = VTOL_INTERCEPT;
		}
		else if (type.compare("SUPER") == 0)
		{
			psStats->type = SUPER;
		}
		else if (type.compare("RADAR DETECTOR") == 0)
		{
			psStats->type = RADAR_DETECTOR;
		}
		else
		{
			ASSERT(false, "Invalid Sensor type: %s", type.toUtf8().c_str());
		}

		//get the IMD for the component
		psStats->pIMD = statsGetIMD(ini, psStats, "sensorModel");
		psStats->pMountGraphic = statsGetIMD(ini, psStats, "mountModel");

		ini.endGroup();
	}
	return true;
}

/*Load the ECM stats from the file exported from Access*/
bool loadECMStats(WzConfig& ini)
{
	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();

	// Hack to make sure ZNULLECM is always first in list
	auto nullecm = std::find(list.begin(), list.end(), WzString::fromUtf8("ZNULLECM"));
	ASSERT_OR_RETURN(false, nullecm != list.end(), "ZNULLECM is mandatory");

	std::iter_swap(nullecm, list.begin());
  ecmStatsList.resize(list.size());
	for (size_t i = 0; i < list.size(); ++i)
	{
    ecmStatsList[i] = EcmStats();
		auto psStats = &ecmStatsList[i];

		ini.beginGroup(list[i]);
		loadCompStats(ini, psStats, i);
		psStats->compType = COMPONENT_TYPE::ECM;

		psStats->base.range = ini.value("range").toInt();
		for (int j = 0; j < MAX_PLAYERS; j++)
		{
			psStats->upgraded[j] = psStats->base;
		}

		psStats->ref = STAT_ECM + i;

		WzString location = ini.value("location").toWzString();
		if (location.compare("DEFAULT") == 0)
		{
			psStats->location = LOC::DEFAULT;
		}
		else if (location.compare("TURRET") == 0)
		{
			psStats->location = LOC::TURRET;
		}
		else
		{
			ASSERT(false, "Invalid ECM location: %s", location.toUtf8().c_str());
		}

		//get the IMD for the component
		psStats->pIMD = statsGetIMD(ini, psStats, "sensorModel");
		psStats->pMountGraphic = statsGetIMD(ini, psStats, "mountModel");

		ini.endGroup();
	}
	return true;
}

/*Load the Repair stats from the file exported from Access*/
bool loadRepairStats(WzConfig& ini)
{
	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();

	// Hack to make sure ZNULLREPAIR is always first in list
	auto nullrepair = std::find(list.begin(), list.end(), WzString::fromUtf8("ZNULLREPAIR"));
	ASSERT_OR_RETURN(false, nullrepair != list.end(), "ZNULLREPAIR is mandatory");

	std::iter_swap(nullrepair, list.begin());
  repairStatsList.resize(list.size());
	for (size_t i = 0; i < list.size(); ++i)
	{
    repairStatsList[i] = RepairStats();
		auto psStats = &repairStatsList[i];

		ini.beginGroup(list[i]);
		loadCompStats(ini, psStats, i);
		psStats->compType = COMPONENT_TYPE::REPAIR_UNIT;

		psStats->base.repairPoints = ini.value("repairPoints").toInt();
		for (int j = 0; j < MAX_PLAYERS; j++)
		{
			psStats->upgraded[j] = psStats->base;
		}
		psStats->time = ini.value("time", 0).toInt() * WEAPON_TIME;

		psStats->ref = STAT_REPAIR + i;

		WzString location = ini.value("location").toWzString();
		if (location.compare("DEFAULT") == 0)
		{
			psStats->location = LOC::DEFAULT;
		}
		else if (location.compare("TURRET") == 0)
		{
			psStats->location = LOC::TURRET;
		}
		else
		{
			ASSERT(false, "Invalid Repair location: %s", location.toUtf8().c_str());
		}

		//check its not 0 since we will be dividing by it at a later stage
		ASSERT_OR_RETURN(false, psStats->time > 0, "Repair delay cannot be zero for %s", getStatsName(psStats));

		//get the IMD for the component
		psStats->pIMD = statsGetIMD(ini, psStats, "model");
		psStats->pMountGraphic = statsGetIMD(ini, psStats, "mountModel");

		ini.endGroup();
	}
	return true;
}

/*Load the Construct stats from the file exported from Access*/
bool loadConstructStats(WzConfig& ini)
{
	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();

	// Hack to make sure ZNULLCONSTRUCT is always first in list
	auto nullconstruct = std::find(list.begin(), list.end(), WzString::fromUtf8("ZNULLCONSTRUCT"));
	ASSERT_OR_RETURN(false, nullconstruct != list.end(), "ZNULLCONSTRUCT is mandatory");

	std::iter_swap(nullconstruct, list.begin());
  constructStatsList.resize(list.size());
	for (size_t i = 0; i < list.size(); ++i)
	{
    constructStatsList[i] = ConstructStats();
		auto psStats = &constructStatsList[i];

		ini.beginGroup(list[i]);
		loadCompStats(ini, psStats, i);
		psStats->compType = COMPONENT_TYPE::CONSTRUCT;

		psStats->base.constructPoints = ini.value("constructPoints").toInt();
		for (int j = 0; j < MAX_PLAYERS; j++)
		{
			psStats->upgraded[j] = psStats->base;
		}
		psStats->ref = STAT_CONSTRUCT + i;

		//get the IMD for the component
		psStats->pIMD = statsGetIMD(ini, psStats, "sensorModel");
		psStats->pMountGraphic = statsGetIMD(ini, psStats, "mountModel");

		ini.endGroup();
	}
	return true;
}

/*Load the Propulsion Types from the file exported from Access*/
bool loadPropulsionTypes(WzConfig& ini)
{
	auto const NumTypes = static_cast<int>(PROPULSION_TYPE::COUNT);

	//allocate storage for the stats
	asPropulsionTypes.resize(NumTypes);
	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();

	for (auto i = 0; i < NumTypes; ++i)
	{
		PROPULSION_TYPE type;

		ini.beginGroup(list[i]);
		unsigned multiplier = ini.value("multiplier").toUInt();

		//set the pointer for this record based on the name
		if (!getPropulsionType(list[i].toUtf8().c_str(), &type)) {
			debug(LOG_FATAL, "Invalid Propulsion type - %s", list[i].toUtf8().c_str());
			return false;
		}

		auto pPropType = &asPropulsionTypes[(int)type];

		WzString flightName = ini.value("flightName").toWzString();
		if (flightName.compare("GROUND") == 0) {
			pPropType->travel = TRAVEL_MEDIUM::GROUND;
		}
		else if (flightName.compare("AIR") == 0)
		{
			pPropType->travel = TRAVEL_MEDIUM::AIR;
		}
		else
		{
			ASSERT(false, "Invalid travel type for Propulsion: %s", flightName.toUtf8().c_str());
		}

		//don't care about this anymore! AB FRIDAY 13/11/98
		//want it back again! AB 27/11/98
		if (multiplier > UWORD_MAX)
		{
			ASSERT(false, "loadPropulsionTypes: power Ratio multiplier too high");
			//set to a default value since not life threatening!
			multiplier = 100;
		}
		pPropType->powerRatioMult = (UWORD)multiplier;

		//initialise all the sound variables
		pPropType->startID = NO_SOUND;
		pPropType->idleID = NO_SOUND;
		pPropType->moveOffID = NO_SOUND;
		pPropType->moveID = NO_SOUND;
		pPropType->hissID = NO_SOUND;
		pPropType->shutDownID = NO_SOUND;

		ini.endGroup();
	}

	return true;
}

bool loadTerrainTable(WzConfig& ini)
{
	asTerrainTable = (int*)malloc(sizeof(*asTerrainTable) * (int)PROPULSION_TYPE::COUNT * TER_MAX);
	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();
	for (int i = 0; i < list.size(); ++i)
	{
		ini.beginGroup(list[i]);
		int terrainType = ini.value("id").toInt();
		ini.beginGroup("speedFactor");
		asTerrainTable[terrainType * (int)PROPULSION_TYPE::COUNT + (int)PROPULSION_TYPE::WHEELED] = ini.value("wheeled", 100).
			toUInt();
		asTerrainTable[terrainType * (int)PROPULSION_TYPE::COUNT + (int)PROPULSION_TYPE::TRACKED] = ini.value("tracked", 100).
			toUInt();
		asTerrainTable[terrainType * (int)PROPULSION_TYPE::COUNT + (int)PROPULSION_TYPE::LEGGED] = ini.value("legged", 100).toUInt();
		asTerrainTable[terrainType * (int)PROPULSION_TYPE::COUNT + (int)PROPULSION_TYPE::HOVER] = ini.value("hover", 100).toUInt();
		asTerrainTable[terrainType * (int)PROPULSION_TYPE::COUNT + (int)PROPULSION_TYPE::LIFT] = ini.value("lift", 100).toUInt();
		asTerrainTable[terrainType * (int)PROPULSION_TYPE::COUNT + (int)PROPULSION_TYPE::PROPELLOR] = ini.value("propellor", 100).
			toUInt();
		asTerrainTable[terrainType * (int)PROPULSION_TYPE::COUNT + (int)PROPULSION_TYPE::HALF_TRACKED] = ini.value(
			"half-tracked", 100).toUInt();
		ini.endGroup();
		ini.endGroup();
	}
	return true;
}

static bool statsGetAudioIDFromString(const WzString& szStatName, const WzString& szWavName, int* piWavID)
{
	if (szWavName.compare("-1") == 0)
	{
		*piWavID = NO_SOUND;
	}
	else if ((*piWavID = audio_GetIDFromStr(szWavName.toUtf8().c_str())) == NO_SOUND)
	{
		debug(LOG_FATAL, "Could not get ID %d for sound %s", *piWavID, szWavName.toUtf8().c_str());
		return false;
	}
	if ((*piWavID < 0 || *piWavID > ID_MAX_SOUND) && *piWavID != NO_SOUND)
	{
		debug(LOG_FATAL, "Invalid ID - %d for sound %s", *piWavID, szStatName.toUtf8().c_str());
		return false;
	}
	return true;
}

bool loadWeaponModifiers(WzConfig& ini)
{
	//initialise to 100%
	for (auto i = 0; i < (int)WEAPON_EFFECT::COUNT; i++)
	{
		for (auto j = 0; j < (int)PROPULSION_TYPE::COUNT; j++)
		{
			asWeaponModifier[i][j] = 100;
		}
		for (auto j = 0; j < (int)BODY_SIZE::COUNT; j++)
		{
			asWeaponModifierBody[i][j] = 100;
		}
	}
	ASSERT(ini.isAtDocumentRoot(), "WzConfig instance is in the middle of traversal");
	std::vector<WzString> list = ini.childGroups();
	for (int i = 0; i < list.size(); i++)
	{
		WEAPON_EFFECT effectInc;
		PROPULSION_TYPE propInc;

		ini.beginGroup(list[i]);
		//get the weapon effect inc
		if (!getWeaponEffect(list[i], &effectInc))
		{
			debug(LOG_FATAL, "Invalid Weapon Effect - %s", list[i].toUtf8().c_str());
			continue;
		}
		std::vector<WzString> keys = ini.childKeys();
		for (auto & key : keys)
		{
			int modifier = ini.value(key).toInt();
			if (!getPropulsionType(key.toUtf8().data(), &propInc))
			{
				// If not propulsion, must be body
				BODY_SIZE body = BODY_SIZE::COUNT;
				if (!getBodySize(key, &body))
				{
					debug(LOG_FATAL, "Invalid Propulsion or Body type - %s", key.toUtf8().c_str());
					continue;
				}
				asWeaponModifierBody[(int)effectInc][(int)body] = modifier;
			}
			else // is propulsion
			{
				asWeaponModifier[(int)effectInc][(int)propInc] = modifier;
			}
		}
		ini.endGroup();
	}
	return true;
}

/*Load the propulsion type sounds from the file exported from Access*/
bool loadPropulsionSounds(const char* pFileName)
{
	SDWORD i, startID, idleID, moveOffID, moveID, hissID, shutDownID;
	PROPULSION_TYPE type;

	ASSERT(!asPropulsionTypes.empty(), "loadPropulsionSounds: Propulsion type stats not loaded");

	WzConfig ini(pFileName, WzConfig::ReadOnlyAndRequired);
	std::vector<WzString> list = ini.childGroups();
	for (i = 0; i < list.size(); ++i)
	{
		ini.beginGroup(list[i]);
		if (!statsGetAudioIDFromString(list[i], ini.value("szStart").toWzString(), &startID))
		{
			return false;
		}
		if (!statsGetAudioIDFromString(list[i], ini.value("szIdle").toWzString(), &idleID))
		{
			return false;
		}
		if (!statsGetAudioIDFromString(list[i], ini.value("szMoveOff").toWzString(), &moveOffID))
		{
			return false;
		}
		if (!statsGetAudioIDFromString(list[i], ini.value("szMove").toWzString(), &moveID))
		{
			return false;
		}
		if (!statsGetAudioIDFromString(list[i], ini.value("szHiss").toWzString(), &hissID))
		{
			return false;
		}
		if (!statsGetAudioIDFromString(list[i], ini.value("szShutDown").toWzString(), &shutDownID))
		{
			return false;
		}
		if (!getPropulsionType(list[i].toUtf8().c_str(), &type))
		{
			debug(LOG_FATAL, "Invalid Propulsion type - %s", list[i].toUtf8().c_str());
			return false;
		}
		Propulsion* pPropType = &asPropulsionTypes[(int)type];
		pPropType->startID = (SWORD)startID;
		pPropType->idleID = (SWORD)idleID;
		pPropType->moveOffID = (SWORD)moveOffID;
		pPropType->moveID = (SWORD)moveID;
		pPropType->hissID = (SWORD)hissID;
		pPropType->shutDownID = (SWORD)shutDownID;

		ini.endGroup();
	}

	return (true);
}

//get the speed factor for a given terrain type and propulsion type
unsigned getSpeedFactor(unsigned type, unsigned propulsionType)
{
	ASSERT(propulsionType < (unsigned)PROPULSION_TYPE::COUNT, "The propulsion type is too large");
	return asTerrainTable[type * (unsigned)PROPULSION_TYPE::COUNT + propulsionType];
}

int getCompFromName(COMPONENT_TYPE compType, const WzString& name)
{
	return getCompFromID(compType, name);
}

int getCompFromID(COMPONENT_TYPE compType, const WzString& name)
{
	ComponentStats* psComp = nullptr;
	auto it = lookupCompStatPtr.find(WzString::fromUtf8(name.toUtf8().c_str()));
	if (it != lookupCompStatPtr.end())
	{
		psComp = it->second;
	}
	ASSERT_OR_RETURN(-1, psComp, "No such component ID [%s] found", name.toUtf8().c_str());
	ASSERT_OR_RETURN(-1, compType == psComp->compType, "Wrong component type for ID %s", name.toUtf8().c_str());
	ASSERT_OR_RETURN(-1, psComp->index <= INT_MAX, "Component index is too large for ID %s", name.toUtf8().c_str());
	return static_cast<int>(psComp->index);
}

/// Get the component for a stat based on the name alone.
/// Returns NULL if record not found
ComponentStats* getCompStatsFromName(const WzString& name)
{
	ComponentStats* psComp = nullptr;
	auto it = lookupCompStatPtr.find(name);
	if (it != lookupCompStatPtr.end())
	{
		psComp = it->second;
	}
	/*if (!psComp)
	{
		debug(LOG_ERROR, "Not found: %s", name.toUtf8().c_str());
		for (auto& it2 : lookupCompStatPtr)
		{
			debug(LOG_ERROR, "    %s", it2.second->id.toUtf8().c_str());
		}
	}*/
	return psComp;
}

BaseStats* getBaseStatsFromName(const WzString& name)
{
	BaseStats* psStat = nullptr;
	auto it = lookupStatPtr.find(name);
	if (it != lookupStatPtr.end())
	{
		psStat = it->second;
	}
	return psStat;
}

/*sets the store to the body size based on the name passed in - returns false
if doesn't compare with any*/
bool getBodySize(const WzString& size, BODY_SIZE* pStore)
{
	if (!strcmp(size.toUtf8().c_str(), "LIGHT"))
	{
		*pStore = BODY_SIZE::LIGHT;
		return true;
	}
	else if (!strcmp(size.toUtf8().c_str(), "MEDIUM"))
	{
		*pStore = BODY_SIZE::MEDIUM;
		return true;
	}
	else if (!strcmp(size.toUtf8().c_str(), "HEAVY"))
	{
		*pStore = BODY_SIZE::HEAVY;
		return true;
	}
	else if (!strcmp(size.toUtf8().c_str(), "SUPER HEAVY"))
	{
		*pStore = BODY_SIZE::SUPER_HEAVY;
		return true;
	}

	ASSERT(false, "Invalid size - %s", size.toUtf8().c_str());
	return false;
}

/*returns the weapon sub class based on the string name passed in */
bool getWeaponSubClass(const char* subClass, WEAPON_SUBCLASS* wclass)
{
	if (strcmp(subClass, "CANNON") == 0)
	{
		*wclass = WEAPON_SUBCLASS::CANNON;
	}
	else if (strcmp(subClass, "MORTARS") == 0)
	{
		*wclass = WEAPON_SUBCLASS::MORTARS;
	}
	else if (strcmp(subClass, "MISSILE") == 0)
	{
		*wclass = WEAPON_SUBCLASS::MISSILE;
	}
	else if (strcmp(subClass, "ROCKET") == 0)
	{
		*wclass = WEAPON_SUBCLASS::ROCKET;
	}
	else if (strcmp(subClass, "ENERGY") == 0)
	{
		*wclass = WEAPON_SUBCLASS::ENERGY;
	}
	else if (strcmp(subClass, "GAUSS") == 0)
	{
		*wclass = WEAPON_SUBCLASS::GAUSS;
	}
	else if (strcmp(subClass, "FLAME") == 0)
	{
		*wclass = WEAPON_SUBCLASS::FLAME;
	}
	else if (strcmp(subClass, "HOWITZERS") == 0)
	{
		*wclass = WEAPON_SUBCLASS::HOWITZERS;
	}
	else if (strcmp(subClass, "MACHINE GUN") == 0)
	{
		*wclass = WEAPON_SUBCLASS::MACHINE_GUN;
	}
	else if (strcmp(subClass, "ELECTRONIC") == 0)
	{
		*wclass = WEAPON_SUBCLASS::ELECTRONIC;
	}
	else if (strcmp(subClass, "A-A GUN") == 0)
	{
		*wclass = WEAPON_SUBCLASS::AA_GUN;
	}
	else if (strcmp(subClass, "SLOW MISSILE") == 0)
	{
		*wclass = WEAPON_SUBCLASS::SLOW_MISSILE;
	}
	else if (strcmp(subClass, "SLOW ROCKET") == 0)
	{
		*wclass = WEAPON_SUBCLASS::SLOW_ROCKET;
	}
	else if (strcmp(subClass, "LAS_SAT") == 0)
	{
		*wclass = WEAPON_SUBCLASS::LAS_SAT;
	}
	else if (strcmp(subClass, "BOMB") == 0)
	{
		*wclass = WEAPON_SUBCLASS::BOMB;
	}
	else if (strcmp(subClass, "COMMAND") == 0)
	{
		*wclass = WEAPON_SUBCLASS::COMMAND;
	}
	else if (strcmp(subClass, "EMP") == 0)
	{
		*wclass = WEAPON_SUBCLASS::EMP;
	}
	else
	{
		ASSERT(!"Invalid weapon sub class", "Invalid weapon sub class: %s", subClass);
		return false;
	}

	return true;
}

/*returns the weapon subclass based on the string name passed in */
const char* getWeaponSubClass(WEAPON_SUBCLASS wclass)
{
	switch (wclass)
	{
	case WEAPON_SUBCLASS::CANNON:
    return "CANNON";
	case WEAPON_SUBCLASS::MORTARS:
    return "MORTARS";
	case WEAPON_SUBCLASS::MISSILE:
    return "MISSILE";
	case WEAPON_SUBCLASS::ROCKET:
    return "ROCKET";
	case WEAPON_SUBCLASS::ENERGY:
    return "ENERGY";
	case WEAPON_SUBCLASS::GAUSS:
    return "GAUSS";
	case WEAPON_SUBCLASS::FLAME:
    return "FLAME";
	case WEAPON_SUBCLASS::HOWITZERS:
    return "HOWITZERS";
	case WEAPON_SUBCLASS::MACHINE_GUN:
    return "MACHINE GUN";
	case WEAPON_SUBCLASS::ELECTRONIC:
    return "ELECTRONIC";
	case WEAPON_SUBCLASS::AA_GUN:
    return "A-A GUN";
	case WEAPON_SUBCLASS::SLOW_MISSILE:
    return "SLOW MISSILE";
	case WEAPON_SUBCLASS::SLOW_ROCKET:
    return "SLOW ROCKET";
	case WEAPON_SUBCLASS::LAS_SAT:
    return "LAS_SAT";
	case WEAPON_SUBCLASS::BOMB:
    return "BOMB";
	case WEAPON_SUBCLASS::COMMAND:
    return "COMMAND";
	case WEAPON_SUBCLASS::EMP:
    return "EMP";
	case WEAPON_SUBCLASS::COUNT:
    break;
	}
	ASSERT(false, "No such weapon subclass");
	return "Bad weapon subclass";
}

/*returns the movement model based on the string name passed in */
bool getMovementModel(const WzString& movementModel, MOVEMENT_MODEL* model)
{
	if (strcmp(movementModel.toUtf8().c_str(), "DIRECT") == 0)
	{
		*model = MOVEMENT_MODEL::DIRECT;
	}
	else if (strcmp(movementModel.toUtf8().c_str(), "INDIRECT") == 0)
	{
		*model = MOVEMENT_MODEL::INDIRECT;
	}
	else if (strcmp(movementModel.toUtf8().c_str(), "HOMING-DIRECT") == 0)
	{
		*model = MOVEMENT_MODEL::HOMING_DIRECT;
	}
	else if (strcmp(movementModel.toUtf8().c_str(), "HOMING-INDIRECT") == 0)
	{
		*model = MOVEMENT_MODEL::HOMING_INDIRECT;
	}
	else
	{
		// We've got problem if we got here
		ASSERT(!"Invalid movement model", "Invalid movement model: %s", movementModel.toUtf8().c_str());
		return false;
	}

	return true;
}

const StringToEnum<WEAPON_EFFECT> mapUnsorted_WEAPON_EFFECT[] =
{
	{"ANTI PERSONNEL", ANTI_PERSONNEL},
	{"ANTI TANK", ANTI_TANK},
	{"BUNKER BUSTER", BUNKER_BUSTER},
	{"ARTILLERY ROUND", ARTILLERY_ROUND},
	{"FLAMER", FLAMER},
	{"ANTI AIRCRAFT", ANTI_AIRCRAFT},
	{"ALL ROUNDER", ANTI_AIRCRAFT}, // Alternative name for WEAPON_EFFECT::ANTI_AIRCRAFT.
};
const StringToEnumMap<WEAPON_EFFECT> map_WEAPON_EFFECT = mapUnsorted_WEAPON_EFFECT;

bool getWeaponEffect(const WzString& weaponEffect, WEAPON_EFFECT* effect)
{
	if (strcmp(weaponEffect.toUtf8().c_str(), "ANTI PERSONNEL") == 0)
	{
		*effect = WEAPON_EFFECT::ANTI_PERSONNEL;
	}
	else if (strcmp(weaponEffect.toUtf8().c_str(), "ANTI TANK") == 0)
	{
		*effect = WEAPON_EFFECT::ANTI_TANK;
	}
	else if (strcmp(weaponEffect.toUtf8().c_str(), "BUNKER BUSTER") == 0)
	{
		*effect = WEAPON_EFFECT::BUNKER_BUSTER;
	}
	else if (strcmp(weaponEffect.toUtf8().c_str(), "ARTILLERY ROUND") == 0)
	{
		*effect = WEAPON_EFFECT::ARTILLERY_ROUND;
	}
	else if (strcmp(weaponEffect.toUtf8().c_str(), "FLAMER") == 0)
	{
		*effect = WEAPON_EFFECT::FLAMER;
	}
	else if (strcmp(weaponEffect.toUtf8().c_str(), "ANTI AIRCRAFT") == 0 || strcmp(
		weaponEffect.toUtf8().c_str(), "ALL ROUNDER") == 0)
	{
		*effect = WEAPON_EFFECT::ANTI_AIRCRAFT;
	}
	else
	{
		ASSERT(!"Invalid weapon effect", "Invalid weapon effect: %s", weaponEffect.toUtf8().c_str());
		return false;
	}

	return true;
}

/*returns the weapon effect string based on the enum passed in */
std::string getWeaponEffect(WEAPON_EFFECT effect)
{
	switch (effect) {
	  case WEAPON_EFFECT::ANTI_PERSONNEL: return "ANTI PERSONNEL";
	  case WEAPON_EFFECT::ANTI_TANK: return "ANTI TANK";
	  case WEAPON_EFFECT::BUNKER_BUSTER: return "BUNKER BUSTER";
	  case WEAPON_EFFECT::ARTILLERY_ROUND: return "ARTILLERY ROUND";
	  case WEAPON_EFFECT::FLAMER: return "FLAMER";
	  case WEAPON_EFFECT::ANTI_AIRCRAFT: return "ANTI AIRCRAFT";
	  case WEAPON_EFFECT::COUNT: break;
	}
	ASSERT(false, "No such weapon effect");
	return "Bad weapon effect";
}

bool getWeaponClass(WzString const& weaponClassStr, WEAPON_CLASS* weaponClass)
{
	if (weaponClassStr.compare("KINETIC") == 0) {
		*weaponClass = WEAPON_CLASS::KINETIC;
    return true;
	}

	if (weaponClassStr.compare("HEAT") == 0) {
		*weaponClass = WEAPON_CLASS::HEAT;
    return true;
	}

  ASSERT(false, "Bad weapon class %s", weaponClassStr.toUtf8().c_str());
  return false;
}

#define ASSERT_PLAYER_OR_RETURN(retVal, player) \
	ASSERT_OR_RETURN(retVal, player >= 0 && player < MAX_PLAYERS, "Invalid player: %" PRIu32 "", player);

/*Access functions for the upgradeable stats of a weapon*/
int weaponFirePause(const WeaponStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].firePause;
}

/* Reload time is reduced for weapons with salvo fire */
int weaponReloadTime(const WeaponStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].reloadTime;
}

int weaponLongHit(const WeaponStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].hitChance;
}

int weaponShortHit(const WeaponStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].shortHitChance;
}

int weaponDamage(const WeaponStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].damage;
}

int weaponRadDamage(const WeaponStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].radiusDamage;
}

int weaponPeriodicalDamage(const WeaponStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].periodicalDamage;
}

int sensorRange(const SensorStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].range;
}

int ecmRange(const EcmStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].range;
}

int repairPoints(const RepairStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].repairPoints;
}

int constructorPoints(const ConstructStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].constructPoints;
}

int bodyPower(const BodyStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].power;
}

//int bodyArmour(const BODY_STATS* psStats, unsigned player, WEAPON_CLASS weaponClass)
//{
//	ASSERT_PLAYER_OR_RETURN(0, player);
//	switch (weaponClass)
//	{
//	case WEAPON_CLASS::KINETIC:
//		return psStats->upgrade[player].armour;
//	case WEAPON_CLASS::HEAT:
//		return psStats->upgrade[player].thermal;
//	case WEAPON_CLASS::NUM_WEAPON_CLASSES:
//		break;
//	}
//	ASSERT(false, "Unknown weapon class");
//	return 0; // Should never get here.
//}

//calculates the weapons ROF based on the fire pause and the salvos
int weaponROF(WeaponStats const* psStat, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	int rof = 0;
	// if there are salvos
	if (player >= 0 &&
      psStat->upgraded[player].numRounds &&
      psStat->upgraded[player].reloadTime != 0) {
		// Rounds per salvo multiplied with the number of salvos per minute
		rof = psStat->upgraded[player].numRounds * 60 * GAME_TICKS_PER_SEC / weaponReloadTime(psStat, player);
	}

	if (rof == 0) {
		rof = weaponFirePause(psStat, player);
		if (rof != 0) {
			rof = (UWORD)(60 * GAME_TICKS_PER_SEC / rof);
		}
		//else leave it at 0
	}
	return rof;
}

/* Check if an object has a weapon */
bool objHasWeapon(BaseObject const* psObj)
{
	//check if valid type
	if (auto droid = dynamic_cast<Droid const*>(psObj)) {
		if (numWeapons(*droid) > 0) {
			return true;
		}
	}
	else if (auto structure = dynamic_cast<Structure const*>(psObj)) {
		if (numWeapons(*structure) > 0) {
			return true;
		}
	}
	return false;
}

SensorStats const* objActiveRadar(BaseObject const* psObj)
{
	SensorStats const* psStats = nullptr;
	if (auto psDroid = dynamic_cast<Droid const*>(psObj)) {
    if (psDroid->getType() != DROID_TYPE::SENSOR &&
        psDroid->getType() != DROID_TYPE::COMMAND) {
      return nullptr;
    }
    psStats = dynamic_cast<SensorStats const*>(psDroid->getComponent(COMPONENT_TYPE::SENSOR));
  }
	else if (auto psStruct = dynamic_cast<const Structure*>(psObj)) {
    psStats = psStruct->getStats()->sensor_stats.get();
    if (!psStats || psStats->location != LOC::TURRET ||
        psStruct->getState() != STRUCTURE_STATE::BUILT) {
      return nullptr;
    }
  }
	return psStats;
}

bool objRadarDetector(BaseObject const* psObj)
{
	if (auto const& psStruct = dynamic_cast<Structure const*>(psObj)) {
		return psStruct->getState() == STRUCTURE_STATE::BUILT &&
            psStruct->getStats()->sensor_stats &&
            psStruct->getStats()->sensor_stats->type == SENSOR_TYPE::RADAR_DETECTOR;
	}

	if (auto const& psDroid = dynamic_cast<Droid const*>(psObj)) {
		auto psSensor = dynamic_cast<SensorStats const*>(psDroid->getComponent(COMPONENT_TYPE::SENSOR));
		return psSensor && psSensor->type == SENSOR_TYPE::RADAR_DETECTOR;
	}

	return false;
}
