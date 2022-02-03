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

#ifndef __INCLUDED_SRC_COMPONENT_H__
#define __INCLUDED_SRC_COMPONENT_H__

#include <glm/fwd.hpp>

/* forward decl */
class BaseStats;
class Droid;
class DroidTemplate;
class iIMDShape;
class Structure;
class StructureStats;
class Weapon;


static constexpr auto BLIP_ANIM_DURATION			= 200;
static constexpr auto OBJECT_RADIUS				= 128;
static constexpr auto COMPONENT_RADIUS			= 64;
static constexpr auto DESIGN_DROID_SCALE			= 200;
static constexpr auto DESIGN_COMPONENT_SCALE			= 150;
static constexpr auto RESEARCH_COMPONENT_SCALE		= 300;
static constexpr auto COMP_BUT_SCALE				= 100;
static constexpr auto DROID_BUT_SCALE				= 72;
static constexpr auto SMALL_STRUCT_SCALE			= 55;
static constexpr auto MED_STRUCT_SCALE			= 25;  //< Reduced from 30 to fit command entre in window
static constexpr auto LARGE_STRUCT_SCALE			= 25;
static constexpr auto ULTRA_SMALL_FEATURE_SCALE		= 146;
static constexpr auto REALLY_SMALL_FEATURE_SCALE		= 116;
static constexpr auto SMALL_FEATURE_SCALE			= 55;
static constexpr auto MED_FEATURE_SCALE			= 26;
static constexpr auto LARGE_FEATURE_SCALE			= 16;
static constexpr auto TOWER_HEIGHT    = 10;


bool setPlayerColour(unsigned player, int col);
uint8_t getPlayerColour(unsigned pl);

unsigned getComponentDroidRadius(Droid* psDroid);
unsigned getComponentDroidTemplateRadius(DroidTemplate* psDroid);
unsigned getComponentRadius(BaseStats* psComponent);
unsigned getResearchRadius(BaseStats* Stat);
unsigned getStructureSizeMax(Structure* psStructure);
unsigned getStructureStatSizeMax(StructureStats* Stats);

unsigned getStructureStatHeight(StructureStats* psStat);

void displayIMDButton(iIMDShape* IMDShape, const Vector3i* Rotation, const Vector3i* Position, int scale);
void displayStructureButton(Structure* psStructure, const Vector3i* Rotation, const Vector3i* Position, int scale);
void displayStructureStatButton(StructureStats* Stats, const Vector3i* Rotation, const Vector3i* Position, int scale);
void displayComponentButton(BaseStats* Stat, const Vector3i* Rotation, const Vector3i* Position, int scale);

/// Render a research item given a \c BaseStats structure
void displayResearchButton(BaseStats* Stat, const Vector3i* Rotation, const Vector3i* Position, int scale);

/// Render a composite droid given a \c DroidTemplate structure
void displayComponentButtonTemplate(DroidTemplate* psTemplate, const Vector3i* Rotation, const Vector3i* Position,
                                    int scale);

/// Render a composite droid given a \c Droid structure.
void displayComponentButtonObject(Droid* psDroid, const Vector3i* Rotation, const Vector3i* Position, int scale);

void displayComponentObject(Droid* psDroid, const glm::mat4& viewMatrix);

void compPersonToBits(Droid* psDroid);

SDWORD rescaleButtonObject(SDWORD radius, SDWORD baseScale, SDWORD baseRadius);
void destroyFXDroid(Droid* psDroid, unsigned impactTime);

void drawMuzzleFlash(Weapon sWeap, iIMDShape* weaponImd, iIMDShape* flashImd, PIELIGHT buildingBrightness, int pieFlag,
                     int pieFlagData, const glm::mat4& viewMatrix, UBYTE colour = 0);

/* Pass in the stats you're interested in and the COMPONENT - double reference, but works. NOTE: Unused!*/
#define PART_IMD(STATS,Droid,COMPONENT,PLAYER)	(STATS[Droid->asBits[COMPONENT]].pIMD)

/* Get the chassis imd */
#define BODY_IMD(Droid,PLAYER) (Droid->getComponent(COMPONENT_TYPE::BODY)->pIMD)
/* Get the brain imd - NOTE: Unused!*/
#define BRAIN_IMD(Droid,PLAYER)	(asBrainStats[Droid->asBits[COMP_BRAIN]].pIMD)
/* Get the weapon imd */
#define WEAPON_IMD(Droid,WEAPON_NUM)	(asWeaponStats[Droid->asWeaps[WEAPON_NUM].nStat].pIMD)
/* Get the propulsion imd  THIS IS A LITTLE MORE COMPLICATED NOW!*/
//#define PROPULSION_IMD(Droid,PLAYER)	(asPropulsionStats[Droid->asBits[COMP_PROPULSION]].pIMD[PLAYER])
/* Get the sensor imd */
#define SENSOR_IMD(Droid,PLAYER)	(asSensorStats[Droid->asBits[COMP_SENSOR]].pIMD)
/* Get an ECM imd!?! */
#define ECM_IMD(Droid,PLAYER)	(asECMStats[Droid->asBits[COMP_ECM]].pIMD)
/* Get an Repair imd!?! */
#define REPAIR_IMD(Droid,PLAYER)	(asRepairStats[Droid->asBits[COMP_REPAIRUNIT]].pIMD)
/* Get a construct imd */
#define CONSTRUCT_IMD(Droid,PLAYER)	(asConstructStats[Droid->asBits[COMP_CONSTRUCT]].pIMD)
/* Get a weapon mount imd*/
#define WEAPON_MOUNT_IMD(Droid,WEAPON_NUM)	(asWeaponStats[Droid->asWeaps[WEAPON_NUM].nStat].pMountGraphic)
/* Get a sensor mount imd*/
#define SENSOR_MOUNT_IMD(Droid,PLAYER)	(asSensorStats[Droid->asBits[COMP_SENSOR]].pMountGraphic)
/* Get a construct mount imd*/
#define CONSTRUCT_MOUNT_IMD(Droid,PLAYER)	(asConstructStats[Droid->asBits[COMP_CONSTRUCT]].pMountGraphic)
/* Get a ecm mount imd*/
#define ECM_MOUNT_IMD(Droid,PLAYER)	(asECMStats[Droid->asBits[COMP_ECM]].pMountGraphic)
/* Get a repair mount imd*/
#define REPAIR_MOUNT_IMD(Droid,PLAYER)	(asRepairStats[Droid->asBits[COMP_REPAIRUNIT]].pMountGraphic)
/* Get a muzzle flash pie*/
#define MUZZLE_FLASH_PIE(Droid,WEAPON_NUM)	(asWeaponStats[Droid->asWeaps[WEAPON_NUM].nStat].pMuzzleGraphic)

#endif // __INCLUDED_SRC_COMPONENT_H__
